#include "TempestCommandMatrix.hpp"

#include "TempestControlDeck.hpp"

#include <OBSApp.hpp>
#ifdef TEMPEST_WEBSOCKET_AVAILABLE
#include <obs-websocket-api.h>
#endif
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr char ConfigSection[] = "TempestCommandMatrix";

struct OverlayVisibilityContext {
	QString selectedSourceName;
};

QString ConfigKey(const QString &protocolId)
{
	return QStringLiteral("Protocol_%1_SceneUuid").arg(protocolId);
}

QString ActionConfigKey(const QString &protocolId, const char *field)
{
	return QStringLiteral("Action_%1_%2").arg(protocolId, QString::fromUtf8(field));
}

void SetComboData(QComboBox *combo, const QString &value, const QString &unavailableLabel = {})
{
	int index = combo->findData(value);
	if (index < 0 && !value.isEmpty()) {
		combo->addItem(unavailableLabel.isEmpty() ? QStringLiteral("Unavailable source") : unavailableLabel, value);
		index = combo->count() - 1;
	}
	combo->setCurrentIndex(std::max(index, 0));
}

bool IsProtocolId(const QString &protocolId)
{
	return protocolId == QStringLiteral("starting") || protocolId == QStringLiteral("live") ||
	       protocolId == QStringLiteral("brb") || protocolId == QStringLiteral("ending");
}

void SetRouterResponse(obs_data_t *response, bool accepted, const char *message)
{
	obs_data_set_bool(response, "accepted", accepted);
	obs_data_set_string(response, "message", message);
}
} // namespace

TempestCommandMatrix::TempestCommandMatrix(OBSBasic *main, TempestControlDeck *controlDeck, QWidget *parent)
	: OBSDock(parent), main(main), controlDeck(controlDeck)
{
	setObjectName(QStringLiteral("tempestCommandMatrix"));
	setWindowTitle(QStringLiteral("Transmission Command Matrix"));
	setMinimumWidth(270);
	BuildInterface();

	config_t *config = App()->GetUserConfig();
	for (const ProtocolWidgets &protocol : protocols) {
		const QByteArray key = ConfigKey(protocol.id).toUtf8();
		configuredSceneUuids.insert(protocol.id,
					    QString::fromUtf8(config_get_string(config, ConfigSection, key.constData())));
	}
	isolateOverlay->setChecked(!config_has_user_value(config, ConfigSection, "IsolateOverlay") ||
				   config_get_bool(config, ConfigSection, "IsolateOverlay"));
	startCountdown->setChecked(config_get_bool(config, ConfigSection, "StartCountdown"));
	LoadActionConfigs();

	connect(isolateOverlay, &QCheckBox::toggled, this, &TempestCommandMatrix::SaveAssignments);
	connect(startCountdown, &QCheckBox::toggled, this, &TempestCommandMatrix::SaveAssignments);

	refreshTimer = new QTimer(this);
	refreshTimer->setInterval(500);
	connect(refreshTimer, &QTimer::timeout, this, &TempestCommandMatrix::RefreshScenes);
	refreshTimer->start();
	RefreshScenes();
}

TempestCommandMatrix::~TempestCommandMatrix()
{
	UnregisterHotkeys();
#ifdef TEMPEST_WEBSOCKET_AVAILABLE
	if (webSocketVendor) {
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"RunProtocol");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"RouteScene");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"SetOverlayState");
	}
#endif
}

void TempestCommandMatrix::RegisterHotkeys()
{
	UnregisterHotkeys();
	for (const ProtocolWidgets &protocol : protocols) {
		const QString name = QStringLiteral("TempestMainframe.Run.%1").arg(protocol.id);
		const QString description = QStringLiteral("Tempest Mainframe: Run %1 Protocol").arg(protocol.label);
		const QByteArray nameUtf8 = name.toUtf8();
		const QByteArray descriptionUtf8 = description.toUtf8();
		const obs_hotkey_id id = obs_hotkey_register_frontend(nameUtf8.constData(), descriptionUtf8.constData(),
								      ProtocolHotkey, this);
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		protocolHotkeys.insert(id, protocol.id);
		LoadHotkey(id, nameUtf8);
	}
	SetRouterState();
}

void TempestCommandMatrix::UnregisterHotkeys()
{
	for (auto it = protocolHotkeys.cbegin(); it != protocolHotkeys.cend(); ++it)
		obs_hotkey_unregister(it.key());
	protocolHotkeys.clear();
	SetRouterState();
}

void TempestCommandMatrix::LoadHotkey(obs_hotkey_id id, const QByteArray &name)
{
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

void TempestCommandMatrix::ProtocolHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	const QString protocolId = matrix->protocolHotkeys.value(id);
	if (protocolId.isEmpty())
		return;
	QPointer<TempestCommandMatrix> guarded(matrix);
	QMetaObject::invokeMethod(matrix, [guarded, protocolId]() {
		if (guarded)
			guarded->ExecuteProtocol(protocolId);
	}, Qt::QueuedConnection);
}

