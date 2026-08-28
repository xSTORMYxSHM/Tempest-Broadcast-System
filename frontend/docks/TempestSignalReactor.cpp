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
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
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

bool IsTempestBrowserSource(obs_source_t *source)
{
	if (!source || strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0)
		return false;
	const char *name = obs_source_get_name(source);
	return name && strncmp(name, "Tempest", 7) == 0;
}

bool EnableTempestBrowserLifecycle(void *, obs_source_t *source)
{
	if (!IsTempestBrowserSource(source))
		return true;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	if (!obs_data_get_bool(settings, "shutdown") || !obs_data_get_bool(settings, "fps_custom") ||
	    obs_data_get_int(settings, "fps") != 30) {
		obs_data_set_bool(settings, "shutdown", true);
		obs_data_set_bool(settings, "fps_custom", true);
		obs_data_set_int(settings, "fps", 30);
		obs_source_update(source, settings);
	}
	return true;
}

bool PublishTempestBrowserTelemetry(void *param, obs_source_t *source)
{
	if (!IsTempestBrowserSource(source))
		return true;
	auto *payload = static_cast<const QByteArray *>(param);
	calldata_t data = {0};
	calldata_set_string(&data, "eventName", "tempestTelemetry");
	calldata_set_string(&data, "jsonString", payload->constData());
	proc_handler_call(obs_source_get_proc_handler(source), "javascript_event", &data);
	calldata_free(&data);
	return true;
}
} // namespace

