#include "TempestStudioBridge.hpp"

#include "TempestDestinationCoordinator.hpp"
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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <thread>

#include "moc_TempestStudioBridge.cpp"

#ifndef TEMPEST_PRODUCT_VERSION
#define TEMPEST_PRODUCT_VERSION "1.4.7"
#endif

namespace {
constexpr char ConfigSection[] = "TempestStudioBridge";
constexpr char ApplicationId[] = "com.tempestmainframe.tempest-broadcast";
constexpr char ProtocolVersion[] = "1.0";
constexpr char InteractionRenderContractVersion[] = "1.0";
constexpr int MaximumRememberedCommands = 512;
constexpr int BrowserRecoveryDelayMs = 5000;

struct BrowserRecoveryContext {
	QString host;
	int port = 4765;
	struct Candidate {
		QString uuid;
		QString name;
		QString statusPath;
		QString statusSection;
	};
	QList<Candidate> candidates;
};

bool IsLoopbackHost(const QString &host)
{
	return host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 ||
	       host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
}

bool IsStudioBrowserUrl(const QString &value, const BrowserRecoveryContext &context)
{
	const QUrl url(value);
	if (!url.isValid() || !IsLoopbackHost(url.host()) || !IsLoopbackHost(context.host)) {
		return false;
	}
	const int port = url.port(url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ? 443 : 80);
	return port == context.port;
}

bool CollectStudioBrowserSource(void *data, obs_source_t *source)
{
	auto *context = static_cast<BrowserRecoveryContext *>(data);
	if (!context || !source || obs_source_removed(source) ||
	    (!obs_source_active(source) && !obs_source_showing(source))) {
		return true;
	}
	const char *id = obs_source_get_unversioned_id(source);
	if (!id || strcmp(id, "browser_source") != 0) {
		return true;
	}

	OBSDataAutoRelease settings = obs_source_get_settings(source);
	const QString url = QString::fromUtf8(obs_data_get_string(settings, "url"));
	if (!IsStudioBrowserUrl(url, *context)) {
		return true;
	}

	const QUrl browserUrl(url);
	QString statusPath;
	QString statusSection;
	if (browserUrl.path() == QStringLiteral("/visual-alerts/twitch")) {
		statusPath = QStringLiteral("/v1/visual-alerts");
		statusSection = QStringLiteral("twitch");
	} else if (browserUrl.path() == QStringLiteral("/visual-alerts") ||
		   browserUrl.path() == QStringLiteral("/visual-alerts/interactions")) {
		statusPath = QStringLiteral("/v1/visual-alerts");
		statusSection = QStringLiteral("interaction");
	} else if (browserUrl.path() == QStringLiteral("/chat-overlay")) {
		statusPath = QStringLiteral("/v1/chat-overlay");
	} else if (browserUrl.path() == QStringLiteral("/emote-wall")) {
		statusPath = QStringLiteral("/v1/emote-wall");
	} else if (browserUrl.path() == QStringLiteral("/dice-overlay")) {
		statusPath = QStringLiteral("/v1/dice-overlay");
	} else if (browserUrl.path() == QStringLiteral("/twitch-experiences")) {
		statusPath = QStringLiteral("/v1/twitch-experiences");
	} else if (browserUrl.path() == QStringLiteral("/discord-voice")) {
		statusPath = QStringLiteral("/v1/discord-voice");
	} else {
		return true;
	}

	const char *uuid = obs_source_get_uuid(source);
	if (uuid && *uuid) {
		context->candidates.append({QString::fromUtf8(uuid), QString::fromUtf8(obs_source_get_name(source)),
					    statusPath, statusSection});
	}
	return true;
}

struct VerticalAlertInventory {
	bool twitch = false;
	bool interactions = false;
};

bool CollectVerticalAlerts(void *data, obs_source_t *source)
{
	if (!source || obs_source_removed(source)) {
		return true;
	}
	const char *id = obs_source_get_unversioned_id(source);
	if (!id || strcmp(id, "browser_source") != 0) {
		return true;
	}
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	const QString url = QString::fromUtf8(obs_data_get_string(settings, "url"));
	auto *inventory = static_cast<VerticalAlertInventory *>(data);
	if (url.contains(QStringLiteral("/visual-alerts/twitch")) &&
	    url.contains(QStringLiteral("orientation=vertical"))) {
		inventory->twitch = true;
	}
	if (url.contains(QStringLiteral("/visual-alerts/interactions")) &&
	    url.contains(QStringLiteral("orientation=vertical"))) {
		inventory->interactions = true;
	}
	return true;
}

int CountLinkedScenes(int width, int height, int &totalScenes)
{
	int linkedScenes = 0;
	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	totalScenes = static_cast<int>(scenes.sources.num);
	for (size_t sceneIndex = 0; sceneIndex < scenes.sources.num; ++sceneIndex) {
		OBSDataAutoRelease settings = obs_source_get_settings(scenes.sources.array[sceneIndex]);
		OBSDataArrayAutoRelease links = obs_data_get_array(settings, "canvas");
		for (size_t linkIndex = 0; links && linkIndex < obs_data_array_count(links); ++linkIndex) {
			OBSDataAutoRelease link = obs_data_array_item(links, linkIndex);
			if (obs_data_get_int(link, "width") == width && obs_data_get_int(link, "height") == height &&
			    *obs_data_get_string(link, "scene")) {
				++linkedScenes;
				break;
			}
		}
	}
	obs_frontend_source_list_free(&scenes);
	return linkedScenes;
}

QString FirstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QString value = object.value(QString::fromUtf8(key)).toString().trimmed();
		if (!value.isEmpty()) {
			return value;
		}
	}
	return {};
}

struct StudioSourceInventory {
	QStringList audio;
	QStringList visual;
};

bool CollectAudioSource(void *data, obs_source_t *source)
{
	if (!source || obs_source_removed(source) || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
		return true;
	}
	const char *name = obs_source_get_name(source);
	if (name && *name) {
		static_cast<StudioSourceInventory *>(data)->audio.append(QString::fromUtf8(name));
	}
	return true;
}

bool CollectVisualSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (source && !obs_source_removed(source) && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) {
		const char *name = obs_source_get_name(source);
		if (name && *name) {
			static_cast<StudioSourceInventory *>(data)->visual.append(QString::fromUtf8(name));
		}
	}
	if (obs_sceneitem_is_group(item)) {
		if (obs_scene_t *groupScene = obs_sceneitem_group_get_scene(item)) {
			obs_scene_enum_items(groupScene, CollectVisualSceneItem, data);
		}
	}
	return true;
}

QJsonObject BuildSourceInventory(OBSBasic *main)
{
	StudioSourceInventory inventory;
	obs_enum_sources(CollectAudioSource, &inventory);
	OBSScene scene = main ? main->GetCurrentScene() : OBSScene();
	if (scene) {
		obs_scene_enum_items(scene, CollectVisualSceneItem, &inventory);
	}
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
	if (!obs_get_video_info(&videoInfo)) {
		return {};
	}
	return {{QStringLiteral("baseWidth"), static_cast<int>(videoInfo.base_width)},
		{QStringLiteral("baseHeight"), static_cast<int>(videoInfo.base_height)},
		{QStringLiteral("outputWidth"), static_cast<int>(videoInfo.output_width)},
		{QStringLiteral("outputHeight"), static_cast<int>(videoInfo.output_height)},
		{QStringLiteral("fpsNumerator"), static_cast<int>(videoInfo.fps_num)},
		{QStringLiteral("fpsDenominator"), static_cast<int>(videoInfo.fps_den)}};
}

QString InteractionKey(const QJsonObject &message, const QJsonObject &payload, const QJsonObject &arguments)
{
	const QString explicitKey = FirstString(arguments, {"instanceId"});
	if (!explicitKey.isEmpty()) {
		return explicitKey;
	}
	const QString runId = payload.value(QStringLiteral("runId")).toString().trimmed();
	const QString actionId = payload.value(QStringLiteral("actionId")).toString().trimmed();
	if (!runId.isEmpty() || !actionId.isEmpty()) {
		return QStringLiteral("%1/%2").arg(runId, actionId);
	}
	return message.value(QStringLiteral("id")).toString();
}