void TempestCommandMatrix::RegisterExternalControls()
{
#ifdef TEMPEST_WEBSOCKET_AVAILABLE
	if (webSocketVendor)
		return;
	webSocketVendor = obs_websocket_register_vendor("tempest-mainframe");
	if (!webSocketVendor) {
		SetRouterState();
		return;
	}

	const auto vendor = static_cast<obs_websocket_vendor>(webSocketVendor);
	const bool protocolReady =
		obs_websocket_vendor_register_request(vendor, "RunProtocol", WebSocketRunProtocol, this);
	const bool sceneReady = obs_websocket_vendor_register_request(vendor, "RouteScene", WebSocketRouteScene, this);
	const bool overlayReady =
		obs_websocket_vendor_register_request(vendor, "SetOverlayState", WebSocketSetOverlayState, this);
	webSocketReady = protocolReady && sceneReady && overlayReady;
#endif
	SetRouterState();
}

void TempestCommandMatrix::WebSocketRunProtocol(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	const QString protocolId = QString::fromUtf8(obs_data_get_string(request, "protocol")).toLower();
	if (!IsProtocolId(protocolId)) {
		SetRouterResponse(response, false, "protocol must be starting, live, brb, or ending");
		return;
	}

	QPointer<TempestCommandMatrix> guarded(matrix);
	QMetaObject::invokeMethod(matrix, [guarded, protocolId]() {
		if (guarded)
			guarded->ExecuteProtocol(protocolId);
	}, Qt::QueuedConnection);
	SetRouterResponse(response, true, "protocol command queued");
}

void TempestCommandMatrix::WebSocketRouteScene(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	const QString uuid = QString::fromUtf8(obs_data_get_string(request, "sceneUuid"));
	const QString name = QString::fromUtf8(obs_data_get_string(request, "sceneName"));
	if (uuid.isEmpty() && name.isEmpty()) {
		SetRouterResponse(response, false, "sceneUuid or sceneName is required");
		return;
	}

	QPointer<TempestCommandMatrix> guarded(matrix);
	QMetaObject::invokeMethod(matrix, [guarded, uuid, name]() {
		if (guarded)
			guarded->RouteExternalScene(uuid, name);
	}, Qt::QueuedConnection);
	SetRouterResponse(response, true, "scene route queued");
}

void TempestCommandMatrix::WebSocketSetOverlayState(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	const QString mode = QString::fromUtf8(obs_data_get_string(request, "mode")).toLower();
	if (!IsProtocolId(mode)) {
		SetRouterResponse(response, false, "mode must be starting, live, brb, or ending");
		return;
	}
	const QString transmission = QString::fromUtf8(obs_data_get_string(request, "transmission"));
	const QString status = QString::fromUtf8(obs_data_get_string(request, "status"));
	const QString messages = QString::fromUtf8(obs_data_get_string(request, "messages"));
	const bool startCountdown = obs_data_get_bool(request, "startCountdown");

	QPointer<TempestCommandMatrix> guarded(matrix);
	QMetaObject::invokeMethod(matrix, [guarded, mode, transmission, status, messages, startCountdown]() {
		if (!guarded || !guarded->controlDeck)
			return;
		guarded->controlDeck->UpdateOverlayText(mode, transmission, status, messages);
		if (startCountdown)
			guarded->controlDeck->ActivateMode(mode, true);
		OBSDataAutoRelease eventData = obs_data_create();
		obs_data_set_string(eventData, "mode", mode.toUtf8().constData());
		guarded->EmitRouterEvent("OverlayStateUpdated", eventData);
	}, Qt::QueuedConnection);
	SetRouterResponse(response, true, "overlay state queued");
}

void TempestCommandMatrix::SetRouterState()
{
	if (!routerLabel)
		return;
	const bool hotkeysReady = protocolHotkeys.size() == protocols.size();
	if (hotkeysReady && webSocketReady)
		routerLabel->setText(QStringLiteral("CONTROL ROUTER // HOTKEYS + OBS WEBSOCKET READY"));
	else if (hotkeysReady)
		routerLabel->setText(QStringLiteral("CONTROL ROUTER // HOTKEYS READY"));
	else
		routerLabel->setText(QStringLiteral("CONTROL ROUTER // INITIALIZING"));
}