TempestSignalReactor::TempestSignalReactor(OBSBasic *main, QWidget *parent) : OBSDock(parent), main(main)
{
	setObjectName(QStringLiteral("tempestSignalReactor"));
	setWindowTitle(QStringLiteral("Audio Reactor"));
	setMinimumWidth(360);

	BuildInterface();
	EnableContentScaling(objectName());
	EnsureOutputDirectory();
	CreateMeter(desktopChannel);
	CreateMeter(microphoneChannel);
	LoadState();

	telemetryTimer = new QTimer(this);
	telemetryTimer->setInterval(100);
	connect(telemetryTimer, &QTimer::timeout, this, &TempestSignalReactor::PublishTelemetry);
	telemetryTimer->start();

	RefreshAudioSources();
	QTimer::singleShot(3500, this, &TempestSignalReactor::RefreshAudioSources);
	// Scene collections finish loading after the docks are constructed.  Delay the
	// one-time browser-source migration so it sees the restored scene sources.
	QTimer::singleShot(6000, this, [this]() {
		obs_enum_sources(EnableTempestBrowserLifecycle, nullptr);
		if (this->main)
			this->main->SaveProject();
	});
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
		QLabel#reactorCircuitState { color: #748fa4; font-size: 9px; letter-spacing: 1px; }
		QLabel#reactorControl { color: #9b8cff; font-size: 10px; letter-spacing: 1px; padding: 5px; border: 1px solid #302d67; background: #090d1d; }
		QLabel#reactorHint { color: #748fa4; font-size: 10px; padding: 7px; border: 1px solid #183a50; background: #06101a; }
		QFrame#reactorChannel { background: #081a27; border: 1px solid #183a50; }
		QFrame#reactorCircuit { background: #06131e; border: 1px solid #17394f; }
		QComboBox, QDoubleSpinBox { min-height: 29px; padding: 0 7px; color: #bdf6ff; background: #06101a; border: 1px solid #1f506d; }
		QProgressBar { min-height: 9px; max-height: 9px; border: 1px solid #17394f; background: #04101a; }
		QProgressBar::chunk { background: #45d9ff; }
		QProgressBar#reactorCircuitMeter::chunk { background: #9b8cff; }
		QPushButton { min-height: 31px; padding: 0 9px; color: #bdf6ff; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QPushButton#reactorCircuitButton { min-height: 24px; padding: 0 3px; font-size: 8px; }
		QPushButton#reactorCircuitButton:checked { color: #ffffff; border-color: #9b8cff; background: #302d67; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QCheckBox { color: #9eb7c8; }
	)"));
	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *title = new QLabel(QStringLiteral("AUDIO REACTOR"), root);
	title->setObjectName(QStringLiteral("reactorTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Audio-driven reactions for overlays and scene sources"), root);
	subtitle->setObjectName(QStringLiteral("reactorSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	reactorEnabled = new QCheckBox(QStringLiteral("REACTOR ONLINE"), root);
	reactorEnabled->setAccessibleName(QStringLiteral("Enable reactive audio engine"));
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

	auto *masterLabel = new QLabel(QStringLiteral("MASTER REACTION BUS"), root);
	masterLabel->setObjectName(QStringLiteral("reactorChannelLabel"));
	masterMeter = new QProgressBar(root);
	masterMeter->setRange(0, 1000);
	masterMeter->setTextVisible(false);
	layout->addWidget(masterLabel);
	layout->addWidget(masterMeter);

	auto *directorFrame = new QFrame(root);
	directorFrame->setObjectName(QStringLiteral("reactorChannel"));
	directorFrame->setAccessibleName(QStringLiteral("Global overlay Reactivity Director"));
	auto *directorLayout = new QVBoxLayout(directorFrame);
	directorLayout->setContentsMargins(8, 8, 8, 8);
	directorLayout->setSpacing(6);
	auto *directorLabel = new QLabel(QStringLiteral("REACTIVITY DIRECTOR // GLOBAL OVERLAY BUS"), directorFrame);
	directorLabel->setObjectName(QStringLiteral("reactorChannelLabel"));
	auto *directorHint =
		new QLabel(QStringLiteral("One persistent response profile drives every generated stream overlay."),
			   directorFrame);
	directorHint->setObjectName(QStringLiteral("reactorHint"));
	directorHint->setWordWrap(true);
	reactionProfile = new QComboBox(directorFrame);
	reactionProfile->addItem(QStringLiteral("CALM"), QStringLiteral("calm"));
	reactionProfile->addItem(QStringLiteral("STANDARD"), QStringLiteral("mainframe"));
	reactionProfile->addItem(QStringLiteral("HIGH ENERGY"), QStringLiteral("storm"));
	reactionProfile->addItem(QStringLiteral("ALERT DANCE"), QStringLiteral("dance"));
	reactionProfile->addItem(QStringLiteral("ALERT WARNING"), QStringLiteral("alert"));
	reactionProfile->setAccessibleName(QStringLiteral("Reactivity Director profile"));
	reactionPalette = new QComboBox(directorFrame);
	reactionPalette->addItem(QStringLiteral("TEMPEST // CYAN TO MAGENTA"), QStringLiteral("tempest"));
	reactionPalette->addItem(QStringLiteral("ULTRAVIOLET"), QStringLiteral("ultraviolet"));
	reactionPalette->addItem(QStringLiteral("EMBER WARNING"), QStringLiteral("ember"));
	reactionPalette->addItem(QStringLiteral("VERDANT"), QStringLiteral("verdant"));
	reactionPalette->addItem(QStringLiteral("FULL SPECTRUM"), QStringLiteral("spectrum"));
	reactionPalette->setAccessibleName(QStringLiteral("Global reactive color palette"));
	reactionThreshold = new QDoubleSpinBox(directorFrame);
	reactionThreshold->setRange(0.0, 0.80);
	reactionThreshold->setSingleStep(0.01);
	reactionThreshold->setDecimals(2);
	reactionThreshold->setAccessibleName(QStringLiteral("Global reaction noise threshold"));
	reactionAttack = new QDoubleSpinBox(directorFrame);
	reactionAttack->setRange(0.05, 1.0);
	reactionAttack->setSingleStep(0.05);
	reactionAttack->setDecimals(2);
	reactionAttack->setAccessibleName(QStringLiteral("Global reaction attack speed"));
	smoothing = new QDoubleSpinBox(directorFrame);
	smoothing->setRange(0.50, 0.98);
	smoothing->setSingleStep(0.02);
	smoothing->setDecimals(2);
	smoothing->setAccessibleName(QStringLiteral("Global reaction release smoothing"));
	reactionMotion = new QDoubleSpinBox(directorFrame);
	reactionMotion->setRange(0.0, 200.0);
	reactionMotion->setSingleStep(5.0);
	reactionMotion->setDecimals(0);
	reactionMotion->setSuffix(QStringLiteral(" %"));
	reactionMotion->setAccessibleName(QStringLiteral("Global overlay motion intensity"));
	reactionGlow = new QDoubleSpinBox(directorFrame);
	reactionGlow->setRange(0.0, 200.0);
	reactionGlow->setSingleStep(5.0);
	reactionGlow->setDecimals(0);
	reactionGlow->setSuffix(QStringLiteral(" %"));
	reactionGlow->setAccessibleName(QStringLiteral("Global overlay glow intensity"));
	reactionTestStrength = new QDoubleSpinBox(directorFrame);
	reactionTestStrength->setRange(5.0, 150.0);
	reactionTestStrength->setSingleStep(5.0);
	reactionTestStrength->setDecimals(0);
	reactionTestStrength->setSuffix(QStringLiteral(" %"));
	reactionTestStrength->setAccessibleName(QStringLiteral("Reactivity Director test signal strength"));
	reducedMotion = new QCheckBox(QStringLiteral("REDUCED MOTION // KEEP COLOR + GLOW"), directorFrame);
	reducedMotion->setAccessibleName(QStringLiteral("Reduce overlay reaction motion"));
	auto *directorForm = new QFormLayout();
	directorForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	directorForm->addRow(QStringLiteral("Response profile"), reactionProfile);
	directorForm->addRow(QStringLiteral("Color palette"), reactionPalette);
	directorForm->addRow(QStringLiteral("Noise threshold"), reactionThreshold);
	directorForm->addRow(QStringLiteral("Attack"), reactionAttack);
	directorForm->addRow(QStringLiteral("Release"), smoothing);
	directorForm->addRow(QStringLiteral("Motion"), reactionMotion);
	directorForm->addRow(QStringLiteral("Glow"), reactionGlow);
	directorForm->addRow(QStringLiteral("Test strength"), reactionTestStrength);
	auto *directorButtons = new QHBoxLayout();
	auto *applyProfile = new QPushButton(QStringLiteral("APPLY PRESET"), directorFrame);
	auto *testProfile = new QPushButton(QStringLiteral("TEST PROFILE"), directorFrame);
	applyProfile->setAccessibleName(QStringLiteral("Apply selected Reactivity Director preset"));
	testProfile->setAccessibleName(QStringLiteral("Test current Reactivity Director settings"));
	directorButtons->addWidget(applyProfile);
	directorButtons->addWidget(testProfile);
	directorLayout->addWidget(directorLabel);
	directorLayout->addWidget(directorHint);
	directorLayout->addLayout(directorForm);
	directorLayout->addWidget(reducedMotion);
	directorLayout->addLayout(directorButtons);
	layout->addWidget(directorFrame);

	auto *networkFrame = new QFrame(root);
	networkFrame->setObjectName(QStringLiteral("reactorChannel"));
	networkFrame->setAccessibleName(QStringLiteral("Source reaction network controls"));
	auto *networkLayout = new QVBoxLayout(networkFrame);
	networkLayout->setContentsMargins(8, 8, 8, 8);
	networkLayout->setSpacing(6);
	auto *networkLabel = new QLabel(QStringLiteral("REACTIVE SOURCE CONTROLS"), networkFrame);
	networkLabel->setObjectName(QStringLiteral("reactorChannelLabel"));
	sourceNetworkStatus = new QLabel(QStringLiteral("NETWORK // NO SOURCE RIGS BOUND"), networkFrame);
	sourceNetworkStatus->setObjectName(QStringLiteral("reactorStatus"));
	sourceNetworkStatus->setAccessibleName(QStringLiteral("Source reaction network binding status"));
	sourceNetworkArmed = new QCheckBox(QStringLiteral("ARM SOURCE REACTIONS"), networkFrame);
	sourceNetworkArmed->setAccessibleName(QStringLiteral("Arm source reaction network"));
	sourceNetworkActiveSceneOnly = new QCheckBox(QStringLiteral("ACTIVE SCENE RIGS ONLY"), networkFrame);
	sourceNetworkActiveSceneOnly->setAccessibleName(QStringLiteral("Limit source reactions to active scene"));
	sourceNetworkCircuitProfile = new QComboBox(networkFrame);
	sourceNetworkCircuitProfile->addItem(QStringLiteral("ALL CIRCUITS"), QStringLiteral("all"));
	sourceNetworkCircuitProfile->addItem(QStringLiteral("AMBIENT // CORE + FRAME + PLATES"),
					     QStringLiteral("ambient"));
	sourceNetworkCircuitProfile->addItem(QStringLiteral("CONVERSATION // CORE + FRAME + CHAT + PLATES"),
					     QStringLiteral("conversation"));
	sourceNetworkCircuitProfile->addItem(QStringLiteral("ALERT FOCUS // CORE + FRAME + ALERTS"),
					     QStringLiteral("alert"));
	sourceNetworkCircuitProfile->addItem(QStringLiteral("CORE ONLY"), QStringLiteral("core"));
	sourceNetworkCircuitProfile->setAccessibleName(QStringLiteral("Source reaction circuit profile"));
	auto *circuitMixer = new QGridLayout();
	circuitMixer->setHorizontalSpacing(6);
	circuitMixer->setVerticalSpacing(5);
	struct CircuitControl {
		const char *id;
		const char *label;
	};
	constexpr CircuitControl circuitControls[] = {
		{"core", "CORE"}, {"frame", "FRAME"}, {"chat", "CHAT"}, {"plates", "PLATES"}, {"alerts", "ALERTS"},
	};
	for (int index = 0; index < 5; ++index) {
		const CircuitControl &control = circuitControls[index];
		const QString circuit = QString::fromUtf8(control.id);
		const QString label = QString::fromUtf8(control.label);
		auto *circuitFrame = new QFrame(networkFrame);
		circuitFrame->setObjectName(QStringLiteral("reactorCircuit"));
		auto *circuitLayout = new QVBoxLayout(circuitFrame);
		circuitLayout->setContentsMargins(6, 6, 6, 6);
		circuitLayout->setSpacing(4);
		auto *gain = new QDoubleSpinBox(circuitFrame);
		gain->setRange(0.0, 200.0);
		gain->setDecimals(0);
		gain->setSingleStep(5.0);
		gain->setPrefix(label + QStringLiteral("  "));
		gain->setSuffix(QStringLiteral(" %"));
		gain->setValue(100.0);
		gain->setAccessibleName(QStringLiteral("%1 reaction circuit gain").arg(label));
		auto *meter = new QProgressBar(circuitFrame);
		meter->setObjectName(QStringLiteral("reactorCircuitMeter"));
		meter->setRange(0, 2000);
		meter->setTextVisible(false);
		meter->setAccessibleName(QStringLiteral("%1 reaction circuit activity").arg(label));
		auto *state = new QLabel(QStringLiteral("UNBOUND"), circuitFrame);
		state->setObjectName(QStringLiteral("reactorCircuitState"));
		state->setAccessibleName(QStringLiteral("%1 reaction circuit state // UNBOUND").arg(label));
		auto *circuitControls = new QHBoxLayout();
		circuitControls->setSpacing(3);
		auto *mute = new QPushButton(QStringLiteral("MUTE"), circuitFrame);
		auto *solo = new QPushButton(QStringLiteral("SOLO"), circuitFrame);
		auto *test = new QPushButton(QStringLiteral("TEST"), circuitFrame);
		for (QPushButton *button : {mute, solo, test})
			button->setObjectName(QStringLiteral("reactorCircuitButton"));
		mute->setCheckable(true);
		solo->setCheckable(true);
		mute->setAccessibleName(QStringLiteral("Mute %1 reaction circuit").arg(label));
		solo->setAccessibleName(QStringLiteral("Solo %1 reaction circuit").arg(label));
		test->setAccessibleName(QStringLiteral("Test %1 reaction circuit").arg(label));
		circuitControls->addWidget(mute);
		circuitControls->addWidget(solo);
		circuitControls->addWidget(test);
		circuitLayout->addWidget(gain);
		circuitLayout->addWidget(meter);
		circuitLayout->addWidget(state);
		circuitLayout->addLayout(circuitControls);
		sourceNetworkCircuitGains.insert(circuit, gain);
		sourceNetworkCircuitMeters.insert(circuit, meter);
		sourceNetworkCircuitStates.insert(circuit, state);
		sourceNetworkCircuitMuteButtons.insert(circuit, mute);
		sourceNetworkCircuitSoloButtons.insert(circuit, solo);
		sourceNetworkCircuitTestButtons.insert(circuit, test);
		sourceNetworkCircuitActivities.insert(circuit, 0.0f);
		sourceNetworkCircuitRestoreGains.insert(circuit, 100.0);
		connect(mute, &QPushButton::clicked, this,
			[this, circuit]() { ToggleSourceNetworkCircuitMute(circuit); });
		connect(solo, &QPushButton::clicked, this,
			[this, circuit]() { ToggleSourceNetworkCircuitSolo(circuit); });
		connect(test, &QPushButton::clicked, this,
			[this, circuit]() { emit SourceNetworkCircuitTestRequested(circuit); });
		circuitMixer->addWidget(circuitFrame, index / 2, index % 2);
	}
	sourceNetworkIntensity = new QDoubleSpinBox(networkFrame);
	sourceNetworkIntensity->setRange(0.0, 200.0);
	sourceNetworkIntensity->setDecimals(0);
	sourceNetworkIntensity->setSingleStep(5.0);
	sourceNetworkIntensity->setSuffix(QStringLiteral(" %"));
	sourceNetworkIntensity->setAccessibleName(QStringLiteral("Source reaction network intensity"));
	auto *networkButtons = new QHBoxLayout();
	auto *testNetwork = new QPushButton(QStringLiteral("TEST NETWORK"), networkFrame);
	auto *restoreNetwork = new QPushButton(QStringLiteral("DISARM + RESTORE"), networkFrame);
	auto *resetMixer = new QPushButton(QStringLiteral("RESET CIRCUIT MIXER"), networkFrame);
	testNetwork->setAccessibleName(QStringLiteral("Test all source reaction rigs"));
	restoreNetwork->setAccessibleName(QStringLiteral("Disarm source reactions and restore all bases"));
	resetMixer->setAccessibleName(QStringLiteral("Reset source reaction circuit mixer"));
	networkButtons->addWidget(testNetwork);
	networkButtons->addWidget(restoreNetwork);
	networkLayout->addWidget(networkLabel);
	networkLayout->addWidget(sourceNetworkStatus);
	networkLayout->addWidget(sourceNetworkArmed);
	networkLayout->addWidget(sourceNetworkActiveSceneOnly);
	networkLayout->addWidget(sourceNetworkCircuitProfile);
	networkLayout->addLayout(circuitMixer);
	networkLayout->addWidget(resetMixer);
	networkLayout->addWidget(sourceNetworkIntensity);
	networkLayout->addLayout(networkButtons);
	layout->addWidget(networkFrame);

	auto *externalFrame = new QFrame(root);
	externalFrame->setObjectName(QStringLiteral("reactorChannel"));
	externalFrame->setAccessibleName(QStringLiteral("Warudo and Twitch reaction event bridge"));
	auto *externalLayout = new QVBoxLayout(externalFrame);
	externalLayout->setContentsMargins(8, 8, 8, 8);
	externalLayout->setSpacing(6);
	auto *externalLabel = new QLabel(QStringLiteral("EXTERNAL EVENT BRIDGE // WARUDO + TWITCH"), externalFrame);
	externalLabel->setObjectName(QStringLiteral("reactorChannelLabel"));
	externalEventStatus = new QLabel(QStringLiteral("EVENT BUS // STANDBY"), externalFrame);
	externalEventStatus->setObjectName(QStringLiteral("reactorStatus"));
	externalEventStatus->setWordWrap(true);
	externalEventBridgeArmed = new QCheckBox(QStringLiteral("ACCEPT EXTERNAL REACTION EVENTS"), externalFrame);
	externalEventBridgeArmed->setAccessibleName(QStringLiteral("Accept Warudo and Twitch reaction events"));
	auto populateCircuitSelector = [](QComboBox *selector) {
		selector->addItem(QStringLiteral("ALL HUD CIRCUITS"), QStringLiteral("all"));
		selector->addItem(QStringLiteral("FRAME"), QStringLiteral("frame"));
		selector->addItem(QStringLiteral("ALERTS"), QStringLiteral("alerts"));
		selector->addItem(QStringLiteral("PLATES"), QStringLiteral("plates"));
		selector->addItem(QStringLiteral("CHAT"), QStringLiteral("chat"));
		selector->addItem(QStringLiteral("CORE"), QStringLiteral("core"));
	};
	externalDanceCircuit = new QComboBox(externalFrame);
	externalTwitchCircuit = new QComboBox(externalFrame);
	populateCircuitSelector(externalDanceCircuit);
	populateCircuitSelector(externalTwitchCircuit);
	externalDanceCircuit->setAccessibleName(QStringLiteral("Sound alert dance target circuit"));
	externalTwitchCircuit->setAccessibleName(QStringLiteral("Twitch interaction target circuit"));
	externalEventCooldown = new QDoubleSpinBox(externalFrame);
	externalEventCooldown->setRange(0.0, 10.0);
	externalEventCooldown->setSingleStep(0.1);
	externalEventCooldown->setDecimals(1);
	externalEventCooldown->setSuffix(QStringLiteral(" s"));
	externalEventCooldown->setAccessibleName(QStringLiteral("External event duplicate cooldown"));
	auto *externalForm = new QFormLayout();
	externalForm->addRow(QStringLiteral("Dance target"), externalDanceCircuit);
	externalForm->addRow(QStringLiteral("Twitch target"), externalTwitchCircuit);
	externalForm->addRow(QStringLiteral("Duplicate cooldown"), externalEventCooldown);
	auto *externalButtons = new QHBoxLayout();
	auto *testDance = new QPushButton(QStringLiteral("TEST DANCE"), externalFrame);
	auto *testTwitch = new QPushButton(QStringLiteral("TEST TWITCH"), externalFrame);
	auto *clearExternal = new QPushButton(QStringLiteral("CLEAR"), externalFrame);
	testDance->setAccessibleName(QStringLiteral("Test Warudo sound alert dance reaction"));
	testTwitch->setAccessibleName(QStringLiteral("Test Twitch interaction reaction"));
	clearExternal->setAccessibleName(QStringLiteral("Clear current external reaction event"));
	externalButtons->addWidget(testDance);
	externalButtons->addWidget(testTwitch);
	externalButtons->addWidget(clearExternal);
	externalLayout->addWidget(externalLabel);
	externalLayout->addWidget(externalEventStatus);
	externalLayout->addWidget(externalEventBridgeArmed);
	externalLayout->addLayout(externalForm);
	externalLayout->addLayout(externalButtons);
	layout->addWidget(externalFrame);

	auto *pulseRow = new QHBoxLayout();
	pulseButton = new QPushButton(QStringLiteral("TEST PULSE"), root);
	peakButton = new QPushButton(QStringLiteral("TEST PEAK"), root);
	pulseButton->setAccessibleName(QStringLiteral("Trigger reaction pulse"));
	peakButton->setAccessibleName(QStringLiteral("Trigger reaction peak"));
	auto *refresh = new QPushButton(QStringLiteral("REFRESH SOURCES"), root);
	refresh->setAccessibleName(QStringLiteral("Refresh Audio Reactor sources"));
	pulseRow->addWidget(pulseButton);
	pulseRow->addWidget(peakButton);
	pulseRow->addWidget(refresh);
	layout->addLayout(pulseRow);
	controlLabel = new QLabel(QStringLiteral("CONTROL BRIDGE // INITIALIZING"), root);
	controlLabel->setObjectName(QStringLiteral("reactorControl"));
	controlLabel->setAccessibleName(QStringLiteral("Audio Reactor external control status"));
	layout->addWidget(controlLabel);

	auto *hint = new QLabel(
		QStringLiteral(
			"Desktop and microphone energy feed the master bus. The Beat bus extracts fast music transients from Desktop Energy. Warudo can trigger the named Sound Alert Dance and Twitch Interaction hotkeys; richer clients can call tempest-mainframe / TriggerReactionEvent."),
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
	connect(reactorEnabled, &QCheckBox::toggled, this, [this]() {
		SaveState();
		RefreshSourceNetworkCircuitMonitors();
	});
	connect(desktopSensitivity, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(microphoneSensitivity, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(beatSensitivity, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(smoothing, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(reactionProfile, &QComboBox::currentIndexChanged, this, &TempestSignalReactor::SaveState);
	connect(reactionPalette, &QComboBox::currentIndexChanged, this, &TempestSignalReactor::SaveState);
	connect(reactionThreshold, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(reactionAttack, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(reactionMotion, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(reactionGlow, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(reactionTestStrength, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(reducedMotion, &QCheckBox::toggled, this, &TempestSignalReactor::SaveState);
	connect(applyProfile, &QPushButton::clicked, this, &TempestSignalReactor::ApplyReactivityProfile);
	connect(testProfile, &QPushButton::clicked, this,
		[this]() { TriggerPulse(float(reactionTestStrength->value() / 100.0), QStringLiteral("director")); });
	connect(sourceNetworkArmed, &QCheckBox::toggled, this, [this](bool armed) {
		SaveState();
		RefreshSourceNetworkCircuitMonitors();
		emit SourceNetworkArmedChanged(armed);
	});
	connect(sourceNetworkIntensity, &QDoubleSpinBox::valueChanged, this, [this](double intensity) {
		SaveState();
		RefreshSourceNetworkCircuitMonitors();
		emit SourceNetworkIntensityChanged(float(intensity / 100.0));
	});
	connect(sourceNetworkActiveSceneOnly, &QCheckBox::toggled, this, [this](bool activeSceneOnly) {
		SaveState();
		RefreshSourceNetworkCircuitMonitors();
		emit SourceNetworkScopeChanged(activeSceneOnly);
	});
	connect(sourceNetworkCircuitProfile, &QComboBox::currentIndexChanged, this, [this](int) {
		SaveState();
		SetSourceNetworkSoloCircuit(QString());
		RefreshSourceNetworkCircuitMonitors();
		emit SourceNetworkCircuitProfileChanged(SourceNetworkCircuitProfile());
	});
	for (auto it = sourceNetworkCircuitGains.begin(); it != sourceNetworkCircuitGains.end(); ++it) {
		const QString circuit = it.key();
		connect(it.value(), &QDoubleSpinBox::valueChanged, this, [this, circuit](double value) {
			if (value > 0.0)
				sourceNetworkCircuitRestoreGains[circuit] = value;
			SaveState();
			RefreshSourceNetworkCircuitMonitors();
			emit SourceNetworkCircuitGainsChanged(SourceNetworkCircuitGain(QStringLiteral("core")),
							      SourceNetworkCircuitGain(QStringLiteral("frame")),
							      SourceNetworkCircuitGain(QStringLiteral("chat")),
							      SourceNetworkCircuitGain(QStringLiteral("plates")),
							      SourceNetworkCircuitGain(QStringLiteral("alerts")));
		});
	}
	connect(resetMixer, &QPushButton::clicked, this, &TempestSignalReactor::ResetSourceNetworkCircuitGains);
	connect(testNetwork, &QPushButton::clicked, this, &TempestSignalReactor::TestSourceNetwork);
	connect(restoreNetwork, &QPushButton::clicked, this, &TempestSignalReactor::DisarmAndRestoreSourceNetwork);
	connect(externalEventBridgeArmed, &QCheckBox::toggled, this, [this](bool armed) {
		SaveState();
		if (!armed)
			ClearExternalEvent();
	});
	connect(externalDanceCircuit, &QComboBox::currentIndexChanged, this, &TempestSignalReactor::SaveState);
	connect(externalTwitchCircuit, &QComboBox::currentIndexChanged, this, &TempestSignalReactor::SaveState);
	connect(externalEventCooldown, &QDoubleSpinBox::valueChanged, this, &TempestSignalReactor::SaveState);
	connect(testDance, &QPushButton::clicked, this, [this]() {
		TriggerExternalEvent(QStringLiteral("sound_alert_dance"), QStringLiteral("SOUND ALERT DANCE"), 1.2f,
				     6000, externalDanceCircuit->currentData().toString(), QStringLiteral("#FF3EC8"),
				     QStringLiteral("spectrum"), QStringLiteral("dock"), QStringLiteral("dock-dance"),
				     0);
	});
	connect(testTwitch, &QPushButton::clicked, this, [this]() {
		TriggerExternalEvent(QStringLiteral("twitch_interaction"), QStringLiteral("TWITCH INTERACTION"), 1.0f,
				     2600, externalTwitchCircuit->currentData().toString(), QStringLiteral("#9B8CFF"),
				     QStringLiteral("glitch"), QStringLiteral("dock"), QStringLiteral("dock-twitch"),
				     0);
	});
	connect(clearExternal, &QPushButton::clicked, this, &TempestSignalReactor::ClearExternalEvent);
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
		{"TempestMainframe.Signal.Pulse", "Tempest Broadcast: Trigger Reaction Pulse", 0.65f},
		{"TempestMainframe.Signal.Peak", "Tempest Broadcast: Trigger Reaction Peak", 1.0f},
	};
	for (const Definition &definition : definitions) {
		const obs_hotkey_id id =
			obs_hotkey_register_frontend(definition.name, definition.description, HotkeyCallback, this);
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		pulseHotkeys.insert(id, definition.strength);
		LoadHotkey(id, QByteArray(definition.name));
	}
	struct NetworkDefinition {
		const char *name;
		const char *description;
		const char *action;
	};
	constexpr NetworkDefinition networkDefinitions[] = {
		{"TempestMainframe.ReactionNetwork.Toggle", "Tempest Broadcast: Toggle Source Reactions", "toggle"},
		{"TempestMainframe.ReactionNetwork.Test", "Tempest Broadcast: Test All Source Reactions", "test"},
		{"TempestMainframe.ReactionNetwork.Restore", "Tempest Broadcast: Disable Reactions and Restore Sources",
		 "restore"},
		{"TempestMainframe.ReactionNetwork.Scope", "Tempest Broadcast: Toggle Active Scene Reaction Scope",
		 "scope"},
		{"TempestMainframe.ReactionNetwork.Circuits", "Tempest Broadcast: Cycle Source Reaction Profile",
		 "circuits"},
		{"TempestMainframe.ReactionNetwork.MixerReset", "Tempest Broadcast: Reset Source Reaction Mixer",
		 "mixer-reset"},
	};
	for (const NetworkDefinition &definition : networkDefinitions) {
		const obs_hotkey_id id =
			obs_hotkey_register_frontend(definition.name, definition.description, HotkeyCallback, this);
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		networkHotkeys.insert(id, QString::fromUtf8(definition.action));
		LoadHotkey(id, QByteArray(definition.name));
	}
	struct EventDefinition {
		const char *name;
		const char *description;
		const char *eventType;
	};
	constexpr EventDefinition eventDefinitions[] = {
		{"TempestMainframe.ExternalEvent.SoundAlertDance",
		 "Tempest Broadcast: Warudo Sound Alert Dance Reaction", "sound_alert_dance"},
		{"TempestMainframe.ExternalEvent.TwitchInteraction", "Tempest Broadcast: Twitch Interaction Reaction",
		 "twitch_interaction"},
	};
	for (const EventDefinition &definition : eventDefinitions) {
		const obs_hotkey_id id =
			obs_hotkey_register_frontend(definition.name, definition.description, HotkeyCallback, this);
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		externalEventHotkeys.insert(id, QString::fromUtf8(definition.eventType));
		LoadHotkey(id, QByteArray(definition.name));
	}
	UpdateControlBridgeState();
}

void TempestSignalReactor::UnregisterHotkeys()
{
	for (auto it = pulseHotkeys.cbegin(); it != pulseHotkeys.cend(); ++it)
		obs_hotkey_unregister(it.key());
	pulseHotkeys.clear();
	for (auto it = networkHotkeys.cbegin(); it != networkHotkeys.cend(); ++it)
		obs_hotkey_unregister(it.key());
	networkHotkeys.clear();
	for (auto it = externalEventHotkeys.cbegin(); it != externalEventHotkeys.cend(); ++it)
		obs_hotkey_unregister(it.key());
	externalEventHotkeys.clear();
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
	const QString networkAction = reactor->networkHotkeys.value(id);
	const QString externalEventType = reactor->externalEventHotkeys.value(id);
	if (strength <= 0.0f && networkAction.isEmpty() && externalEventType.isEmpty())
		return;
	QPointer<TempestSignalReactor> guarded(reactor);
	QMetaObject::invokeMethod(
		reactor,
		[guarded, strength, networkAction, externalEventType]() {
			if (!guarded)
				return;
			if (strength > 0.0f) {
				guarded->TriggerPulse(strength, QStringLiteral("hotkey"));
			} else if (networkAction == QStringLiteral("toggle")) {
				guarded->SetSourceNetworkArmed(!guarded->SourceNetworkArmed());
			} else if (networkAction == QStringLiteral("test")) {
				guarded->TestSourceNetwork();
			} else if (networkAction == QStringLiteral("restore")) {
				guarded->DisarmAndRestoreSourceNetwork();
			} else if (networkAction == QStringLiteral("scope")) {
				guarded->SetSourceNetworkActiveSceneOnly(!guarded->SourceNetworkActiveSceneOnly());
			} else if (networkAction == QStringLiteral("circuits")) {
				guarded->CycleSourceNetworkCircuitProfile();
			} else if (networkAction == QStringLiteral("mixer-reset")) {
				guarded->ResetSourceNetworkCircuitGains();
			} else if (externalEventType == QStringLiteral("sound_alert_dance")) {
				guarded->TriggerExternalEvent(externalEventType, QStringLiteral("SOUND ALERT DANCE"),
							      1.2f, 6000,
							      guarded->externalDanceCircuit->currentData().toString(),
							      QStringLiteral("#FF3EC8"), QStringLiteral("spectrum"),
							      QStringLiteral("warudo-hotkey"));
			} else if (externalEventType == QStringLiteral("twitch_interaction")) {
				guarded->TriggerExternalEvent(externalEventType, QStringLiteral("TWITCH INTERACTION"),
							      1.0f, 2600,
							      guarded->externalTwitchCircuit->currentData().toString(),
							      QStringLiteral("#9B8CFF"), QStringLiteral("glitch"),
							      QStringLiteral("warudo-hotkey"));
			}
		},
		Qt::QueuedConnection);
}

void TempestSignalReactor::ApplyReactivityProfile()
{
	if (!reactionProfile || !reactionPalette)
		return;
	struct Preset {
		double threshold;
		double attack;
		double release;
		double motion;
		double glow;
		const char *palette;
	};
	const QString profile = reactionProfile->currentData().toString();
	Preset preset{0.05, 0.55, 0.82, 100.0, 100.0, "tempest"};
	if (profile == QStringLiteral("calm"))
		preset = {0.12, 0.28, 0.90, 45.0, 65.0, "tempest"};
	else if (profile == QStringLiteral("storm"))
		preset = {0.03, 0.72, 0.76, 140.0, 150.0, "ultraviolet"};
	else if (profile == QStringLiteral("dance"))
		preset = {0.01, 0.90, 0.70, 170.0, 165.0, "spectrum"};
	else if (profile == QStringLiteral("alert"))
		preset = {0.04, 0.80, 0.75, 115.0, 180.0, "ember"};

	const bool previousLoadingState = loadingState;
	loadingState = true;
	reactionThreshold->setValue(preset.threshold);
	reactionAttack->setValue(preset.attack);
	smoothing->setValue(preset.release);
	reactionMotion->setValue(preset.motion);
	reactionGlow->setValue(preset.glow);
	const int paletteIndex = reactionPalette->findData(QString::fromUtf8(preset.palette));
	if (paletteIndex >= 0)
		reactionPalette->setCurrentIndex(paletteIndex);
	reducedMotion->setChecked(false);
	loadingState = previousLoadingState;
	SaveState();
	SetStatus(QStringLiteral("DIRECTOR PRESET APPLIED // %1").arg(reactionProfile->currentText()));
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
	const QString savedReactionProfile =
		QString::fromUtf8(config_get_string(config, ConfigSection, "ReactionProfile"));
	const int reactionProfileIndex = reactionProfile->findData(savedReactionProfile);
	reactionProfile->setCurrentIndex(reactionProfileIndex >= 0
						 ? reactionProfileIndex
						 : reactionProfile->findData(QStringLiteral("mainframe")));
	const QString savedReactionPalette =
		QString::fromUtf8(config_get_string(config, ConfigSection, "ReactionPalette"));
	const int reactionPaletteIndex = reactionPalette->findData(savedReactionPalette);
	reactionPalette->setCurrentIndex(reactionPaletteIndex >= 0 ? reactionPaletteIndex : 0);
	const double savedReactionThreshold = config_get_double(config, ConfigSection, "ReactionThreshold");
	reactionThreshold->setValue(config_has_user_value(config, ConfigSection, "ReactionThreshold")
					    ? std::clamp(savedReactionThreshold, 0.0, 0.80)
					    : 0.05);
	const double savedReactionAttack = config_get_double(config, ConfigSection, "ReactionAttack");
	reactionAttack->setValue(config_has_user_value(config, ConfigSection, "ReactionAttack")
					 ? std::clamp(savedReactionAttack, 0.05, 1.0)
					 : 0.55);
	const double savedReactionMotion = config_get_double(config, ConfigSection, "ReactionMotion");
	reactionMotion->setValue(config_has_user_value(config, ConfigSection, "ReactionMotion")
					 ? std::clamp(savedReactionMotion, 0.0, 200.0)
					 : 100.0);
	const double savedReactionGlow = config_get_double(config, ConfigSection, "ReactionGlow");
	reactionGlow->setValue(config_has_user_value(config, ConfigSection, "ReactionGlow")
				       ? std::clamp(savedReactionGlow, 0.0, 200.0)
				       : 100.0);
	const double savedTestStrength = config_get_double(config, ConfigSection, "ReactionTestStrength");
	reactionTestStrength->setValue(config_has_user_value(config, ConfigSection, "ReactionTestStrength")
					       ? std::clamp(savedTestStrength, 5.0, 150.0)
					       : 65.0);
	reducedMotion->setChecked(config_get_bool(config, ConfigSection, "ReducedMotion"));
	sourceNetworkArmed->setChecked(!config_has_user_value(config, ConfigSection, "SourceNetworkArmed") ||
				       config_get_bool(config, ConfigSection, "SourceNetworkArmed"));
	sourceNetworkActiveSceneOnly->setChecked(
		!config_has_user_value(config, ConfigSection, "SourceNetworkActiveSceneOnly") ||
		config_get_bool(config, ConfigSection, "SourceNetworkActiveSceneOnly"));
	const QString savedCircuitProfile =
		QString::fromUtf8(config_get_string(config, ConfigSection, "SourceNetworkCircuitProfile"));
	const int circuitIndex = sourceNetworkCircuitProfile->findData(savedCircuitProfile);
	sourceNetworkCircuitProfile->setCurrentIndex(circuitIndex >= 0 ? circuitIndex : 0);
	for (auto it = sourceNetworkCircuitGains.begin(); it != sourceNetworkCircuitGains.end(); ++it) {
		const QByteArray key = QStringLiteral("SourceNetworkCircuitGain_%1").arg(it.key()).toUtf8();
		const double savedGain = config_has_user_value(config, ConfigSection, key.constData())
						 ? config_get_double(config, ConfigSection, key.constData())
						 : 100.0;
		it.value()->setValue(std::clamp(savedGain, 0.0, 200.0));
	}
	const double savedNetworkIntensity = config_get_double(config, ConfigSection, "SourceNetworkIntensity");
	sourceNetworkIntensity->setValue(config_has_user_value(config, ConfigSection, "SourceNetworkIntensity")
						 ? std::clamp(savedNetworkIntensity, 0.0, 200.0)
						 : 100.0);
	externalEventBridgeArmed->setChecked(
		!config_has_user_value(config, ConfigSection, "ExternalEventBridgeArmed") ||
		config_get_bool(config, ConfigSection, "ExternalEventBridgeArmed"));
	const QString savedDanceCircuit =
		QString::fromUtf8(config_get_string(config, ConfigSection, "ExternalDanceCircuit"));
	const QString savedTwitchCircuit =
		QString::fromUtf8(config_get_string(config, ConfigSection, "ExternalTwitchCircuit"));
	const int danceCircuitIndex = externalDanceCircuit->findData(savedDanceCircuit);
	const int twitchCircuitIndex = externalTwitchCircuit->findData(savedTwitchCircuit);
	externalDanceCircuit->setCurrentIndex(danceCircuitIndex >= 0 ? danceCircuitIndex : 0);
	externalTwitchCircuit->setCurrentIndex(twitchCircuitIndex >= 0
						       ? twitchCircuitIndex
						       : externalTwitchCircuit->findData(QStringLiteral("alerts")));
	const double savedExternalCooldown = config_get_double(config, ConfigSection, "ExternalEventCooldown");
	externalEventCooldown->setValue(config_has_user_value(config, ConfigSection, "ExternalEventCooldown")
						? std::clamp(savedExternalCooldown, 0.0, 10.0)
						: 0.8);
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
	config_set_string(config, ConfigSection, "ReactionProfile",
			  reactionProfile->currentData().toString().toUtf8().constData());
	config_set_string(config, ConfigSection, "ReactionPalette",
			  reactionPalette->currentData().toString().toUtf8().constData());
	config_set_double(config, ConfigSection, "ReactionThreshold", reactionThreshold->value());
	config_set_double(config, ConfigSection, "ReactionAttack", reactionAttack->value());
	config_set_double(config, ConfigSection, "ReactionMotion", reactionMotion->value());
	config_set_double(config, ConfigSection, "ReactionGlow", reactionGlow->value());
	config_set_double(config, ConfigSection, "ReactionTestStrength", reactionTestStrength->value());
	config_set_bool(config, ConfigSection, "ReducedMotion", reducedMotion->isChecked());
	config_set_bool(config, ConfigSection, "SourceNetworkArmed", sourceNetworkArmed->isChecked());
	config_set_bool(config, ConfigSection, "SourceNetworkActiveSceneOnly",
			sourceNetworkActiveSceneOnly->isChecked());
	config_set_string(config, ConfigSection, "SourceNetworkCircuitProfile",
			  SourceNetworkCircuitProfile().toUtf8().constData());
	for (auto it = sourceNetworkCircuitGains.cbegin(); it != sourceNetworkCircuitGains.cend(); ++it) {
		const QByteArray key = QStringLiteral("SourceNetworkCircuitGain_%1").arg(it.key()).toUtf8();
		config_set_double(config, ConfigSection, key.constData(), it.value()->value());
	}
	config_set_double(config, ConfigSection, "SourceNetworkIntensity", sourceNetworkIntensity->value());
	config_set_bool(config, ConfigSection, "ExternalEventBridgeArmed", externalEventBridgeArmed->isChecked());
	config_set_string(config, ConfigSection, "ExternalDanceCircuit",
			  externalDanceCircuit->currentData().toString().toUtf8().constData());
	config_set_string(config, ConfigSection, "ExternalTwitchCircuit",
			  externalTwitchCircuit->currentData().toString().toUtf8().constData());
	config_set_double(config, ConfigSection, "ExternalEventCooldown", externalEventCooldown->value());
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

bool TempestSignalReactor::TriggerExternalEvent(const QString &type, const QString &name, float strength,
						int durationMs, const QString &circuit, const QString &accent,
						const QString &effect, const QString &origin, const QString &dedupeId,
						int cooldownMs)
{
	if (!ExternalEventBridgeArmed()) {
		if (externalEventStatus)
			externalEventStatus->setText(QStringLiteral("EVENT BUS // BLOCKED // BRIDGE DISARMED"));
		return false;
	}

	const QString eventType = type.trimmed().toLower().left(48);
	const bool danceEvent = eventType == QStringLiteral("sound_alert_dance") ||
				eventType == QStringLiteral("dance") || eventType == QStringLiteral("sound_alert");
	const bool twitchEvent = eventType == QStringLiteral("twitch_interaction") ||
				 eventType == QStringLiteral("twitch");
	const float defaultStrength = danceEvent ? 1.2f : twitchEvent ? 1.0f : 0.9f;
	const int defaultDuration = danceEvent ? 6000 : twitchEvent ? 2600 : 2200;
	QString routedCircuit = circuit.trimmed().toLower();
	if (routedCircuit.isEmpty())
		routedCircuit = danceEvent && externalDanceCircuit     ? externalDanceCircuit->currentData().toString()
				: twitchEvent && externalTwitchCircuit ? externalTwitchCircuit->currentData().toString()
								       : QStringLiteral("all");
	const QStringList validCircuits = {QStringLiteral("all"),  QStringLiteral("core"),   QStringLiteral("frame"),
					   QStringLiteral("chat"), QStringLiteral("plates"), QStringLiteral("alerts")};
	if (!validCircuits.contains(routedCircuit))
		routedCircuit = QStringLiteral("all");
	QString routedAccent = accent.trimmed().toUpper();
	if (!QRegularExpression(QStringLiteral("^#[0-9A-F]{6}$")).match(routedAccent).hasMatch())
		routedAccent = danceEvent    ? QStringLiteral("#FF3EC8")
			       : twitchEvent ? QStringLiteral("#9B8CFF")
					     : QStringLiteral("#45D9FF");
	QString routedEffect = effect.trimmed().toLower();
	const QStringList validEffects = {QStringLiteral("pulse"), QStringLiteral("glow"), QStringLiteral("glitch"),
					  QStringLiteral("spectrum"), QStringLiteral("surge")};
	if (!validEffects.contains(routedEffect))
		routedEffect = danceEvent    ? QStringLiteral("spectrum")
			       : twitchEvent ? QStringLiteral("glitch")
					     : QStringLiteral("surge");

	const int boundedCooldown =
		cooldownMs >= 0 ? std::clamp(cooldownMs, 0, 10000)
				: qRound((externalEventCooldown ? externalEventCooldown->value() : 0.8) * 1000.0);
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const QString eventKey = (dedupeId.trimmed().isEmpty()
					  ? QStringLiteral("%1:%2").arg(eventType, name.trimmed().toLower())
					  : dedupeId.trimmed())
					 .left(128);
	const qint64 lastTrigger = externalEventLastTrigger.value(eventKey, 0);
	if (boundedCooldown > 0 && lastTrigger > 0 && now - lastTrigger < boundedCooldown) {
		const int remaining = int(boundedCooldown - (now - lastTrigger));
		if (externalEventStatus)
			externalEventStatus->setText(
				QStringLiteral("EVENT BUS // DUPLICATE SUPPRESSED // %1 ms").arg(remaining));
		return false;
	}
	externalEventLastTrigger[eventKey] = now;

	activeExternalEventType = eventType.isEmpty() ? QStringLiteral("custom") : eventType;
	activeExternalEventName = (name.trimmed().isEmpty() ? activeExternalEventType : name.trimmed()).left(96);
	activeExternalEventCircuit = routedCircuit;
	activeExternalEventAccent = routedAccent;
	activeExternalEventEffect = routedEffect;
	activeExternalEventOrigin =
		(origin.trimmed().isEmpty() ? QStringLiteral("external") : origin.trimmed()).left(48);
	activeExternalEventStrength = std::clamp(strength > 0.0f ? strength : defaultStrength, 0.05f, 1.5f);
	const int boundedDuration = std::clamp(durationMs > 0 ? durationMs : defaultDuration, 250, 300000);
	activeExternalEventUntil = now + boundedDuration;
	const quint64 sequence = ++externalEventSequence;
	TriggerPulse(activeExternalEventStrength, activeExternalEventOrigin);
	if (externalEventStatus) {
		const QString state =
			QStringLiteral("EVENT LIVE // %1 // %2 // %3 // %4 ms")
				.arg(activeExternalEventName.toUpper(), routedCircuit.toUpper(), routedEffect.toUpper())
				.arg(boundedDuration);
		externalEventStatus->setText(state);
		externalEventStatus->setAccessibleName(state);
	}
	emit ExternalEventTriggered(activeExternalEventType, activeExternalEventName, activeExternalEventStrength,
				    boundedDuration, activeExternalEventCircuit, activeExternalEventAccent,
				    activeExternalEventEffect, activeExternalEventOrigin);
	QTimer::singleShot(boundedDuration, this, [this, sequence]() {
		if (externalEventSequence == sequence)
			ClearExternalEvent();
	});
	return true;
}

void TempestSignalReactor::ClearExternalEvent()
{
	const bool wasActive = activeExternalEventUntil > 0;
	activeExternalEventUntil = 0;
	activeExternalEventStrength = 0.0f;
	++externalEventSequence;
	if (externalEventStatus) {
		externalEventStatus->setText(QStringLiteral("EVENT BUS // STANDBY"));
		externalEventStatus->setAccessibleName(QStringLiteral("External event bus standby"));
	}
	if (wasActive)
		emit ExternalEventCleared();
}

bool TempestSignalReactor::ExternalEventBridgeArmed() const
{
	return externalEventBridgeArmed && externalEventBridgeArmed->isChecked();
}

void TempestSignalReactor::SetWebSocketReady(bool ready)
{
	webSocketReady = ready;
	UpdateControlBridgeState();
}

void TempestSignalReactor::SetSourceBindingSummary(int total, int enabled, int active, int activeEnabled)
{
	if (!sourceNetworkStatus)
		return;
	QString summary;
	if (total <= 0) {
		summary = QStringLiteral("NETWORK // NO SOURCE RIGS BOUND");
	} else if (SourceNetworkActiveSceneOnly()) {
		summary = QStringLiteral("NETWORK // %1 RIG%2 // ACTIVE SCENE %3 / %4 ENABLED")
				  .arg(total)
				  .arg(total == 1 ? QString() : QStringLiteral("S"))
				  .arg(activeEnabled)
				  .arg(active);
	} else {
		summary = QStringLiteral("NETWORK // %1 RIG%2 // %3 ENABLED")
				  .arg(total)
				  .arg(total == 1 ? QString() : QStringLiteral("S"))
				  .arg(enabled);
	}
	sourceNetworkStatus->setText(summary);
	sourceNetworkStatus->setAccessibleName(summary);
}

void TempestSignalReactor::SetSourceCircuitSummary(const QString &circuit, int total, int scoped, int enabled)
{
	if (!sourceNetworkCircuitStates.contains(circuit))
		return;
	sourceNetworkCircuitTotals[circuit] = std::max(0, total);
	sourceNetworkCircuitScoped[circuit] = std::max(0, scoped);
	sourceNetworkCircuitEnabled[circuit] = std::max(0, enabled);
	RefreshSourceNetworkCircuitMonitor(circuit);
}

void TempestSignalReactor::SetSourceCircuitActivity(const QString &circuit, float activity)
{
	if (!sourceNetworkCircuitMeters.contains(circuit))
		return;
	sourceNetworkCircuitActivities[circuit] = std::clamp(activity, 0.0f, 2.0f);
	if (isVisible())
		RefreshSourceNetworkCircuitMonitor(circuit);
}

bool TempestSignalReactor::SourceNetworkArmed() const
{
	return sourceNetworkArmed && sourceNetworkArmed->isChecked();
}

float TempestSignalReactor::SourceNetworkIntensity() const
{
	return sourceNetworkIntensity ? float(sourceNetworkIntensity->value() / 100.0) : 1.0f;
}

bool TempestSignalReactor::SourceNetworkActiveSceneOnly() const
{
	return !sourceNetworkActiveSceneOnly || sourceNetworkActiveSceneOnly->isChecked();
}

QString TempestSignalReactor::SourceNetworkCircuitProfile() const
{
	return sourceNetworkCircuitProfile ? sourceNetworkCircuitProfile->currentData().toString()
					   : QStringLiteral("all");
}

float TempestSignalReactor::SourceNetworkCircuitGain(const QString &circuit) const
{
	const auto found = sourceNetworkCircuitGains.constFind(circuit);
	return found != sourceNetworkCircuitGains.cend() && found.value() ? float(found.value()->value() / 100.0)
									  : 1.0f;
}

QString TempestSignalReactor::SourceNetworkSoloCircuit() const
{
	return sourceNetworkSoloCircuit;
}

bool TempestSignalReactor::SourceNetworkCircuitActive(const QString &circuit) const
{
	if (!sourceNetworkSoloCircuit.isEmpty())
		return circuit == sourceNetworkSoloCircuit;
	const QString profile = SourceNetworkCircuitProfile();
	if (profile == QStringLiteral("all"))
		return true;
	if (profile == QStringLiteral("core"))
		return circuit == QStringLiteral("core");
	if (profile == QStringLiteral("ambient"))
		return circuit == QStringLiteral("core") || circuit == QStringLiteral("frame") ||
		       circuit == QStringLiteral("plates");
	if (profile == QStringLiteral("conversation"))
		return circuit == QStringLiteral("core") || circuit == QStringLiteral("frame") ||
		       circuit == QStringLiteral("chat") || circuit == QStringLiteral("plates");
	if (profile == QStringLiteral("alert"))
		return circuit == QStringLiteral("core") || circuit == QStringLiteral("frame") ||
		       circuit == QStringLiteral("alerts");
	return true;
}

QString TempestSignalReactor::SourceNetworkCircuitState(const QString &circuit) const
{
	const int total = sourceNetworkCircuitTotals.value(circuit);
	const float activity = sourceNetworkCircuitActivities.value(circuit);
	if (total <= 0)
		return QStringLiteral("UNBOUND");
	if (activity > 0.005f)
		return QStringLiteral("LIVE // %1%").arg(qRound(activity * 100.0f));
	if (!sourceNetworkSoloCircuit.isEmpty() && circuit != sourceNetworkSoloCircuit)
		return QStringLiteral("MUTED // SOLO");
	if (!SourceNetworkCircuitActive(circuit))
		return QStringLiteral("MUTED // PROFILE");
	if (SourceNetworkCircuitGain(circuit) <= 0.0f)
		return QStringLiteral("MUTED // GAIN");
	if (SourceNetworkIntensity() <= 0.0f)
		return QStringLiteral("MUTED // MASTER");
	if (SourceNetworkActiveSceneOnly() && sourceNetworkCircuitScoped.value(circuit) <= 0)
		return QStringLiteral("OUTSIDE ACTIVE SCENE");
	if (sourceNetworkCircuitEnabled.value(circuit) <= 0)
		return QStringLiteral("NO ENABLED RIGS");
	if (!reactorEnabled || !reactorEnabled->isChecked())
		return QStringLiteral("REACTOR OFFLINE");
	if (!SourceNetworkArmed())
		return QStringLiteral("NETWORK STANDBY");
	return QStringLiteral("READY // %1 RIG%2")
		.arg(sourceNetworkCircuitEnabled.value(circuit))
		.arg(sourceNetworkCircuitEnabled.value(circuit) == 1 ? QString() : QStringLiteral("S"));
}

void TempestSignalReactor::RefreshSourceNetworkCircuitMonitor(const QString &circuit)
{
	const auto meter = sourceNetworkCircuitMeters.value(circuit);
	const auto stateLabel = sourceNetworkCircuitStates.value(circuit);
	if (!meter || !stateLabel)
		return;
	const float activity = sourceNetworkCircuitActivities.value(circuit);
	meter->setValue(qRound(std::clamp(activity, 0.0f, 2.0f) * 1000.0f));
	const QString state = SourceNetworkCircuitState(circuit);
	if (stateLabel->text() != state) {
		stateLabel->setText(state);
		QString stateClass = QStringLiteral("idle");
		if (state.startsWith(QStringLiteral("LIVE")))
			stateClass = QStringLiteral("live");
		else if (state.startsWith(QStringLiteral("MUTED")))
			stateClass = QStringLiteral("muted");
		else if (state.startsWith(QStringLiteral("READY")))
			stateClass = QStringLiteral("ready");
		if (stateLabel->property("circuitStateClass").toString() != stateClass) {
			stateLabel->setProperty("circuitStateClass", stateClass);
			if (stateClass == QStringLiteral("live"))
				stateLabel->setStyleSheet(QStringLiteral("color:#45d9ff;"));
			else if (stateClass == QStringLiteral("muted"))
				stateLabel->setStyleSheet(QStringLiteral("color:#ff4b70;"));
			else if (stateClass == QStringLiteral("ready"))
				stateLabel->setStyleSheet(QStringLiteral("color:#9b8cff;"));
			else
				stateLabel->setStyleSheet(QStringLiteral("color:#748fa4;"));
		}
		stateLabel->setAccessibleName(
			QStringLiteral("%1 reaction circuit state // %2").arg(circuit.toUpper(), state));
	}
	const auto muteButton = sourceNetworkCircuitMuteButtons.value(circuit);
	const auto soloButton = sourceNetworkCircuitSoloButtons.value(circuit);
	const auto testButton = sourceNetworkCircuitTestButtons.value(circuit);
	if (muteButton && muteButton->isChecked() != (SourceNetworkCircuitGain(circuit) <= 0.0f)) {
		QSignalBlocker blocker(muteButton);
		muteButton->setChecked(SourceNetworkCircuitGain(circuit) <= 0.0f);
	}
	if (soloButton && soloButton->isChecked() != (sourceNetworkSoloCircuit == circuit)) {
		QSignalBlocker blocker(soloButton);
		soloButton->setChecked(sourceNetworkSoloCircuit == circuit);
	}
	if (soloButton)
		soloButton->setEnabled(sourceNetworkCircuitTotals.value(circuit) > 0);
	if (testButton)
		testButton->setEnabled(sourceNetworkCircuitTotals.value(circuit) > 0);
}

void TempestSignalReactor::RefreshSourceNetworkCircuitMonitors()
{
	for (auto it = sourceNetworkCircuitStates.cbegin(); it != sourceNetworkCircuitStates.cend(); ++it)
		RefreshSourceNetworkCircuitMonitor(it.key());
}

void TempestSignalReactor::SetSourceNetworkArmed(bool armed)
{
	if (sourceNetworkArmed)
		sourceNetworkArmed->setChecked(armed);
}

void TempestSignalReactor::SetSourceNetworkIntensity(float intensity)
{
	if (sourceNetworkIntensity)
		sourceNetworkIntensity->setValue(std::clamp(double(intensity), 0.0, 2.0) * 100.0);
}

void TempestSignalReactor::SetSourceNetworkActiveSceneOnly(bool activeSceneOnly)
{
	if (sourceNetworkActiveSceneOnly)
		sourceNetworkActiveSceneOnly->setChecked(activeSceneOnly);
}

void TempestSignalReactor::SetSourceNetworkCircuitProfile(const QString &profile)
{
	if (!sourceNetworkCircuitProfile)
		return;
	SetSourceNetworkSoloCircuit(QString());
	const int index = sourceNetworkCircuitProfile->findData(profile);
	if (index >= 0)
		sourceNetworkCircuitProfile->setCurrentIndex(index);
}

void TempestSignalReactor::SetSourceNetworkSoloCircuit(const QString &circuit)
{
	const QString normalized = sourceNetworkCircuitGains.contains(circuit.toLower()) ? circuit.toLower()
											 : QString();
	if (sourceNetworkSoloCircuit == normalized)
		return;
	sourceNetworkSoloCircuit = normalized;
	RefreshSourceNetworkCircuitMonitors();
	emit SourceNetworkCircuitSoloChanged(sourceNetworkSoloCircuit);
}

void TempestSignalReactor::CycleSourceNetworkCircuitProfile()
{
	if (!sourceNetworkCircuitProfile || sourceNetworkCircuitProfile->count() <= 0)
		return;
	sourceNetworkCircuitProfile->setCurrentIndex((sourceNetworkCircuitProfile->currentIndex() + 1) %
						     sourceNetworkCircuitProfile->count());
}

void TempestSignalReactor::ResetSourceNetworkCircuitGains()
{
	for (auto it = sourceNetworkCircuitGains.begin(); it != sourceNetworkCircuitGains.end(); ++it) {
		QSignalBlocker blocker(it.value());
		it.value()->setValue(100.0);
		sourceNetworkCircuitRestoreGains[it.key()] = 100.0;
	}
	SaveState();
	RefreshSourceNetworkCircuitMonitors();
	emit SourceNetworkCircuitGainsChanged(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
}

void TempestSignalReactor::ToggleSourceNetworkCircuitMute(const QString &circuit)
{
	const auto gain = sourceNetworkCircuitGains.value(circuit);
	if (!gain)
		return;
	if (gain->value() > 0.0) {
		sourceNetworkCircuitRestoreGains[circuit] = gain->value();
		gain->setValue(0.0);
	} else {
		gain->setValue(std::clamp(sourceNetworkCircuitRestoreGains.value(circuit, 100.0), 5.0, 200.0));
	}
}

void TempestSignalReactor::ToggleSourceNetworkCircuitSolo(const QString &circuit)
{
	SetSourceNetworkSoloCircuit(sourceNetworkSoloCircuit == circuit ? QString() : circuit);
}

void TempestSignalReactor::TestSourceNetwork()
{
	emit SourceNetworkTestRequested();
}

void TempestSignalReactor::DisarmAndRestoreSourceNetwork()
{
	SetSourceNetworkArmed(false);
	emit SourceNetworkRestoreRequested();
}

void TempestSignalReactor::UpdateControlBridgeState()
{
	if (!controlLabel)
		return;
	const bool hotkeysReady = pulseHotkeys.size() == 2 && networkHotkeys.size() == 6 &&
				  externalEventHotkeys.size() == 2;
	QString state;
	if (hotkeysReady && webSocketReady)
		state = QStringLiteral("CONTROL BRIDGE // 10 HOTKEYS + WEBSOCKET EVENT API READY");
	else if (hotkeysReady)
		state = QStringLiteral("CONTROL BRIDGE // 10 HOTKEYS READY");
	else
		state = QStringLiteral("CONTROL BRIDGE // INITIALIZING");
	controlLabel->setText(state);
	controlLabel->setAccessibleName(state);
	controlLabel->setAccessibleDescription(state);
}

void TempestSignalReactor::PublishTelemetry()
{
	// Preserve the original 50 ms response curve while publishing at 10 Hz.
	const float decay = std::pow((float)smoothing->value(), 2.0f);
	const float attack = 1.0f - std::pow(1.0f - (float)reactionAttack->value(), 2.0f);
	auto processChannel = [attack, decay](SignalChannel &channel, float input) {
		if (input > channel.smoothedLevel)
			channel.smoothedLevel += (input - channel.smoothedLevel) * attack;
		else
			channel.smoothedLevel *= decay;
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
	beatBaseline += (desktopInput - beatBaseline) * (1.0f - std::pow(1.0f - 0.055f, 2.0f));
	const float transient = std::max(0.0f, desktopInput - beatBaseline);
	beatLevel = std::max(transient * (float)beatSensitivity->value() * 2.6f, beatLevel * std::pow(0.68f, 2.0f));
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
	const float threshold = (float)reactionThreshold->value();
	auto applyThreshold = [threshold](float level) {
		if (level <= threshold)
			return 0.0f;
		return std::clamp((level - threshold) / std::max(0.01f, 1.0f - threshold), 0.0f, 1.5f);
	};
	desktop = applyThreshold(desktop);
	microphone = applyThreshold(microphone);
	beat = applyThreshold(beat);
	const float pulse = applyThreshold(manualPulse);
	const float master = std::clamp(std::max({desktop, microphone, pulse}), 0.0f, 1.5f);
	emit LevelsUpdated(master, desktop, microphone, beat);
	if (isVisible()) {
		desktopMeter->setValue((int)(std::min(desktop, 1.0f) * 1000.0f));
		microphoneMeter->setValue((int)(std::min(microphone, 1.0f) * 1000.0f));
		beatMeter->setValue((int)(std::min(beat, 1.0f) * 1000.0f));
		masterMeter->setValue((int)(std::min(master, 1.0f) * 1000.0f));
		const QString status = QStringLiteral("MASTER %1% // DESKTOP %2% // VOICE %3% // BEAT %4%")
					       .arg(qRound(master * 100.0f), 3)
					       .arg(qRound(desktop * 100.0f), 3)
					       .arg(qRound(microphone * 100.0f), 3)
					       .arg(qRound(beat * 100.0f), 3);
		if (statusLabel->text() != status)
			statusLabel->setText(status);
	}

	if (telemetryPath.isEmpty() && !EnsureOutputDirectory())
		return;
	QJsonObject telemetry;
	telemetry.insert(QStringLiteral("level"), master);
	telemetry.insert(QStringLiteral("master"), master);
	telemetry.insert(QStringLiteral("desktop"), desktop);
	telemetry.insert(QStringLiteral("microphone"), microphone);
	telemetry.insert(QStringLiteral("beat"), beat);
	telemetry.insert(QStringLiteral("pulse"), pulse);
	telemetry.insert(QStringLiteral("reactorEnabled"), reactorEnabled->isChecked());
	telemetry.insert(QStringLiteral("reactionProfile"), reactionProfile->currentData().toString());
	telemetry.insert(QStringLiteral("reactionPalette"), reactionPalette->currentData().toString());
	telemetry.insert(QStringLiteral("reactionThreshold"), reactionThreshold->value());
	telemetry.insert(QStringLiteral("reactionAttack"), reactionAttack->value());
	telemetry.insert(QStringLiteral("reactionRelease"), smoothing->value());
	telemetry.insert(QStringLiteral("reactionMotion"), reactionMotion->value() / 100.0);
	telemetry.insert(QStringLiteral("reactionGlow"), reactionGlow->value() / 100.0);
	telemetry.insert(QStringLiteral("reducedMotion"), reducedMotion->isChecked());
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool externalEventActive = ExternalEventBridgeArmed() && now < activeExternalEventUntil;
	telemetry.insert(QStringLiteral("externalEventActive"), externalEventActive);
	telemetry.insert(QStringLiteral("externalEventType"), activeExternalEventType);
	telemetry.insert(QStringLiteral("externalEventName"), activeExternalEventName);
	telemetry.insert(QStringLiteral("externalEventCircuit"), activeExternalEventCircuit);
	telemetry.insert(QStringLiteral("externalEventAccent"), activeExternalEventAccent);
	telemetry.insert(QStringLiteral("externalEventEffect"), activeExternalEventEffect);
	telemetry.insert(QStringLiteral("externalEventOrigin"), activeExternalEventOrigin);
	telemetry.insert(QStringLiteral("externalEventStrength"), activeExternalEventStrength);
	telemetry.insert(QStringLiteral("externalEventSequence"), static_cast<qint64>(externalEventSequence));
	telemetry.insert(QStringLiteral("externalEventEndsAt"), activeExternalEventUntil);
	telemetry.insert(QStringLiteral("sourceNetworkArmed"), SourceNetworkArmed());
	telemetry.insert(QStringLiteral("sourceNetworkIntensity"), SourceNetworkIntensity());
	telemetry.insert(QStringLiteral("sourceNetworkActiveSceneOnly"), SourceNetworkActiveSceneOnly());
	telemetry.insert(QStringLiteral("sourceNetworkCircuitProfile"), SourceNetworkCircuitProfile());
	telemetry.insert(QStringLiteral("sourceNetworkSoloCircuit"), SourceNetworkSoloCircuit());
	QJsonObject circuitGains;
	QJsonObject circuitActivity;
	QJsonObject circuitStates;
	for (const QString &circuit : {QStringLiteral("core"), QStringLiteral("frame"), QStringLiteral("chat"),
				       QStringLiteral("plates"), QStringLiteral("alerts")}) {
		circuitGains.insert(circuit, SourceNetworkCircuitGain(circuit));
		circuitActivity.insert(circuit, sourceNetworkCircuitActivities.value(circuit));
		circuitStates.insert(circuit, SourceNetworkCircuitState(circuit));
	}
	telemetry.insert(QStringLiteral("sourceNetworkCircuitGains"), circuitGains);
	telemetry.insert(QStringLiteral("sourceNetworkCircuitActivity"), circuitActivity);
	telemetry.insert(QStringLiteral("sourceNetworkCircuitStates"), circuitStates);
	telemetry.insert(QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch());
	const QByteArray payload = QJsonDocument(telemetry).toJson(QJsonDocument::Compact);
	obs_enum_sources(PublishTempestBrowserTelemetry, const_cast<QByteArray *>(&payload));
	QSaveFile file(telemetryPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	file.write(payload);
	file.commit();
}

void TempestSignalReactor::SetStatus(const QString &message, bool error)
{
	statusLabel->setStyleSheet(error ? QStringLiteral("color:#ff4b70;") : QStringLiteral("color:#45d9ff;"));
	statusLabel->setText(message);
}
