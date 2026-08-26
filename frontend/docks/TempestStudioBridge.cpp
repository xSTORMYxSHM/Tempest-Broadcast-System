#include "TempestStudioBridge.hpp"

#include "TempestSignalReactor.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>
#include <obs-frontend-api.h>
#include <obs.h>

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include <algorithm>
#include <atomic>
#include <initializer_list>
#include <mutex>
#include <thread>

#include "moc_TempestStudioBridge.cpp"

#ifndef TEMPEST_PRODUCT_VERSION
#define TEMPEST_PRODUCT_VERSION "0.20.0"
#endif

namespace {
constexpr char ConfigSection[] = "TempestStudioBridge";
constexpr char ApplicationId[] = "com.tempestmainframe.tempest-broadcast";
constexpr char ProtocolVersion[] = "1.0";
constexpr int MaximumRememberedCommands = 512;

QString FirstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QString value = object.value(QString::fromUtf8(key)).toString().trimmed();
		if (!value.isEmpty())
			return value;
	}
	return {};
}

struct StudioSourceInventory {
	QStringList audio;
	QStringList visual;
};

bool CollectAudioSource(void *data, obs_source_t *source)
{
	if (!source || obs_source_removed(source) || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
		return true;
	const char *name = obs_source_get_name(source);
	if (name && *name)
		static_cast<StudioSourceInventory *>(data)->audio.append(QString::fromUtf8(name));
	return true;
}

bool CollectVisualSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (source && !obs_source_removed(source) && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) {
		const char *name = obs_source_get_name(source);
		if (name && *name)
			static_cast<StudioSourceInventory *>(data)->visual.append(QString::fromUtf8(name));
	}
	if (obs_sceneitem_is_group(item)) {
		if (obs_scene_t *groupScene = obs_sceneitem_group_get_scene(item))
			obs_scene_enum_items(groupScene, CollectVisualSceneItem, data);
	}
	return true;
}

QJsonObject BuildSourceInventory(OBSBasic *main)
{
	StudioSourceInventory inventory;
	obs_enum_sources(CollectAudioSource, &inventory);
	OBSScene scene = main ? main->GetCurrentScene() : OBSScene();
	if (scene)
		obs_scene_enum_items(scene, CollectVisualSceneItem, &inventory);
	inventory.audio.removeDuplicates();
	inventory.visual.removeDuplicates();
	inventory.audio.sort(Qt::CaseInsensitive);
	inventory.visual.sort(Qt::CaseInsensitive);
	const obs_source_t *sceneSource = scene ? obs_scene_get_source(scene) : nullptr;
	return {{QStringLiteral("audio"), QJsonArray::fromStringList(inventory.audio)},
		{QStringLiteral("visual"), QJsonArray::fromStringList(inventory.visual)},
		{QStringLiteral("currentScene"),
		 sceneSource ? QString::fromUtf8(obs_source_get_name(sceneSource)) : QString()}};
}

QJsonObject BuildCanvasProfile()
{
	obs_video_info videoInfo{};
	if (!obs_get_video_info(&videoInfo))
		return {};
	return {{QStringLiteral("baseWidth"), static_cast<int>(videoInfo.base_width)},
		{QStringLiteral("baseHeight"), static_cast<int>(videoInfo.base_height)},
		{QStringLiteral("outputWidth"), static_cast<int>(videoInfo.output_width)},
		{QStringLiteral("outputHeight"), static_cast<int>(videoInfo.output_height)},
		{QStringLiteral("fpsNumerator"), static_cast<int>(videoInfo.fps_num)},
		{QStringLiteral("fpsDenominator"), static_cast<int>(videoInfo.fps_den)}};
}
} // namespace

struct TempestStudioBridge::ClientState {
	using Endpoint = websocketpp::client<websocketpp::config::asio_client>;

	std::unique_ptr<Endpoint> endpoint;
	websocketpp::connection_hdl connection;
	std::thread worker;
	std::mutex sendMutex;
	std::atomic_bool connected{false};
};

TempestStudioBridge::TempestStudioBridge(OBSBasic *main, TempestSignalReactor *reactor, QWidget *parent)
	: OBSDock(parent),
	  main(main),
	  reactor(reactor)
{
	setObjectName(QStringLiteral("tempestStudioBridge"));
	setWindowTitle(QStringLiteral("Studio Integration"));
	setMinimumWidth(340);
	BuildInterface();
	EnableContentScaling(objectName());
	LoadState();

	reconnectTimer = new QTimer(this);
	reconnectTimer->setSingleShot(true);
	reconnectTimer->setInterval(3000);
	connect(reconnectTimer, &QTimer::timeout, this, &TempestStudioBridge::ConnectToStudio);
	heartbeatTimer = new QTimer(this);
	heartbeatTimer->setInterval(10000);
	connect(heartbeatTimer, &QTimer::timeout, this, [this]() {
		SendHeartbeat();
		PublishHealth(QStringLiteral("periodic"));
	});

	if (autoConnectCheck->isChecked())
		QTimer::singleShot(1800, this, &TempestStudioBridge::ConnectToStudio);
}

