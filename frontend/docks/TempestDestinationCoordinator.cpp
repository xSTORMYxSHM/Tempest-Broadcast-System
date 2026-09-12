#include "TempestDestinationCoordinator.hpp"

#include <OBSApp.hpp>
#include <utility/audio-encoders.hpp>
#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cstring>

#include "moc_TempestDestinationCoordinator.cpp"

namespace {
constexpr char ConfigSection[] = "TempestDestinations";
constexpr char EnabledKey[] = "KickEnabled";
constexpr char ServerKey[] = "KickServer";
constexpr char StreamKeyKey[] = "KickStreamKey";
constexpr char EncoderKey[] = "KickEncoder";
constexpr char VideoBitrateKey[] = "KickVideoBitrate";
constexpr char AudioBitrateKey[] = "KickAudioBitrate";

bool EncoderIsAvailable(const QString &wanted)
{
	const QByteArray wantedUtf8 = wanted.toUtf8();
	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		if (wantedUtf8 == id && obs_get_encoder_type(id) == OBS_ENCODER_VIDEO) {
			const char *codec = obs_get_encoder_codec(id);
			return codec && strcmp(codec, "h264") == 0;
		}
	}
	return false;
}

QString OutputError(obs_output_t *output)
{
	const char *lastError = output ? obs_output_get_last_error(output) : nullptr;
	return lastError && *lastError ? QString::fromUtf8(lastError)
				       : QStringLiteral("The output could not be started.");
}
} // namespace

TempestDestinationCoordinator::TempestDestinationCoordinator(OBSBasic *main, QWidget *parent)
	: OBSDock(parent),
	  main(main)
{
	setObjectName(QStringLiteral("tempestDestinationCoordinator"));
	setWindowTitle(QStringLiteral("Destinations"));
	setMinimumWidth(390);
	BuildInterface();
	EnableContentScaling(objectName());
	LoadSettings();

	connect(main, &OBSBasic::StreamingStarted, this, &TempestDestinationCoordinator::MainStreamStarted);
	connect(main, &OBSBasic::StreamingStopping, this, &TempestDestinationCoordinator::MainStreamStopping);
	connect(main, &OBSBasic::StreamingStopped, this, &TempestDestinationCoordinator::MainStreamStopped);
	connect(main, &OBSBasic::StreamingStartFailed, this, &TempestDestinationCoordinator::CancelPreparedStart);
}

TempestDestinationCoordinator::~TempestDestinationCoordinator()
{
	StopKickOutput(true);
	ReleaseKickOutput();
}