void TempestCommandMatrix::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestMatrixRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestMatrixRoot { background: #07131e; }
		QLabel#matrixTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#matrixSubtitle { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
		QLabel#matrixCurrent { color: #bdf6ff; padding: 8px; border: 1px solid #0c7ccb; background: #06101a; font-weight: 700; }
		QLabel#matrixSection { color: #748fa4; font-size: 10px; font-weight: 700; letter-spacing: 1px; }
		QLabel#matrixStatus { color: #45d9ff; font-size: 10px; padding-top: 4px; }
		QPushButton { min-height: 36px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:checked { border: 2px solid #45d9ff; background: #073c5f; color: white; }
		QPushButton[protocol="true"] { min-height: 48px; font-size: 11px; letter-spacing: 1px; }
		QComboBox { min-height: 28px; background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; }
		QCheckBox { color: #748fa4; spacing: 7px; }
		QScrollArea { border: none; background: transparent; }
	)"));

	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *title = new QLabel(QStringLiteral("TRANSMISSION MATRIX"), root);
	title->setObjectName(QStringLiteral("matrixTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Scene protocols and direct routing"), root);
	subtitle->setObjectName(QStringLiteral("matrixSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	currentSceneLabel = new QLabel(QStringLiteral("ACTIVE // INITIALIZING"), root);
	currentSceneLabel->setObjectName(QStringLiteral("matrixCurrent"));
	layout->addWidget(currentSceneLabel);
	routerLabel = new QLabel(QStringLiteral("CONTROL ROUTER // INITIALIZING"), root);
	routerLabel->setObjectName(QStringLiteral("matrixSubtitle"));
	routerLabel->setToolTip(QStringLiteral(
		"Assign Stream Deck keyboard buttons in OBS Settings > Hotkeys. Advanced clients can use the "
		"tempest-mainframe OBS WebSocket vendor."));
	layout->addWidget(routerLabel);

	auto *protocolLabel = new QLabel(QStringLiteral("TRANSMISSION PROTOCOLS"), root);
	protocolLabel->setObjectName(QStringLiteral("matrixSection"));
	layout->addWidget(protocolLabel);

	struct ProtocolDefinition {
		const char *id;
		const char *label;
		const char *sourceName;
	};
	constexpr ProtocolDefinition definitions[] = {
		{"starting", "STARTING", "Tempest // Starting Soon"},
		{"live", "LIVE", "Tempest // Live HUD"},
		{"brb", "BRB", "Tempest // BRB"},
		{"ending", "ENDING", "Tempest // Stream Ending"},
	};

	auto *protocolGrid = new QGridLayout();
	protocolGrid->setHorizontalSpacing(6);
	protocolGrid->setVerticalSpacing(6);
	for (int index = 0; index < 4; ++index) {
		const ProtocolDefinition &definition = definitions[index];
		ProtocolWidgets protocol;
		protocol.id = QString::fromUtf8(definition.id);
		protocol.label = QString::fromUtf8(definition.label);
		protocol.sourceName = QString::fromUtf8(definition.sourceName);
		protocol.button = new QPushButton(protocol.label, root);
		protocol.button->setProperty("protocol", true);
		connect(protocol.button, &QPushButton::clicked, this,
			[this, id = protocol.id]() { ExecuteProtocol(id); });
		protocolGrid->addWidget(protocol.button, index / 2, index % 2);
		protocols.push_back(protocol);
	}
	layout->addLayout(protocolGrid);

	auto *assignmentLabel = new QLabel(QStringLiteral("PROTOCOL SCENE ASSIGNMENTS"), root);
	assignmentLabel->setObjectName(QStringLiteral("matrixSection"));
	layout->addWidget(assignmentLabel);

	auto *assignmentGrid = new QGridLayout();
	assignmentGrid->setColumnStretch(1, 1);
	assignmentGrid->setHorizontalSpacing(7);
	assignmentGrid->setVerticalSpacing(5);
	for (int index = 0; index < protocols.size(); ++index) {
		ProtocolWidgets &protocol = protocols[index];
		auto *label = new QLabel(protocol.label, root);
		label->setObjectName(QStringLiteral("matrixSubtitle"));
		protocol.sceneCombo = new QComboBox(root);
		protocol.sceneCombo->setAccessibleName(QStringLiteral("%1 scene assignment").arg(protocol.label));
		connect(protocol.sceneCombo, &QComboBox::currentIndexChanged, this,
			&TempestCommandMatrix::SaveAssignments);
		assignmentGrid->addWidget(label, index, 0);
		assignmentGrid->addWidget(protocol.sceneCombo, index, 1);
	}
	layout->addLayout(assignmentGrid);

	isolateOverlay = new QCheckBox(QStringLiteral("Isolate matching Tempest overlay"), root);
	startCountdown = new QCheckBox(QStringLiteral("Start countdown with STARTING"), root);
	layout->addWidget(isolateOverlay);
	layout->addWidget(startCountdown);

	auto *configureActions = new QPushButton(QStringLiteral("CONFIGURE PROTOCOL ACTIONS"), root);
	configureActions->setAccessibleName(QStringLiteral("Configure protocol actions"));
	connect(configureActions, &QPushButton::clicked, this, &TempestCommandMatrix::OpenActionEditor);
	layout->addWidget(configureActions);

	auto *sceneLabel = new QLabel(QStringLiteral("DIRECT SCENE ROUTING"), root);
	sceneLabel->setObjectName(QStringLiteral("matrixSection"));
	layout->addWidget(sceneLabel);

	auto *scroll = new QScrollArea(root);
	scroll->setWidgetResizable(true);
	auto *sceneContainer = new QWidget(scroll);
	sceneContainer->setStyleSheet(QStringLiteral("background: transparent;"));
	sceneGrid = new QGridLayout(sceneContainer);
	sceneGrid->setContentsMargins(0, 0, 0, 0);
	sceneGrid->setHorizontalSpacing(6);
	sceneGrid->setVerticalSpacing(6);
	sceneGrid->setColumnStretch(0, 1);
	sceneGrid->setColumnStretch(1, 1);
	scroll->setWidget(sceneContainer);
	layout->addWidget(scroll, 1);

	statusLabel = new QLabel(QStringLiteral("MATRIX SYNCHRONIZING"), root);
	statusLabel->setObjectName(QStringLiteral("matrixStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);

	setWidget(root);
}

bool TempestCommandMatrix::EnumScene(void *data, obs_source_t *source)
{
	auto *scenes = static_cast<QVector<SceneInfo> *>(data);
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	if (uuid && name)
		scenes->push_back({QString::fromUtf8(uuid), QString::fromUtf8(name)});
	return true;
}

bool TempestCommandMatrix::EnumSource(void *data, obs_source_t *source)
{
	auto *sources = static_cast<QVector<SourceInfo> *>(data);
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	if (uuid && name)
		sources->push_back({QString::fromUtf8(uuid), QString::fromUtf8(name)});
	return true;
}

QVector<TempestCommandMatrix::SceneInfo> TempestCommandMatrix::EnumerateScenes() const
{
	QVector<SceneInfo> scenes;
	obs_enum_scenes(EnumScene, &scenes);
	return scenes;
}

void TempestCommandMatrix::RefreshScenes()
{
	const QVector<SceneInfo> scenes = EnumerateScenes();
	QString fingerprint;
	for (const SceneInfo &scene : scenes)
		fingerprint += scene.uuid + QLatin1Char('|') + scene.name + QLatin1Char('\n');

	if (fingerprint != sceneFingerprint) {
		sceneFingerprint = fingerprint;
		RebuildAssignments(scenes);
		RebuildSceneGrid(scenes);
	}
	UpdateActiveScene();
}

void TempestCommandMatrix::RebuildAssignments(const QVector<SceneInfo> &scenes)
{
	for (ProtocolWidgets &protocol : protocols) {
		QSignalBlocker blocker(protocol.sceneCombo);
		protocol.sceneCombo->clear();
		protocol.sceneCombo->addItem(QStringLiteral("Assign scene..."), QString());
		for (const SceneInfo &scene : scenes)
			protocol.sceneCombo->addItem(scene.name, scene.uuid);

		QString wanted = configuredSceneUuids.value(protocol.id);
		int index = wanted.isEmpty() ? -1 : protocol.sceneCombo->findData(wanted);
		if (index < 0 && scenes.size() == 1)
			index = 1;
		if (index < 0) {
			QString hint = protocol.id;
			if (hint == QStringLiteral("starting"))
				hint = QStringLiteral("start");
			for (int sceneIndex = 0; sceneIndex < scenes.size(); ++sceneIndex) {
				if (scenes[sceneIndex].name.contains(hint, Qt::CaseInsensitive)) {
					index = sceneIndex + 1;
					break;
				}
			}
		}
		protocol.sceneCombo->setCurrentIndex(std::max(index, 0));
		configuredSceneUuids[protocol.id] = protocol.sceneCombo->currentData().toString();
	}
	SaveAssignments();
	SetStatus(QStringLiteral("MATRIX READY // %1 SCENE%2")
			  .arg(scenes.size())
			  .arg(scenes.size() == 1 ? QString() : QStringLiteral("S")));
}

void TempestCommandMatrix::RebuildSceneGrid(const QVector<SceneInfo> &scenes)
{
	while (QLayoutItem *item = sceneGrid->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}
	sceneButtons.clear();

	for (int index = 0; index < scenes.size(); ++index) {
		const SceneInfo &scene = scenes[index];
		auto *button = new QPushButton(QString(scene.name).replace(QStringLiteral("&"), QStringLiteral("&&")),
						  sceneGrid->parentWidget());
		button->setCheckable(true);
		button->setMinimumHeight(44);
		button->setAccessibleName(QStringLiteral("Route scene %1").arg(scene.name));
		connect(button, &QPushButton::clicked, this,
			[this, uuid = scene.uuid, name = scene.name]() { SwitchScene(uuid, name); });
		sceneGrid->addWidget(button, index / 2, index % 2);
		sceneButtons.insert(scene.uuid, button);
	}

	if (scenes.isEmpty()) {
		auto *empty = new QLabel(QStringLiteral("No scenes available."), sceneGrid->parentWidget());
		empty->setObjectName(QStringLiteral("matrixSubtitle"));
		sceneGrid->addWidget(empty, 0, 0, 1, 2);
	}
}

void TempestCommandMatrix::SaveAssignments()
{
	config_t *config = App()->GetUserConfig();
	for (const ProtocolWidgets &protocol : protocols) {
		const QString uuid = protocol.sceneCombo ? protocol.sceneCombo->currentData().toString()
							 : configuredSceneUuids.value(protocol.id);
		configuredSceneUuids[protocol.id] = uuid;
		const QByteArray key = ConfigKey(protocol.id).toUtf8();
		config_set_string(config, ConfigSection, key.constData(), uuid.toUtf8().constData());
	}
	config_set_bool(config, ConfigSection, "IsolateOverlay", isolateOverlay && isolateOverlay->isChecked());
	config_set_bool(config, ConfigSection, "StartCountdown", startCountdown && startCountdown->isChecked());
	config_save_safe(config, "tmp", nullptr);
}

TempestCommandMatrix::ProtocolWidgets *TempestCommandMatrix::FindProtocol(const QString &id)
{
	for (ProtocolWidgets &protocol : protocols) {
		if (protocol.id == id)
			return &protocol;
	}
	return nullptr;
}

void TempestCommandMatrix::ExecuteProtocol(const QString &protocolId)
{
	ProtocolWidgets *protocol = FindProtocol(protocolId);
	if (!protocol)
		return;

	const QString uuid = protocol->sceneCombo->currentData().toString();
	if (uuid.isEmpty()) {
		SetStatus(QStringLiteral("Assign a scene to %1 before executing it.").arg(protocol->label), true);
		return;
	}

	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!sceneSource || obs_source_get_type(sceneSource) != OBS_SOURCE_TYPE_SCENE) {
		SetStatus(QStringLiteral("The assigned %1 scene is unavailable.").arg(protocol->label), true);
		RefreshScenes();
		return;
	}

	const ProtocolActionConfig config = actionConfigs.value(protocolId);
	const quint64 revision = ++executionRevision;

	if (!config.transitionUuid.isEmpty()) {
		OBSSourceAutoRelease transition = obs_get_source_by_uuid(config.transitionUuid.toUtf8().constData());
		if (transition && obs_source_get_type(transition) == OBS_SOURCE_TYPE_TRANSITION) {
			main->SetTransitionDuration(config.transitionDuration);
			main->SetCurrentTransition(config.transitionUuid);
		}
	}
	ApplyAudioAction(config.audioSourceAUuid, config.audioActionA);
	ApplyAudioAction(config.audioSourceBUuid, config.audioActionB);
	ApplyRecordingAction(config.recordingAction);
	const bool launchFailed = config.launchEnabled && !LaunchConfiguredProgram(config);

	if (config.delayMs > 0) {
		SetStatus(QStringLiteral("%1 ARMED // ROUTING IN %2 MS%3")
				  .arg(protocol->label)
				  .arg(config.delayMs)
				  .arg(launchFailed ? QStringLiteral(" // PROGRAM FAILED") : QString()));
		QTimer::singleShot(config.delayMs, this, [this, protocolId, uuid, revision, launchFailed]() {
			CompleteProtocolRoute(protocolId, uuid, revision, launchFailed);
		});
		return;
	}
	CompleteProtocolRoute(protocolId, uuid, revision, launchFailed);
}

void TempestCommandMatrix::CompleteProtocolRoute(const QString &protocolId, const QString &sceneUuid,
						 quint64 revision, bool launchFailed)
{
	if (revision != executionRevision)
		return;
	ProtocolWidgets *protocol = FindProtocol(protocolId);
	if (!protocol)
		return;

	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
	if (!sceneSource || obs_source_get_type(sceneSource) != OBS_SOURCE_TYPE_SCENE) {
		SetStatus(QStringLiteral("The assigned %1 scene became unavailable.").arg(protocol->label), true);
		RefreshScenes();
		return;
	}
	if (isolateOverlay->isChecked())
		ApplyProtocolOverlay(sceneSource.Get(), protocol->sourceName);
	controlDeck->ActivateMode(protocol->id,
				  protocol->id == QStringLiteral("starting") && startCountdown->isChecked());
	main->SetCurrentScene(OBSSource(sceneSource.Get()));
	SetStatus(QStringLiteral("%1 PROTOCOL // %2%3")
			  .arg(protocol->label, obs_source_get_name(sceneSource),
			       launchFailed ? QStringLiteral(" // PROGRAM FAILED") : QString()),
		  launchFailed);
	OBSDataAutoRelease eventData = obs_data_create();
	obs_data_set_string(eventData, "protocol", protocolId.toUtf8().constData());
	obs_data_set_string(eventData, "sceneUuid", sceneUuid.toUtf8().constData());
	obs_data_set_string(eventData, "sceneName", obs_source_get_name(sceneSource));
	obs_data_set_bool(eventData, "programLaunchFailed", launchFailed);
	EmitRouterEvent("ProtocolExecuted", eventData);
	UpdateActiveScene();
}

void TempestCommandMatrix::ApplyAudioAction(const QString &sourceUuid, const QString &action)
{
	if (sourceUuid.isEmpty() || action == QStringLiteral("keep"))
		return;
	if (action != QStringLiteral("mute") && action != QStringLiteral("unmute"))
		return;
	OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.toUtf8().constData());
	if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
		return;
	obs_source_set_muted(source, action == QStringLiteral("mute"));
}

void TempestCommandMatrix::ApplyRecordingAction(const QString &action)
{
	if (action == QStringLiteral("start") && !main->RecordingActive())
		main->StartRecording();
	else if (action == QStringLiteral("stop") && main->RecordingActive())
		main->StopRecording();
}

bool TempestCommandMatrix::LaunchConfiguredProgram(const ProtocolActionConfig &config)
{
	if (!config.launchEnabled)
		return true;
	const QFileInfo program(config.programPath);
	if (!program.isFile())
		return false;
	return QProcess::startDetached(program.absoluteFilePath(), QProcess::splitCommand(config.programArguments),
				       program.absolutePath());
}

void TempestCommandMatrix::SwitchScene(const QString &uuid, const QString &name)
{
	++executionRevision;
	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!sceneSource || obs_source_get_type(sceneSource) != OBS_SOURCE_TYPE_SCENE) {
		SetStatus(QStringLiteral("Scene route unavailable: %1").arg(name), true);
		RefreshScenes();
		return;
	}
	main->SetCurrentScene(OBSSource(sceneSource.Get()));
	SetStatus(QStringLiteral("DIRECT ROUTE // %1").arg(name));
	OBSDataAutoRelease eventData = obs_data_create();
	obs_data_set_string(eventData, "sceneUuid", uuid.toUtf8().constData());
	obs_data_set_string(eventData, "sceneName", name.toUtf8().constData());
	EmitRouterEvent("SceneRouted", eventData);
	UpdateActiveScene();
}

void TempestCommandMatrix::RouteExternalScene(const QString &uuid, const QString &name)
{
	const QVector<SceneInfo> scenes = EnumerateScenes();
	for (const SceneInfo &scene : scenes) {
		if ((!uuid.isEmpty() && scene.uuid == uuid) ||
		    (uuid.isEmpty() && scene.name.compare(name, Qt::CaseInsensitive) == 0)) {
			SwitchScene(scene.uuid, scene.name);
			return;
		}
	}
	SetStatus(QStringLiteral("EXTERNAL ROUTE FAILED // SCENE NOT FOUND"), true);
}

void TempestCommandMatrix::EmitRouterEvent(const char *eventName, obs_data_t *eventData)
{
#ifdef TEMPEST_WEBSOCKET_AVAILABLE
	if (webSocketReady && webSocketVendor)
		obs_websocket_vendor_emit_event(static_cast<obs_websocket_vendor>(webSocketVendor), eventName,
						eventData);
#else
	UNUSED_PARAMETER(eventName);
	UNUSED_PARAMETER(eventData);
#endif
}

QVector<TempestCommandMatrix::SourceInfo> TempestCommandMatrix::EnumerateAudioSources() const
{
	QVector<SourceInfo> sources;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
				return true;
			return EnumSource(data, source);
		},
		&sources);
	std::sort(sources.begin(), sources.end(), [](const SourceInfo &a, const SourceInfo &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return sources;
}

QVector<TempestCommandMatrix::SourceInfo> TempestCommandMatrix::EnumerateTransitions() const
{
	QVector<SourceInfo> sources;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (obs_source_get_type(source) != OBS_SOURCE_TYPE_TRANSITION)
				return true;
			return EnumSource(data, source);
		},
		&sources);
	return sources;
}

void TempestCommandMatrix::LoadActionConfigs()
{
	config_t *config = App()->GetUserConfig();
	for (const ProtocolWidgets &protocol : protocols) {
		ProtocolActionConfig actions;
		auto stringValue = [&](const char *field) {
			const QByteArray key = ActionConfigKey(protocol.id, field).toUtf8();
			return QString::fromUtf8(config_get_string(config, ConfigSection, key.constData()));
		};
		auto intValue = [&](const char *field, int fallback) {
			const QByteArray key = ActionConfigKey(protocol.id, field).toUtf8();
			return config_has_user_value(config, ConfigSection, key.constData())
				       ? static_cast<int>(config_get_int(config, ConfigSection, key.constData()))
				       : fallback;
		};
		actions.delayMs = intValue("DelayMs", 0);
		actions.transitionUuid = stringValue("TransitionUuid");
		actions.transitionDuration = intValue("TransitionDuration", 300);
		actions.audioSourceAUuid = stringValue("AudioSourceAUuid");
		actions.audioActionA = stringValue("AudioActionA");
		if (actions.audioActionA.isEmpty())
			actions.audioActionA = QStringLiteral("keep");
		actions.audioSourceBUuid = stringValue("AudioSourceBUuid");
		actions.audioActionB = stringValue("AudioActionB");
		if (actions.audioActionB.isEmpty())
			actions.audioActionB = QStringLiteral("keep");
		actions.recordingAction = stringValue("RecordingAction");
		if (actions.recordingAction.isEmpty())
			actions.recordingAction = QStringLiteral("keep");
		const QByteArray enabledKey = ActionConfigKey(protocol.id, "LaunchEnabled").toUtf8();
		actions.launchEnabled = config_get_bool(config, ConfigSection, enabledKey.constData());
		actions.programPath = stringValue("ProgramPath");
		actions.programArguments = stringValue("ProgramArguments");
		actionConfigs.insert(protocol.id, actions);
	}
}

void TempestCommandMatrix::SaveActionConfigs()
{
	config_t *config = App()->GetUserConfig();
	for (const ProtocolWidgets &protocol : protocols) {
		const ProtocolActionConfig actions = actionConfigs.value(protocol.id);
		auto setString = [&](const char *field, const QString &value) {
			const QByteArray key = ActionConfigKey(protocol.id, field).toUtf8();
			config_set_string(config, ConfigSection, key.constData(), value.toUtf8().constData());
		};
		auto setInt = [&](const char *field, int value) {
			const QByteArray key = ActionConfigKey(protocol.id, field).toUtf8();
			config_set_int(config, ConfigSection, key.constData(), value);
		};
		setInt("DelayMs", actions.delayMs);
		setString("TransitionUuid", actions.transitionUuid);
		setInt("TransitionDuration", actions.transitionDuration);
		setString("AudioSourceAUuid", actions.audioSourceAUuid);
		setString("AudioActionA", actions.audioActionA);
		setString("AudioSourceBUuid", actions.audioSourceBUuid);
		setString("AudioActionB", actions.audioActionB);
		setString("RecordingAction", actions.recordingAction);
		const QByteArray enabledKey = ActionConfigKey(protocol.id, "LaunchEnabled").toUtf8();
		config_set_bool(config, ConfigSection, enabledKey.constData(), actions.launchEnabled);
		setString("ProgramPath", actions.programPath);
		setString("ProgramArguments", actions.programArguments);
	}
	config_save_safe(config, "tmp", nullptr);
}

void TempestCommandMatrix::OpenActionEditor()
{
	struct EditorFields {
		QString protocolId;
		QSpinBox *delay = nullptr;
		QComboBox *transition = nullptr;
		QSpinBox *transitionDuration = nullptr;
		QComboBox *audioSourceA = nullptr;
		QComboBox *audioActionA = nullptr;
		QComboBox *audioSourceB = nullptr;
		QComboBox *audioActionB = nullptr;
		QComboBox *recordingAction = nullptr;
		QCheckBox *launchEnabled = nullptr;
		QLineEdit *programPath = nullptr;
		QLineEdit *programArguments = nullptr;
	};

	const QVector<SourceInfo> audioSources = EnumerateAudioSources();
	const QVector<SourceInfo> transitions = EnumerateTransitions();
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("tempestProtocolActionEditor"));
	dialog.setWindowTitle(QStringLiteral("Tempest Protocol Actions"));
	dialog.resize(720, 760);
	dialog.setStyleSheet(QStringLiteral(R"(
		QDialog { background: #07131e; color: #bdf6ff; }
		QTabWidget::pane { border: 1px solid #1f506d; }
		QTabBar::tab { background: #0d2230; color: #748fa4; padding: 9px 18px; border: 1px solid #1f506d; }
		QTabBar::tab:selected { color: #bdf6ff; background: #073c5f; border-color: #45d9ff; }
		QGroupBox { border: 1px solid #1f506d; margin-top: 12px; padding-top: 10px; color: #45d9ff; font-weight: 700; }
		QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
		QLabel, QCheckBox { color: #9eb7c8; }
		QComboBox, QSpinBox, QLineEdit { min-height: 28px; background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; padding: 0 6px; }
		QPushButton { min-height: 30px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; padding: 0 12px; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
	)"));

	auto *dialogLayout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(
		QStringLiteral("Each protocol can prepare the workstation, then route its assigned scene. "
			       "KEEP leaves the current OBS state untouched."),
		&dialog);
	intro->setWordWrap(true);
	dialogLayout->addWidget(intro);
	auto *tabs = new QTabWidget(&dialog);
	tabs->setObjectName(QStringLiteral("protocolActionTabs"));
	dialogLayout->addWidget(tabs, 1);

	QVector<EditorFields> editors;
	for (const ProtocolWidgets &protocol : protocols) {
		const ProtocolActionConfig actions = actionConfigs.value(protocol.id);
		EditorFields fields;
		fields.protocolId = protocol.id;
		auto *page = new QWidget(tabs);
		auto *pageLayout = new QVBoxLayout(page);

		auto *routeGroup = new QGroupBox(QStringLiteral("ROUTING SEQUENCE"), page);
		auto *routeForm = new QFormLayout(routeGroup);
		fields.delay = new QSpinBox(routeGroup);
		fields.delay->setObjectName(protocol.id + QStringLiteral("DelayMs"));
		fields.delay->setRange(0, 10000);
		fields.delay->setSingleStep(100);
		fields.delay->setSuffix(QStringLiteral(" ms"));
		fields.delay->setValue(actions.delayMs);
		routeForm->addRow(QStringLiteral("Delay before scene route"), fields.delay);

		fields.transition = new QComboBox(routeGroup);
		fields.transition->setObjectName(protocol.id + QStringLiteral("Transition"));
		fields.transition->addItem(QStringLiteral("Keep current transition"), QString());
		for (const SourceInfo &transition : transitions)
			fields.transition->addItem(transition.name, transition.uuid);
		SetComboData(fields.transition, actions.transitionUuid, QStringLiteral("Unavailable transition"));
		routeForm->addRow(QStringLiteral("Transition"), fields.transition);

		fields.transitionDuration = new QSpinBox(routeGroup);
		fields.transitionDuration->setRange(50, 20000);
		fields.transitionDuration->setSingleStep(50);
		fields.transitionDuration->setSuffix(QStringLiteral(" ms"));
		fields.transitionDuration->setValue(actions.transitionDuration);
		routeForm->addRow(QStringLiteral("Transition duration"), fields.transitionDuration);
		pageLayout->addWidget(routeGroup);

		auto addAudioSourceItems = [&](QComboBox *combo) {
			combo->addItem(QStringLiteral("No source selected"), QString());
			for (const SourceInfo &source : audioSources)
				combo->addItem(source.name, source.uuid);
		};
		auto addAudioActionItems = [](QComboBox *combo) {
			combo->addItem(QStringLiteral("KEEP"), QStringLiteral("keep"));
			combo->addItem(QStringLiteral("MUTE"), QStringLiteral("mute"));
			combo->addItem(QStringLiteral("UNMUTE"), QStringLiteral("unmute"));
		};
		auto *audioGroup = new QGroupBox(QStringLiteral("AUDIO SOURCE STATES"), page);
		auto *audioGrid = new QGridLayout(audioGroup);
		audioGrid->addWidget(new QLabel(QStringLiteral("Source"), audioGroup), 0, 0);
		audioGrid->addWidget(new QLabel(QStringLiteral("Action"), audioGroup), 0, 1);
		fields.audioSourceA = new QComboBox(audioGroup);
		fields.audioActionA = new QComboBox(audioGroup);
		fields.audioSourceB = new QComboBox(audioGroup);
		fields.audioActionB = new QComboBox(audioGroup);
		addAudioSourceItems(fields.audioSourceA);
		addAudioSourceItems(fields.audioSourceB);
		addAudioActionItems(fields.audioActionA);
		addAudioActionItems(fields.audioActionB);
		SetComboData(fields.audioSourceA, actions.audioSourceAUuid);
		SetComboData(fields.audioSourceB, actions.audioSourceBUuid);
		SetComboData(fields.audioActionA, actions.audioActionA);
		SetComboData(fields.audioActionB, actions.audioActionB);
		audioGrid->addWidget(fields.audioSourceA, 1, 0);
		audioGrid->addWidget(fields.audioActionA, 1, 1);
		audioGrid->addWidget(fields.audioSourceB, 2, 0);
		audioGrid->addWidget(fields.audioActionB, 2, 1);
		pageLayout->addWidget(audioGroup);

		auto *outputGroup = new QGroupBox(QStringLiteral("RECORDING AND PROGRAM"), page);
		auto *outputForm = new QFormLayout(outputGroup);
		fields.recordingAction = new QComboBox(outputGroup);
		fields.recordingAction->addItem(QStringLiteral("KEEP"), QStringLiteral("keep"));
		fields.recordingAction->addItem(QStringLiteral("START RECORDING"), QStringLiteral("start"));
		fields.recordingAction->addItem(QStringLiteral("STOP RECORDING"), QStringLiteral("stop"));
		SetComboData(fields.recordingAction, actions.recordingAction);
		outputForm->addRow(QStringLiteral("Recording"), fields.recordingAction);
		fields.launchEnabled = new QCheckBox(QStringLiteral("Launch an external program"), outputGroup);
		fields.launchEnabled->setChecked(actions.launchEnabled);
		outputForm->addRow(QString(), fields.launchEnabled);

		auto *programRow = new QWidget(outputGroup);
		auto *programLayout = new QHBoxLayout(programRow);
		programLayout->setContentsMargins(0, 0, 0, 0);
		fields.programPath = new QLineEdit(actions.programPath, programRow);
		fields.programPath->setPlaceholderText(QStringLiteral("C:\\Path\\To\\Program.exe"));
		auto *browse = new QPushButton(QStringLiteral("BROWSE"), programRow);
		programLayout->addWidget(fields.programPath, 1);
		programLayout->addWidget(browse);
		connect(browse, &QPushButton::clicked, &dialog, [&dialog, path = fields.programPath]() {
			const QString selected = QFileDialog::getOpenFileName(
				&dialog, QStringLiteral("Select program"), path->text(),
				QStringLiteral("Programs (*.exe *.com);;All files (*.*)"));
			if (!selected.isEmpty())
				path->setText(selected);
		});
		outputForm->addRow(QStringLiteral("Program"), programRow);
		fields.programArguments = new QLineEdit(actions.programArguments, outputGroup);
		fields.programArguments->setPlaceholderText(QStringLiteral("Optional command-line arguments"));
		outputForm->addRow(QStringLiteral("Arguments"), fields.programArguments);
		pageLayout->addWidget(outputGroup);
		pageLayout->addStretch(1);

		tabs->addTab(page, protocol.label);
		editors.push_back(fields);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialogLayout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;

	for (const EditorFields &fields : editors) {
		ProtocolActionConfig actions;
		actions.delayMs = fields.delay->value();
		actions.transitionUuid = fields.transition->currentData().toString();
		actions.transitionDuration = fields.transitionDuration->value();
		actions.audioSourceAUuid = fields.audioSourceA->currentData().toString();
		actions.audioActionA = fields.audioActionA->currentData().toString();
		actions.audioSourceBUuid = fields.audioSourceB->currentData().toString();
		actions.audioActionB = fields.audioActionB->currentData().toString();
		actions.recordingAction = fields.recordingAction->currentData().toString();
		actions.launchEnabled = fields.launchEnabled->isChecked();
		actions.programPath = fields.programPath->text().trimmed();
		actions.programArguments = fields.programArguments->text();
		actionConfigs.insert(fields.protocolId, actions);
	}
	SaveActionConfigs();
	SetStatus(QStringLiteral("PROTOCOL ACTIONS SAVED // MATRIX READY"));
}

bool TempestCommandMatrix::SetOverlayVisibility(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto *context = static_cast<OverlayVisibilityContext *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);
	const QString name = QString::fromUtf8(obs_source_get_name(source));
	static const QStringList overlaySources = {
		QStringLiteral("Tempest // Starting Soon"),
		QStringLiteral("Tempest // Live HUD"),
		QStringLiteral("Tempest // BRB"),
		QStringLiteral("Tempest // Stream Ending"),
	};
	if (overlaySources.contains(name))
		obs_sceneitem_set_visible(item, name == context->selectedSourceName);
	return true;
}

void TempestCommandMatrix::ApplyProtocolOverlay(obs_source_t *sceneSource, const QString &sourceName)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return;
	OverlayVisibilityContext context{sourceName};
	obs_scene_enum_items(scene, SetOverlayVisibility, &context);
}

void TempestCommandMatrix::UpdateActiveScene()
{
	OBSSource current = main->GetCurrentSceneSource();
	const QString currentUuid = current ? QString::fromUtf8(obs_source_get_uuid(current)) : QString();
	const QString currentName = current ? QString::fromUtf8(obs_source_get_name(current)) : QStringLiteral("NONE");
	currentSceneLabel->setText(QStringLiteral("ACTIVE // %1").arg(currentName.toUpper()));
	for (auto it = sceneButtons.begin(); it != sceneButtons.end(); ++it) {
		if (it.value())
			it.value()->setChecked(it.key() == currentUuid);
	}
}

void TempestCommandMatrix::SetStatus(const QString &message, bool error)
{
	statusLabel->setText(message);
	statusLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
					 .arg(error ? QStringLiteral("#ff799c") : QStringLiteral("#45d9ff")));
}