TempestStudioBridge::~TempestStudioBridge()
{
	DisconnectFromStudio(true);
}

bool TempestStudioBridge::IsConnected() const
{
	return client && client->connected.load();
}

void TempestStudioBridge::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestStudioBridgeRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestStudioBridgeRoot { background: #07131e; }
		QLabel#studioBridgeTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#studioBridgeSubtitle, QLabel#studioBridgeMeta { color: #748fa4; font-size: 9px; letter-spacing: 1px; }
		QLabel#studioBridgeState { color: #45d9ff; font-size: 11px; font-weight: 700; padding: 7px; border: 1px solid #1f506d; background: #06101a; }
		QLabel#studioBridgeDetail { color: #9eb7c8; font-size: 10px; }
		QFrame#studioBridgePanel { background: #081a27; border: 1px solid #183a50; }
		QLineEdit { min-height: 29px; padding: 0 7px; color: #bdf6ff; background: #06101a; border: 1px solid #1f506d; }
		QComboBox { min-height: 29px; padding: 0 7px; color: #bdf6ff; background: #06101a; border: 1px solid #1f506d; }
		QPushButton { min-height: 31px; padding: 0 9px; color: #bdf6ff; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QCheckBox { color: #9eb7c8; }
	)"));
	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *title = new QLabel(QStringLiteral("STUDIO INTEGRATION"), root);
	title->setObjectName(QStringLiteral("studioBridgeTitle"));
	auto *subtitle = new QLabel(QStringLiteral("TEMPEST CROSS-SUITE WORKFLOW ADAPTER // v%1")
					    .arg(QStringLiteral(TEMPEST_PRODUCT_VERSION)),
				    root);
	subtitle->setObjectName(QStringLiteral("studioBridgeSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	connectionLabel = new QLabel(QStringLiteral("OFFLINE // WAITING FOR STUDIO"), root);
	connectionLabel->setObjectName(QStringLiteral("studioBridgeState"));
	connectionLabel->setAccessibleName(QStringLiteral("Tempest Studio Bridge offline"));
	detailLabel = new QLabel(
		QStringLiteral("Studio owns Twitch interactions; Broadcast owns OBS output and reactions."), root);
	detailLabel->setObjectName(QStringLiteral("studioBridgeDetail"));
	detailLabel->setWordWrap(true);
	layout->addWidget(connectionLabel);
	layout->addWidget(detailLabel);

	auto *connectionPanel = new QFrame(root);
	connectionPanel->setObjectName(QStringLiteral("studioBridgePanel"));
	auto *connectionLayout = new QVBoxLayout(connectionPanel);
	connectionLayout->setContentsMargins(8, 8, 8, 8);
	connectionLayout->setSpacing(6);
	endpointEdit = new QLineEdit(connectionPanel);
	endpointEdit->setPlaceholderText(QStringLiteral("ws://127.0.0.1:4765/v1/socket"));
	endpointEdit->setAccessibleName(QStringLiteral("Tempest Studio Bridge endpoint"));
	tokenPathEdit = new QLineEdit(connectionPanel);
	tokenPathEdit->setPlaceholderText(QStringLiteral("Studio bridge-token path"));
	tokenPathEdit->setAccessibleName(QStringLiteral("Tempest Studio Bridge token file"));
	auto *form = new QFormLayout();
	form->addRow(QStringLiteral("Endpoint"), endpointEdit);
	form->addRow(QStringLiteral("Token file"), tokenPathEdit);
	connectionLayout->addLayout(form);
	autoConnectCheck = new QCheckBox(QStringLiteral("CONNECT AUTOMATICALLY WITH STUDIO"), connectionPanel);
	autoConnectCheck->setAccessibleName(QStringLiteral("Automatically connect to Tempest Studio"));
	connectionLayout->addWidget(autoConnectCheck);
	auto *buttons = new QHBoxLayout();
	connectButton = new QPushButton(QStringLiteral("CONNECT"), connectionPanel);
	auto *reloadButton = new QPushButton(QStringLiteral("RELOAD TOKEN"), connectionPanel);
	connectButton->setAccessibleName(QStringLiteral("Connect or disconnect Tempest Studio Bridge"));
	reloadButton->setAccessibleName(QStringLiteral("Reload Tempest Studio Bridge token"));
	buttons->addWidget(connectButton);
	buttons->addWidget(reloadButton);
	connectionLayout->addLayout(buttons);
	layout->addWidget(connectionPanel);

	auto *capabilityPanel = new QFrame(root);
	capabilityPanel->setObjectName(QStringLiteral("studioBridgePanel"));
	auto *capabilityLayout = new QVBoxLayout(capabilityPanel);
	capabilityLayout->setContentsMargins(8, 8, 8, 8);
	capabilityLayout->setSpacing(5);
	auto *capabilityTitle = new QLabel(QStringLiteral("ADAPTER CAPABILITIES"), capabilityPanel);
	capabilityTitle->setObjectName(QStringLiteral("studioBridgeMeta"));
	capabilityLabel = new QLabel(
		QStringLiteral(
			"EFFECT TRIGGER + CLEAR // ALERT AUDIO PLAYBACK // POPUP SHOW + HIDE // OBS STATUS // LOCAL LEASE RESTORE"),
		capabilityPanel);
	capabilityLabel->setObjectName(QStringLiteral("studioBridgeDetail"));
	capabilityLabel->setWordWrap(true);
	commandLabel = new QLabel(QStringLiteral("COMMANDS // 0 // ACTIVE LEASES // 0"), capabilityPanel);
	commandLabel->setObjectName(QStringLiteral("studioBridgeMeta"));
	capabilityLayout->addWidget(capabilityTitle);
	capabilityLayout->addWidget(capabilityLabel);
	capabilityLayout->addWidget(commandLabel);
	layout->addWidget(capabilityPanel);

	layout->addStretch(1);
	setWidget(root);

	connect(connectButton, &QPushButton::clicked, this, [this]() {
		if (IsConnected() || client)
			DisconnectFromStudio(true);
		else
			ConnectToStudio();
	});
	connect(reloadButton, &QPushButton::clicked, this, [this]() {
		QString error;
		const QString token = ResolveToken(&error);
		SetConnectionState(IsConnected() ? QStringLiteral("ONLINE // TOKEN READY")
						 : QStringLiteral("OFFLINE // TOKEN READY"),
				   token.isEmpty() ? error
						   : QStringLiteral("Loaded Studio authentication token from disk."),
				   token.isEmpty());
	});
	connect(endpointEdit, &QLineEdit::editingFinished, this, &TempestStudioBridge::SaveState);
	connect(tokenPathEdit, &QLineEdit::editingFinished, this, &TempestStudioBridge::SaveState);
	connect(autoConnectCheck, &QCheckBox::toggled, this, [this](bool enabled) {
		SaveState();
		if (enabled && !IsConnected())
			ScheduleReconnect();
		else if (!enabled && reconnectTimer)
			reconnectTimer->stop();
	});
}

void TempestStudioBridge::LoadState()
{
	config_t *config = App()->GetUserConfig();
	QString endpoint = QString::fromUtf8(config_get_string(config, ConfigSection, "Endpoint")).trimmed();
	if (endpoint.isEmpty())
		endpoint = QStringLiteral("ws://127.0.0.1:4765/v1/socket");
	QString tokenPath = QString::fromUtf8(config_get_string(config, ConfigSection, "TokenPath")).trimmed();
	if (tokenPath.isEmpty()) {
		const QString appData = qEnvironmentVariable("APPDATA");
		tokenPath = QDir::cleanPath(appData + QStringLiteral("/@tempest/studio-desktop/bridge/bridge-token"));
	}
	endpointEdit->setText(endpoint);
	tokenPathEdit->setText(tokenPath);
	autoConnectCheck->setChecked(!config_has_user_value(config, ConfigSection, "AutoConnect") ||
				     config_get_bool(config, ConfigSection, "AutoConnect"));
}

void TempestStudioBridge::SaveState()
{
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "Endpoint", endpointEdit->text().trimmed().toUtf8().constData());
	config_set_string(config, ConfigSection, "TokenPath", tokenPathEdit->text().trimmed().toUtf8().constData());
	config_set_bool(config, ConfigSection, "AutoConnect", autoConnectCheck->isChecked());
	config_save_safe(config, "tmp", nullptr);
}

QString TempestStudioBridge::ResolveToken(QString *error) const
{
	QFile file(tokenPathEdit->text().trimmed());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		if (error)
			*error = QStringLiteral(
				"Studio token was not found. Start Tempest Studio once, then reconnect.");
		return {};
	}
	const QString token = QString::fromUtf8(file.readAll()).trimmed();
	if (token.size() < 32) {
		if (error)
			*error = QStringLiteral("Studio token file is invalid or incomplete.");
		return {};
	}
	return token;
}

void TempestStudioBridge::ConnectToStudio()
{
	if (IsConnected())
		return;
	if (client)
		DisconnectFromStudio(false);
	SaveState();
	operatorDisconnect = false;
	QString tokenError;
	const QString token = ResolveToken(&tokenError);
	if (token.isEmpty()) {
		SetConnectionState(QStringLiteral("OFFLINE // AUTHENTICATION REQUIRED"), tokenError, true);
		ScheduleReconnect();
		return;
	}
	const QString uri = endpointEdit->text().trimmed();
	if (!uri.startsWith(QStringLiteral("ws://"), Qt::CaseInsensitive)) {
		SetConnectionState(QStringLiteral("OFFLINE // INVALID ENDPOINT"),
				   QStringLiteral("The local Studio endpoint must begin with ws://."), true);
		return;
	}

	SetConnectionState(QStringLiteral("CONNECTING // STUDIO BRIDGE"), uri);
	connectButton->setText(QStringLiteral("CANCEL"));
	client = std::make_unique<ClientState>();
	ClientState *state = client.get();
	state->endpoint = std::make_unique<ClientState::Endpoint>();
	state->endpoint->clear_access_channels(websocketpp::log::alevel::all);
	state->endpoint->clear_error_channels(websocketpp::log::elevel::all);
	state->endpoint->init_asio();
	state->endpoint->start_perpetual();
	QPointer<TempestStudioBridge> guarded(this);
	state->endpoint->set_open_handler([guarded, state](websocketpp::connection_hdl handle) {
		state->connection = handle;
		state->connected.store(true);
		if (guarded)
			QMetaObject::invokeMethod(guarded, [guarded]() {
				if (guarded)
					guarded->HandleConnected();
			});
	});
	state->endpoint->set_message_handler(
		[guarded](websocketpp::connection_hdl, ClientState::Endpoint::message_ptr message) {
			const QByteArray payload(message->get_payload().data(), int(message->get_payload().size()));
			if (guarded)
				QMetaObject::invokeMethod(guarded, [guarded, payload]() {
					if (guarded)
						guarded->HandleIncoming(payload);
				});
		});
	auto closed = [guarded, state](websocketpp::connection_hdl handle) {
		state->connected.store(false);
		QString reason = QStringLiteral("Studio connection closed.");
		try {
			auto connection = state->endpoint->get_con_from_hdl(handle);
			if (connection && !connection->get_remote_close_reason().empty())
				reason = QString::fromStdString(connection->get_remote_close_reason());
			else if (connection && connection->get_ec())
				reason = QString::fromStdString(connection->get_ec().message());
		} catch (...) {
		}
		if (guarded)
			QMetaObject::invokeMethod(guarded, [guarded, reason]() {
				if (guarded)
					guarded->HandleDisconnected(reason);
			});
	};
	state->endpoint->set_close_handler(closed);
	state->endpoint->set_fail_handler(closed);

	websocketpp::lib::error_code error;
	auto connection = state->endpoint->get_connection(uri.toStdString(), error);
	if (error) {
		SetConnectionState(QStringLiteral("OFFLINE // CONNECTION FAILED"),
				   QString::fromStdString(error.message()), true);
		DisconnectFromStudio(false);
		ScheduleReconnect();
		return;
	}
	connection->append_header("Authorization", std::string("Bearer ") + token.toStdString());
	state->connection = connection->get_handle();
	state->endpoint->connect(connection);
	state->worker = std::thread([state]() {
		try {
			state->endpoint->run();
		} catch (...) {
		}
	});
}

void TempestStudioBridge::DisconnectFromStudio(bool operatorRequested)
{
	operatorDisconnect = operatorRequested;
	if (reconnectTimer)
		reconnectTimer->stop();
	if (heartbeatTimer)
		heartbeatTimer->stop();
	if (client) {
		ClientState *state = client.get();
		if (state->endpoint) {
			websocketpp::lib::error_code error;
			if (state->connected.load())
				state->endpoint->close(state->connection, websocketpp::close::status::going_away,
						       "Tempest Broadcast disconnecting", error);
			state->endpoint->stop_perpetual();
			state->endpoint->stop();
		}
		if (state->worker.joinable())
			state->worker.join();
		client.reset();
	}
	if (operatorRequested) {
		SetConnectionState(
			QStringLiteral("OFFLINE // OPERATOR DISCONNECTED"),
			QStringLiteral("Local lease timers remain armed until their reactions are restored."));
		connectButton->setText(QStringLiteral("CONNECT"));
	}
}

void TempestStudioBridge::ScheduleReconnect()
{
	if (!operatorDisconnect && autoConnectCheck && autoConnectCheck->isChecked() && reconnectTimer &&
	    !reconnectTimer->isActive())
		reconnectTimer->start();
}

void TempestStudioBridge::HandleConnected()
{
	SetConnectionState(QStringLiteral("ONLINE // STUDIO AUTHENTICATED"),
			   QStringLiteral("Workflow commands are routed through Tempest Studio."));
	connectButton->setText(QStringLiteral("DISCONNECT"));
	SendHello();
	PublishHealth(QStringLiteral("connected"));
	if (heartbeatTimer)
		heartbeatTimer->start();
}

void TempestStudioBridge::HandleDisconnected(const QString &reason)
{
	if (heartbeatTimer)
		heartbeatTimer->stop();
	SetConnectionState(QStringLiteral("OFFLINE // STUDIO UNAVAILABLE"), reason, !operatorDisconnect);
	connectButton->setText(QStringLiteral("CONNECT"));
	if (!operatorDisconnect)
		ScheduleReconnect();
}

void TempestStudioBridge::SetConnectionState(const QString &state, const QString &detail, bool error)
{
	if (connectionLabel) {
		connectionLabel->setText(state);
		connectionLabel->setAccessibleName(state);
		connectionLabel->setStyleSheet(error ? QStringLiteral("color: #ff6b8a;") : QString());
	}
	if (detailLabel)
		detailLabel->setText(detail);
}

QJsonObject TempestStudioBridge::CreateMessage(const QString &kind, const QString &topic, const QJsonObject &payload,
					       const QString &target, const QString &correlationId) const
{
	QJsonObject message{{QStringLiteral("protocolVersion"), QString::fromUtf8(ProtocolVersion)},
			    {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
			    {QStringLiteral("kind"), kind},
			    {QStringLiteral("source"), QString::fromUtf8(ApplicationId)},
			    {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
	if (!topic.isEmpty())
		message.insert(QStringLiteral("topic"), topic);
	if (!target.isEmpty())
		message.insert(QStringLiteral("target"), target);
	if (!correlationId.isEmpty())
		message.insert(QStringLiteral("correlationId"), correlationId);
	if (!payload.isEmpty())
		message.insert(QStringLiteral("payload"), payload);
	return message;
}

void TempestStudioBridge::SendDocument(const QJsonObject &document)
{
	if (!IsConnected())
		return;
	const std::string data = QJsonDocument(document).toJson(QJsonDocument::Compact).toStdString();
	ClientState *state = client.get();
	std::lock_guard<std::mutex> lock(state->sendMutex);
	websocketpp::lib::error_code error;
	state->endpoint->send(state->connection, data, websocketpp::frame::opcode::text, error);
	if (error)
		SetConnectionState(QStringLiteral("ONLINE // SEND DEGRADED"), QString::fromStdString(error.message()),
				   true);
}

void TempestStudioBridge::SendHello()
{
	const QJsonArray capabilities{
		QStringLiteral("broadcast.reaction.trigger"), QStringLiteral("broadcast.reaction.clear"),
		QStringLiteral("broadcast.audio.play"),       QStringLiteral("broadcast.visual.show"),
		QStringLiteral("broadcast.visual.hide"),      QStringLiteral("broadcast.status")};
	SendDocument(CreateMessage(QStringLiteral("hello"), {},
				   {{QStringLiteral("applicationId"), QString::fromUtf8(ApplicationId)},
				    {QStringLiteral("version"), QStringLiteral(TEMPEST_PRODUCT_VERSION)},
				    {QStringLiteral("protocolVersion"), QString::fromUtf8(ProtocolVersion)},
				    {QStringLiteral("capabilities"), capabilities}}));
	QJsonArray topics = capabilities;
	topics.append(QStringLiteral("system.*"));
	SendDocument(CreateMessage(QStringLiteral("subscribe"), {}, {{QStringLiteral("topics"), topics}}));
}

void TempestStudioBridge::SendHeartbeat()
{
	SendDocument(CreateMessage(QStringLiteral("heartbeat"), {},
				   {{QStringLiteral("activeLeases"), activeLeases.size()},
				    {QStringLiteral("commandsHandled"), commandsHandled}}));
}

void TempestStudioBridge::PublishHealth(const QString &reason)
{
	QJsonObject payload{{QStringLiteral("status"),
			     IsConnected() ? QStringLiteral("online") : QStringLiteral("offline")},
			    {QStringLiteral("ready"), IsConnected() && reactor && reactor->ExternalEventBridgeArmed()},
			    {QStringLiteral("version"), QStringLiteral(TEMPEST_PRODUCT_VERSION)},
			    {QStringLiteral("protocolVersion"), QString::fromUtf8(ProtocolVersion)},
			    {QStringLiteral("streaming"), obs_frontend_streaming_active()},
			    {QStringLiteral("recording"), obs_frontend_recording_active()},
			    {QStringLiteral("reactionArmed"), reactor && reactor->ExternalEventBridgeArmed()},
			    {QStringLiteral("activeLeases"), activeLeases.size()},
			    {QStringLiteral("canvasProfile"), BuildCanvasProfile()},
			    {QStringLiteral("sourceInventory"), BuildSourceInventory(main)},
			    {QStringLiteral("commandsHandled"), commandsHandled}};
	if (!reason.isEmpty())
		payload.insert(QStringLiteral("reason"), reason);
	PublishEvent(QStringLiteral("broadcast.status"), payload);
}

void TempestStudioBridge::PublishEvent(const QString &topic, const QJsonObject &payload)
{
	SendDocument(CreateMessage(QStringLiteral("publish"), topic, payload));
}

void TempestStudioBridge::HandleIncoming(const QByteArray &messageBytes)
{
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(messageBytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		PublishEvent(QStringLiteral("broadcast.failure"),
			     {{QStringLiteral("error"), QStringLiteral("Invalid Studio Bridge message")},
			      {QStringLiteral("detail"), error.errorString()}});
		return;
	}
	const QJsonObject message = document.object();
	if (message.value(QStringLiteral("protocolVersion")).toString() != QString::fromUtf8(ProtocolVersion)) {
		SetConnectionState(QStringLiteral("ONLINE // PROTOCOL MISMATCH"),
				   QStringLiteral("Studio and Broadcast must both use Bridge protocol 1.0."), true);
		return;
	}
	const QString kind = message.value(QStringLiteral("kind")).toString();
	if (kind == QStringLiteral("command"))
		HandleCommand(message);
	else if (kind == QStringLiteral("welcome"))
		SetConnectionState(QStringLiteral("ONLINE // STUDIO AUTHENTICATED"),
				   QStringLiteral("Bridge protocol 1.0 welcome received."));
	else if (kind == QStringLiteral("error"))
		SetConnectionState(
			QStringLiteral("ONLINE // BRIDGE ERROR"),
			message.value(QStringLiteral("payload")).toObject().value(QStringLiteral("message")).toString(),
			true);
}

QString TempestStudioBridge::CommandKey(const QJsonObject &payload) const
{
	return QStringLiteral("%1/%2/%3")
		.arg(payload.value(QStringLiteral("runId")).toString(),
		     payload.value(QStringLiteral("actionId")).toString(),
		     payload.value(QStringLiteral("phase")).toString());
}

QString TempestStudioBridge::LeaseKey(const QJsonObject &payload) const
{
	return QStringLiteral("%1/%2").arg(payload.value(QStringLiteral("runId")).toString(),
					   payload.value(QStringLiteral("actionId")).toString());
}

void TempestStudioBridge::RememberCommand(const QString &key)
{
	processedCommands.insert(key);
	processedCommandOrder.append(key);
	while (processedCommandOrder.size() > MaximumRememberedCommands)
		processedCommands.remove(processedCommandOrder.takeFirst());
}

void TempestStudioBridge::HandleCommand(const QJsonObject &message)
{
	const QString target = message.value(QStringLiteral("target")).toString();
	if (!target.isEmpty() && target != QString::fromUtf8(ApplicationId))
		return;
	const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
	const QString commandKey = CommandKey(payload);
	if (processedCommands.contains(commandKey)) {
		SendResponse(message, true, QStringLiteral("duplicate"),
			     QStringLiteral("Command was already applied; no state was changed."));
		return;
	}
	RememberCommand(commandKey);
	++commandsHandled;
	const QString topic = message.value(QStringLiteral("topic")).toString();
	if (topic == QStringLiteral("broadcast.reaction.trigger"))
		HandleReactionTrigger(message, payload);
	else if (topic == QStringLiteral("broadcast.reaction.clear"))
		HandleReactionClear(message, payload);
	else if (topic == QStringLiteral("broadcast.audio.play"))
		HandleAudioPlay(message, payload);
	else if (topic == QStringLiteral("broadcast.visual.show"))
		HandleVisualShow(message, payload);
	else if (topic == QStringLiteral("broadcast.visual.hide"))
		HandleVisualHide(message, payload);
	else if (topic == QStringLiteral("broadcast.status"))
		HandleStatusRequest(message);
	else
		SendResponse(message, false, QStringLiteral("unsupported"),
			     QStringLiteral("Broadcast does not provide capability %1.").arg(topic));
	commandLabel->setText(
		QStringLiteral("COMMANDS // %1 // ACTIVE LEASES // %2").arg(commandsHandled).arg(activeLeases.size()));
}

void TempestStudioBridge::HandleReactionTrigger(const QJsonObject &message, const QJsonObject &payload)
{
	if (!reactor) {
		SendResponse(message, false, QStringLiteral("unavailable"),
			     QStringLiteral("Audio Reactor is unavailable."));
		return;
	}
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	const QJsonObject lease = payload.value(QStringLiteral("lease")).toObject();
	const QString leaseKey = LeaseKey(payload);
	const QString runId = payload.value(QStringLiteral("runId")).toString();
	const int requestedDuration = arguments.value(QStringLiteral("durationMs"))
					      .toInt(lease.value(QStringLiteral("durationMs")).toInt(2200));
	const int durationMs = std::clamp(requestedDuration, 250, 300000);
	const QString reactionId = FirstString(arguments, {"reactionId", "name", "cue"});
	const QString type = FirstString(arguments, {"eventType", "type"}).isEmpty()
				     ? QStringLiteral("studio_reaction")
				     : FirstString(arguments, {"eventType", "type"});
	const QString name = reactionId.isEmpty() ? QStringLiteral("STUDIO REACTION") : reactionId;
	const QString dedupeId = FirstString(arguments, {"dedupeId"}).isEmpty() ? leaseKey
										: FirstString(arguments, {"dedupeId"});
	const float strength = float(arguments.value(QStringLiteral("strength"))
					     .toDouble(arguments.value(QStringLiteral("intensity")).toDouble(0.9)));

	if (!currentLeaseKey.isEmpty() && currentLeaseKey != leaseKey) {
		if (auto timer = activeLeases.take(currentLeaseKey)) {
			timer->stop();
			timer->deleteLater();
		}
		reactor->ClearExternalEvent();
	}
	const bool applied =
		reactor->TriggerExternalEvent(type, name, strength, durationMs, FirstString(arguments, {"circuit"}),
					      FirstString(arguments, {"accent"}), FirstString(arguments, {"effect"}),
					      QStringLiteral("tempest-studio"), dedupeId, 0);
	if (!applied) {
		SendResponse(message, false, QStringLiteral("degraded"),
			     QStringLiteral("Reaction was rejected. Arm external reactions in Audio Reactor."));
		PublishEvent(QStringLiteral("broadcast.failure"),
			     {{QStringLiteral("runId"), runId},
			      {QStringLiteral("actionId"), payload.value(QStringLiteral("actionId"))},
			      {QStringLiteral("reason"), QStringLiteral("reaction-disarmed")}});
		return;
	}

	auto *timer = new QTimer(this);
	timer->setSingleShot(true);
	timer->setInterval(durationMs);
	connect(timer, &QTimer::timeout, this, [this, leaseKey]() { ExpireLease(leaseKey); });
	if (auto oldTimer = activeLeases.take(leaseKey))
		oldTimer->deleteLater();
	activeLeases.insert(leaseKey, timer);
	currentLeaseKey = leaseKey;
	timer->start();
	SendResponse(message, true, QStringLiteral("active"), QStringLiteral("Reaction lease applied."),
		     {{QStringLiteral("leaseKey"), leaseKey}, {QStringLiteral("durationMs"), durationMs}});
	PublishHealth(QStringLiteral("reaction-activated"));
}

void TempestStudioBridge::HandleReactionClear(const QJsonObject &message, const QJsonObject &payload)
{
	const QString leaseKey = LeaseKey(payload);
	if (auto timer = activeLeases.take(leaseKey)) {
		timer->stop();
		timer->deleteLater();
	}
	const bool wasCurrent = currentLeaseKey == leaseKey;
	if (wasCurrent && reactor) {
		reactor->ClearExternalEvent();
		currentLeaseKey.clear();
	}
	SendResponse(message, true, wasCurrent ? QStringLiteral("released") : QStringLiteral("already-released"),
		     wasCurrent ? QStringLiteral("Reaction state restored.")
				: QStringLiteral("No active state remained for this workflow action."));
	PublishHealth(QStringLiteral("reaction-released"));
}

void TempestStudioBridge::ExpireLease(const QString &leaseKey)
{
	if (auto timer = activeLeases.take(leaseKey))
		timer->deleteLater();
	if (currentLeaseKey == leaseKey && reactor) {
		reactor->ClearExternalEvent();
		currentLeaseKey.clear();
	}
	const QString visualSource = activeVisualSources.take(leaseKey);
	if (!visualSource.isEmpty())
		SetCurrentSceneSourceVisible(visualSource, false);
	PublishEvent(QStringLiteral("broadcast.reaction.expired"),
		     {{QStringLiteral("leaseKey"), leaseKey},
		      {QStringLiteral("reason"), QStringLiteral("local-fallback-expiry")}});
	PublishHealth(QStringLiteral("lease-expired"));
	if (commandLabel)
		commandLabel->setText(QStringLiteral("COMMANDS // %1 // ACTIVE LEASES // %2")
					      .arg(commandsHandled)
					      .arg(activeLeases.size()));
}

void TempestStudioBridge::HandleAudioPlay(const QJsonObject &message, const QJsonObject &payload)
{
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	const QString cue = FirstString(arguments, {"cue"}).toLower();
	const QString sourceName = FirstString(arguments, {"broadcastAudioSource", "sourceName", "source"});
	obs_source_t *source = sourceName.isEmpty() ? nullptr : obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source) {
		SendResponse(message, false, QStringLiteral("degraded"),
			     sourceName.isEmpty()
				     ? QStringLiteral("Studio did not supply an OBS audio source.")
				     : QStringLiteral("OBS audio source was not found: %1").arg(sourceName));
		PublishEvent(QStringLiteral("broadcast.failure"),
			     {{QStringLiteral("capability"), QStringLiteral("broadcast.audio.play")},
			      {QStringLiteral("cue"), cue},
			      {QStringLiteral("sourceName"), sourceName},
			      {QStringLiteral("reason"), QStringLiteral("source-not-found")}});
		return;
	}
	obs_source_media_restart(source);
	obs_source_release(source);
	SendResponse(message, true, QStringLiteral("played"),
		     QStringLiteral("OBS audio cue restarted: %1").arg(sourceName),
		     {{QStringLiteral("cue"), cue}, {QStringLiteral("sourceName"), sourceName}});
}

bool TempestStudioBridge::SetCurrentSceneSourceVisible(const QString &sourceName, bool visible, bool restartMedia)
{
	if (!main || sourceName.isEmpty())
		return false;
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return false;
	const QByteArray name = sourceName.toUtf8();
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, name.constData());
	if (!item)
		return false;
	obs_sceneitem_set_visible(item, visible);
	if (visible && restartMedia) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (source && (obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0)
			obs_source_media_restart(source);
	}
	return true;
}

void TempestStudioBridge::HandleVisualShow(const QJsonObject &message, const QJsonObject &payload)
{
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	const QJsonObject lease = payload.value(QStringLiteral("lease")).toObject();
	const QString sourceName =
		FirstString(arguments, {"broadcastVisualSource", "visualSource", "sourceName", "cue"});
	if (!SetCurrentSceneSourceVisible(sourceName, true, true)) {
		SendResponse(
			message, false, QStringLiteral("degraded"),
			QStringLiteral("OBS visual source was not found in the current scene: %1").arg(sourceName));
		PublishEvent(QStringLiteral("broadcast.failure"),
			     {{QStringLiteral("capability"), QStringLiteral("broadcast.visual.show")},
			      {QStringLiteral("sourceName"), sourceName},
			      {QStringLiteral("reason"), QStringLiteral("scene-source-not-found")}});
		return;
	}
	const QString leaseKey = LeaseKey(payload);
	const int requestedDuration = arguments.value(QStringLiteral("durationMs"))
					      .toInt(lease.value(QStringLiteral("durationMs")).toInt(2200));
	const int durationMs = std::clamp(requestedDuration, 250, 300000);
	auto *timer = new QTimer(this);
	timer->setSingleShot(true);
	timer->setInterval(durationMs);
	connect(timer, &QTimer::timeout, this, [this, leaseKey]() { ExpireLease(leaseKey); });
	if (auto oldTimer = activeLeases.take(leaseKey))
		oldTimer->deleteLater();
	activeVisualSources.insert(leaseKey, sourceName);
	activeLeases.insert(leaseKey, timer);
	timer->start();
	SendResponse(message, true, QStringLiteral("visible"),
		     QStringLiteral("OBS visual source shown: %1").arg(sourceName),
		     {{QStringLiteral("leaseKey"), leaseKey}, {QStringLiteral("durationMs"), durationMs}});
}

void TempestStudioBridge::HandleVisualHide(const QJsonObject &message, const QJsonObject &payload)
{
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	const QString leaseKey = LeaseKey(payload);
	if (auto timer = activeLeases.take(leaseKey)) {
		timer->stop();
		timer->deleteLater();
	}
	QString sourceName = activeVisualSources.take(leaseKey);
	if (sourceName.isEmpty())
		sourceName = FirstString(arguments, {"broadcastVisualSource", "visualSource", "sourceName", "cue"});
	const bool hidden = SetCurrentSceneSourceVisible(sourceName, false);
	SendResponse(message, true, hidden ? QStringLiteral("hidden") : QStringLiteral("already-hidden"),
		     hidden ? QStringLiteral("OBS visual source hidden: %1").arg(sourceName)
			    : QStringLiteral("No visible OBS source remained for this Sound Alert."));
}

void TempestStudioBridge::HandleStatusRequest(const QJsonObject &message)
{
	const QJsonObject status{{QStringLiteral("ready"), reactor && reactor->ExternalEventBridgeArmed()},
				 {QStringLiteral("streaming"), obs_frontend_streaming_active()},
				 {QStringLiteral("recording"), obs_frontend_recording_active()},
				 {QStringLiteral("activeLeases"), activeLeases.size()},
				 {QStringLiteral("canvasProfile"), BuildCanvasProfile()},
				 {QStringLiteral("sourceInventory"), BuildSourceInventory(main)},
				 {QStringLiteral("version"), QStringLiteral(TEMPEST_PRODUCT_VERSION)}};
	SendResponse(message, true, QStringLiteral("ready"), QStringLiteral("Broadcast status snapshot."), status);
	PublishHealth(QStringLiteral("requested"));
}

void TempestStudioBridge::SendResponse(const QJsonObject &command, bool success, const QString &status,
				       const QString &detail, const QJsonObject &extra)
{
	QJsonObject payload{{QStringLiteral("commandId"), command.value(QStringLiteral("id"))},
			    {QStringLiteral("success"), success},
			    {QStringLiteral("status"), status},
			    {QStringLiteral("detail"), detail}};
	for (auto it = extra.begin(); it != extra.end(); ++it)
		payload.insert(it.key(), it.value());
	const QString correlationId = command.value(QStringLiteral("correlationId")).toString();
	SendDocument(CreateMessage(QStringLiteral("response"), command.value(QStringLiteral("topic")).toString(),
				   payload, command.value(QStringLiteral("source")).toString(), correlationId));
}