QString InteractionStorageKey(const QString &instanceId)
{
	return QStringLiteral("interaction/%1").arg(instanceId);
}

QJsonObject InteractionRenderTargets(const QJsonObject &arguments)
{
	QJsonObject targets = arguments.value(QStringLiteral("renderTargets")).toObject();
	if (!targets.isEmpty()) {
		return targets;
	}
	const QString legacySource = FirstString(arguments, {"sourceName", "broadcastVisualSource", "visualSource"});
	if (legacySource.isEmpty()) {
		return {};
	}
	QJsonObject target{{QStringLiteral("sourceName"), legacySource}};
	if (arguments.value(QStringLiteral("placement")).isObject()) {
		target.insert(QStringLiteral("placement"), arguments.value(QStringLiteral("placement")));
	}
	if (arguments.value(QStringLiteral("restartMedia")).isBool()) {
		target.insert(QStringLiteral("restartMedia"), arguments.value(QStringLiteral("restartMedia")));
	}
	return {{QStringLiteral("horizontal"), target}};
}

bool BuildInteractionTransform(obs_sceneitem_t *item, int canvasWidth, int canvasHeight, const QJsonObject &target,
			       obs_transform_info &transform, bool &hasPlacement, QString &error)
{
	obs_sceneitem_get_info2(item, &transform);
	hasPlacement = target.value(QStringLiteral("placement")).isObject();
	if (!hasPlacement) {
		return true;
	}

	const QJsonObject placement = target.value(QStringLiteral("placement")).toObject();
	for (const QString &field :
	     {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height")}) {
		if (!placement.value(field).isDouble()) {
			error = QStringLiteral("Normalized placement requires numeric x, y, width, and height.");
			return false;
		}
	}
	const double x = placement.value(QStringLiteral("x")).toDouble();
	const double y = placement.value(QStringLiteral("y")).toDouble();
	const double width = placement.value(QStringLiteral("width")).toDouble();
	const double height = placement.value(QStringLiteral("height")).toDouble();
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || x < 0.0 ||
	    y < 0.0 || width <= 0.0 || height <= 0.0 || x + width > 1.0 || y + height > 1.0) {
		error = QStringLiteral("Normalized placement must stay within the 0..1 canvas and have positive size.");
		return false;
	}

	const QString fit = target.value(QStringLiteral("fit")).toString(QStringLiteral("contain")).toLower();
	if (fit != QStringLiteral("contain") && fit != QStringLiteral("cover") && fit != QStringLiteral("stretch")) {
		error = QStringLiteral("Render target fit must be contain, cover, or stretch.");
		return false;
	}
	const double rotation = target.value(QStringLiteral("rotationDegrees")).toDouble(0.0);
	if (!std::isfinite(rotation) || rotation < -360.0 || rotation > 360.0) {
		error = QStringLiteral("rotationDegrees must be between -360 and 360.");
		return false;
	}

	vec2_set(&transform.pos, static_cast<float>(x * canvasWidth), static_cast<float>(y * canvasHeight));
	vec2_set(&transform.scale, 1.0f, 1.0f);
	vec2_set(&transform.bounds, static_cast<float>(width * canvasWidth), static_cast<float>(height * canvasHeight));
	transform.rot = static_cast<float>(rotation);
	transform.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	transform.bounds_alignment = OBS_ALIGN_CENTER;
	transform.bounds_type =
		fit == QStringLiteral("stretch")
			? OBS_BOUNDS_STRETCH
			: (fit == QStringLiteral("cover") ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER);
	transform.crop_to_bounds = fit == QStringLiteral("cover");
	return true;
}

OBSScene ResolveInteractionScene(OBSBasic *main, const QString &orientation, QString &sceneName, QString &canvasId,
				 int &canvasWidth, int &canvasHeight, QString &error)
{
	if (!main) {
		error = QStringLiteral("Broadcast has no active scene controller.");
		return {};
	}
	OBSScene current = main->GetCurrentScene();
	if (!current) {
		error = QStringLiteral("Broadcast has no current program scene.");
		return {};
	}
	obs_source_t *currentSource = obs_scene_get_source(current);
	if (orientation == QStringLiteral("horizontal")) {
		sceneName = QString::fromUtf8(obs_source_get_name(currentSource));
		canvasId = QStringLiteral("main");
		canvasWidth = static_cast<int>(obs_source_get_width(currentSource));
		canvasHeight = static_cast<int>(obs_source_get_height(currentSource));
		return current;
	}
	if (orientation != QStringLiteral("vertical")) {
		error = QStringLiteral("Render target orientation must be horizontal or vertical.");
		return {};
	}
	if (!main->Config()) {
		error = QStringLiteral("The active Broadcast profile is unavailable.");
		return {};
	}

	const QString selectedUuid =
		QString::fromUtf8(config_get_string(main->Config(), "Stream1", "MultitrackExtraCanvas"));
	if (selectedUuid.isEmpty()) {
		error = QStringLiteral("No additional portrait canvas is selected.");
		return {};
	}
	OBSCanvasAutoRelease canvas = obs_get_canvas_by_uuid(selectedUuid.toUtf8().constData());
	obs_video_info info{};
	if (!canvas || !obs_canvas_get_video_info(canvas, &info)) {
		error = QStringLiteral("The selected portrait canvas is unavailable.");
		return {};
	}

	OBSDataAutoRelease settings = obs_source_get_settings(currentSource);
	OBSDataArrayAutoRelease links = obs_data_get_array(settings, "canvas");
	for (size_t index = 0; links && index < obs_data_array_count(links); ++index) {
		OBSDataAutoRelease link = obs_data_array_item(links, index);
		if (obs_data_get_int(link, "width") != static_cast<long long>(info.base_width) ||
		    obs_data_get_int(link, "height") != static_cast<long long>(info.base_height)) {
			continue;
		}
		const QString linkedName = QString::fromUtf8(obs_data_get_string(link, "scene")).trimmed();
		if (linkedName.isEmpty()) {
			continue;
		}
		OBSSceneAutoRelease linked = obs_canvas_get_scene_by_name(canvas, linkedName.toUtf8().constData());
		if (!linked) {
			continue;
		}
		sceneName = linkedName;
		canvasId = selectedUuid;
		obs_source_t *linkedSource = obs_scene_get_source(linked);
		canvasWidth = static_cast<int>(obs_source_get_width(linkedSource));
		canvasHeight = static_cast<int>(obs_source_get_height(linkedSource));
		return OBSScene(linked);
	}

	error = QStringLiteral("The current program scene has no link to the selected portrait canvas.");
	return {};
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

struct TempestStudioBridge::InteractionState {
	struct Target {
		QString orientation;
		QString sourceName;
		QString sceneName;
		QString canvasId;
		int canvasWidth = 0;
		int canvasHeight = 0;
		OBSScene scene;
		OBSSceneItem item;
		obs_transform_info originalTransform{};
		bool originalVisible = false;
	};

	QString instanceId;
	QList<Target> targets;
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
	recoveryNetwork = new QNetworkAccessManager(this);

	if (autoConnectCheck->isChecked()) {
		QTimer::singleShot(1800, this, &TempestStudioBridge::ConnectToStudio);
	}
}

TempestStudioBridge::~TempestStudioBridge()
{
	DisconnectFromStudio(true);
}

bool TempestStudioBridge::IsConnected() const
{
	return client && client->connected.load();
}

void TempestStudioBridge::SetDestinationCoordinator(TempestDestinationCoordinator *coordinator)
{
	if (destinationCoordinator) {
		disconnect(destinationCoordinator, nullptr, this, nullptr);
	}
	destinationCoordinator = coordinator;
	if (destinationCoordinator) {
		connect(destinationCoordinator, &TempestDestinationCoordinator::StatusChanged, this,
			[this]() { PublishHealth(QStringLiteral("destination-changed")); });
	}
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
		if (IsConnected() || client) {
			DisconnectFromStudio(true);
		} else {
			ConnectToStudio();
		}
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
		if (enabled && !IsConnected()) {
			ScheduleReconnect();
		} else if (!enabled && reconnectTimer) {
			reconnectTimer->stop();
		}
	});
}