void TempestDestinationCoordinator::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestDestinationRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestDestinationRoot { background: #07131e; }
		QLabel#destinationTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#destinationSubtitle, QLabel#destinationHint { color: #748fa4; font-size: 10px; }
		QLabel#destinationPrimary { color: #bdf6ff; padding: 9px; border: 1px solid #1f506d; background: #06101a; }
		QLabel#destinationStatus { color: #45d9ff; padding: 8px; border: 1px solid #1f506d; background: #06101a; font-weight: 700; }
		QCheckBox { color: #bdf6ff; font-weight: 700; }
		QLineEdit, QComboBox, QSpinBox { min-height: 29px; padding: 0 7px; color: #bdf6ff; background: #06101a; border: 1px solid #1f506d; }
		QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: #40576a; border-color: #172d3d; }
		QPushButton { min-height: 31px; padding: 0 9px; color: #bdf6ff; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
	)"));

	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *title = new QLabel(QStringLiteral("STREAM DESTINATIONS"), root);
	title->setObjectName(QStringLiteral("destinationTitle"));
	auto *subtitle = new QLabel(
		QStringLiteral("One start / stop path for the OBS primary output and an optional Kick H.264 output"),
		root);
	subtitle->setObjectName(QStringLiteral("destinationSubtitle"));
	subtitle->setWordWrap(true);
	layout->addWidget(title);
	layout->addWidget(subtitle);

	primaryStatus = new QLabel(root);
	primaryStatus->setObjectName(QStringLiteral("destinationPrimary"));
	primaryStatus->setWordWrap(true);
	layout->addWidget(primaryStatus);

	kickEnabled = new QCheckBox(QStringLiteral("Enable coordinated Kick destination for this profile"), root);
	layout->addWidget(kickEnabled);

	auto *form = new QFormLayout();
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	kickServer = new QLineEdit(root);
	kickServer->setPlaceholderText(QStringLiteral("Paste the server URL from the Kick creator dashboard"));
	kickServer->setAccessibleName(QStringLiteral("Kick server URL"));
	kickKey = new QLineEdit(root);
	kickKey->setEchoMode(QLineEdit::Password);
	kickKey->setPlaceholderText(QStringLiteral("Paste the Kick stream key"));
	kickKey->setAccessibleName(QStringLiteral("Kick stream key"));
	kickEncoder = new QComboBox(root);
	kickEncoder->setAccessibleName(QStringLiteral("Kick H.264 encoder"));
	kickVideoBitrate = new QSpinBox(root);
	kickVideoBitrate->setRange(1000, 20000);
	kickVideoBitrate->setSingleStep(500);
	kickVideoBitrate->setSuffix(QStringLiteral(" Kbps"));
	kickAudioBitrate = new QSpinBox(root);
	kickAudioBitrate->setRange(64, 320);
	kickAudioBitrate->setSingleStep(16);
	kickAudioBitrate->setSuffix(QStringLiteral(" Kbps"));
	form->addRow(QStringLiteral("Server URL"), kickServer);
	form->addRow(QStringLiteral("Stream key"), kickKey);
	form->addRow(QStringLiteral("H.264 encoder"), kickEncoder);
	form->addRow(QStringLiteral("Video bitrate"), kickVideoBitrate);
	form->addRow(QStringLiteral("Audio bitrate"), kickAudioBitrate);
	layout->addLayout(form);

	auto *hint = new QLabel(
		QStringLiteral(
			"Credentials stay in the active profile and the key is never shown in status text or logs. "
			"Confirm current bitrate limits in your Kick creator dashboard."),
		root);
	hint->setObjectName(QStringLiteral("destinationHint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);

	auto *buttons = new QHBoxLayout();
	readinessButton = new QPushButton(QStringLiteral("CHECK READINESS"), root);
	saveButton = new QPushButton(QStringLiteral("SAVE PROFILE"), root);
	buttons->addWidget(readinessButton);
	buttons->addWidget(saveButton);
	layout->addLayout(buttons);

	statusLabel = new QLabel(QStringLiteral("KICK DESTINATION DISABLED"), root);
	statusLabel->setObjectName(QStringLiteral("destinationStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	layout->addStretch(1);
	setWidget(root);

	connect(kickEnabled, &QCheckBox::toggled, this, &TempestDestinationCoordinator::UpdateEditorState);
	connect(saveButton, &QPushButton::clicked, this, &TempestDestinationCoordinator::SaveSettings);
	connect(readinessButton, &QPushButton::clicked, this, &TempestDestinationCoordinator::CheckReadiness);
	PopulateEncoders();
}

void TempestDestinationCoordinator::PopulateEncoders()
{
	const QString selected = kickEncoder ? kickEncoder->currentData().toString() : QStringLiteral("auto");
	kickEncoder->clear();
	kickEncoder->addItem(QStringLiteral("Automatic (prefer hardware)"), QStringLiteral("auto"));

	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO) {
			continue;
		}
		const char *codec = obs_get_encoder_codec(id);
		if (!codec || strcmp(codec, "h264") != 0) {
			continue;
		}
		const char *displayName = obs_encoder_get_display_name(id);
		kickEncoder->addItem(QStringLiteral("%1 (%2)").arg(QString::fromUtf8(displayName ? displayName : id),
								   QString::fromUtf8(id)),
				     QString::fromUtf8(id));
	}

	int selectedIndex = kickEncoder->findData(selected);
	if (selectedIndex < 0 && selected != QStringLiteral("auto")) {
		kickEncoder->addItem(QStringLiteral("Unavailable (%1)").arg(selected), selected);
		selectedIndex = kickEncoder->count() - 1;
	}
	kickEncoder->setCurrentIndex(std::max(0, selectedIndex));
}

void TempestDestinationCoordinator::LoadSettings()
{
	if (!main || !main->Config() || IsBusy()) {
		return;
	}

	loading = true;
	config_t *config = main->Config();
	kickEnabled->setChecked(config_get_bool(config, ConfigSection, EnabledKey));
	kickServer->setText(QString::fromUtf8(config_get_string(config, ConfigSection, ServerKey)));
	kickKey->setText(QString::fromUtf8(config_get_string(config, ConfigSection, StreamKeyKey)));
	kickVideoBitrate->setValue(config_has_user_value(config, ConfigSection, VideoBitrateKey)
					   ? static_cast<int>(config_get_int(config, ConfigSection, VideoBitrateKey))
					   : 6000);
	kickAudioBitrate->setValue(config_has_user_value(config, ConfigSection, AudioBitrateKey)
					   ? static_cast<int>(config_get_int(config, ConfigSection, AudioBitrateKey))
					   : 160);
	const QString encoder = QString::fromUtf8(config_get_string(config, ConfigSection, EncoderKey));
	PopulateEncoders();
	int encoderIndex = kickEncoder->findData(encoder.isEmpty() ? QStringLiteral("auto") : encoder);
	if (encoderIndex < 0 && !encoder.isEmpty()) {
		kickEncoder->addItem(QStringLiteral("Unavailable (%1)").arg(encoder), encoder);
		encoderIndex = kickEncoder->count() - 1;
	}
	kickEncoder->setCurrentIndex(std::max(0, encoderIndex));
	loading = false;
	RefreshPrimarySummary();
	UpdateEditorState();
	SetStatus(kickEnabled->isChecked() ? QStringLiteral("SAVED / RUN READINESS CHECK")
					   : QStringLiteral("KICK DESTINATION DISABLED"));
}

void TempestDestinationCoordinator::SaveSettings()
{
	if (!main || !main->Config() || IsBusy()) {
		return;
	}

	config_t *config = main->Config();
	config_set_bool(config, ConfigSection, EnabledKey, kickEnabled->isChecked());
	config_set_string(config, ConfigSection, ServerKey, kickServer->text().trimmed().toUtf8().constData());
	config_set_string(config, ConfigSection, StreamKeyKey, kickKey->text().toUtf8().constData());
	config_set_string(config, ConfigSection, EncoderKey,
			  kickEncoder->currentData().toString().toUtf8().constData());
	config_set_int(config, ConfigSection, VideoBitrateKey, kickVideoBitrate->value());
	config_set_int(config, ConfigSection, AudioBitrateKey, kickAudioBitrate->value());
	config_save_safe(config, "tmp", nullptr);
	SetStatus(kickEnabled->isChecked() ? QStringLiteral("PROFILE SAVED / RUN READINESS CHECK")
					   : QStringLiteral("KICK DESTINATION DISABLED"));
}

QString TempestDestinationCoordinator::ResolveEncoderId() const
{
	const QString configured = kickEncoder->currentData().toString();
	if (!configured.isEmpty() && configured != QStringLiteral("auto")) {
		return EncoderIsAvailable(configured) ? configured : QString();
	}

	static constexpr std::array<const char *, 6> PreferredEncoders = {
		"obs_nvenc_h264_tex", "ffmpeg_nvenc", "h264_texture_amf", "obs_qsv11_v2", "obs_qsv11", "obs_x264",
	};
	for (const char *id : PreferredEncoders) {
		const QString candidate = QString::fromUtf8(id);
		if (EncoderIsAvailable(candidate)) {
			return candidate;
		}
	}

	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO) {
			continue;
		}
		const char *codec = obs_get_encoder_codec(id);
		if (codec && strcmp(codec, "h264") == 0) {
			return QString::fromUtf8(id);
		}
	}
	return {};
}

bool TempestDestinationCoordinator::ValidateSettings(QString &error, QString *encoderId) const
{
	if (!kickEnabled->isChecked()) {
		return true;
	}

	const QString server = kickServer->text().trimmed();
	const QUrl url(server, QUrl::StrictMode);
	if (server.isEmpty() || !url.isValid() || url.host().isEmpty() ||
	    (url.scheme().compare(QStringLiteral("rtmp"), Qt::CaseInsensitive) != 0 &&
	     url.scheme().compare(QStringLiteral("rtmps"), Qt::CaseInsensitive) != 0)) {
		error = QStringLiteral(
			"Kick is enabled, but its server URL is missing or is not a valid RTMP/RTMPS URL.");
		return false;
	}
	if (kickKey->text().trimmed().isEmpty()) {
		error = QStringLiteral("Kick is enabled, but its stream key is missing.");
		return false;
	}

	const QString resolvedEncoder = ResolveEncoderId();
	if (resolvedEncoder.isEmpty()) {
		error = kickEncoder->currentData().toString() == QStringLiteral("auto")
				? QStringLiteral("No H.264 encoder is available for the Kick destination.")
				: QStringLiteral(
					  "The selected Kick H.264 encoder is unavailable. Choose Automatic or another encoder.");
		return false;
	}
	if (!obs_get_output_flags("rtmp_output")) {
		error = QStringLiteral("The RTMP output module required by Kick is unavailable.");
		return false;
	}
	if (encoderId) {
		*encoderId = resolvedEncoder;
	}
	return true;
}

void TempestDestinationCoordinator::CheckReadiness()
{
	QString error;
	QString encoderId;
	if (!ValidateSettings(error, &encoderId)) {
		SetStatus(error, true);
		return;
	}
	if (!kickEnabled->isChecked()) {
		SetStatus(QStringLiteral("KICK DESTINATION DISABLED"));
		return;
	}
	SetStatus(QStringLiteral("READY / %1 / %2 KBPS VIDEO").arg(encoderId.toUpper()).arg(kickVideoBitrate->value()));
}

bool TempestDestinationCoordinator::CreateKickOutput(const QString &encoderId, QString &error)
{
	ReleaseKickOutput();

	OBSDataAutoRelease serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "server", kickServer->text().trimmed().toUtf8().constData());
	obs_data_set_string(serviceSettings, "key", kickKey->text().toUtf8().constData());
	service = obs_service_create("rtmp_custom", "tempest_kick_service", serviceSettings, nullptr);
	if (!service) {
		error = QStringLiteral("Could not create the Kick RTMP service.");
		return false;
	}

	OBSDataAutoRelease videoSettings = obs_data_create();
	obs_data_set_string(videoSettings, "rate_control", "CBR");
	obs_data_set_int(videoSettings, "bitrate", kickVideoBitrate->value());
	obs_data_set_int(videoSettings, "keyint_sec", 2);
	obs_data_set_string(videoSettings, "profile", "high");
	OBSDataAutoRelease audioSettings = obs_data_create();
	obs_data_set_int(audioSettings, "bitrate", kickAudioBitrate->value());
	obs_service_apply_encoder_settings(service, videoSettings, audioSettings);

	videoEncoder =
		obs_video_encoder_create(encoderId.toUtf8().constData(), "tempest_kick_video", videoSettings, nullptr);
	if (!videoEncoder) {
		error = QStringLiteral("Could not create the selected Kick H.264 encoder (%1).").arg(encoderId);
		ReleaseKickOutput();
		return false;
	}

	config_t *config = main->Config();
	if (!config) {
		error = QStringLiteral("The active profile is not ready.");
		ReleaseKickOutput();
		return false;
	}
	const char *mode = config_get_string(config, "Output", "Mode");
	size_t mixerIndex = 0;
	if (mode && strcmp(mode, "Advanced") == 0) {
		const uint64_t track = config_get_uint(config, "AdvOut", "TrackIndex");
		if (track >= 1 && track <= MAX_AUDIO_MIXES) {
			mixerIndex = static_cast<size_t>(track - 1);
		}
	}
	const char *audioEncoderId = GetSimpleAACEncoderForBitrate(kickAudioBitrate->value());
	if (!audioEncoderId) {
		audioEncoderId = "ffmpeg_aac";
	}
	audioEncoder =
		obs_audio_encoder_create(audioEncoderId, "tempest_kick_audio", audioSettings, mixerIndex, nullptr);
	if (!audioEncoder) {
		error = QStringLiteral("Could not create the AAC encoder for the Kick destination.");
		ReleaseKickOutput();
		return false;
	}

	output = obs_output_create("rtmp_output", "tempest_kick_output", nullptr, nullptr);
	if (!output) {
		error = QStringLiteral("Could not create the Kick RTMP output.");
		ReleaseKickOutput();
		return false;
	}

	video_t *video = obs_get_video();
	if (!video || !obs_get_audio()) {
		error = QStringLiteral("The main video or audio subsystem is not ready.");
		ReleaseKickOutput();
		return false;
	}
	const video_format format = video_output_get_format(video);
	if (format != VIDEO_FORMAT_I420 && format != VIDEO_FORMAT_NV12 && format != VIDEO_FORMAT_I010 &&
	    format != VIDEO_FORMAT_P010) {
		obs_encoder_set_preferred_video_format(videoEncoder, VIDEO_FORMAT_NV12);
	}
	obs_encoder_set_video(videoEncoder, video);
	obs_encoder_set_audio(audioEncoder, obs_get_audio());
	obs_output_set_video_encoder(output, videoEncoder);
	obs_output_set_audio_encoder(output, audioEncoder, 0);
	obs_output_set_service(output, service);

	OBSDataAutoRelease outputSettings = obs_data_create();
	obs_data_set_string(outputSettings, "bind_ip", config_get_string(config, "Output", "BindIP"));
	obs_data_set_string(outputSettings, "ip_family", config_get_string(config, "Output", "IPFamily"));
	obs_data_set_bool(outputSettings, "dyn_bitrate", config_get_bool(config, "Output", "DynamicBitrate"));
#ifdef _WIN32
	obs_data_set_bool(outputSettings, "new_socket_loop_enabled",
			  config_get_bool(config, "Output", "NewSocketLoopEnable"));
	obs_data_set_bool(outputSettings, "low_latency_mode_enabled",
			  config_get_bool(config, "Output", "LowLatencyEnable"));
#endif
	obs_output_update(output, outputSettings);
	const bool reconnect = config_get_bool(config, "Output", "Reconnect");
	obs_output_set_reconnect_settings(output, reconnect ? config_get_uint(config, "Output", "MaxRetries") : 0,
					  config_get_uint(config, "Output", "RetryDelay"));

	signal_handler_t *signalHandler = obs_output_get_signal_handler(output);
	outputStartedSignal.Connect(signalHandler, "start", KickOutputStarted, this);
	outputStoppedSignal.Connect(signalHandler, "stop", KickOutputStopped, this);
	return true;
}

bool TempestDestinationCoordinator::PrepareForMainStream(QString &error)
{
	if (!kickEnabled->isChecked()) {
		return true;
	}
	if (IsBusy()) {
		error = QStringLiteral("The Kick destination is already preparing or active.");
		return false;
	}

	QString encoderId;
	if (!ValidateSettings(error, &encoderId)) {
		SetStatus(error, true);
		return false;
	}
	if (!CreateKickOutput(encoderId, error)) {
		SetStatus(error, true);
		return false;
	}

	prepared = true;
	startRequested = false;
	stopRequested = false;
	SetStatus(QStringLiteral("READY / WAITING FOR PRIMARY OUTPUT"));
	UpdateEditorState();
	return true;
}

void TempestDestinationCoordinator::StartKickOutput()
{
	if (!prepared || !output || startRequested || obs_output_active(output)) {
		return;
	}
	startRequested = true;
	SetStatus(QStringLiteral("KICK / CONNECTING"));
	if (!obs_output_start(output)) {
		const QString error = OutputError(output);
		startRequested = false;
		prepared = false;
		SetStatus(QStringLiteral("KICK FAILED / %1").arg(error), true);
		if (main) {
			main->ShowStatusBarMessage(QStringLiteral("Kick destination failed to start: %1").arg(error));
		}
		ReleaseKickOutput();
		UpdateEditorState();
	}
}

void TempestDestinationCoordinator::StopKickOutput(bool force)
{
	if (!output) {
		return;
	}
	stopRequested = true;
	if (obs_output_active(output)) {
		SetStatus(QStringLiteral("KICK / STOPPING"));
		if (force) {
			obs_output_force_stop(output);
		} else {
			obs_output_stop(output);
		}
	} else {
		prepared = false;
		startRequested = false;
		ReleaseKickOutput();
		UpdateEditorState();
	}
}

void TempestDestinationCoordinator::ReleaseKickOutput()
{
	outputStartedSignal.Disconnect();
	outputStoppedSignal.Disconnect();
	if (output) {
		obs_output_set_video_encoder(output, nullptr);
		obs_output_set_audio_encoder(output, nullptr, 0);
		obs_output_set_service(output, nullptr);
	}
	output = nullptr;
	videoEncoder = nullptr;
	audioEncoder = nullptr;
	service = nullptr;
}

void TempestDestinationCoordinator::CancelPreparedStart()
{
	StopKickOutput();
	prepared = false;
	startRequested = false;
	stopRequested = false;
	SetStatus(kickEnabled->isChecked() ? QStringLiteral("PRIMARY START FAILED / KICK NOT STARTED")
					   : QStringLiteral("KICK DESTINATION DISABLED"),
		  kickEnabled->isChecked());
	UpdateEditorState();
}

void TempestDestinationCoordinator::ReloadProfile()
{
	if (!IsBusy()) {
		LoadSettings();
	}
}

void TempestDestinationCoordinator::MainStreamStarted(bool withDelay)
{
	if (!withDelay) {
		StartKickOutput();
	}
	UpdateEditorState();
}

void TempestDestinationCoordinator::MainStreamStopping()
{
	StopKickOutput();
	UpdateEditorState();
}

void TempestDestinationCoordinator::MainStreamStopped(bool withDelay)
{
	if (withDelay) {
		return;
	}
	StopKickOutput();
	if (!output) {
		prepared = false;
		startRequested = false;
		stopRequested = false;
		SetStatus(kickEnabled->isChecked() ? QStringLiteral("KICK / OFFLINE")
						   : QStringLiteral("KICK DESTINATION DISABLED"));
	}
	UpdateEditorState();
}

void TempestDestinationCoordinator::HandleKickStarted()
{
	startRequested = false;
	SetStatus(QStringLiteral("KICK / LIVE"), false, true);
	UpdateEditorState();
}

void TempestDestinationCoordinator::HandleKickStopped(int code, const QString &lastError)
{
	const bool expected = stopRequested || !obs_frontend_streaming_active();
	prepared = false;
	startRequested = false;
	stopRequested = false;
	ReleaseKickOutput();

	if (expected || code == OBS_OUTPUT_SUCCESS) {
		SetStatus(kickEnabled->isChecked() ? QStringLiteral("KICK / OFFLINE")
						   : QStringLiteral("KICK DESTINATION DISABLED"));
	} else {
		const QString detail = lastError.isEmpty() ? QStringLiteral("connection ended") : lastError;
		SetStatus(QStringLiteral("KICK FAILED / %1").arg(detail), true);
		if (main) {
			main->ShowStatusBarMessage(QStringLiteral("Kick destination stopped: %1").arg(detail));
		}
	}
	UpdateEditorState();
}

void TempestDestinationCoordinator::KickOutputStarted(void *data, calldata_t *)
{
	auto *coordinator = static_cast<TempestDestinationCoordinator *>(data);
	QMetaObject::invokeMethod(coordinator, "HandleKickStarted", Qt::QueuedConnection);
}

void TempestDestinationCoordinator::KickOutputStopped(void *data, calldata_t *params)
{
	auto *coordinator = static_cast<TempestDestinationCoordinator *>(data);
	const int code = static_cast<int>(calldata_int(params, "code"));
	const char *lastError = calldata_string(params, "last_error");
	QMetaObject::invokeMethod(coordinator, "HandleKickStopped", Qt::QueuedConnection, Q_ARG(int, code),
				  Q_ARG(QString, QString::fromUtf8(lastError ? lastError : "")));
}

void TempestDestinationCoordinator::RefreshPrimarySummary()
{
	if (!main || !primaryStatus) {
		return;
	}
	obs_service_t *primaryService = main->GetService();
	QString serviceName = QStringLiteral("Not configured");
	if (primaryService) {
		OBSDataAutoRelease settings = obs_service_get_settings(primaryService);
		serviceName = QString::fromUtf8(obs_data_get_string(settings, "service"));
		if (serviceName.isEmpty()) {
			serviceName = QString::fromUtf8(obs_service_get_id(primaryService));
		}
	}
	config_t *config = main->Config();
	const bool enhanced = config && config_get_bool(config, "Stream1", "EnableMultitrackVideo");
	primaryStatus->setText(
		QStringLiteral("PRIMARY OUTPUT  //  %1\nENHANCED BROADCASTING  //  %2")
			.arg(serviceName.toUpper(), enhanced ? QStringLiteral("ENABLED") : QStringLiteral("DISABLED")));
}

void TempestDestinationCoordinator::SetStatus(const QString &message, bool error, bool live)
{
	if (!statusLabel) {
		return;
	}
	statusLabel->setText(message);
	const QString color = error  ? QStringLiteral("#ff799c")
			      : live ? QStringLiteral("#65f7a1")
				     : QStringLiteral("#45d9ff");
	statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(color));
}

bool TempestDestinationCoordinator::IsBusy() const
{
	return prepared || startRequested || (output && obs_output_active(output));
}

void TempestDestinationCoordinator::UpdateEditorState()
{
	if (loading) {
		return;
	}
	const bool locked = IsBusy() || (main && main->StreamingActive());
	const bool enabled = kickEnabled->isChecked() && !locked;
	kickEnabled->setEnabled(!locked);
	kickServer->setEnabled(enabled);
	kickKey->setEnabled(enabled);
	kickEncoder->setEnabled(enabled);
	kickVideoBitrate->setEnabled(enabled);
	kickAudioBitrate->setEnabled(enabled);
	readinessButton->setEnabled(!locked);
	saveButton->setEnabled(!locked);
}

void TempestDestinationCoordinator::showEvent(QShowEvent *event)
{
	OBSDock::showEvent(event);
	RefreshPrimarySummary();
	if (!IsBusy() && main && !main->StreamingActive()) {
		LoadSettings();
	}
}
