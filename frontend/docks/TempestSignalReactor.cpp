#include "TempestSignalReactor.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>
#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

#include "moc_TempestSignalReactor.cpp"

namespace {
constexpr char ConfigSection[] = "TempestSignalReactor";

struct AudioEntry {
	QString name;
	QString uuid;
};

int SuggestedSourceIndex(QComboBox *selector, const QStringList &terms)
{
	for (const QString &term : terms) {
		for (int index = 1; index < selector->count(); ++index) {
			if (selector->itemText(index).contains(term, Qt::CaseInsensitive))
				return index;
		}
	}
	return selector->count() > 1 ? 1 : 0;
}
} // namespace

TempestSignalReactor::TempestSignalReactor(OBSBasic *main, QWidget *parent) : OBSDock(parent), main(main)
{
	setObjectName(QStringLiteral("tempestSignalReactor"));
	setWindowTitle(QStringLiteral("Mainframe Signal Reactor"));
	setMinimumWidth(360);

	BuildInterface();
	EnableContentScaling(objectName());
	EnsureOutputDirectory();
	CreateMeter(desktopChannel);
	CreateMeter(microphoneChannel);
	LoadState();

	telemetryTimer = new QTimer(this);
	telemetryTimer->setInterval(50);
	connect(telemetryTimer, &QTimer::timeout, this, &TempestSignalReactor::PublishTelemetry);
	telemetryTimer->start();

	RefreshAudioSources();
	QTimer::singleShot(3500, this, &TempestSignalReactor::RefreshAudioSources);
}

TempestSignalReactor::~TempestSignalReactor()
{
	UnregisterHotkeys();
	if (telemetryTimer)
		telemetryTimer->stop();
	DestroyMeter(desktopChannel);
	DestroyMeter(microphoneChannel);
}