void TempestStudioBridge::LoadState()
{
	config_t *config = App()->GetUserConfig();
	QString endpoint = QString::fromUtf8(config_get_string(config, ConfigSection, "Endpoint")).trimmed();
	if (endpoint.isEmpty()) {
		endpoint = QStringLiteral("ws://127.0.0.1:4765/v1/socket");
	}
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
		if (error) {
			*error = QStringLiteral(
				"Studio token was not found. Start Tempest Studio once, then reconnect.");
		}
		return {};
	}
	const QString token = QString::fromUtf8(file.readAll()).trimmed();
	if (token.size() < 32) {
		if (error) {
			*error = QStringLiteral("Studio token file is invalid or incomplete.");
		}
		return {};
	}
	return token;
}

void TempestStudioBridge::ConnectToStudio()
{
	if (IsConnected()) {
		return;
	}
	if (client) {
		DisconnectFromStudio(false);
	}
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
		if (guarded) {
			QMetaObject::invokeMethod(guarded, [guarded]() {
				if (guarded) {
					guarded->HandleConnected();
				}
			});
		}
	});
	state->endpoint->set_message_handler(
		[guarded](websocketpp::connection_hdl, ClientState::Endpoint::message_ptr message) {
			const QByteArray payload(message->get_payload().data(), int(message->get_payload().size()));
			if (guarded) {
				QMetaObject::invokeMethod(guarded, [guarded, payload]() {
					if (guarded) {
						guarded->HandleIncoming(payload);
					}
				});
			}
		});
	auto closed = [guarded, state](websocketpp::connection_hdl handle) {
		state->connected.store(false);
		QString reason = QStringLiteral("Studio connection closed.");
		try {
			auto connection = state->endpoint->get_con_from_hdl(handle);
			if (connection && !connection->get_remote_close_reason().empty()) {
				reason = QString::fromStdString(connection->get_remote_close_reason());
			} else if (connection && connection->get_ec()) {
				reason = QString::fromStdString(connection->get_ec().message());
			}
		} catch (...) {
		}
		if (guarded) {
			QMetaObject::invokeMethod(guarded, [guarded, reason]() {
				if (guarded) {
					guarded->HandleDisconnected(reason);
				}
			});
		}
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
	if (reconnectTimer) {
		reconnectTimer->stop();
	}
	if (heartbeatTimer) {
		heartbeatTimer->stop();
	}
	if (client) {
		ClientState *state = client.get();
		if (state->endpoint) {
			websocketpp::lib::error_code error;
			if (state->connected.load()) {
				state->endpoint->close(state->connection, websocketpp::close::status::going_away,
						       "Tempest Broadcast disconnecting", error);
			}
			state->endpoint->stop_perpetual();
			state->endpoint->stop();
		}
		if (state->worker.joinable()) {
			state->worker.join();
		}
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
	    !reconnectTimer->isActive()) {
		reconnectTimer->start();
	}
}

void TempestStudioBridge::HandleConnected()
{
	SetConnectionState(QStringLiteral("ONLINE // STUDIO AUTHENTICATED"),
			   QStringLiteral("Workflow commands are routed through Tempest Studio."));
	connectButton->setText(QStringLiteral("DISCONNECT"));
	SendHello();
	PublishHealth(QStringLiteral("connected"));
	if (heartbeatTimer) {
		heartbeatTimer->start();
	}
	// Every fresh Studio session can invalidate a Browser Source connection even
	// when Broadcast never observed the brief outage. Recheck once per session;
	// healthy sources are left untouched by RecoverFailedBrowserSources().
	ScheduleBrowserSourceRecovery();
}

void TempestStudioBridge::HandleDisconnected(const QString &reason)
{
	if (heartbeatTimer) {
		heartbeatTimer->stop();
	}
	SetConnectionState(QStringLiteral("OFFLINE // STUDIO UNAVAILABLE"), reason, !operatorDisconnect);
	connectButton->setText(QStringLiteral("CONNECT"));
	if (!operatorDisconnect) {
		ScheduleReconnect();
	}
}

void TempestStudioBridge::ScheduleBrowserSourceRecovery()
{
	if (browserRecoveryScheduled) {
		return;
	}
	browserRecoveryScheduled = true;
	QPointer<TempestStudioBridge> guarded(this);
	QTimer::singleShot(BrowserRecoveryDelayMs, this, [guarded]() {
		if (!guarded) {
			return;
		}
		guarded->browserRecoveryScheduled = false;
		if (!guarded->IsConnected()) {
			return;
		}
		guarded->RecoverFailedBrowserSources();
		QTimer::singleShot(20000, guarded, [guarded]() {
			if (guarded && guarded->IsConnected()) {
				guarded->RecoverFailedBrowserSources();
			}
		});
	});
}

void TempestStudioBridge::RecoverFailedBrowserSources()
{
	const QUrl endpoint(endpointEdit ? endpointEdit->text().trimmed() : QString());
	QString tokenError;
	const QString token = ResolveToken(&tokenError);
	if (!endpoint.isValid() || token.isEmpty() || !recoveryNetwork) {
		blog(LOG_WARNING, "[Tempest Studio Bridge] Browser recovery status check could not start");
		return;
	}

	BrowserRecoveryContext context;
	context.host = endpoint.host();
	context.port = endpoint.port(4765);
	obs_enum_sources(CollectStudioBrowserSource, &context);
	if (context.candidates.isEmpty()) {
		blog(LOG_INFO, "[Tempest Studio Bridge] Browser recovery found no active Studio sources");
		return;
	}

	for (const BrowserRecoveryContext::Candidate &candidate : context.candidates) {
		if (pendingBrowserRecoverySources.contains(candidate.uuid)) {
			continue;
		}
		QUrl statusUrl = endpoint;
		statusUrl.setScheme(endpoint.scheme().compare(QStringLiteral("wss"), Qt::CaseInsensitive) == 0
					    ? QStringLiteral("https")
					    : QStringLiteral("http"));
		statusUrl.setPath(candidate.statusPath);
		statusUrl.setQuery(QString());
		QNetworkRequest request(statusUrl);
		request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
		request.setRawHeader("Cache-Control", "no-store");
		pendingBrowserRecoverySources.insert(candidate.uuid);
		QNetworkReply *reply = recoveryNetwork->get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply, candidate]() {
			pendingBrowserRecoverySources.remove(candidate.uuid);
			const QByteArray response = reply->readAll();
			const QNetworkReply::NetworkError networkError = reply->error();
			reply->deleteLater();
			if (networkError != QNetworkReply::NoError) {
				blog(LOG_WARNING, "[Tempest Studio Bridge] Browser recovery status check failed for '%s': %s",
				     candidate.name.toUtf8().constData(), reply->errorString().toUtf8().constData());
				return;
			}
			const QJsonDocument document = QJsonDocument::fromJson(response);
			if (!document.isObject()) {
				return;
			}
			QJsonObject status = document.object();
			if (!candidate.statusSection.isEmpty()) {
				status = status.value(candidate.statusSection).toObject();
			}
			const int connectedClients = status.value(QStringLiteral("connectedClients")).toInt(-1);
			// A connected source can legitimately spend several seconds initializing a WebGL
			// renderer. Readiness belongs to Studio's engine diagnostics; recovering it here
			// would interrupt initialization and create a reload loop. Only revive a source
			// when Studio confirms that no Browser Source transport is connected.
			if (connectedClients != 0) {
				return;
			}

			OBSSourceAutoRelease source = obs_get_source_by_uuid(candidate.uuid.toUtf8().constData());
			if (!source || (!obs_source_active(source) && !obs_source_showing(source))) {
				return;
			}
			OBSDataAutoRelease settings = obs_source_get_settings(source);
			QUrl browserUrl(QString::fromUtf8(obs_data_get_string(settings, "url")));
			if (!browserUrl.isValid()) {
				return;
			}
			// Reassert the current-scene item as visible before refreshing it. OBS can leave a
			// transparent browser source loaded but JavaScript-suspended after its local server
			// disappears; a visibility edge wakes CEF without recreating the user's source.
			SetCurrentSceneSourceVisible(candidate.name, false);
			SetCurrentSceneSourceVisible(candidate.name, true);
			QUrlQuery query(browserUrl);
			query.removeAllQueryItems(QStringLiteral("tempest_recovery"));
			query.addQueryItem(QStringLiteral("tempest_recovery"),
					   QString::number(QDateTime::currentMSecsSinceEpoch()));
			browserUrl.setQuery(query);
			const QByteArray recoveredUrl = browserUrl.toString(QUrl::FullyEncoded).toUtf8();
			obs_data_set_string(settings, "url", recoveredUrl.constData());
			obs_source_update(source, settings);
			obs_properties_t *properties = obs_source_properties(source);
			obs_property_t *refresh = properties ? obs_properties_get(properties, "refreshnocache") : nullptr;
			if (refresh) {
				obs_property_button_clicked(refresh, source);
			}
			if (properties) {
				obs_properties_destroy(properties);
			}
			blog(LOG_INFO,
			     "[Tempest Studio Bridge] Reinitialized disconnected Studio browser source '%s' with a cache-busting URL",
			     candidate.name.toUtf8().constData());
		});
	}
}

void TempestStudioBridge::SetConnectionState(const QString &state, const QString &detail, bool error)
{
	if (connectionLabel) {
		connectionLabel->setText(state);
		connectionLabel->setAccessibleName(state);
		connectionLabel->setStyleSheet(error ? QStringLiteral("color: #ff6b8a;") : QString());
	}
	if (detailLabel) {
		detailLabel->setText(detail);
	}
}

QJsonObject TempestStudioBridge::CreateMessage(const QString &kind, const QString &topic, const QJsonObject &payload,
					       const QString &target, const QString &correlationId) const
{
	QJsonObject message{{QStringLiteral("protocolVersion"), QString::fromUtf8(ProtocolVersion)},
			    {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
			    {QStringLiteral("kind"), kind},
			    {QStringLiteral("source"), QString::fromUtf8(ApplicationId)},
			    {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
	if (!topic.isEmpty()) {
		message.insert(QStringLiteral("topic"), topic);
	}
	if (!target.isEmpty()) {
		message.insert(QStringLiteral("target"), target);
	}
	if (!correlationId.isEmpty()) {
		message.insert(QStringLiteral("correlationId"), correlationId);
	}
	if (!payload.isEmpty()) {
		message.insert(QStringLiteral("payload"), payload);
	}
	return message;
}

void TempestStudioBridge::SendDocument(const QJsonObject &document)
{
	if (!IsConnected()) {
		return;
	}
	const std::string data = QJsonDocument(document).toJson(QJsonDocument::Compact).toStdString();
	ClientState *state = client.get();
	std::lock_guard<std::mutex> lock(state->sendMutex);
	websocketpp::lib::error_code error;
	state->endpoint->send(state->connection, data, websocketpp::frame::opcode::text, error);
	if (error) {
		SetConnectionState(QStringLiteral("ONLINE // SEND DEGRADED"), QString::fromStdString(error.message()),
				   true);
	}
}

void TempestStudioBridge::SendHello()
{
	const QJsonArray capabilities{QStringLiteral("broadcast.reaction.trigger"),
				      QStringLiteral("broadcast.reaction.clear"),
				      QStringLiteral("broadcast.audio.play"),
				      QStringLiteral("broadcast.visual.show"),
				      QStringLiteral("broadcast.visual.hide"),
				      QStringLiteral("broadcast.interaction.show"),
				      QStringLiteral("broadcast.interaction.update"),
				      QStringLiteral("broadcast.interaction.clear"),
				      QStringLiteral("broadcast.status"),
				      QStringLiteral("broadcast.dual-format.configure"),
				      QStringLiteral("broadcast.dual-format.preview"),
				      QStringLiteral("broadcast.simulcast.configure"),
				      QStringLiteral("broadcast.simulcast.preflight"),
				      QStringLiteral("broadcast.simulcast.start"),
				      QStringLiteral("broadcast.simulcast.stop"),
				      QStringLiteral("broadcast.simulcast.retry-kick")};
	SendDocument(CreateMessage(QStringLiteral("hello"), {},
				   {{QStringLiteral("applicationId"), QString::fromUtf8(ApplicationId)},
				    {QStringLiteral("version"), QStringLiteral(TEMPEST_PRODUCT_VERSION)},
				    {QStringLiteral("protocolVersion"), QString::fromUtf8(ProtocolVersion)},
				    {QStringLiteral("interactionRenderContractVersion"),
				     QString::fromUtf8(InteractionRenderContractVersion)},
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
	QJsonObject payload = BuildStatusSnapshot();
	payload.insert(QStringLiteral("status"), IsConnected() ? QStringLiteral("online") : QStringLiteral("offline"));
	payload.insert(QStringLiteral("protocolVersion"), QString::fromUtf8(ProtocolVersion));
	if (!reason.isEmpty()) {
		payload.insert(QStringLiteral("reason"), reason);
	}
	PublishEvent(QStringLiteral("broadcast.status"), payload);
}

QJsonObject TempestStudioBridge::BuildDualFormatStatus() const
{
	QJsonObject status{{QStringLiteral("supported"), true},
			   {QStringLiteral("enabled"), false},
			   {QStringLiteral("enhancedBroadcastingEnabled"), false},
			   {QStringLiteral("additionalCanvasSelected"), false},
			   {QStringLiteral("sceneLinksReady"), false},
			   {QStringLiteral("linkedScenes"), 0},
			   {QStringLiteral("totalScenes"), 0},
			   {QStringLiteral("audioReady"), false},
			   {QStringLiteral("browserSourcesReady"), false},
			   {QStringLiteral("previewAvailable"), false}};
	if (!main || !main->Config()) {
		status.insert(QStringLiteral("lastError"),
			      QStringLiteral("The active Broadcast profile is not ready."));
		return status;
	}
	config_t *config = main->Config();
	const bool enhanced = config_get_bool(config, "Stream1", "EnableMultitrackVideo");
	const QString selectedUuid = QString::fromUtf8(config_get_string(config, "Stream1", "MultitrackExtraCanvas"));
	status.insert(QStringLiteral("enhancedBroadcastingEnabled"), enhanced);

	obs_frontend_canvas_list canvases{};
	obs_frontend_get_canvases(&canvases);
	for (size_t index = 0; index < canvases.canvases.num; ++index) {
		obs_canvas_t *canvas = canvases.canvases.array[index];
		const QString uuid = QString::fromUtf8(obs_canvas_get_uuid(canvas));
		if (selectedUuid.isEmpty() || uuid != selectedUuid) {
			continue;
		}
		obs_video_info info{};
		if (!obs_canvas_get_video_info(canvas, &info)) {
			continue;
		}
		const bool supportedPreset = (info.output_width == 1080 && info.output_height == 1920) ||
					     (info.output_width == 720 && info.output_height == 1280);
		status.insert(QStringLiteral("additionalCanvasSelected"), supportedPreset);
		status.insert(QStringLiteral("previewAvailable"), true);
		status.insert(QStringLiteral("audioReady"), obs_get_audio() != nullptr);
		status.insert(QStringLiteral("canvas"),
			      QJsonObject{{QStringLiteral("id"), uuid},
					  {QStringLiteral("name"), QString::fromUtf8(obs_canvas_get_name(canvas))},
					  {QStringLiteral("baseWidth"), static_cast<int>(info.base_width)},
					  {QStringLiteral("baseHeight"), static_cast<int>(info.base_height)},
					  {QStringLiteral("outputWidth"), static_cast<int>(info.output_width)},
					  {QStringLiteral("outputHeight"), static_cast<int>(info.output_height)},
					  {QStringLiteral("fpsNumerator"), static_cast<int>(info.fps_num)},
					  {QStringLiteral("fpsDenominator"), static_cast<int>(info.fps_den)}});
		int totalScenes = 0;
		const int linkedScenes = CountLinkedScenes(static_cast<int>(info.base_width),
							   static_cast<int>(info.base_height), totalScenes);
		status.insert(QStringLiteral("linkedScenes"), linkedScenes);
		status.insert(QStringLiteral("totalScenes"), totalScenes);
		status.insert(QStringLiteral("sceneLinksReady"), totalScenes > 0 && linkedScenes == totalScenes);
		break;
	}
	obs_frontend_canvas_list_free(&canvases);

	VerticalAlertInventory alertInventory;
	obs_enum_sources(CollectVerticalAlerts, &alertInventory);
	status.insert(QStringLiteral("browserSourcesReady"), alertInventory.twitch && alertInventory.interactions);
	const bool enabled = enhanced && status.value(QStringLiteral("additionalCanvasSelected")).toBool();
	status.insert(QStringLiteral("enabled"), enabled);
	return status;
}

QJsonObject TempestStudioBridge::BuildStatusSnapshot() const
{
	const QJsonObject dualFormat = BuildDualFormatStatus();
	const bool dualFormatReady = dualFormat.value(QStringLiteral("enabled")).toBool() &&
				     dualFormat.value(QStringLiteral("sceneLinksReady")).toBool() &&
				     dualFormat.value(QStringLiteral("audioReady")).toBool() &&
				     dualFormat.value(QStringLiteral("browserSourcesReady")).toBool();
	const QJsonObject interactionRender{
		{QStringLiteral("contractVersion"), QString::fromUtf8(InteractionRenderContractVersion)},
		{QStringLiteral("coordinateSpace"), QStringLiteral("normalized-top-left")},
		{QStringLiteral("orientations"), QJsonArray{QStringLiteral("horizontal"), QStringLiteral("vertical")}},
		{QStringLiteral("fitModes"),
		 QJsonArray{QStringLiteral("contain"), QStringLiteral("cover"), QStringLiteral("stretch")}},
		{QStringLiteral("lifecycle"),
		 QJsonArray{QStringLiteral("show"), QStringLiteral("update"), QStringLiteral("clear")}},
		{QStringLiteral("audioPolicy"), QStringLiteral("source-native-single-path")},
		{QStringLiteral("supported"), true},
		{QStringLiteral("activeInstances"), activeInteractions.size()},
		{QStringLiteral("maximumDurationMs"), 300000}};
	QJsonObject status{{QStringLiteral("ready"), IsConnected() && reactor && reactor->ExternalEventBridgeArmed()},
			   {QStringLiteral("version"), QStringLiteral(TEMPEST_PRODUCT_VERSION)},
			   {QStringLiteral("streaming"), obs_frontend_streaming_active()},
			   {QStringLiteral("recording"), obs_frontend_recording_active()},
			   {QStringLiteral("reactionArmed"), reactor && reactor->ExternalEventBridgeArmed()},
			   {QStringLiteral("activeLeases"), activeLeases.size()},
			   {QStringLiteral("canvasProfile"), BuildCanvasProfile()},
			   {QStringLiteral("sourceInventory"), BuildSourceInventory(main)},
			   {QStringLiteral("commandsHandled"), commandsHandled},
			   {QStringLiteral("interactionRender"), interactionRender},
			   {QStringLiteral("dualFormat"), dualFormat}};
	if (destinationCoordinator) {
		status.insert(QStringLiteral("simulcast"), destinationCoordinator->StatusSnapshot(dualFormatReady));
	}
	return status;
}

bool TempestStudioBridge::ConfigureDualFormat(const QJsonObject &arguments, QString &error)
{
	if (!main || !main->Config() || obs_frontend_streaming_active()) {
		error = QStringLiteral("Stop streaming before changing Dual Format configuration.");
		return false;
	}
	if (!arguments.value(QStringLiteral("enabled")).isBool()) {
		error = QStringLiteral("Dual Format configuration must include enabled=true or enabled=false.");
		return false;
	}
	config_t *config = main->Config();
	const bool enabled = arguments.value(QStringLiteral("enabled")).toBool();
	if (!enabled) {
		config_set_bool(config, "Stream1", "EnableMultitrackVideo", false);
		config_save_safe(config, "tmp", nullptr);
		return true;
	}
	const QJsonObject canvasRequest = arguments.value(QStringLiteral("additionalCanvas")).toObject();
	const int width = canvasRequest.value(QStringLiteral("outputWidth"))
				  .toInt(canvasRequest.value(QStringLiteral("baseWidth")).toInt());
	const int height = canvasRequest.value(QStringLiteral("outputHeight"))
				   .toInt(canvasRequest.value(QStringLiteral("baseHeight")).toInt());
	if (!((width == 1080 && height == 1920) || (width == 720 && height == 1280))) {
		error = QStringLiteral("Dual Format supports only 1080x1920 or 720x1280 canvases.");
		return false;
	}
	QString selectedUuid;
	obs_frontend_canvas_list canvases{};
	obs_frontend_get_canvases(&canvases);
	for (size_t index = 0; index < canvases.canvases.num; ++index) {
		obs_video_info info{};
		if (obs_canvas_get_video_info(canvases.canvases.array[index], &info) &&
		    static_cast<int>(info.output_width) == width && static_cast<int>(info.output_height) == height) {
			selectedUuid = QString::fromUtf8(obs_canvas_get_uuid(canvases.canvases.array[index]));
			break;
		}
	}
	obs_frontend_canvas_list_free(&canvases);
	if (selectedUuid.isEmpty()) {
		error = QStringLiteral("No existing Broadcast canvas matches the requested %1x%2 preset.")
				.arg(width)
				.arg(height);
		return false;
	}
	config_set_bool(config, "Stream1", "EnableMultitrackVideo", true);
	config_set_string(config, "Stream1", "MultitrackExtraCanvas", selectedUuid.toUtf8().constData());
	config_save_safe(config, "tmp", nullptr);
	return true;
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
	if (kind == QStringLiteral("command")) {
		HandleCommand(message);
	} else if (kind == QStringLiteral("welcome")) {
		SetConnectionState(QStringLiteral("ONLINE // STUDIO AUTHENTICATED"),
				   QStringLiteral("Bridge protocol 1.0 welcome received."));
	} else if (kind == QStringLiteral("error")) {
		SetConnectionState(
			QStringLiteral("ONLINE // BRIDGE ERROR"),
			message.value(QStringLiteral("payload")).toObject().value(QStringLiteral("message")).toString(),
			true);
	}
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
	while (processedCommandOrder.size() > MaximumRememberedCommands) {
		processedCommands.remove(processedCommandOrder.takeFirst());
	}
}

void TempestStudioBridge::HandleCommand(const QJsonObject &message)
{
	const QString target = message.value(QStringLiteral("target")).toString();
	if (!target.isEmpty() && target != QString::fromUtf8(ApplicationId)) {
		return;
	}
	const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
	const QString topic = message.value(QStringLiteral("topic")).toString();
	QString commandKey = CommandKey(payload);
	if (topic == QStringLiteral("broadcast.interaction.update")) {
		const QJsonValue revision =
			payload.value(QStringLiteral("arguments")).toObject().value(QStringLiteral("revision"));
		if (revision.isString() && !revision.toString().isEmpty()) {
			commandKey.append(QStringLiteral("/%1").arg(revision.toString()));
		} else if (revision.isDouble()) {
			commandKey.append(QStringLiteral("/%1").arg(revision.toVariant().toLongLong()));
		} else {
			commandKey = message.value(QStringLiteral("id")).toString();
		}
	}
	if (commandKey == QStringLiteral("//")) {
		commandKey = message.value(QStringLiteral("id")).toString();
	}
	if (processedCommands.contains(commandKey)) {
		SendResponse(message, true, QStringLiteral("duplicate"),
			     QStringLiteral("Command was already applied; no state was changed."));
		return;
	}
	RememberCommand(commandKey);
	++commandsHandled;
	if (topic == QStringLiteral("broadcast.reaction.trigger")) {
		HandleReactionTrigger(message, payload);
	} else if (topic == QStringLiteral("broadcast.reaction.clear")) {
		HandleReactionClear(message, payload);
	} else if (topic == QStringLiteral("broadcast.audio.play")) {
		HandleAudioPlay(message, payload);
	} else if (topic == QStringLiteral("broadcast.visual.show")) {
		HandleVisualShow(message, payload);
	} else if (topic == QStringLiteral("broadcast.visual.hide")) {
		HandleVisualHide(message, payload);
	} else if (topic == QStringLiteral("broadcast.interaction.show")) {
		HandleInteractionShow(message, payload);
	} else if (topic == QStringLiteral("broadcast.interaction.update")) {
		HandleInteractionUpdate(message, payload);
	} else if (topic == QStringLiteral("broadcast.interaction.clear")) {
		HandleInteractionClear(message, payload);
	} else if (topic == QStringLiteral("broadcast.dual-format.configure")) {
		HandleDualFormatConfigure(message, payload);
	} else if (topic == QStringLiteral("broadcast.dual-format.preview")) {
		HandleDualFormatPreview(message);
	} else if (topic == QStringLiteral("broadcast.simulcast.configure")) {
		HandleSimulcastConfigure(message, payload);
	} else if (topic == QStringLiteral("broadcast.simulcast.preflight")) {
		HandleSimulcastPreflight(message);
	} else if (topic == QStringLiteral("broadcast.simulcast.start")) {
		HandleSimulcastStart(message, payload);
	} else if (topic == QStringLiteral("broadcast.simulcast.stop")) {
		HandleSimulcastStop(message, payload);
	} else if (topic == QStringLiteral("broadcast.simulcast.retry-kick")) {
		HandleSimulcastRetryKick(message);
	} else if (topic == QStringLiteral("broadcast.status")) {
		HandleStatusRequest(message);
	} else {
		SendResponse(message, false, QStringLiteral("unsupported"),
			     QStringLiteral("Broadcast does not provide capability %1.").arg(topic));
	}
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
	if (auto oldTimer = activeLeases.take(leaseKey)) {
		oldTimer->deleteLater();
	}
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
	if (auto timer = activeLeases.take(leaseKey)) {
		timer->deleteLater();
	}
	if (const auto interaction = activeInteractions.take(leaseKey)) {
		RestoreInteraction(interaction);
		PublishEvent(QStringLiteral("broadcast.interaction.expired"),
			     {{QStringLiteral("instanceId"), interaction->instanceId},
			      {QStringLiteral("reason"), QStringLiteral("local-fallback-expiry")}});
		PublishHealth(QStringLiteral("interaction-expired"));
		if (commandLabel) {
			commandLabel->setText(QStringLiteral("COMMANDS // %1 // ACTIVE LEASES // %2")
						      .arg(commandsHandled)
						      .arg(activeLeases.size()));
		}
		return;
	}
	if (currentLeaseKey == leaseKey && reactor) {
		reactor->ClearExternalEvent();
		currentLeaseKey.clear();
	}
	const QString visualSource = activeVisualSources.take(leaseKey);
	if (!visualSource.isEmpty()) {
		SetCurrentSceneSourceVisible(visualSource, false);
	}
	PublishEvent(QStringLiteral("broadcast.reaction.expired"),
		     {{QStringLiteral("leaseKey"), leaseKey},
		      {QStringLiteral("reason"), QStringLiteral("local-fallback-expiry")}});
	PublishHealth(QStringLiteral("lease-expired"));
	if (commandLabel) {
		commandLabel->setText(QStringLiteral("COMMANDS // %1 // ACTIVE LEASES // %2")
					      .arg(commandsHandled)
					      .arg(activeLeases.size()));
	}
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
	if (!main || sourceName.isEmpty()) {
		return false;
	}
	OBSScene scene = main->GetCurrentScene();
	if (!scene) {
		return false;
	}
	const QByteArray name = sourceName.toUtf8();
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, name.constData());
	if (!item) {
		return false;
	}
	obs_sceneitem_set_visible(item, visible);
	if (visible && restartMedia) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (source && (obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0) {
			obs_source_media_restart(source);
		}
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
	if (auto oldTimer = activeLeases.take(leaseKey)) {
		oldTimer->deleteLater();
	}
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
	if (sourceName.isEmpty()) {
		sourceName = FirstString(arguments, {"broadcastVisualSource", "visualSource", "sourceName", "cue"});
	}
	const bool hidden = SetCurrentSceneSourceVisible(sourceName, false);
	SendResponse(message, true, hidden ? QStringLiteral("hidden") : QStringLiteral("already-hidden"),
		     hidden ? QStringLiteral("OBS visual source hidden: %1").arg(sourceName)
			    : QStringLiteral("No visible OBS source remained for this Sound Alert."));
}

void TempestStudioBridge::RestoreInteraction(const std::shared_ptr<InteractionState> &state)
{
	if (!state) {
		return;
	}
	for (const InteractionState::Target &target : state->targets) {
		obs_sceneitem_t *item = target.item;
		obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
		if (!item || !source || obs_source_removed(source)) {
			continue;
		}
		obs_sceneitem_defer_update_begin(item);
		obs_sceneitem_set_info2(item, &target.originalTransform);
		obs_sceneitem_set_visible(item, target.originalVisible);
		obs_sceneitem_defer_update_end(item);
	}
}

void TempestStudioBridge::HandleInteractionShow(const QJsonObject &message, const QJsonObject &payload)
{
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	const QJsonObject lease = payload.value(QStringLiteral("lease")).toObject();
	const QString instanceId = InteractionKey(message, payload, arguments);
	const QString storageKey = InteractionStorageKey(instanceId);
	const QJsonObject renderTargets = InteractionRenderTargets(arguments);
	if (instanceId.isEmpty() || renderTargets.isEmpty()) {
		SendResponse(message, false, QStringLiteral("rejected"),
			     QStringLiteral("Interaction show requires instanceId and at least one render target."));
		return;
	}

	for (auto iterator = renderTargets.constBegin(); iterator != renderTargets.constEnd(); ++iterator) {
		if (iterator.key() != QStringLiteral("horizontal") && iterator.key() != QStringLiteral("vertical")) {
			SendResponse(message, false, QStringLiteral("rejected"),
				     QStringLiteral("Unknown interaction render orientation: %1").arg(iterator.key()));
			return;
		}
		if (!iterator.value().isObject()) {
			SendResponse(message, false, QStringLiteral("rejected"),
				     QStringLiteral("Each interaction render target must be an object."));
			return;
		}
	}

	if (auto timer = activeLeases.take(storageKey)) {
		timer->stop();
		timer->deleteLater();
	}
	if (const auto replaced = activeInteractions.take(storageKey)) {
		RestoreInteraction(replaced);
	}

	auto state = std::make_shared<InteractionState>();
	state->instanceId = instanceId;
	QJsonArray appliedTargets;
	QSet<quintptr> restartSources;
	QString error;
	for (const QString &orientation : {QStringLiteral("horizontal"), QStringLiteral("vertical")}) {
		if (!renderTargets.value(orientation).isObject()) {
			continue;
		}
		const QJsonObject targetObject = renderTargets.value(orientation).toObject();
		const QString sourceName = FirstString(targetObject, {"sourceName"});
		if (sourceName.isEmpty()) {
			error = QStringLiteral("The %1 render target has no sourceName.").arg(orientation);
			break;
		}

		QString sceneName;
		QString canvasId;
		int canvasWidth = 0;
		int canvasHeight = 0;
		OBSScene scene = ResolveInteractionScene(main, orientation, sceneName, canvasId, canvasWidth,
							 canvasHeight, error);
		if (!scene || canvasWidth <= 0 || canvasHeight <= 0) {
			if (error.isEmpty()) {
				error = QStringLiteral("The %1 render canvas has invalid dimensions.").arg(orientation);
			}
			break;
		}
		obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, sourceName.toUtf8().constData());
		if (!item) {
			error = QStringLiteral("OBS source %1 was not found in the %2 render scene.")
					.arg(sourceName, orientation);
			break;
		}

		InteractionState::Target target;
		target.orientation = orientation;
		target.sourceName = sourceName;
		target.sceneName = sceneName;
		target.canvasId = canvasId;
		target.canvasWidth = canvasWidth;
		target.canvasHeight = canvasHeight;
		target.scene = scene;
		target.item = item;
		obs_sceneitem_get_info2(item, &target.originalTransform);
		target.originalVisible = obs_sceneitem_visible(item);

		obs_transform_info transform{};
		bool hasPlacement = false;
		if (!BuildInteractionTransform(item, canvasWidth, canvasHeight, targetObject, transform, hasPlacement,
					       error)) {
			break;
		}
		if (hasPlacement && obs_sceneitem_get_scene(item) != scene) {
			error = QStringLiteral("Normalized placement requires %1 to be a top-level scene source.")
					.arg(sourceName);
			break;
		}

		state->targets.append(target);
		obs_sceneitem_defer_update_begin(item);
		if (hasPlacement) {
			obs_sceneitem_set_info2(item, &transform);
		}
		obs_sceneitem_set_visible(item, true);
		obs_sceneitem_defer_update_end(item);

		obs_source_t *source = obs_sceneitem_get_source(item);
		if (targetObject.value(QStringLiteral("restartMedia")).toBool(false) && source &&
		    (obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0) {
			restartSources.insert(reinterpret_cast<quintptr>(source));
		}
		appliedTargets.append(QJsonObject{{QStringLiteral("orientation"), orientation},
						  {QStringLiteral("sourceName"), sourceName},
						  {QStringLiteral("sceneName"), sceneName},
						  {QStringLiteral("canvasId"), canvasId},
						  {QStringLiteral("canvasWidth"), canvasWidth},
						  {QStringLiteral("canvasHeight"), canvasHeight}});
	}

	if (!error.isEmpty() || state->targets.isEmpty()) {
		RestoreInteraction(state);
		SendResponse(message, false, QStringLiteral("rejected"),
			     error.isEmpty() ? QStringLiteral("No interaction render target was applied.") : error);
		PublishEvent(QStringLiteral("broadcast.failure"),
			     {{QStringLiteral("capability"), QStringLiteral("broadcast.interaction.show")},
			      {QStringLiteral("instanceId"), instanceId},
			      {QStringLiteral("reason"), QStringLiteral("render-target-rejected")},
			      {QStringLiteral("detail"), error}});
		return;
	}

	for (quintptr pointer : restartSources) {
		obs_source_media_restart(reinterpret_cast<obs_source_t *>(pointer));
	}
	const int requestedDuration = arguments.value(QStringLiteral("durationMs"))
					      .toInt(lease.value(QStringLiteral("durationMs")).toInt(2200));
	const int durationMs = std::clamp(requestedDuration, 250, 300000);
	auto *timer = new QTimer(this);
	timer->setSingleShot(true);
	timer->setInterval(durationMs);
	connect(timer, &QTimer::timeout, this, [this, storageKey]() { ExpireLease(storageKey); });
	activeInteractions.insert(storageKey, state);
	activeLeases.insert(storageKey, timer);
	timer->start();

	const QJsonObject result{{QStringLiteral("interactionRenderContractVersion"),
				  QString::fromUtf8(InteractionRenderContractVersion)},
				 {QStringLiteral("instanceId"), instanceId},
				 {QStringLiteral("durationMs"), durationMs},
				 {QStringLiteral("targets"), appliedTargets},
				 {QStringLiteral("audioPolicy"), QStringLiteral("source-native-single-path")}};
	SendResponse(message, true, QStringLiteral("shown"), QStringLiteral("Interaction render targets are visible."),
		     result);
	PublishEvent(QStringLiteral("broadcast.interaction.shown"), result);
}

void TempestStudioBridge::HandleInteractionUpdate(const QJsonObject &message, const QJsonObject &payload)
{
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	const QString instanceId = InteractionKey(message, payload, arguments);
	const QString storageKey = InteractionStorageKey(instanceId);
	const auto state = activeInteractions.value(storageKey);
	if (!state) {
		SendResponse(message, false, QStringLiteral("not-found"),
			     QStringLiteral("No active interaction render instance matches %1.").arg(instanceId));
		return;
	}

	const QJsonObject renderTargets = InteractionRenderTargets(arguments);
	struct PendingTransform {
		obs_sceneitem_t *item = nullptr;
		obs_transform_info transform{};
	};
	QList<PendingTransform> pending;
	QJsonArray updatedTargets;
	QString error;
	for (auto iterator = renderTargets.constBegin(); iterator != renderTargets.constEnd(); ++iterator) {
		const QString orientation = iterator.key();
		if ((orientation != QStringLiteral("horizontal") && orientation != QStringLiteral("vertical")) ||
		    !iterator.value().isObject()) {
			error = QStringLiteral("Interaction update contains an invalid render target.");
			break;
		}
		auto targetIterator = std::find_if(state->targets.begin(), state->targets.end(),
						   [&orientation](const InteractionState::Target &target) {
							   return target.orientation == orientation;
						   });
		if (targetIterator == state->targets.end()) {
			error = QStringLiteral("The active interaction has no %1 render target.").arg(orientation);
			break;
		}
		const QJsonObject targetObject = iterator.value().toObject();
		const QString requestedSource = FirstString(targetObject, {"sourceName"});
		if (!requestedSource.isEmpty() && requestedSource != targetIterator->sourceName) {
			error = QStringLiteral(
				"Interaction update cannot replace an active OBS source; clear and show instead.");
			break;
		}
		if (!targetObject.value(QStringLiteral("placement")).isObject()) {
			continue;
		}
		if (obs_sceneitem_get_scene(targetIterator->item) != targetIterator->scene) {
			error = QStringLiteral("Normalized placement requires %1 to remain a top-level scene source.")
					.arg(targetIterator->sourceName);
			break;
		}
		obs_transform_info transform{};
		bool hasPlacement = false;
		if (!BuildInteractionTransform(targetIterator->item, targetIterator->canvasWidth,
					       targetIterator->canvasHeight, targetObject, transform, hasPlacement,
					       error)) {
			break;
		}
		pending.append({targetIterator->item, transform});
		updatedTargets.append(orientation);
	}

	if (!error.isEmpty()) {
		SendResponse(message, false, QStringLiteral("rejected"), error);
		return;
	}
	int durationMs = -1;
	if (arguments.contains(QStringLiteral("durationMs"))) {
		if (!arguments.value(QStringLiteral("durationMs")).isDouble()) {
			SendResponse(message, false, QStringLiteral("rejected"),
				     QStringLiteral("Interaction update durationMs must be numeric."));
			return;
		}
		durationMs = std::clamp(arguments.value(QStringLiteral("durationMs")).toInt(), 250, 300000);
	}
	for (const PendingTransform &update : pending) {
		obs_sceneitem_defer_update_begin(update.item);
		obs_sceneitem_set_info2(update.item, &update.transform);
		obs_sceneitem_set_visible(update.item, true);
		obs_sceneitem_defer_update_end(update.item);
	}

	if (durationMs >= 0) {
		if (auto timer = activeLeases.value(storageKey)) {
			timer->start(durationMs);
		}
	}
	QJsonObject result{{QStringLiteral("interactionRenderContractVersion"),
			    QString::fromUtf8(InteractionRenderContractVersion)},
			   {QStringLiteral("instanceId"), instanceId},
			   {QStringLiteral("updatedOrientations"), updatedTargets},
			   {QStringLiteral("durationMs"), durationMs}};
	if (arguments.contains(QStringLiteral("revision"))) {
		result.insert(QStringLiteral("revision"), arguments.value(QStringLiteral("revision")));
	}
	SendResponse(message, true, QStringLiteral("updated"), QStringLiteral("Interaction render state was updated."),
		     result);
	PublishEvent(QStringLiteral("broadcast.interaction.updated"), result);
}

void TempestStudioBridge::HandleInteractionClear(const QJsonObject &message, const QJsonObject &payload)
{
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	const QString instanceId = InteractionKey(message, payload, arguments);
	const QString storageKey = InteractionStorageKey(instanceId);
	if (auto timer = activeLeases.take(storageKey)) {
		timer->stop();
		timer->deleteLater();
	}
	const auto state = activeInteractions.take(storageKey);
	if (!state) {
		SendResponse(message, true, QStringLiteral("already-cleared"),
			     QStringLiteral("No active interaction render state remained."),
			     {{QStringLiteral("instanceId"), instanceId}});
		return;
	}
	RestoreInteraction(state);
	const QJsonObject result{{QStringLiteral("interactionRenderContractVersion"),
				  QString::fromUtf8(InteractionRenderContractVersion)},
				 {QStringLiteral("instanceId"), instanceId}};
	SendResponse(message, true, QStringLiteral("cleared"),
		     QStringLiteral("Interaction sources were restored to their previous compositor state."), result);
	PublishEvent(QStringLiteral("broadcast.interaction.cleared"), result);
}

void TempestStudioBridge::HandleStatusRequest(const QJsonObject &message)
{
	const QJsonObject status = BuildStatusSnapshot();
	SendResponse(message, true, QStringLiteral("ready"), QStringLiteral("Broadcast status snapshot."), status);
	PublishHealth(QStringLiteral("requested"));
}

void TempestStudioBridge::HandleDualFormatConfigure(const QJsonObject &message, const QJsonObject &payload)
{
	QString error;
	const bool configured = ConfigureDualFormat(payload.value(QStringLiteral("arguments")).toObject(), error);
	SendResponse(message, configured, configured ? QStringLiteral("configured") : QStringLiteral("rejected"),
		     configured ? QStringLiteral("Dual Format profile settings were updated.") : error,
		     {{QStringLiteral("dualFormat"), BuildDualFormatStatus()}});
	PublishHealth(configured ? QStringLiteral("dual-format-configured")
				 : QStringLiteral("dual-format-configuration-rejected"));
}

void TempestStudioBridge::HandleDualFormatPreview(const QJsonObject &message)
{
	const bool opened = proc_handler_call(obs_get_proc_handler(), "tempest_vertical_open_workspace", nullptr);
	SendResponse(message, opened, opened ? QStringLiteral("opened") : QStringLiteral("unavailable"),
		     opened ? QStringLiteral("Vertical canvas workspace opened.")
			    : QStringLiteral("The Tempest vertical canvas workspace is unavailable."));
}

void TempestStudioBridge::HandleSimulcastConfigure(const QJsonObject &message, const QJsonObject &payload)
{
	if (!destinationCoordinator) {
		SendResponse(message, false, QStringLiteral("unavailable"),
			     QStringLiteral("The Broadcast destination controller is unavailable."));
		return;
	}
	QString error;
	const bool configured = destinationCoordinator->ConfigureFromStudio(
		payload.value(QStringLiteral("arguments")).toObject(), error);
	const QJsonObject dual = BuildDualFormatStatus();
	const bool dualReady = dual.value(QStringLiteral("enabled")).toBool() &&
			       dual.value(QStringLiteral("sceneLinksReady")).toBool() &&
			       dual.value(QStringLiteral("audioReady")).toBool() &&
			       dual.value(QStringLiteral("browserSourcesReady")).toBool();
	SendResponse(message, configured, configured ? QStringLiteral("configured") : QStringLiteral("rejected"),
		     configured ? QStringLiteral("Simulcast profile settings were stored in Broadcast.") : error,
		     {{QStringLiteral("simulcast"), destinationCoordinator->StatusSnapshot(dualReady)}});
	PublishHealth(configured ? QStringLiteral("simulcast-configured")
				 : QStringLiteral("simulcast-configuration-rejected"));
}

void TempestStudioBridge::HandleSimulcastPreflight(const QJsonObject &message)
{
	if (!destinationCoordinator) {
		SendResponse(message, false, QStringLiteral("unavailable"),
			     QStringLiteral("The Broadcast destination controller is unavailable."));
		return;
	}
	const QJsonObject dual = BuildDualFormatStatus();
	const bool dualReady = dual.value(QStringLiteral("enabled")).toBool() &&
			       dual.value(QStringLiteral("sceneLinksReady")).toBool() &&
			       dual.value(QStringLiteral("audioReady")).toBool() &&
			       dual.value(QStringLiteral("browserSourcesReady")).toBool();
	QString error;
	const bool passed = destinationCoordinator->RunPreflight(dualReady, error);
	SendResponse(message, passed, passed ? QStringLiteral("passed") : QStringLiteral("failed"),
		     passed ? QStringLiteral("Local production preflight passed.") : error,
		     {{QStringLiteral("simulcast"), destinationCoordinator->StatusSnapshot(dualReady)}});
	PublishHealth(passed ? QStringLiteral("simulcast-preflight-passed")
			     : QStringLiteral("simulcast-preflight-failed"));
}

void TempestStudioBridge::HandleSimulcastStart(const QJsonObject &message, const QJsonObject &payload)
{
	if (!destinationCoordinator) {
		SendResponse(message, false, QStringLiteral("unavailable"),
			     QStringLiteral("The Broadcast destination controller is unavailable."));
		return;
	}
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	if (arguments.value(QStringLiteral("operatorChecklistAccepted")).toBool() != true) {
		SendResponse(message, false, QStringLiteral("rejected"),
			     QStringLiteral("The operator rehearsal checklist must be accepted before going live."));
		return;
	}
	QString error;
	const bool started =
		destinationCoordinator->StartFromStudio(arguments.value(QStringLiteral("recording")).toBool(), error);
	SendResponse(message, started, started ? QStringLiteral("starting") : QStringLiteral("rejected"),
		     started ? QStringLiteral("Coordinated Twitch and Kick startup requested.") : error);
	PublishHealth(started ? QStringLiteral("simulcast-starting") : QStringLiteral("simulcast-start-rejected"));
}

void TempestStudioBridge::HandleSimulcastStop(const QJsonObject &message, const QJsonObject &payload)
{
	if (!destinationCoordinator) {
		SendResponse(message, false, QStringLiteral("unavailable"),
			     QStringLiteral("The Broadcast destination controller is unavailable."));
		return;
	}
	const QJsonObject arguments = payload.value(QStringLiteral("arguments")).toObject();
	QString error;
	const bool stopped = destinationCoordinator->StopFromStudio(
		arguments.value(QStringLiteral("scope")).toString(QStringLiteral("all")),
		arguments.value(QStringLiteral("force")).toBool(), error);
	SendResponse(message, stopped, stopped ? QStringLiteral("stopping") : QStringLiteral("rejected"),
		     stopped ? QStringLiteral("Requested Broadcast outputs are stopping.") : error);
	PublishHealth(stopped ? QStringLiteral("simulcast-stopping") : QStringLiteral("simulcast-stop-rejected"));
}

void TempestStudioBridge::HandleSimulcastRetryKick(const QJsonObject &message)
{
	if (!destinationCoordinator) {
		SendResponse(message, false, QStringLiteral("unavailable"),
			     QStringLiteral("The Broadcast destination controller is unavailable."));
		return;
	}
	QString error;
	const bool retrying = destinationCoordinator->RetryKick(error);
	SendResponse(message, retrying, retrying ? QStringLiteral("starting") : QStringLiteral("rejected"),
		     retrying ? QStringLiteral("Kick recovery attempt started; Twitch remains live.") : error);
	PublishHealth(retrying ? QStringLiteral("kick-retry-starting") : QStringLiteral("kick-retry-rejected"));
}

void TempestStudioBridge::SendResponse(const QJsonObject &command, bool success, const QString &status,
				       const QString &detail, const QJsonObject &extra)
{
	QJsonObject payload{{QStringLiteral("commandId"), command.value(QStringLiteral("id"))},
			    {QStringLiteral("success"), success},
			    {QStringLiteral("status"), status},
			    {QStringLiteral("detail"), detail}};
	for (auto it = extra.begin(); it != extra.end(); ++it) {
		payload.insert(it.key(), it.value());
	}
	const QString correlationId = command.value(QStringLiteral("correlationId")).toString();
	SendDocument(CreateMessage(QStringLiteral("response"), command.value(QStringLiteral("topic")).toString(),
				   payload, command.value(QStringLiteral("source")).toString(), correlationId));
}