void TempestSignalReactor::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestSignalReactorRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestSignalReactorRoot { background: #07131e; }
		QLabel#reactorTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#reactorSubtitle, QLabel#reactorChannelLabel { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
		QLabel#reactorStatus { color: #45d9ff; font-size: 10px; }
		QLabel#reactorControl { color: #9b8cff; font-size: 10px; letter-spacing: 1px; padding: 5px; border: 1px solid #302d67; background: #090d1d; }
		QLabel#reactorHint { color: #748fa4; font-size: 10px; padding: 7px; border: 1px solid #183a50; background: #06101a; }
		QFrame#reactorChannel { background: #081a27; border: 1px solid #183a50; }
		QComboBox, QDoubleSpinBox { min-height: 29px; padding: 0 7px; color: #bdf6ff; background: #06101a; border: 1px solid #1f506d; }
		QProgressBar { min-height: 9px; max-height: 9px; border: 1px solid #17394f; background: #04101a; }
		QProgressBar::chunk { background: #45d9ff; }
		QPushButton { min-height: 31px; padding: 0 9px; color: #bdf6ff; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QCheckBox { color: #9eb7c8; }
	)"));
	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *title = new QLabel(QStringLiteral("SIGNAL REACTOR"), root);
	title->setObjectName(QStringLiteral("reactorTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Live audio energy routing for Tempest HUD elements"), root);
	subtitle->setObjectName(QStringLiteral("reactorSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	reactorEnabled = new QCheckBox(QStringLiteral("REACTOR ONLINE"), root);
	reactorEnabled->setAccessibleName(QStringLiteral("Enable Tempest Signal Reactor"));
	layout->addWidget(reactorEnabled);

	auto addChannel = [root, layout](const QString &label, QPointer<QComboBox> &selector,
					 QPointer<QDoubleSpinBox> &sensitivity, QPointer<QProgressBar> &meter) {
		auto *frame = new QFrame(root);
		frame->setObjectName(QStringLiteral("reactorChannel"));
		auto *channelLayout = new QVBoxLayout(frame);
		channelLayout->setContentsMargins(8, 8, 8, 8);
		channelLayout->setSpacing(6);
		auto *channelLabel = new QLabel(label, frame);
		channelLabel->setObjectName(QStringLiteral("reactorChannelLabel"));
		selector = new QComboBox(frame);
		selector->setAccessibleName(QStringLiteral("%1 reactive audio source").arg(label));
		selector->addItem(QStringLiteral("WAITING FOR OBS AUDIO SOURCES"), QString());
		sensitivity = new QDoubleSpinBox(frame);
		sensitivity->setRange(0.0, 2.5);
		sensitivity->setSingleStep(0.1);
		sensitivity->setDecimals(1);
		sensitivity->setPrefix(QStringLiteral("SENSITIVITY  "));
		meter = new QProgressBar(frame);
		meter->setRange(0, 1000);
		meter->setTextVisible(false);
		channelLayout->addWidget(channelLabel);
		channelLayout->addWidget(selector);
		channelLayout->addWidget(sensitivity);
		channelLayout->addWidget(meter);
		layout->addWidget(frame);
	};
	addChannel(QStringLiteral("DESKTOP ENERGY"), desktopSource, desktopSensitivity, desktopMeter);
	addChannel(QStringLiteral("MICROPHONE / VOICE"), microphoneSource, microphoneSensitivity, microphoneMeter);

	auto *beatFrame = new QFrame(root);
	beatFrame->setObjectName(QStringLiteral("reactorChannel"));
	auto *beatLayout = new QVBoxLayout(beatFrame);
	beatLayout->setContentsMargins(8, 8, 8, 8);
	beatLayout->setSpacing(6);
	auto *beatLabel = new QLabel(QStringLiteral("MUSIC TRANSIENT / BEAT"), beatFrame);
	beatLabel->setObjectName(QStringLiteral("reactorChannelLabel"));
	beatSensitivity = new QDoubleSpinBox(beatFrame);
	beatSensitivity->setRange(0.50, 4.00);
	beatSensitivity->setSingleStep(0.10);
	beatSensitivity->setDecimals(1);
	beatSensitivity->setPrefix(QStringLiteral("BEAT SENSITIVITY  "));
	beatSensitivity->setAccessibleName(QStringLiteral("Music transient beat sensitivity"));
	beatMeter = new QProgressBar(beatFrame);
	beatMeter->setRange(0, 1000);
	beatMeter->setTextVisible(false);
	beatMeter->setAccessibleName(QStringLiteral("Music transient beat meter"));
	beatLayout->addWidget(beatLabel);
	beatLayout->addWidget(beatSensitivity);
	beatLayout->addWidget(beatMeter);
	layout->addWidget(beatFrame);

	auto *masterForm = new QFormLayout();
	smoothing = new QDoubleSpinBox(root);
	smoothing->setRange(0.50, 0.98);
	smoothing->setSingleStep(0.02);
	smoothing->setDecimals(2);
	smoothing->setAccessibleName(QStringLiteral("Signal decay smoothing"));
	masterForm->addRow(QStringLiteral("Signal smoothing"), smoothing);
	layout->addLayout(masterForm);
	auto *masterLabel = new QLabel(QStringLiteral("MASTER REACTION BUS"), root);
	masterLabel->setObjectName(QStringLiteral("reactorChannelLabel"));
	masterMeter = new QProgressBar(root);
	masterMeter->setRange(0, 1000);
	masterMeter->setTextVisible(false);
	layout->addWidget(masterLabel);
	layout->addWidget(masterMeter);

	auto *pulseRow = new QHBoxLayout();
	pulseButton = new QPushButton(QStringLiteral("TEST PULSE"), root);
	peakButton = new QPushButton(QStringLiteral("TEST PEAK"), root);
	pulseButton->setAccessibleName(QStringLiteral("Trigger Tempest signal pulse"));
	peakButton->setAccessibleName(QStringLiteral("Trigger Tempest signal peak"));
	auto *refresh = new QPushButton(QStringLiteral("REFRESH SOURCES"), root);
	refresh->setAccessibleName(QStringLiteral("Refresh Signal Reactor audio sources"));
	pulseRow->addWidget(pulseButton);
	pulseRow->addWidget(peakButton);
	pulseRow->addWidget(refresh);
	layout->addLayout(pulseRow);
	controlLabel = new QLabel(QStringLiteral("CONTROL BRIDGE // INITIALIZING"), root);
	controlLabel->setObjectName(QStringLiteral("reactorControl"));
	controlLabel->setAccessibleName(QStringLiteral("Signal Reactor external control status"));
	layout->addWidget(controlLabel);

	auto *hint = new QLabel(
		QStringLiteral(
			"Desktop and microphone energy feed the master bus. The Beat bus extracts fast music transients from Desktop Energy. Assign Pulse and Peak in Settings > Hotkeys, or call tempest-mainframe / TriggerSignal after enabling the OBS WebSocket server."),
		root);
	hint->setObjectName(QStringLiteral("reactorHint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);
	statusLabel = new QLabel(QStringLiteral("REACTOR INITIALIZING"), root);
	statusLabel->setObjectName(QStringLiteral("reactorStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	layout->addStretch(1);
	setWidget(root);

	connect(refresh, &QPushButton::clicked, this, &TempestSignalReactor::RefreshAudioSources);
	connect(desktopSource, &QComboBox::currentIndexChanged, this, &TempestSignalReactor::AttachDesktopSource);
	connect(microphoneSource, &QComboBox::currentIndexChanged, this, &TempestSignalReactor::AttachMicrophoneSource);
	connect(reactorEnabled, &QCheckBox::toggled, this, &TempestSignalReactor::SaveState);
	connect(desktopSensitivity, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(microphoneSensitivity, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(beatSensitivity, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(smoothing, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(pulseButton, &QPushButton::clicked, this, [this]() { TriggerPulse(0.65f, QStringLiteral("dock")); });
	connect(peakButton, &QPushButton::clicked, this, [this]() { TriggerPulse(1.0f, QStringLiteral("dock")); });
}

void TempestSignalReactor::RegisterHotkeys()
{
	UnregisterHotkeys();
	struct Definition {
		const char *name;
		const char *description;
		float strength;
	};
	constexpr Definition definitions[] = {
		{"TempestMainframe.Signal.Pulse", "Tempest Mainframe: Trigger Signal Pulse", 0.65f},
		{"TempestMainframe.Signal.Peak", "Tempest Mainframe: Trigger Signal Peak", 1.0f},
	};
	for (const Definition &definition : definitions) {
		const obs_hotkey_id id =
			obs_hotkey_register_frontend(definition.name, definition.description, HotkeyCallback, this);
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		pulseHotkeys.insert(id, definition.strength);
		LoadHotkey(id, QByteArray(definition.name));
	}
	UpdateControlBridgeState();
}

void TempestSignalReactor::UnregisterHotkeys()
{
	for (auto it = pulseHotkeys.cbegin(); it != pulseHotkeys.cend(); ++it)
		obs_hotkey_unregister(it.key());
	pulseHotkeys.clear();
	UpdateControlBridgeState();
}

void TempestSignalReactor::LoadHotkey(obs_hotkey_id id, const QByteArray &name)
{
	if (!main)
		return;
	const char *json = config_get_string(main->Config(), "Hotkeys", name.constData());
	if (!json || !*json)
		return;
	OBSDataAutoRelease data = obs_data_create_from_json(json);
	if (!data)
		return;
	OBSDataArrayAutoRelease bindings = obs_data_get_array(data, "bindings");
	if (bindings)
		obs_hotkey_load(id, bindings);
}

void TempestSignalReactor::HotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *reactor = static_cast<TempestSignalReactor *>(data);
	const float strength = reactor->pulseHotkeys.value(id, 0.0f);
	if (strength <= 0.0f)
		return;
	QPointer<TempestSignalReactor> guarded(reactor);
	QMetaObject::invokeMethod(
		reactor,
		[guarded, strength]() {
			if (guarded)
				guarded->TriggerPulse(strength, QStringLiteral("hotkey"));
		},
		Qt::QueuedConnection);
}

void TempestSignalReactor::LoadState()
{
	loadingState = true;
	config_t *config = App()->GetUserConfig();
	reactorEnabled->setChecked(!config_has_user_value(config, ConfigSection, "Enabled") ||
				   config_get_bool(config, ConfigSection, "Enabled"));
	const double desktopGain = config_get_double(config, ConfigSection, "DesktopSensitivity");
	const double microphoneGain = config_get_double(config, ConfigSection, "MicrophoneSensitivity");
	const double savedBeatSensitivity = config_get_double(config, ConfigSection, "BeatSensitivity");
	const double savedSmoothing = config_get_double(config, ConfigSection, "Smoothing");
	desktopSensitivity->setValue(desktopGain > 0.0 ? desktopGain : 1.0);
	microphoneSensitivity->setValue(microphoneGain > 0.0 ? microphoneGain : 1.2);
	beatSensitivity->setValue(savedBeatSensitivity > 0.0 ? savedBeatSensitivity : 1.8);
	smoothing->setValue(savedSmoothing >= 0.50 ? savedSmoothing : 0.82);
	const char *desktopUuid = config_get_string(config, ConfigSection, "DesktopSourceUuid");
	const char *microphoneUuid = config_get_string(config, ConfigSection, "MicrophoneSourceUuid");
	configuredDesktopUuid = QString::fromUtf8(desktopUuid ? desktopUuid : "");
	configuredMicrophoneUuid = QString::fromUtf8(microphoneUuid ? microphoneUuid : "");
	if (configuredDesktopUuid.isEmpty()) {
		const char *legacyUuid = config_get_string(config, "TempestControlDeck", "AudioSourceUuid");
		configuredDesktopUuid = QString::fromUtf8(legacyUuid ? legacyUuid : "");
	}
	loadingState = false;
}

void TempestSignalReactor::SaveState()
{
	if (loadingState)
		return;
	config_t *config = App()->GetUserConfig();
	config_set_bool(config, ConfigSection, "Enabled", reactorEnabled->isChecked());
	config_set_double(config, ConfigSection, "DesktopSensitivity", desktopSensitivity->value());
	config_set_double(config, ConfigSection, "MicrophoneSensitivity", microphoneSensitivity->value());
	config_set_double(config, ConfigSection, "BeatSensitivity", beatSensitivity->value());
	config_set_double(config, ConfigSection, "Smoothing", smoothing->value());
	if (audioSourcesLoaded) {
		configuredDesktopUuid = desktopSource->currentData().toString();
		configuredMicrophoneUuid = microphoneSource->currentData().toString();
		config_set_string(config, ConfigSection, "DesktopSourceUuid", QT_TO_UTF8(configuredDesktopUuid));
		config_set_string(config, ConfigSection, "MicrophoneSourceUuid", QT_TO_UTF8(configuredMicrophoneUuid));
	}
	config_save_safe(config, "tmp", nullptr);
}

bool TempestSignalReactor::EnsureOutputDirectory()
{
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "tempest-broadcast-system/control-deck") <= 0)
		return false;
	const QString directory = QString::fromUtf8(path);
	if (!QDir().mkpath(directory))
		return false;
	telemetryPath = QDir(directory).filePath(QStringLiteral("telemetry.json"));
	return true;
}

void TempestSignalReactor::CreateMeter(SignalChannel &channel)
{
	channel.meter = obs_volmeter_create(OBS_FADER_LOG);
	if (!channel.meter)
		return;
	obs_volmeter_set_peak_meter_type(channel.meter, SAMPLE_PEAK_METER);
	obs_volmeter_add_callback(channel.meter, AudioLevelCallback, &channel);
}

void TempestSignalReactor::DestroyMeter(SignalChannel &channel)
{
	if (!channel.meter)
		return;
	obs_volmeter_remove_callback(channel.meter, AudioLevelCallback, &channel);
	obs_volmeter_detach_source(channel.meter);
	obs_volmeter_destroy(channel.meter);
	channel.meter = nullptr;
}

void TempestSignalReactor::AudioLevelCallback(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
					      const float peak[MAX_AUDIO_CHANNELS],
					      const float inputPeak[MAX_AUDIO_CHANNELS])
{
	(void)magnitude;
	(void)inputPeak;
	auto *channel = static_cast<SignalChannel *>(param);
	float peakDb = -96.0f;
	for (size_t index = 0; index < MAX_AUDIO_CHANNELS; ++index) {
		if (std::isfinite(peak[index]))
			peakDb = std::max(peakDb, peak[index]);
	}
	const float normalized = std::clamp((peakDb + 55.0f) / 55.0f, 0.0f, 1.0f);
	channel->rawLevel.store(std::pow(normalized, 0.72f), std::memory_order_relaxed);
}

void TempestSignalReactor::RefreshAudioSources()
{
	std::vector<AudioEntry> entries;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
				return true;
			const char *name = obs_source_get_name(source);
			const char *uuid = obs_source_get_uuid(source);
			if (name && *name && uuid && *uuid)
				static_cast<std::vector<AudioEntry> *>(param)->push_back(
					{QString::fromUtf8(name), QString::fromUtf8(uuid)});
			return true;
		},
		&entries);
	std::sort(entries.begin(), entries.end(), [](const AudioEntry &left, const AudioEntry &right) {
		return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
	});

	auto populate = [&entries](QComboBox *selector, const QString &wanted, const QStringList &suggestions) {
		QSignalBlocker blocker(selector);
		selector->clear();
		selector->addItem(QStringLiteral("OFF // NO SOURCE"), QString());
		for (const AudioEntry &entry : entries)
			selector->addItem(entry.name, entry.uuid);
		int selected = wanted.isEmpty() ? -1 : selector->findData(wanted);
		if (selected < 0)
			selected = SuggestedSourceIndex(selector, suggestions);
		selector->setCurrentIndex(std::max(0, selected));
	};
	populate(desktopSource, configuredDesktopUuid,
		 {QStringLiteral("Desktop Audio"), QStringLiteral("Application Audio"), QStringLiteral("Output")});
	populate(microphoneSource, configuredMicrophoneUuid,
		 {QStringLiteral("Mic/Aux"), QStringLiteral("Microphone"), QStringLiteral("Mic")});
	audioSourcesLoaded = true;
	AttachDesktopSource();
	AttachMicrophoneSource();
	SaveState();
	SetStatus(entries.empty() ? QStringLiteral("NO AUDIO SOURCES DETECTED // REFRESH AFTER SCENE LOAD")
				  : QStringLiteral("REACTOR ONLINE // %1 AUDIO SOURCES INDEXED").arg(entries.size()),
		  entries.empty());
}

void TempestSignalReactor::AttachChannel(SignalChannel &channel, QComboBox *selector, const QString &label)
{
	if (!channel.meter)
		return;
	obs_volmeter_detach_source(channel.meter);
	channel.rawLevel.store(0.0f, std::memory_order_relaxed);
	channel.smoothedLevel = 0.0f;
	const QString uuid = selector->currentData().toString();
	if (uuid.isEmpty())
		return;
	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(uuid));
	if (!source || !obs_volmeter_attach_source(channel.meter, source))
		SetStatus(QStringLiteral("%1 LINK FAILED // REFRESH SOURCES").arg(label), true);
}

void TempestSignalReactor::AttachDesktopSource()
{
	AttachChannel(desktopChannel, desktopSource, QStringLiteral("DESKTOP ENERGY"));
	SaveState();
}

void TempestSignalReactor::AttachMicrophoneSource()
{
	AttachChannel(microphoneChannel, microphoneSource, QStringLiteral("MICROPHONE"));
	SaveState();
}

void TempestSignalReactor::TriggerPulse(float strength, const QString &origin)
{
	const float boundedStrength = std::clamp(strength, 0.05f, 1.5f);
	manualPulse = std::max(manualPulse, boundedStrength);
	if (!reactorEnabled->isChecked())
		reactorEnabled->setChecked(true);
	emit PulseTriggered(boundedStrength, origin);
}

void TempestSignalReactor::SetWebSocketReady(bool ready)
{
	webSocketReady = ready;
	UpdateControlBridgeState();
}

void TempestSignalReactor::UpdateControlBridgeState()
{
	if (!controlLabel)
		return;
	const bool hotkeysReady = pulseHotkeys.size() == 2;
	QString state;
	if (hotkeysReady && webSocketReady)
		state = QStringLiteral("CONTROL BRIDGE // 2 HOTKEYS + WEBSOCKET VENDOR API READY");
	else if (hotkeysReady)
		state = QStringLiteral("CONTROL BRIDGE // 2 HOTKEYS READY");
	else
		state = QStringLiteral("CONTROL BRIDGE // INITIALIZING");
	controlLabel->setText(state);
	controlLabel->setAccessibleName(state);
	controlLabel->setAccessibleDescription(state);
}

void TempestSignalReactor::PublishTelemetry()
{
	const float decay = (float)smoothing->value();
	auto processChannel = [decay](SignalChannel &channel, float input) {
		channel.smoothedLevel = std::max(input, channel.smoothedLevel * decay);
		return channel.smoothedLevel;
	};
	const float desktopInput =
		std::clamp(desktopChannel.rawLevel.load(std::memory_order_relaxed) * (float)desktopSensitivity->value(),
			   0.0f, 1.5f);
	const float microphoneInput = std::clamp(microphoneChannel.rawLevel.load(std::memory_order_relaxed) *
							 (float)microphoneSensitivity->value(),
						 0.0f, 1.5f);
	float desktop = processChannel(desktopChannel, desktopInput);
	float microphone = processChannel(microphoneChannel, microphoneInput);
	beatBaseline += (desktopInput - beatBaseline) * 0.055f;
	const float transient = std::max(0.0f, desktopInput - beatBaseline);
	beatLevel = std::max(transient * (float)beatSensitivity->value() * 2.6f, beatLevel * 0.68f);
	float beat = std::clamp(beatLevel, 0.0f, 1.5f);
	manualPulse *= decay;
	if (!reactorEnabled->isChecked()) {
		desktop = 0.0f;
		microphone = 0.0f;
		beat = 0.0f;
		beatBaseline = 0.0f;
		beatLevel = 0.0f;
		manualPulse = 0.0f;
	}
	const float master = std::clamp(std::max({desktop, microphone, manualPulse}), 0.0f, 1.5f);
	desktopMeter->setValue((int)(std::min(desktop, 1.0f) * 1000.0f));
	microphoneMeter->setValue((int)(std::min(microphone, 1.0f) * 1000.0f));
	beatMeter->setValue((int)(std::min(beat, 1.0f) * 1000.0f));
	masterMeter->setValue((int)(std::min(master, 1.0f) * 1000.0f));
	statusLabel->setStyleSheet(QStringLiteral("color:#45d9ff;"));
	statusLabel->setText(QStringLiteral("MASTER %1% // DESKTOP %2% // VOICE %3% // BEAT %4%")
				     .arg(qRound(master * 100.0f), 3)
				     .arg(qRound(desktop * 100.0f), 3)
				     .arg(qRound(microphone * 100.0f), 3)
				     .arg(qRound(beat * 100.0f), 3));

	if (telemetryPath.isEmpty() && !EnsureOutputDirectory())
		return;
	QJsonObject telemetry;
	telemetry.insert(QStringLiteral("level"), master);
	telemetry.insert(QStringLiteral("master"), master);
	telemetry.insert(QStringLiteral("desktop"), desktop);
	telemetry.insert(QStringLiteral("microphone"), microphone);
	telemetry.insert(QStringLiteral("beat"), beat);
	telemetry.insert(QStringLiteral("pulse"), manualPulse);
	telemetry.insert(QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch());
	QSaveFile file(telemetryPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	file.write(QJsonDocument(telemetry).toJson(QJsonDocument::Compact));
	file.commit();
}

void TempestSignalReactor::SetStatus(const QString &message, bool error)
{
	statusLabel->setStyleSheet(error ? QStringLiteral("color:#ff4b70;") : QStringLiteral("color:#45d9ff;"));
	statusLabel->setText(message);
}
