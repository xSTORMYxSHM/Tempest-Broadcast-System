#include "TempestCommandMatrix.hpp"

#include "TempestControlDeck.hpp"
#include "TempestHUDComposer.hpp"
#include "TempestMediaBay.hpp"
#include "TempestSequenceDirector.hpp"
#include "TempestSignalReactor.hpp"

#include <OBSApp.hpp>
#include <components/SourceTree.hpp>
#ifdef TEMPEST_WEBSOCKET_AVAILABLE
#include <obs-websocket-api.h>
#endif
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QAction>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

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
		combo->addItem(unavailableLabel.isEmpty() ? QStringLiteral("Unavailable source") : unavailableLabel,
			       value);
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

void GetSceneItemBox(obs_sceneitem_t *item, vec3 &topLeft, vec3 &bottomRight)
{
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);
	vec3_set(&topLeft, FLT_MAX, FLT_MAX, 0.0f);
	vec3_set(&bottomRight, -FLT_MAX, -FLT_MAX, 0.0f);
	for (int y = 0; y <= 1; ++y) {
		for (int x = 0; x <= 1; ++x) {
			vec3 point;
			vec3_set(&point, float(x), float(y), 0.0f);
			vec3_transform(&point, &point, &transform);
			vec3_min(&topLeft, &topLeft, &point);
			vec3_max(&bottomRight, &bottomRight, &point);
		}
	}
}
} // namespace

TempestCommandMatrix::TempestCommandMatrix(OBSBasic *main, TempestControlDeck *controlDeck, QWidget *parent)
	: OBSDock(parent),
	  main(main),
	  controlDeck(controlDeck)
{
	setObjectName(QStringLiteral("tempestCommandMatrix"));
	setWindowTitle(QStringLiteral("Scene Control"));
	setMinimumWidth(270);
	BuildInterface();
	EnableContentScaling(objectName());

	config_t *config = App()->GetUserConfig();
	for (const ProtocolWidgets &protocol : protocols) {
		const QByteArray key = ConfigKey(protocol.id).toUtf8();
		configuredSceneUuids.insert(
			protocol.id, QString::fromUtf8(config_get_string(config, ConfigSection, key.constData())));
	}
	isolateOverlay->setChecked(!config_has_user_value(config, ConfigSection, "IsolateOverlay") ||
				   config_get_bool(config, ConfigSection, "IsolateOverlay"));
	startCountdown->setChecked(config_get_bool(config, ConfigSection, "StartCountdown"));
	const char *savedView = config_get_string(config, ConfigSection, "ViewMode");
	SetViewMode(savedView && QString::fromUtf8(savedView) == QStringLiteral("protocol") ? QStringLiteral("protocol")
											    : QStringLiteral("basic"),
		    false);
	LoadActionConfigs();
	LoadSourceReactions();

	connect(isolateOverlay, &QCheckBox::toggled, this, &TempestCommandMatrix::SaveAssignments);
	connect(startCountdown, &QCheckBox::toggled, this, &TempestCommandMatrix::SaveAssignments);

	refreshTimer = new QTimer(this);
	refreshTimer->setInterval(1000);
	connect(refreshTimer, &QTimer::timeout, this, &TempestCommandMatrix::RefreshScenes);
	refreshTimer->start();
	RefreshScenes();
}

TempestCommandMatrix::~TempestCommandMatrix()
{
	RestoreAllReactions();
	UnregisterHotkeys();
#ifdef TEMPEST_WEBSOCKET_AVAILABLE
	if (webSocketVendor) {
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"RunProtocol");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"RouteScene");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"SetOverlayState");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"RunSequence");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"ControlSequence");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"TriggerSignal");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"TriggerReactionEvent");
		obs_websocket_vendor_unregister_request(static_cast<obs_websocket_vendor>(webSocketVendor),
							"ClearReactionEvent");
	}
#endif
}

QWidget *TempestCommandMatrix::TakeSourceInspectorPanel()
{
	if (!sourceInspectorPanel)
		return nullptr;

	QWidget *panel = sourceInspectorPanel;
	panel->setParent(nullptr);
	return panel;
}

void TempestCommandMatrix::SetSequenceDirector(TempestSequenceDirector *director)
{
	sequenceDirector = director;
}

void TempestCommandMatrix::SetHUDComposer(TempestHUDComposer *composer)
{
	hudComposer = composer;
}

void TempestCommandMatrix::SetSignalReactor(TempestSignalReactor *reactor)
{
	signalReactor = reactor;
	if (!signalReactor)
		return;
	connect(signalReactor, &TempestSignalReactor::PulseTriggered, this,
		[this](float strength, const QString &origin) {
			OBSDataAutoRelease eventData = obs_data_create();
			obs_data_set_double(eventData, "strength", strength);
			obs_data_set_string(eventData, "origin", origin.toUtf8().constData());
			EmitRouterEvent("SignalTriggered", eventData);
		});
	connect(signalReactor, &TempestSignalReactor::LevelsUpdated, this, &TempestCommandMatrix::ApplyReactionLevels);
	connect(signalReactor, &TempestSignalReactor::ExternalEventTriggered, this,
		[this](const QString &type, const QString &name, float strength, int durationMs, const QString &circuit,
		       const QString &accent, const QString &effect, const QString &origin) {
			reactionExternalEventStrength = std::clamp(strength, 0.05f, 1.5f);
			reactionExternalEventCircuit = circuit;
			reactionExternalEventUntil = QDateTime::currentMSecsSinceEpoch() + durationMs;
			OBSDataAutoRelease eventData = obs_data_create();
			obs_data_set_string(eventData, "type", type.toUtf8().constData());
			obs_data_set_string(eventData, "name", name.toUtf8().constData());
			obs_data_set_double(eventData, "strength", strength);
			obs_data_set_int(eventData, "durationMs", durationMs);
			obs_data_set_string(eventData, "circuit", circuit.toUtf8().constData());
			obs_data_set_string(eventData, "accent", accent.toUtf8().constData());
			obs_data_set_string(eventData, "effect", effect.toUtf8().constData());
			obs_data_set_string(eventData, "origin", origin.toUtf8().constData());
			EmitRouterEvent("ReactionEventTriggered", eventData);
			SetStatus(QStringLiteral("External reaction // %1 // %2").arg(name, circuit.toUpper()));
		});
	connect(signalReactor, &TempestSignalReactor::ExternalEventCleared, this, [this]() {
		reactionExternalEventUntil = 0;
		reactionExternalEventStrength = 0.0f;
		reactionExternalEventCircuit.clear();
		ApplyReactionLevels(0.0f, 0.0f, 0.0f, 0.0f);
		EmitRouterEvent("ReactionEventCleared", nullptr);
	});
	reactionNetworkArmed = signalReactor->SourceNetworkArmed();
	reactionNetworkIntensity = signalReactor->SourceNetworkIntensity();
	reactionNetworkActiveSceneOnly = signalReactor->SourceNetworkActiveSceneOnly();
	reactionCircuitProfile = signalReactor->SourceNetworkCircuitProfile();
	reactionSoloCircuit = signalReactor->SourceNetworkSoloCircuit();
	for (const QString &circuit : {QStringLiteral("core"), QStringLiteral("frame"), QStringLiteral("chat"),
				       QStringLiteral("plates"), QStringLiteral("alerts")})
		reactionCircuitGains.insert(circuit, signalReactor->SourceNetworkCircuitGain(circuit));
	connect(signalReactor, &TempestSignalReactor::SourceNetworkArmedChanged, this, [this](bool armed) {
		reactionNetworkArmed = armed;
		if (!armed) {
			reactionTestKey.clear();
			reactionTestUntil = 0;
			reactionNetworkTestUntil = 0;
			reactionNetworkTestCircuit.clear();
			RestoreAllReactions();
		}
		SetStatus(armed ? QStringLiteral("Source reaction network armed")
				: QStringLiteral("Source reaction network disarmed // bases restored"));
	});
	connect(signalReactor, &TempestSignalReactor::SourceNetworkIntensityChanged, this, [this](float intensity) {
		reactionNetworkIntensity = std::clamp(double(intensity), 0.0, 2.0);
		if (reactionNetworkIntensity <= 0.0)
			RestoreAllReactions();
	});
	connect(signalReactor, &TempestSignalReactor::SourceNetworkScopeChanged, this, [this](bool activeSceneOnly) {
		reactionNetworkActiveSceneOnly = activeSceneOnly;
		if (activeSceneOnly) {
			for (auto it = sourceReactions.begin(); it != sourceReactions.end(); ++it) {
				if (it->sceneUuid != reactionActiveSceneUuid)
					RestoreReaction(it.value());
			}
		}
		UpdateReactionNetworkSummary();
		SetStatus(activeSceneOnly ? QStringLiteral("Reaction scope // active scene rigs only")
					  : QStringLiteral("Reaction scope // all bound scenes"));
	});
	connect(signalReactor, &TempestSignalReactor::SourceNetworkCircuitProfileChanged, this,
		[this](const QString &profile) {
			reactionCircuitProfile = profile;
			for (auto it = sourceReactions.begin(); it != sourceReactions.end(); ++it) {
				if (!ReactionCircuitActive(it->circuit))
					RestoreReaction(it.value());
			}
			UpdateReactionNetworkSummary();
			SetStatus(QStringLiteral("Reaction circuits // %1").arg(profile.toUpper()));
		});
	connect(signalReactor, &TempestSignalReactor::SourceNetworkCircuitSoloChanged, this,
		[this](const QString &circuit) {
			reactionSoloCircuit = circuit;
			for (auto it = sourceReactions.begin(); it != sourceReactions.end(); ++it) {
				if (!ReactionCircuitActive(it->circuit))
					RestoreReaction(it.value());
			}
			UpdateReactionNetworkSummary();
			SetStatus(circuit.isEmpty()
					  ? QStringLiteral("Reaction circuit solo cleared")
					  : QStringLiteral("Reaction circuit solo // %1").arg(circuit.toUpper()));
		});
	connect(signalReactor, &TempestSignalReactor::SourceNetworkCircuitGainsChanged, this,
		[this](float core, float frame, float chat, float plates, float alerts) {
			reactionCircuitGains[QStringLiteral("core")] = std::clamp(double(core), 0.0, 2.0);
			reactionCircuitGains[QStringLiteral("frame")] = std::clamp(double(frame), 0.0, 2.0);
			reactionCircuitGains[QStringLiteral("chat")] = std::clamp(double(chat), 0.0, 2.0);
			reactionCircuitGains[QStringLiteral("plates")] = std::clamp(double(plates), 0.0, 2.0);
			reactionCircuitGains[QStringLiteral("alerts")] = std::clamp(double(alerts), 0.0, 2.0);
			for (auto it = sourceReactions.begin(); it != sourceReactions.end(); ++it) {
				if (ReactionCircuitGain(it->circuit) <= 0.0)
					RestoreReaction(it.value());
			}
			UpdateReactionNetworkSummary();
			SetStatus(QStringLiteral("Reaction circuit mixer updated"));
		});
	connect(signalReactor, &TempestSignalReactor::SourceNetworkTestRequested, this,
		&TempestCommandMatrix::TestReactionNetwork);
	connect(signalReactor, &TempestSignalReactor::SourceNetworkCircuitTestRequested, this,
		&TempestCommandMatrix::TestReactionCircuit);
	connect(signalReactor, &TempestSignalReactor::SourceNetworkRestoreRequested, this, [this]() {
		reactionTestKey.clear();
		reactionTestUntil = 0;
		reactionNetworkTestUntil = 0;
		reactionNetworkTestCircuit.clear();
		RestoreAllReactions();
		SetStatus(QStringLiteral("All source reaction bases restored"));
	});
	UpdateReactionNetworkSummary();
	RefreshReactionConsole();
}

void TempestCommandMatrix::RunProtocol(const QString &protocolId)
{
	ExecuteProtocol(protocolId.toLower());
}

void TempestCommandMatrix::RegisterHotkeys()
{
	UnregisterHotkeys();
	for (const ProtocolWidgets &protocol : protocols) {
		const QString name = QStringLiteral("TempestMainframe.Run.%1").arg(protocol.id);
		const QString description = QStringLiteral("Tempest Broadcast: Run %1 Automation").arg(protocol.label);
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
	QMetaObject::invokeMethod(
		matrix,
		[guarded, protocolId]() {
			if (guarded)
				guarded->ExecuteProtocol(protocolId);
		},
		Qt::QueuedConnection);
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
	const bool sequenceReady = sequenceDirector && obs_websocket_vendor_register_request(
							       vendor, "RunSequence", WebSocketRunSequence, this);
	const bool sequenceControlReady =
		sequenceDirector &&
		obs_websocket_vendor_register_request(vendor, "ControlSequence", WebSocketControlSequence, this);
	const bool signalReady = signalReactor && obs_websocket_vendor_register_request(vendor, "TriggerSignal",
											WebSocketTriggerSignal, this);
	const bool reactionEventReady = signalReactor &&
					obs_websocket_vendor_register_request(vendor, "TriggerReactionEvent",
									      WebSocketTriggerReactionEvent, this);
	const bool clearReactionEventReady =
		signalReactor &&
		obs_websocket_vendor_register_request(vendor, "ClearReactionEvent", WebSocketClearReactionEvent, this);
	webSocketReady = protocolReady && sceneReady && overlayReady && sequenceReady && sequenceControlReady &&
			 signalReady && reactionEventReady && clearReactionEventReady;
#endif
	SetRouterState();
}

void TempestCommandMatrix::WebSocketTriggerSignal(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	if (!matrix->signalReactor) {
		SetRouterResponse(response, false, "signal reactor is unavailable");
		return;
	}
	const double requestedStrength =
		obs_data_has_user_value(request, "strength") ? obs_data_get_double(request, "strength") : 1.0;
	if (!std::isfinite(requestedStrength) || requestedStrength < 0.05 || requestedStrength > 1.5) {
		SetRouterResponse(response, false, "strength must be between 0.05 and 1.5");
		return;
	}
	QPointer<TempestSignalReactor> guarded(matrix->signalReactor);
	QMetaObject::invokeMethod(
		matrix,
		[guarded, requestedStrength]() {
			if (guarded)
				guarded->TriggerPulse((float)requestedStrength, QStringLiteral("websocket"));
		},
		Qt::QueuedConnection);
	obs_data_set_double(response, "strength", requestedStrength);
	SetRouterResponse(response, true, "signal command queued");
}

void TempestCommandMatrix::WebSocketTriggerReactionEvent(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	if (!matrix->signalReactor) {
		SetRouterResponse(response, false, "signal reactor is unavailable");
		return;
	}
	const QString type = QString::fromUtf8(obs_data_get_string(request, "type")).trimmed().toLower();
	const QString name = QString::fromUtf8(obs_data_get_string(request, "name")).trimmed();
	const QString circuit = QString::fromUtf8(obs_data_get_string(request, "circuit")).trimmed().toLower();
	const QString accent = QString::fromUtf8(obs_data_get_string(request, "accent")).trimmed().toUpper();
	const QString effect = QString::fromUtf8(obs_data_get_string(request, "effect")).trimmed().toLower();
	const QString origin = QString::fromUtf8(obs_data_get_string(request, "origin")).trimmed();
	const QString dedupeId = QString::fromUtf8(obs_data_get_string(request, "dedupeId")).trimmed();
	const double strength = obs_data_has_user_value(request, "strength") ? obs_data_get_double(request, "strength")
									     : 0.0;
	const int64_t durationMs =
		obs_data_has_user_value(request, "durationMs") ? obs_data_get_int(request, "durationMs") : 0;
	const int64_t cooldownMs =
		obs_data_has_user_value(request, "cooldownMs") ? obs_data_get_int(request, "cooldownMs") : -1;
	if (type.isEmpty() || type.size() > 48) {
		SetRouterResponse(response, false, "type is required and must be 48 characters or fewer");
		return;
	}
	if (name.size() > 96 || origin.size() > 48 || dedupeId.size() > 128) {
		SetRouterResponse(response, false, "name, origin, or dedupeId is too long");
		return;
	}
	if (!std::isfinite(strength) || strength < 0.0 || strength > 1.5) {
		SetRouterResponse(response, false, "strength must be omitted or between 0.05 and 1.5");
		return;
	}
	if (strength > 0.0 && strength < 0.05) {
		SetRouterResponse(response, false, "strength must be omitted or between 0.05 and 1.5");
		return;
	}
	if (durationMs < 0 || durationMs > 30000 || cooldownMs < -1 || cooldownMs > 10000) {
		SetRouterResponse(response, false, "durationMs or cooldownMs is outside the supported range");
		return;
	}
	const QStringList validCircuits = {QString(),
					   QStringLiteral("all"),
					   QStringLiteral("core"),
					   QStringLiteral("frame"),
					   QStringLiteral("chat"),
					   QStringLiteral("plates"),
					   QStringLiteral("alerts")};
	const QStringList validEffects = {QString(),
					  QStringLiteral("pulse"),
					  QStringLiteral("glow"),
					  QStringLiteral("glitch"),
					  QStringLiteral("spectrum"),
					  QStringLiteral("surge")};
	if (!validCircuits.contains(circuit) || !validEffects.contains(effect) ||
	    (!accent.isEmpty() && !QRegularExpression(QStringLiteral("^#[0-9A-F]{6}$")).match(accent).hasMatch())) {
		SetRouterResponse(response, false, "circuit, effect, or accent is invalid");
		return;
	}
	QPointer<TempestSignalReactor> guarded(matrix->signalReactor);
	QMetaObject::invokeMethod(
		matrix,
		[guarded, type, name, strength, durationMs, circuit, accent, effect, origin, dedupeId, cooldownMs]() {
			if (guarded)
				guarded->TriggerExternalEvent(type, name, float(strength), int(durationMs), circuit,
							      accent, effect, origin, dedupeId, int(cooldownMs));
		},
		Qt::QueuedConnection);
	obs_data_set_string(response, "type", type.toUtf8().constData());
	obs_data_set_string(response, "circuit",
			    (circuit.isEmpty() ? QStringLiteral("preset") : circuit).toUtf8().constData());
	SetRouterResponse(response, true, "reaction event queued");
}

void TempestCommandMatrix::WebSocketClearReactionEvent(obs_data_t *, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	if (!matrix->signalReactor) {
		SetRouterResponse(response, false, "signal reactor is unavailable");
		return;
	}
	QPointer<TempestSignalReactor> guarded(matrix->signalReactor);
	QMetaObject::invokeMethod(
		matrix,
		[guarded]() {
			if (guarded)
				guarded->ClearExternalEvent();
		},
		Qt::QueuedConnection);
	SetRouterResponse(response, true, "reaction event clear queued");
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
	QMetaObject::invokeMethod(
		matrix,
		[guarded, protocolId]() {
			if (guarded)
				guarded->ExecuteProtocol(protocolId);
		},
		Qt::QueuedConnection);
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
	QMetaObject::invokeMethod(
		matrix,
		[guarded, uuid, name]() {
			if (guarded)
				guarded->RouteExternalScene(uuid, name);
		},
		Qt::QueuedConnection);
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
	QMetaObject::invokeMethod(
		matrix,
		[guarded, mode, transmission, status, messages, startCountdown]() {
			if (!guarded || !guarded->controlDeck)
				return;
			guarded->controlDeck->UpdateOverlayText(mode, transmission, status, messages);
			if (startCountdown)
				guarded->controlDeck->ActivateMode(mode, true);
			OBSDataAutoRelease eventData = obs_data_create();
			obs_data_set_string(eventData, "mode", mode.toUtf8().constData());
			guarded->EmitRouterEvent("OverlayStateUpdated", eventData);
		},
		Qt::QueuedConnection);
	SetRouterResponse(response, true, "overlay state queued");
}

void TempestCommandMatrix::WebSocketRunSequence(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	const QString sequenceId = QString::fromUtf8(obs_data_get_string(request, "sequence")).toLower();
	if (!IsProtocolId(sequenceId)) {
		SetRouterResponse(response, false, "sequence must be starting, live, brb, or ending");
		return;
	}
	QPointer<TempestSequenceDirector> guarded(matrix->sequenceDirector);
	QMetaObject::invokeMethod(
		matrix,
		[guarded, sequenceId]() {
			if (guarded)
				guarded->RunSequence(sequenceId);
		},
		Qt::QueuedConnection);
	SetRouterResponse(response, true, "sequence command queued");
}

void TempestCommandMatrix::WebSocketControlSequence(obs_data_t *request, obs_data_t *response, void *data)
{
	auto *matrix = static_cast<TempestCommandMatrix *>(data);
	const QString action = QString::fromUtf8(obs_data_get_string(request, "action")).toLower();
	static const QStringList actions = {QStringLiteral("hold"),       QStringLiteral("resume"),
					    QStringLiteral("togglehold"), QStringLiteral("next"),
					    QStringLiteral("restart"),    QStringLiteral("stop")};
	if (!actions.contains(action)) {
		SetRouterResponse(response, false, "action must be hold, resume, toggleHold, next, restart, or stop");
		return;
	}
	QPointer<TempestSequenceDirector> guarded(matrix->sequenceDirector);
	QMetaObject::invokeMethod(
		matrix,
		[guarded, action]() {
			if (guarded)
				guarded->ControlSequence(action);
		},
		Qt::QueuedConnection);
	SetRouterResponse(response, true, "sequence control queued");
}

void TempestCommandMatrix::SetRouterState()
{
	if (!routerLabel)
		return;
	const bool hotkeysReady = protocolHotkeys.size() == protocols.size();
	if (hotkeysReady && webSocketReady)
		routerLabel->setText(QStringLiteral("CONTROL ROUTER // HOTKEYS + WEBSOCKET VENDOR API READY"));
	else if (hotkeysReady)
		routerLabel->setText(QStringLiteral("CONTROL ROUTER // HOTKEYS READY"));
	else
		routerLabel->setText(QStringLiteral("CONTROL ROUTER // INITIALIZING"));
	if (signalReactor)
		signalReactor->SetWebSocketReady(webSocketReady);
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
		QPushButton[sceneRoute="true"] { min-height: 44px; }
		QPushButton[sceneRoute="true"][activeScene="true"] { border: 2px solid #45d9ff; background: #073c5f; color: white; }
		QComboBox { min-height: 28px; background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; }
		QCheckBox { color: #748fa4; spacing: 7px; }
		QScrollArea { border: none; background: transparent; }
		QListView#matrixSourceTree { border: 1px solid #153b52; background: #07131e; color: #bdf6ff; }
		QListView#matrixSourceTree QCheckBox { spacing: 0; margin: 0; }
		QWidget#matrixSourceToolbar { border: none; background: #07131e; }
		QWidget#matrixSourceToolbar QToolButton { min-width: 0; min-height: 30px; border: 1px solid #153b52; border-radius: 3px; background: #0d2230; color: #bdf6ff; }
		QWidget#matrixSourceToolbar QToolButton:hover { border-color: #45d9ff; background: #0c456b; }
		QWidget#matrixSourceToolbar QToolButton:pressed { border-color: #45d9ff; background: #073c5f; }
		QWidget#matrixSourceToolbar QToolButton:disabled { color: #40596b; border-color: #102c3d; background: #091722; }
		QWidget#matrixSourceInspector { border: 1px solid #153b52; background: #06101a; }
		QWidget#matrixSourceInspector QLabel { border: none; background: transparent; }
		QLabel#matrixInspectorSource { color: #bdf6ff; font-size: 10px; font-weight: 700; letter-spacing: 1px; }
		QLabel#matrixInspectorType { color: #748fa4; font-size: 9px; }
		QPushButton[inspectorAction="true"] { min-height: 27px; padding: 0 5px; font-size: 9px; }
		QPushButton[inspectorAction="true"]:disabled { color: #40596b; border-color: #102c3d; background: #091722; }
		QPushButton[layoutToggle="true"] { min-height: 29px; padding: 0 7px; font-size: 9px; letter-spacing: 1px; text-align: left; }
		QWidget#matrixLayoutConsole { border: 1px solid #102f42; background: #07131e; }
		QWidget#matrixLayoutConsole QLabel { border: none; background: transparent; color: #748fa4; font-size: 9px; }
		QWidget#matrixLayoutConsole QDoubleSpinBox { min-height: 25px; padding: 0 4px; border: 1px solid #1f506d; background: #06101a; color: #bdf6ff; selection-background-color: #0c7ccb; }
		QPushButton[layoutMini="true"] { min-height: 25px; padding: 0 4px; font-size: 8px; }
		QSplitter::handle { background: #153b52; height: 2px; margin: 4px 0; }
	)"));

	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *title = new QLabel(QStringLiteral("SCENE CONTROL"), root);
	title->setObjectName(QStringLiteral("matrixTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Scenes, sources, and stream automation"), root);
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

	auto *viewRow = new QHBoxLayout();
	viewRow->setSpacing(6);
	basicViewButton = new QPushButton(QStringLiteral("BASIC"), root);
	protocolViewButton = new QPushButton(QStringLiteral("AUTOMATION"), root);
	basicViewButton->setCheckable(true);
	protocolViewButton->setCheckable(true);
	basicViewButton->setAccessibleName(QStringLiteral("Basic scene routing view"));
	protocolViewButton->setAccessibleName(QStringLiteral("Stream automation view"));
	auto *viewGroup = new QButtonGroup(root);
	viewGroup->setExclusive(true);
	viewGroup->addButton(basicViewButton);
	viewGroup->addButton(protocolViewButton);
	connect(basicViewButton, &QPushButton::clicked, this, [this]() { SetViewMode(QStringLiteral("basic")); });
	connect(protocolViewButton, &QPushButton::clicked, this, [this]() { SetViewMode(QStringLiteral("protocol")); });
	viewRow->addWidget(basicViewButton);
	viewRow->addWidget(protocolViewButton);
	layout->addLayout(viewRow);

	viewStack = new QStackedWidget(root);
	viewStack->setObjectName(QStringLiteral("tempestMatrixViews"));
	layout->addWidget(viewStack, 1);

	basicViewPage = new QWidget(viewStack);
	auto *basicLayout = new QVBoxLayout(basicViewPage);
	basicLayout->setContentsMargins(0, 0, 0, 0);
	basicLayout->setSpacing(0);
	auto *routingSplitter = new QSplitter(Qt::Vertical, basicViewPage);
	routingSplitter->setObjectName(QStringLiteral("matrixRoutingSplitter"));
	routingSplitter->setChildrenCollapsible(false);

	auto *scenePane = new QWidget(routingSplitter);
	auto *scenePaneLayout = new QVBoxLayout(scenePane);
	scenePaneLayout->setContentsMargins(0, 0, 0, 4);
	scenePaneLayout->setSpacing(7);
	auto *sceneLabel = new QLabel(QStringLiteral("DIRECT SCENE ROUTING"), scenePane);
	sceneLabel->setObjectName(QStringLiteral("matrixSection"));
	scenePaneLayout->addWidget(sceneLabel);

	auto *scroll = new QScrollArea(scenePane);
	scroll->setWidgetResizable(true);
	auto *sceneContainer = new QWidget(scroll);
	sceneContainer->setStyleSheet(QStringLiteral("background: transparent;"));
	sceneGrid = new QGridLayout(sceneContainer);
	sceneGrid->setContentsMargins(0, 0, 0, 0);
	sceneGrid->setHorizontalSpacing(6);
	sceneGrid->setVerticalSpacing(6);
	sceneGrid->setAlignment(Qt::AlignTop);
	sceneGrid->setColumnStretch(0, 1);
	sceneGrid->setColumnStretch(1, 1);
	scroll->setWidget(sceneContainer);
	scenePaneLayout->addWidget(scroll, 1);
	routingSplitter->addWidget(scenePane);

	auto *sourcePane = new QWidget(routingSplitter);
	auto *sourcePaneLayout = new QVBoxLayout(sourcePane);
	sourcePaneLayout->setContentsMargins(0, 4, 0, 0);
	sourcePaneLayout->setSpacing(7);
	sourceSceneLabel = new QLabel(QStringLiteral("SCENE SOURCES // INITIALIZING"), sourcePane);
	sourceSceneLabel->setObjectName(QStringLiteral("matrixSection"));
	sourceSceneLabel->setAccessibleName(QStringLiteral("Active scene sources"));
	sourcePaneLayout->addWidget(sourceSceneLabel);

	sourceTree = new SourceTree(sourcePane);
	sourceTree->setProperty("stableSourceIndicators", true);
	sourceTree->setObjectName(QStringLiteral("matrixSourceTree"));
	sourceTree->setAccessibleName(QStringLiteral("Scene Control native sources"));
	sourceTree->setContextMenuPolicy(Qt::CustomContextMenu);
	sourceTree->setFrameShape(QFrame::NoFrame);
	sourceTree->setDragEnabled(true);
	sourceTree->setDragDropMode(QAbstractItemView::InternalMove);
	sourceTree->setDefaultDropAction(Qt::MoveAction);
	sourceTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	sourceTree->setSpacing(0);
	connect(sourceTree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		if (!sourceTree || !main)
			return;
		const QModelIndex index = sourceTree->indexAt(pos);
		main->CreateSourcePopupMenu(index.row(), false);
	});
	connect(sourceTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
		[this](const QModelIndex &, const QModelIndex &) { UpdateSourceInspector(); });
	connect(sourceTree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
		[this](const QItemSelection &, const QItemSelection &) { UpdateSourceInspector(); });
	sourcePaneLayout->addWidget(sourceTree, 1);

	auto *sourceToolbar = new QWidget(sourcePane);
	sourceToolbar->setObjectName(QStringLiteral("matrixSourceToolbar"));
	sourceToolbar->setAccessibleName(QStringLiteral("Scene Control source controls"));
	auto *sourceToolbarLayout = new QHBoxLayout(sourceToolbar);
	sourceToolbarLayout->setContentsMargins(0, 0, 0, 0);
	sourceToolbarLayout->setSpacing(4);
	auto addSourceAction = [this, sourceToolbar, sourceToolbarLayout](const char *objectName,
									  const QString &accessibleName) {
		QAction *action = main ? main->findChild<QAction *>(QString::fromUtf8(objectName)) : nullptr;
		if (!action)
			return;
		auto *button = new QToolButton(sourceToolbar);
		button->setDefaultAction(action);
		button->setAccessibleName(accessibleName);
		button->setIconSize(QSize(16, 16));
		button->setToolButtonStyle(Qt::ToolButtonIconOnly);
		button->setAutoRaise(false);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		sourceToolbarLayout->addWidget(button, 1);
	};
	addSourceAction("actionAddSource", QStringLiteral("Add source"));
	addSourceAction("actionRemoveSource", QStringLiteral("Remove selected source"));
	addSourceAction("actionSourceProperties", QStringLiteral("Open selected source properties"));
	addSourceAction("actionSourceUp", QStringLiteral("Move selected source up"));
	addSourceAction("actionSourceDown", QStringLiteral("Move selected source down"));
	sourcePaneLayout->addWidget(sourceToolbar);

	sourceInspectorPanel = new QWidget();
	sourceInspectorPanel->setObjectName(QStringLiteral("matrixSourceInspector"));
	sourceInspectorPanel->setAccessibleName(QStringLiteral("Selected source inspector"));
	sourceInspectorPanel->setStyleSheet(root->styleSheet());
	auto *inspectorLayout = new QVBoxLayout(sourceInspectorPanel);
	inspectorLayout->setContentsMargins(7, 6, 7, 7);
	inspectorLayout->setSpacing(4);
	inspectorSourceLabel = new QLabel(QStringLiteral("SOURCE INSPECTOR // SELECT A SOURCE"), sourceInspectorPanel);
	inspectorSourceLabel->setObjectName(QStringLiteral("matrixInspectorSource"));
	inspectorTypeLabel = new QLabel(QStringLiteral("No source selected"), sourceInspectorPanel);
	inspectorTypeLabel->setObjectName(QStringLiteral("matrixInspectorType"));
	inspectorLayout->addWidget(inspectorSourceLabel);
	inspectorLayout->addWidget(inspectorTypeLabel);

	auto *inspectorGrid = new QGridLayout();
	inspectorGrid->setContentsMargins(0, 2, 0, 0);
	inspectorGrid->setHorizontalSpacing(4);
	inspectorGrid->setVerticalSpacing(4);
	auto addInspectorButton = [this, inspectorGrid](QPointer<QPushButton> &target, const QString &text, int row,
							int column, int columnSpan = 1) {
		target = new QPushButton(text, sourceInspectorPanel);
		target->setProperty("inspectorAction", true);
		target->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		target->setAccessibleName(QStringLiteral("Source inspector %1").arg(text.toLower()));
		inspectorGrid->addWidget(target, row, column, 1, columnSpan);
	};
	addInspectorButton(inspectorFitButton, QStringLiteral("FIT"), 0, 0);
	addInspectorButton(inspectorCenterButton, QStringLiteral("CENTER"), 0, 1);
	addInspectorButton(inspectorResetButton, QStringLiteral("RESET"), 0, 2);
	addInspectorButton(inspectorPropertiesButton, QStringLiteral("PROPERTIES"), 1, 0);
	addInspectorButton(inspectorFiltersButton, QStringLiteral("FILTERS"), 1, 1);
	addInspectorButton(inspectorRenameButton, QStringLiteral("RENAME"), 1, 2);
	addInspectorButton(inspectorDuplicateButton, QStringLiteral("DUPLICATE"), 2, 0);
	addInspectorButton(inspectorInteractButton, QStringLiteral("INTERACT"), 2, 1);
	addInspectorButton(inspectorRefreshButton, QStringLiteral("REFRESH"), 2, 2);
	addInspectorButton(inspectorPlayButton, QStringLiteral("PLAY"), 3, 0);
	addInspectorButton(inspectorPauseButton, QStringLiteral("PAUSE"), 3, 1);
	addInspectorButton(inspectorRestartButton, QStringLiteral("RESTART"), 3, 2);
	addInspectorButton(inspectorMuteButton, QStringLiteral("MUTE AUDIO"), 4, 0, 3);
	inspectorLayout->addLayout(inspectorGrid);

	layoutToggleButton = new QPushButton(QStringLiteral("PRECISION LAYOUT"), sourceInspectorPanel);
	layoutToggleButton->setProperty("layoutToggle", true);
	layoutToggleButton->setCheckable(true);
	layoutToggleButton->setAccessibleName(QStringLiteral("Toggle precision source layout controls"));
	inspectorLayout->addWidget(layoutToggleButton);

	layoutConsolePanel = new QWidget(sourceInspectorPanel);
	layoutConsolePanel->setObjectName(QStringLiteral("matrixLayoutConsole"));
	layoutConsolePanel->setAccessibleName(QStringLiteral("Precision source layout console"));
	auto *layoutConsole = new QVBoxLayout(layoutConsolePanel);
	layoutConsole->setContentsMargins(6, 6, 6, 6);
	layoutConsole->setSpacing(5);

	auto *transformGrid = new QGridLayout();
	transformGrid->setContentsMargins(0, 0, 0, 0);
	transformGrid->setHorizontalSpacing(4);
	transformGrid->setVerticalSpacing(4);
	auto addLayoutField = [this, transformGrid](QPointer<QDoubleSpinBox> &target, const QString &label, int row,
						    int column, double minimum, double maximum, int decimals,
						    double step) {
		auto *fieldLabel = new QLabel(label, layoutConsolePanel);
		target = new QDoubleSpinBox(layoutConsolePanel);
		target->setRange(minimum, maximum);
		target->setDecimals(decimals);
		target->setSingleStep(step);
		target->setKeyboardTracking(false);
		target->setAccelerated(true);
		target->setAccessibleName(QStringLiteral("Source layout %1").arg(label.toLower()));
		transformGrid->addWidget(fieldLabel, row, column * 2);
		transformGrid->addWidget(target, row, column * 2 + 1);
	};
	addLayoutField(layoutPosX, QStringLiteral("X"), 0, 0, -100000.0, 100000.0, 1, 1.0);
	addLayoutField(layoutPosY, QStringLiteral("Y"), 0, 1, -100000.0, 100000.0, 1, 1.0);
	addLayoutField(layoutWidth, QStringLiteral("W"), 1, 0, -32768.0, 32768.0, 1, 1.0);
	addLayoutField(layoutHeight, QStringLiteral("H"), 1, 1, -32768.0, 32768.0, 1, 1.0);
	addLayoutField(layoutRotation, QStringLiteral("ROT"), 2, 0, -3600.0, 3600.0, 1, 1.0);
	addLayoutField(layoutSafeMargin, QStringLiteral("SAFE %"), 2, 1, 0.0, 25.0, 1, 0.5);
	layoutSafeMargin->setValue(5.0);
	addLayoutField(layoutCropLeft, QStringLiteral("CROP L"), 3, 0, 0.0, 32768.0, 0, 1.0);
	addLayoutField(layoutCropRight, QStringLiteral("R"), 3, 1, 0.0, 32768.0, 0, 1.0);
	addLayoutField(layoutCropTop, QStringLiteral("CROP T"), 4, 0, 0.0, 32768.0, 0, 1.0);
	addLayoutField(layoutCropBottom, QStringLiteral("B"), 4, 1, 0.0, 32768.0, 0, 1.0);
	transformGrid->setColumnStretch(1, 1);
	transformGrid->setColumnStretch(3, 1);
	layoutConsole->addLayout(transformGrid);

	layoutAspectLock = new QCheckBox(QStringLiteral("LOCK ASPECT RATIO"), layoutConsolePanel);
	layoutAspectLock->setChecked(true);
	layoutAspectLock->setAccessibleName(QStringLiteral("Lock source layout aspect ratio"));
	layoutConsole->addWidget(layoutAspectLock);
	connect(layoutWidth, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		if (!layoutSyncing && layoutAspectLock && layoutAspectLock->isChecked() && layoutHeight &&
		    std::abs(layoutAspectRatio) > 0.0001) {
			QSignalBlocker blocker(layoutHeight);
			layoutHeight->setValue(value / layoutAspectRatio);
		}
	});
	connect(layoutHeight, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		if (!layoutSyncing && layoutAspectLock && layoutAspectLock->isChecked() && layoutWidth) {
			QSignalBlocker blocker(layoutWidth);
			layoutWidth->setValue(value * layoutAspectRatio);
		}
	});

	auto addMiniButton = [this](const QString &text, const QString &accessibleName) {
		auto *button = new QPushButton(text, layoutConsolePanel);
		button->setProperty("layoutMini", true);
		button->setAccessibleName(accessibleName);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		return button;
	};
	auto *historyRow = new QHBoxLayout();
	historyRow->setContentsMargins(0, 0, 0, 0);
	historyRow->setSpacing(4);
	auto *undoLayout = addMiniButton(QStringLiteral("UNDO"), QStringLiteral("Undo last OBS action"));
	auto *redoLayout = addMiniButton(QStringLiteral("REDO"), QStringLiteral("Redo last OBS action"));
	historyRow->addWidget(undoLayout, 1);
	historyRow->addWidget(redoLayout, 1);
	layoutConsole->addLayout(historyRow);
	connect(undoLayout, &QPushButton::clicked, this, [this]() { TriggerMainAction("actionMainUndo"); });
	connect(redoLayout, &QPushButton::clicked, this, [this]() { TriggerMainAction("actionMainRedo"); });

	auto *nudgeLabel = new QLabel(QStringLiteral("NUDGE // STEP"), layoutConsolePanel);
	layoutConsole->addWidget(nudgeLabel);
	auto *nudgeGrid = new QGridLayout();
	nudgeGrid->setContentsMargins(0, 0, 0, 0);
	nudgeGrid->setSpacing(4);
	auto *nudgeUp = addMiniButton(QStringLiteral("UP"), QStringLiteral("Nudge source up"));
	auto *nudgeLeft = addMiniButton(QStringLiteral("LEFT"), QStringLiteral("Nudge source left"));
	auto *nudgeRight = addMiniButton(QStringLiteral("RIGHT"), QStringLiteral("Nudge source right"));
	auto *nudgeDown = addMiniButton(QStringLiteral("DOWN"), QStringLiteral("Nudge source down"));
	layoutNudgeStep = new QDoubleSpinBox(layoutConsolePanel);
	layoutNudgeStep->setRange(0.1, 1000.0);
	layoutNudgeStep->setDecimals(1);
	layoutNudgeStep->setValue(10.0);
	layoutNudgeStep->setSingleStep(1.0);
	layoutNudgeStep->setAccessibleName(QStringLiteral("Source nudge step in pixels"));
	nudgeGrid->addWidget(nudgeUp, 0, 1);
	nudgeGrid->addWidget(nudgeLeft, 1, 0);
	nudgeGrid->addWidget(layoutNudgeStep, 1, 1);
	nudgeGrid->addWidget(nudgeRight, 1, 2);
	nudgeGrid->addWidget(nudgeDown, 2, 1);
	layoutConsole->addLayout(nudgeGrid);
	connect(nudgeUp, &QPushButton::clicked, this,
		[this]() { NudgeSelectedSource(0.0f, -float(layoutNudgeStep->value())); });
	connect(nudgeLeft, &QPushButton::clicked, this,
		[this]() { NudgeSelectedSource(-float(layoutNudgeStep->value()), 0.0f); });
	connect(nudgeRight, &QPushButton::clicked, this,
		[this]() { NudgeSelectedSource(float(layoutNudgeStep->value()), 0.0f); });
	connect(nudgeDown, &QPushButton::clicked, this,
		[this]() { NudgeSelectedSource(0.0f, float(layoutNudgeStep->value())); });

	auto *snapLabel = new QLabel(QStringLiteral("SAFE-ZONE SNAP"), layoutConsolePanel);
	layoutConsole->addWidget(snapLabel);
	auto *snapGrid = new QGridLayout();
	snapGrid->setContentsMargins(0, 0, 0, 0);
	snapGrid->setSpacing(4);
	const QString snapText[3][3] = {{QStringLiteral("TL"), QStringLiteral("T"), QStringLiteral("TR")},
					{QStringLiteral("L"), QStringLiteral("C"), QStringLiteral("R")},
					{QStringLiteral("BL"), QStringLiteral("B"), QStringLiteral("BR")}};
	for (int vertical = -1; vertical <= 1; ++vertical) {
		for (int horizontal = -1; horizontal <= 1; ++horizontal) {
			auto *button = addMiniButton(snapText[vertical + 1][horizontal + 1],
						     QStringLiteral("Snap source %1 %2")
							     .arg(vertical < 0   ? QStringLiteral("top")
								  : vertical > 0 ? QStringLiteral("bottom")
										 : QStringLiteral("center"),
								  horizontal < 0   ? QStringLiteral("left")
								  : horizontal > 0 ? QStringLiteral("right")
										   : QStringLiteral("center")));
			snapGrid->addWidget(button, vertical + 1, horizontal + 1);
			connect(button, &QPushButton::clicked, this,
				[this, horizontal, vertical]() { SnapSelectedSource(horizontal, vertical); });
		}
	}
	layoutConsole->addLayout(snapGrid);

	auto *applyLayout =
		addMiniButton(QStringLiteral("APPLY TRANSFORM"), QStringLiteral("Apply precision transform"));
	layoutConsole->addWidget(applyLayout);
	connect(applyLayout, &QPushButton::clicked, this, &TempestCommandMatrix::ApplyLayoutConsole);

	auto *snapshotLabel = new QLabel(QStringLiteral("SCENE LAYOUT SNAPSHOTS"), layoutConsolePanel);
	layoutConsole->addWidget(snapshotLabel);
	auto *snapshotGrid = new QGridLayout();
	snapshotGrid->setContentsMargins(0, 0, 0, 0);
	snapshotGrid->setSpacing(4);
	auto *saveA = addMiniButton(QStringLiteral("SAVE A"), QStringLiteral("Save scene layout snapshot A"));
	auto *recallA = addMiniButton(QStringLiteral("RECALL A"), QStringLiteral("Recall scene layout snapshot A"));
	auto *saveB = addMiniButton(QStringLiteral("SAVE B"), QStringLiteral("Save scene layout snapshot B"));
	auto *recallB = addMiniButton(QStringLiteral("RECALL B"), QStringLiteral("Recall scene layout snapshot B"));
	snapshotGrid->addWidget(saveA, 0, 0);
	snapshotGrid->addWidget(recallA, 0, 1);
	snapshotGrid->addWidget(saveB, 1, 0);
	snapshotGrid->addWidget(recallB, 1, 1);
	layoutConsole->addLayout(snapshotGrid);
	connect(saveA, &QPushButton::clicked, this, [this]() { SaveLayoutSnapshot("A"); });
	connect(recallA, &QPushButton::clicked, this, [this]() { RecallLayoutSnapshot("A"); });
	connect(saveB, &QPushButton::clicked, this, [this]() { SaveLayoutSnapshot("B"); });
	connect(recallB, &QPushButton::clicked, this, [this]() { RecallLayoutSnapshot("B"); });
	inspectorLayout->addWidget(layoutConsolePanel);

	config_t *matrixConfig = App()->GetUserConfig();
	const bool layoutExpanded = !config_has_user_value(matrixConfig, ConfigSection, "LayoutExpanded") ||
				    config_get_bool(matrixConfig, ConfigSection, "LayoutExpanded");
	layoutToggleButton->setChecked(layoutExpanded);
	layoutConsolePanel->setVisible(layoutExpanded);
	layoutToggleButton->setText(layoutExpanded ? QStringLiteral("PRECISION LAYOUT // OPEN")
						   : QStringLiteral("PRECISION LAYOUT"));
	connect(layoutToggleButton, &QPushButton::toggled, this, [this](bool expanded) {
		if (layoutConsolePanel)
			layoutConsolePanel->setVisible(expanded);
		layoutToggleButton->setText(expanded ? QStringLiteral("PRECISION LAYOUT // OPEN")
						     : QStringLiteral("PRECISION LAYOUT"));
		config_t *config = App()->GetUserConfig();
		config_set_bool(config, ConfigSection, "LayoutExpanded", expanded);
		config_save_safe(config, "tmp", nullptr);
	});

	reactionToggleButton = new QPushButton(QStringLiteral("REACTIVE BINDING"), sourceInspectorPanel);
	reactionToggleButton->setProperty("layoutToggle", true);
	reactionToggleButton->setCheckable(true);
	reactionToggleButton->setAccessibleName(QStringLiteral("Toggle reactive source binding controls"));
	inspectorLayout->addWidget(reactionToggleButton);

	reactionConsolePanel = new QWidget(sourceInspectorPanel);
	reactionConsolePanel->setObjectName(QStringLiteral("matrixReactionConsole"));
	reactionConsolePanel->setAccessibleName(QStringLiteral("Reactive source binding console"));
	auto *reactionConsole = new QVBoxLayout(reactionConsolePanel);
	reactionConsole->setContentsMargins(6, 6, 6, 6);
	reactionConsole->setSpacing(5);

	auto *reactionForm = new QFormLayout();
	reactionForm->setContentsMargins(0, 0, 0, 0);
	reactionForm->setHorizontalSpacing(6);
	reactionForm->setVerticalSpacing(4);
	reactionEnabled = new QCheckBox(QStringLiteral("REACTION ENABLED"), reactionConsolePanel);
	reactionEnabled->setAccessibleName(QStringLiteral("Enable selected source reaction"));
	reactionPreset = new QComboBox(reactionConsolePanel);
	reactionPreset->addItem(QStringLiteral("CUSTOM RIG"), QStringLiteral("custom"));
	reactionPreset->addItem(QStringLiteral("ENERGY SURGE"), QStringLiteral("mainframe-surge"));
	reactionPreset->addItem(QStringLiteral("BEAT LOCK"), QStringLiteral("beat-lock"));
	reactionPreset->addItem(QStringLiteral("VOICE RELAY"), QStringLiteral("voice-relay"));
	reactionPreset->addItem(QStringLiteral("FRACTAL DRIFT"), QStringLiteral("fractal-drift"));
	reactionPreset->addItem(QStringLiteral("SOFT PULSE"), QStringLiteral("ghost-signal"));
	reactionPreset->setAccessibleName(QStringLiteral("Reactive source motion rig preset"));
	reactionCircuit = new QComboBox(reactionConsolePanel);
	reactionCircuit->addItem(QStringLiteral("CORE"), QStringLiteral("core"));
	reactionCircuit->addItem(QStringLiteral("FRAME BORDER"), QStringLiteral("frame"));
	reactionCircuit->addItem(QStringLiteral("CHAT"), QStringLiteral("chat"));
	reactionCircuit->addItem(QStringLiteral("ELEMENT PLATES"), QStringLiteral("plates"));
	reactionCircuit->addItem(QStringLiteral("ALERTS"), QStringLiteral("alerts"));
	reactionCircuit->setAccessibleName(QStringLiteral("Reactive source circuit assignment"));
	reactionSignal = new QComboBox(reactionConsolePanel);
	reactionSignal->addItem(QStringLiteral("MASTER ENERGY"), QStringLiteral("master"));
	reactionSignal->addItem(QStringLiteral("DESKTOP AUDIO"), QStringLiteral("desktop"));
	reactionSignal->addItem(QStringLiteral("MICROPHONE"), QStringLiteral("microphone"));
	reactionSignal->addItem(QStringLiteral("BEAT TRANSIENT"), QStringLiteral("beat"));
	reactionSignal->setAccessibleName(QStringLiteral("Reactive source signal channel"));
	reactionThreshold = new QDoubleSpinBox(reactionConsolePanel);
	reactionThreshold->setRange(0.0, 1.0);
	reactionThreshold->setDecimals(2);
	reactionThreshold->setSingleStep(0.01);
	reactionThreshold->setValue(0.08);
	reactionThreshold->setAccessibleName(QStringLiteral("Reactive source signal threshold"));
	reactionForm->addRow(reactionEnabled);
	reactionForm->addRow(QStringLiteral("PRESET"), reactionPreset);
	reactionForm->addRow(QStringLiteral("CIRCUIT"), reactionCircuit);
	reactionForm->addRow(QStringLiteral("AUDIO INPUT"), reactionSignal);
	reactionForm->addRow(QStringLiteral("GATE"), reactionThreshold);
	reactionConsole->addLayout(reactionForm);

	auto *rigLabel = new QLabel(QStringLiteral("MOTION RIG // COMBINE MODULATORS"), reactionConsolePanel);
	rigLabel->setObjectName(QStringLiteral("matrixInspectorType"));
	reactionConsole->addWidget(rigLabel);
	auto *rigGrid = new QGridLayout();
	rigGrid->setContentsMargins(0, 0, 0, 0);
	rigGrid->setHorizontalSpacing(5);
	rigGrid->setVerticalSpacing(4);
	auto addRigControl = [this, rigGrid](QPointer<QCheckBox> &toggle, QPointer<QDoubleSpinBox> &amount,
					     const QString &label, const QString &accessibleName, const QString &suffix,
					     double minimum, double maximum, double value, double step, int row,
					     int pair) {
		toggle = new QCheckBox(label, reactionConsolePanel);
		toggle->setAccessibleName(QStringLiteral("Enable reactive source %1").arg(accessibleName));
		amount = new QDoubleSpinBox(reactionConsolePanel);
		amount->setRange(minimum, maximum);
		amount->setDecimals(1);
		amount->setSingleStep(step);
		amount->setValue(value);
		amount->setSuffix(suffix);
		amount->setKeyboardTracking(false);
		amount->setAccessibleName(QStringLiteral("Reactive source %1 amount").arg(accessibleName));
		amount->setEnabled(false);
		rigGrid->addWidget(toggle, row, pair * 2);
		rigGrid->addWidget(amount, row, pair * 2 + 1);
	};
	addRigControl(reactionScaleEnabled, reactionScaleAmount, QStringLiteral("SCALE"), QStringLiteral("scale"),
		      QStringLiteral(" %"), 0.1, 100.0, 12.0, 1.0, 0, 0);
	addRigControl(reactionLiftEnabled, reactionLiftAmount, QStringLiteral("LIFT"), QStringLiteral("lift"),
		      QStringLiteral(" px"), 0.1, 1000.0, 24.0, 2.0, 0, 1);
	addRigControl(reactionSwayEnabled, reactionSwayAmount, QStringLiteral("SWAY"), QStringLiteral("sway"),
		      QStringLiteral(" px"), 0.1, 1000.0, 18.0, 2.0, 1, 0);
	addRigControl(reactionRotateEnabled, reactionRotateAmount, QStringLiteral("ROTATE"), QStringLiteral("rotation"),
		      QStringLiteral(" deg"), 0.1, 180.0, 2.0, 0.5, 1, 1);
	reactionVisibilityEnabled = new QCheckBox(QStringLiteral("AUDIO-GATED VISIBILITY"), reactionConsolePanel);
	reactionVisibilityEnabled->setAccessibleName(QStringLiteral("Enable reactive source visibility gate"));
	rigGrid->addWidget(reactionVisibilityEnabled, 2, 0, 1, 4);
	rigGrid->setColumnStretch(1, 1);
	rigGrid->setColumnStretch(3, 1);
	reactionConsole->addLayout(rigGrid);

	reactionStatusLabel =
		new QLabel(QStringLiteral("NO BINDING // SELECT SETTINGS AND APPLY"), reactionConsolePanel);
	reactionStatusLabel->setObjectName(QStringLiteral("matrixInspectorType"));
	reactionStatusLabel->setWordWrap(true);
	reactionStatusLabel->setAccessibleName(QStringLiteral("Reactive source binding status"));
	reactionConsole->addWidget(reactionStatusLabel);

	auto *reactionButtons = new QGridLayout();
	reactionButtons->setContentsMargins(0, 0, 0, 0);
	reactionButtons->setSpacing(4);
	auto addReactionButton = [this, reactionButtons](const QString &text, const QString &accessibleName, int row,
							 int column) {
		auto *button = new QPushButton(text, reactionConsolePanel);
		button->setProperty("layoutMini", true);
		button->setAccessibleName(accessibleName);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		reactionButtons->addWidget(button, row, column);
		return button;
	};
	auto *applyReaction = addReactionButton(QStringLiteral("APPLY BINDING"),
						QStringLiteral("Apply selected source reaction binding"), 0, 0);
	auto *captureReaction = addReactionButton(QStringLiteral("CAPTURE BASE"),
						  QStringLiteral("Capture selected source reaction base transform"), 0,
						  1);
	auto *testReaction = addReactionButton(QStringLiteral("TEST"),
					       QStringLiteral("Test selected source reaction binding"), 1, 0);
	auto *removeReaction = addReactionButton(QStringLiteral("REMOVE"),
						 QStringLiteral("Remove selected source reaction binding"), 1, 1);
	reactionConsole->addLayout(reactionButtons);
	inspectorLayout->addWidget(reactionConsolePanel);

	const bool reactionExpanded = config_get_bool(matrixConfig, ConfigSection, "ReactionExpanded");
	reactionToggleButton->setChecked(reactionExpanded);
	reactionConsolePanel->setVisible(reactionExpanded);
	reactionToggleButton->setText(reactionExpanded ? QStringLiteral("REACTIVE BINDING // OPEN")
						       : QStringLiteral("REACTIVE BINDING"));
	connect(reactionToggleButton, &QPushButton::toggled, this, [this](bool expanded) {
		if (reactionConsolePanel)
			reactionConsolePanel->setVisible(expanded);
		reactionToggleButton->setText(expanded ? QStringLiteral("REACTIVE BINDING // OPEN")
						       : QStringLiteral("REACTIVE BINDING"));
		config_t *config = App()->GetUserConfig();
		config_set_bool(config, ConfigSection, "ReactionExpanded", expanded);
		config_save_safe(config, "tmp", nullptr);
	});
	connect(reactionPreset, &QComboBox::currentIndexChanged, this, &TempestCommandMatrix::ApplyReactionPreset);
	connect(reactionSignal, &QComboBox::currentIndexChanged, this, [this](int) { MarkReactionPresetCustom(); });
	connect(reactionThreshold, &QDoubleSpinBox::valueChanged, this, [this](double) { MarkReactionPresetCustom(); });
	auto connectRigControl = [this](QCheckBox *toggle, QDoubleSpinBox *amount) {
		connect(toggle, &QCheckBox::toggled, this, [this, amount](bool enabled) {
			amount->setEnabled(enabled);
			MarkReactionPresetCustom();
		});
		connect(amount, &QDoubleSpinBox::valueChanged, this, [this](double) { MarkReactionPresetCustom(); });
	};
	connectRigControl(reactionScaleEnabled, reactionScaleAmount);
	connectRigControl(reactionLiftEnabled, reactionLiftAmount);
	connectRigControl(reactionSwayEnabled, reactionSwayAmount);
	connectRigControl(reactionRotateEnabled, reactionRotateAmount);
	connect(reactionVisibilityEnabled, &QCheckBox::toggled, this, [this](bool) { MarkReactionPresetCustom(); });
	connect(applyReaction, &QPushButton::clicked, this, &TempestCommandMatrix::ApplyReactionBinding);
	connect(captureReaction, &QPushButton::clicked, this, &TempestCommandMatrix::CaptureReactionBaseline);
	connect(testReaction, &QPushButton::clicked, this, &TempestCommandMatrix::TestReactionBinding);
	connect(removeReaction, &QPushButton::clicked, this, &TempestCommandMatrix::RemoveReactionBinding);
	reactionScaleEnabled->setChecked(true);

	connect(inspectorFitButton, &QPushButton::clicked, this, [this]() { TriggerMainAction("actionFitToScreen"); });
	connect(inspectorCenterButton, &QPushButton::clicked, this,
		[this]() { TriggerMainAction("actionCenterToScreen"); });
	connect(inspectorResetButton, &QPushButton::clicked, this,
		[this]() { TriggerMainAction("actionResetTransform"); });
	connect(inspectorPropertiesButton, &QPushButton::clicked, this, [this]() {
		obs_source_t *source = SelectedInspectorSource();
		if (main && source)
			main->CreatePropertiesWindow(source);
	});
	connect(inspectorFiltersButton, &QPushButton::clicked, this, [this]() {
		obs_source_t *source = SelectedInspectorSource();
		if (main && source)
			main->CreateFiltersWindow(source);
	});
	connect(inspectorRenameButton, &QPushButton::clicked, this, [this]() {
		if (!sourceTree)
			return;
		QModelIndex index = sourceTree->currentIndex();
		const QModelIndexList selected = sourceTree->selectionModel()->selectedIndexes();
		if (!index.isValid() && !selected.isEmpty())
			index = selected.constFirst();
		if (index.isValid())
			sourceTree->Edit(index.row());
	});
	connect(inspectorDuplicateButton, &QPushButton::clicked, this, &TempestCommandMatrix::DuplicateSelectedSource);
	connect(inspectorInteractButton, &QPushButton::clicked, this, [this]() {
		obs_source_t *source = SelectedInspectorSource();
		if (main && source)
			main->CreateInteractionWindow(source);
	});
	connect(inspectorRefreshButton, &QPushButton::clicked, this, &TempestCommandMatrix::RefreshSelectedBrowser);
	connect(inspectorPlayButton, &QPushButton::clicked, this, [this]() { ApplySelectedMediaAction("play"); });
	connect(inspectorPauseButton, &QPushButton::clicked, this, [this]() { ApplySelectedMediaAction("pause"); });
	connect(inspectorRestartButton, &QPushButton::clicked, this, [this]() { ApplySelectedMediaAction("restart"); });
	connect(inspectorMuteButton, &QPushButton::clicked, this, &TempestCommandMatrix::ToggleSelectedMute);
	UpdateSourceInspector();
	routingSplitter->addWidget(sourcePane);
	routingSplitter->setStretchFactor(0, 1);
	routingSplitter->setStretchFactor(1, 1);
	routingSplitter->setSizes(QList<int>{240, 320});
	basicLayout->addWidget(routingSplitter, 1);
	viewStack->addWidget(basicViewPage);

	protocolViewPage = new QWidget(viewStack);
	auto *protocolLayout = new QVBoxLayout(protocolViewPage);
	protocolLayout->setContentsMargins(0, 0, 0, 0);
	protocolLayout->setSpacing(7);
	auto *protocolLabel = new QLabel(QStringLiteral("STREAM AUTOMATIONS"), protocolViewPage);
	protocolLabel->setObjectName(QStringLiteral("matrixSection"));
	protocolLayout->addWidget(protocolLabel);

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

	protocolGrid = new QGridLayout();
	protocolGrid->setHorizontalSpacing(6);
	protocolGrid->setVerticalSpacing(6);
	protocolGrid->setAlignment(Qt::AlignTop);
	for (int index = 0; index < 4; ++index) {
		const ProtocolDefinition &definition = definitions[index];
		ProtocolWidgets protocol;
		protocol.id = QString::fromUtf8(definition.id);
		protocol.label = QString::fromUtf8(definition.label);
		protocol.sourceName = QString::fromUtf8(definition.sourceName);
		protocol.button = new QPushButton(protocol.label, protocolViewPage);
		protocol.button->setProperty("protocol", true);
		protocol.button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		connect(protocol.button, &QPushButton::clicked, this,
			[this, id = protocol.id]() { ExecuteProtocol(id); });
		protocolGrid->addWidget(protocol.button, index / 2, index % 2);
		protocols.push_back(protocol);
	}
	protocolLayout->addLayout(protocolGrid);

	auto *assignmentLabel = new QLabel(QStringLiteral("AUTOMATION SCENE ASSIGNMENTS"), protocolViewPage);
	assignmentLabel->setObjectName(QStringLiteral("matrixSection"));
	protocolLayout->addWidget(assignmentLabel);

	auto *assignmentGrid = new QGridLayout();
	assignmentGrid->setColumnStretch(1, 1);
	assignmentGrid->setHorizontalSpacing(7);
	assignmentGrid->setVerticalSpacing(5);
	for (int index = 0; index < protocols.size(); ++index) {
		ProtocolWidgets &protocol = protocols[index];
		auto *label = new QLabel(protocol.label, protocolViewPage);
		label->setObjectName(QStringLiteral("matrixSubtitle"));
		protocol.sceneCombo = new QComboBox(protocolViewPage);
		protocol.sceneCombo->setAccessibleName(QStringLiteral("%1 scene assignment").arg(protocol.label));
		connect(protocol.sceneCombo, &QComboBox::currentIndexChanged, this,
			&TempestCommandMatrix::SaveAssignments);
		assignmentGrid->addWidget(label, index, 0);
		assignmentGrid->addWidget(protocol.sceneCombo, index, 1);
	}
	protocolLayout->addLayout(assignmentGrid);

	isolateOverlay = new QCheckBox(QStringLiteral("Isolate matching overlay sources"), protocolViewPage);
	startCountdown = new QCheckBox(QStringLiteral("Start countdown with STARTING"), protocolViewPage);
	protocolLayout->addWidget(isolateOverlay);
	protocolLayout->addWidget(startCountdown);

	auto *configureActions = new QPushButton(QStringLiteral("CONFIGURE AUTOMATION ACTIONS"), protocolViewPage);
	configureActions->setAccessibleName(QStringLiteral("Configure stream automation actions"));
	connect(configureActions, &QPushButton::clicked, this, &TempestCommandMatrix::OpenActionEditor);
	protocolLayout->addWidget(configureActions);
	protocolLayout->addStretch(1);
	viewStack->addWidget(protocolViewPage);

	statusLabel = new QLabel(QStringLiteral("MATRIX SYNCHRONIZING"), root);
	statusLabel->setObjectName(QStringLiteral("matrixStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);

	setWidget(root);
	QTimer::singleShot(0, this, [this]() { RelayoutRoutingGrids(); });
}

void TempestCommandMatrix::resizeEvent(QResizeEvent *event)
{
	OBSDock::resizeEvent(event);
	RelayoutRoutingGrids();
}

void TempestCommandMatrix::contentScaleChanged()
{
	routingColumnCount = 0;
	RelayoutRoutingGrids();
}

void TempestCommandMatrix::SetViewMode(const QString &mode, bool save)
{
	const bool protocolMode = mode == QStringLiteral("protocol");
	if (viewStack)
		viewStack->setCurrentWidget(protocolMode ? protocolViewPage : basicViewPage);
	if (basicViewButton)
		basicViewButton->setChecked(!protocolMode);
	if (protocolViewButton)
		protocolViewButton->setChecked(protocolMode);
	if (save) {
		config_t *config = App()->GetUserConfig();
		config_set_string(config, ConfigSection, "ViewMode", protocolMode ? "protocol" : "basic");
		config_save_safe(config, "tmp", nullptr);
		SetStatus(protocolMode ? QStringLiteral("STREAM AUTOMATION VIEW")
				       : QStringLiteral("BASIC SCENE ROUTING VIEW"));
	}
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

bool TempestCommandMatrix::EnumSceneSource(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto *sources = static_cast<QVector<SceneSourceInfo> *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source || obs_source_removed(source))
		return true;

	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	const char *sourceId = obs_source_get_unversioned_id(source);
	const char *displayName = sourceId ? obs_source_get_display_name(sourceId) : nullptr;
	SceneSourceInfo info;
	info.itemId = obs_sceneitem_get_id(item);
	info.uuid = uuid ? QString::fromUtf8(uuid) : QString();
	info.name = name ? QString::fromUtf8(name) : QStringLiteral("Unnamed source");
	info.typeName = displayName ? QString::fromUtf8(displayName) : QStringLiteral("Source");
	sources->push_back(info);
	if (obs_sceneitem_is_group(item)) {
		if (obs_scene_t *groupScene = obs_sceneitem_group_get_scene(item))
			obs_scene_enum_items(groupScene, EnumSceneSource, data);
	}
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
	SetActiveSceneButton(nullptr);
	emptySceneLabel = nullptr;
	while (QLayoutItem *item = sceneGrid->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}
	sceneButtons.clear();
	currentScenes = scenes;

	for (int index = 0; index < scenes.size(); ++index) {
		const SceneInfo &scene = scenes[index];
		auto *button = new QPushButton(QString(scene.name).replace(QStringLiteral("&"), QStringLiteral("&&")),
					       sceneGrid->parentWidget());
		button->setCheckable(false);
		button->setProperty("sceneRoute", true);
		button->setProperty("activeScene", false);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		button->setAccessibleName(QStringLiteral("Route scene %1").arg(scene.name));
		connect(button, &QPushButton::clicked, this,
			[this, uuid = scene.uuid, name = scene.name]() { SwitchScene(uuid, name); });
		sceneButtons.insert(scene.uuid, button);
	}

	if (scenes.isEmpty()) {
		emptySceneLabel = new QLabel(QStringLiteral("No scenes available."), sceneGrid->parentWidget());
		emptySceneLabel->setObjectName(QStringLiteral("matrixSubtitle"));
	}
	routingColumnCount = 0;
	RelayoutRoutingGrids();
}

void TempestCommandMatrix::RefreshSourcePanel(const QString &sceneUuid, const QString &sceneName)
{
	if (!sourceTree || !sourceSceneLabel)
		return;

	QVector<SceneSourceInfo> sources;
	if (!sceneUuid.isEmpty()) {
		OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (scene)
			obs_scene_enum_items(scene, EnumSceneSource, &sources);
	}
	std::reverse(sources.begin(), sources.end());

	QString fingerprint = sceneUuid + QLatin1Char('\n');
	for (const SceneSourceInfo &source : sources) {
		fingerprint += QString::number(source.itemId) + QLatin1Char('|') + source.uuid + QLatin1Char('|') +
			       source.name + QLatin1Char('|') + source.typeName + QLatin1Char('\n');
	}
	if (fingerprint == sourceFingerprint)
		return;

	sourceFingerprint = fingerprint;
	sourceSceneLabel->setText(
		QStringLiteral("SCENE SOURCES // %1 // %2").arg(sceneName.toUpper()).arg(sources.size()));
	QPointer<SourceTree> guardedTree(sourceTree);
	QPointer<TempestCommandMatrix> guardedMatrix(this);
	QTimer::singleShot(0, sourceTree, [guardedTree, guardedMatrix]() {
		if (guardedTree) {
			guardedTree->RefreshItems();
			if (guardedMatrix)
				guardedMatrix->UpdateSourceInspector();
		}
	});
}

obs_sceneitem_t *TempestCommandMatrix::SelectedInspectorItem() const
{
	if (!sourceTree)
		return nullptr;
	QModelIndex index = sourceTree->currentIndex();
	const QModelIndexList selected = sourceTree->selectionModel()->selectedIndexes();
	if (!index.isValid() && !selected.isEmpty())
		index = selected.constFirst();
	if (!index.isValid())
		return nullptr;
	OBSSceneItem item = sourceTree->Get(index.row());
	return item;
}

obs_source_t *TempestCommandMatrix::SelectedInspectorSource() const
{
	obs_sceneitem_t *item = SelectedInspectorItem();
	return item ? obs_sceneitem_get_source(item) : nullptr;
}

void TempestCommandMatrix::UpdateSourceInspector()
{
	if (!sourceInspectorPanel || !inspectorSourceLabel || !inspectorTypeLabel)
		return;

	obs_sceneitem_t *item = SelectedInspectorItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	const bool selected = source != nullptr;
	sourceInspectorPanel->setEnabled(selected);
	if (!selected) {
		inspectorSourceLabel->setText(QStringLiteral("SOURCE INSPECTOR // SELECT A SOURCE"));
		inspectorTypeLabel->setText(QStringLiteral("No source selected"));
		inspectorInteractButton->setVisible(false);
		inspectorRefreshButton->setVisible(false);
		inspectorPlayButton->setVisible(false);
		inspectorPauseButton->setVisible(false);
		inspectorRestartButton->setVisible(false);
		inspectorMuteButton->setVisible(false);
		RefreshLayoutConsole();
		RefreshReactionConsole();
		return;
	}

	const char *sourceName = obs_source_get_name(source);
	const char *sourceId = obs_source_get_unversioned_id(source);
	const char *displayName = sourceId ? obs_source_get_display_name(sourceId) : nullptr;
	const uint32_t flags = obs_source_get_output_flags(source);
	const bool isBrowser = sourceId && strcmp(sourceId, "browser_source") == 0;
	const bool canInteract = (flags & OBS_SOURCE_INTERACTION) != 0;
	const bool isMedia = (flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0;
	const bool hasAudio = (flags & OBS_SOURCE_AUDIO) != 0;
	const bool locked = obs_sceneitem_locked(item);

	inspectorSourceLabel->setText(QStringLiteral("SOURCE INSPECTOR // %1")
					      .arg(QString::fromUtf8(sourceName ? sourceName : "Unnamed").toUpper()));
	QStringList capabilities;
	if (isBrowser)
		capabilities.push_back(QStringLiteral("BROWSER"));
	else if (displayName)
		capabilities.push_back(QString::fromUtf8(displayName).toUpper());
	if (isMedia)
		capabilities.push_back(QStringLiteral("MEDIA"));
	if (hasAudio)
		capabilities.push_back(QStringLiteral("AUDIO"));
	capabilities.push_back(locked ? QStringLiteral("LOCKED") : QStringLiteral("EDITABLE"));
	capabilities.push_back(obs_source_active(source) ? QStringLiteral("ACTIVE") : QStringLiteral("INACTIVE"));
	inspectorTypeLabel->setText(capabilities.join(QStringLiteral(" // ")));

	inspectorFitButton->setEnabled(!locked);
	inspectorCenterButton->setEnabled(!locked);
	inspectorResetButton->setEnabled(!locked);
	inspectorPropertiesButton->setEnabled(true);
	inspectorFiltersButton->setEnabled(true);
	inspectorRenameButton->setEnabled(true);
	inspectorDuplicateButton->setEnabled(true);
	inspectorInteractButton->setVisible(canInteract);
	inspectorRefreshButton->setVisible(isBrowser);
	inspectorPlayButton->setVisible(isMedia);
	inspectorPauseButton->setVisible(isMedia);
	inspectorRestartButton->setVisible(isMedia);
	inspectorMuteButton->setVisible(hasAudio);
	if (hasAudio)
		inspectorMuteButton->setText(obs_source_muted(source) ? QStringLiteral("UNMUTE AUDIO")
								      : QStringLiteral("MUTE AUDIO"));
	RefreshLayoutConsole();
	RefreshReactionConsole();
}

void TempestCommandMatrix::TriggerMainAction(const char *objectName)
{
	if (!main || !SelectedInspectorItem())
		return;
	QAction *action = main->findChild<QAction *>(QString::fromUtf8(objectName));
	if (action && action->isEnabled()) {
		action->trigger();
		QTimer::singleShot(0, this, [this]() { RefreshLayoutConsole(); });
	}
}

void TempestCommandMatrix::RefreshSelectedBrowser()
{
	obs_source_t *source = SelectedInspectorSource();
	const char *sourceId = source ? obs_source_get_unversioned_id(source) : nullptr;
	if (!sourceId || strcmp(sourceId, "browser_source") != 0)
		return;

	obs_properties_t *properties = obs_source_properties(source);
	obs_property_t *refresh = properties ? obs_properties_get(properties, "refreshnocache") : nullptr;
	if (refresh)
		obs_property_button_clicked(refresh, source);
	if (properties)
		obs_properties_destroy(properties);
	SetStatus(QStringLiteral("Browser source refreshed"));
}

void TempestCommandMatrix::ApplySelectedMediaAction(const char *action)
{
	obs_source_t *source = SelectedInspectorSource();
	if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA))
		return;
	if (strcmp(action, "play") == 0)
		obs_source_media_play_pause(source, false);
	else if (strcmp(action, "pause") == 0)
		obs_source_media_play_pause(source, true);
	else if (strcmp(action, "restart") == 0)
		obs_source_media_restart(source);
	SetStatus(QStringLiteral("Media command // %1").arg(QString::fromUtf8(action).toUpper()));
}

void TempestCommandMatrix::ToggleSelectedMute()
{
	obs_source_t *source = SelectedInspectorSource();
	if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
		return;
	const bool muted = !obs_source_muted(source);
	obs_source_set_muted(source, muted);
	SetStatus(muted ? QStringLiteral("Source audio muted") : QStringLiteral("Source audio restored"));
	UpdateSourceInspector();
}

void TempestCommandMatrix::DuplicateSelectedSource()
{
	if (!main || !SelectedInspectorItem())
		return;
	QAction *copyAction = main->findChild<QAction *>(QStringLiteral("actionCopySource"));
	if (!copyAction || !copyAction->isEnabled())
		return;
	copyAction->trigger();
	QPointer<OBSBasic> guardedMain(main);
	QTimer::singleShot(0, this, [this, guardedMain]() {
		if (!guardedMain)
			return;
		QAction *pasteAction = guardedMain->findChild<QAction *>(QStringLiteral("actionPasteDup"));
		if (pasteAction && pasteAction->isEnabled()) {
			pasteAction->trigger();
			SetStatus(QStringLiteral("Source duplicated"));
		}
	});
}

void TempestCommandMatrix::RefreshLayoutConsole()
{
	if (!layoutConsolePanel || !layoutPosX || !layoutPosY || !layoutWidth || !layoutHeight || !layoutRotation ||
	    !layoutCropLeft || !layoutCropRight || !layoutCropTop || !layoutCropBottom)
		return;

	obs_sceneitem_t *item = SelectedInspectorItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	const bool editable = source && !obs_sceneitem_locked(item);
	layoutConsolePanel->setEnabled(editable);
	if (!source)
		return;

	obs_transform_info info;
	obs_sceneitem_crop crop;
	obs_sceneitem_get_info2(item, &info);
	obs_sceneitem_get_crop(item, &crop);
	const double sourceWidth = double(obs_source_get_width(source));
	const double sourceHeight = double(obs_source_get_height(source));
	const double width = sourceWidth * info.scale.x;
	const double height = sourceHeight * info.scale.y;

	layoutSyncing = true;
	layoutPosX->setValue(info.pos.x);
	layoutPosY->setValue(info.pos.y);
	layoutWidth->setValue(width);
	layoutHeight->setValue(height);
	layoutRotation->setValue(info.rot);
	layoutCropLeft->setValue(crop.left);
	layoutCropRight->setValue(crop.right);
	layoutCropTop->setValue(crop.top);
	layoutCropBottom->setValue(crop.bottom);
	if (std::abs(height) > 0.0001)
		layoutAspectRatio = width / height;
	else if (sourceHeight > 0.0)
		layoutAspectRatio = sourceWidth / sourceHeight;
	layoutSyncing = false;
}

void TempestCommandMatrix::RegisterTransformUndo(obs_scene_t *scene, obs_data_t *undoData, obs_data_t *redoData,
						 const QString &actionName)
{
	if (!main || !scene || !undoData || !redoData)
		return;
	const std::string undoJson(obs_data_get_json(undoData));
	const std::string redoJson(obs_data_get_json(redoData));
	if (undoJson == redoJson)
		return;
	auto restoreTransform = [](const std::string &json) {
		OBSDataAutoRelease state = obs_data_create_from_json(json.c_str());
		const char *sceneUuid = state ? obs_data_get_string(state, "scene_uuid") : nullptr;
		OBSSourceAutoRelease sceneSource = sceneUuid ? obs_get_source_by_uuid(sceneUuid) : nullptr;
		if (sceneSource && OBSBasic::Get())
			OBSBasic::Get()->SetCurrentScene(sceneSource.Get(), true);
		obs_scene_load_transform_states(json.c_str());
	};
	main->undo_s.add_action(actionName, restoreTransform, restoreTransform, undoJson, redoJson);
}

void TempestCommandMatrix::ApplyLayoutConsole()
{
	obs_sceneitem_t *item = SelectedInspectorItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	if (!item || !source || obs_sceneitem_locked(item) || !main)
		return;
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;

	OBSDataAutoRelease undoState = obs_scene_save_transform_states(scene, false);
	obs_transform_info info;
	obs_sceneitem_crop crop;
	obs_sceneitem_get_info2(item, &info);
	obs_sceneitem_get_crop(item, &crop);
	const uint32_t sourceWidth = obs_source_get_width(source);
	const uint32_t sourceHeight = obs_source_get_height(source);
	if (sourceWidth > 0)
		info.scale.x = float(layoutWidth->value() / double(sourceWidth));
	if (sourceHeight > 0)
		info.scale.y = float(layoutHeight->value() / double(sourceHeight));
	info.pos.x = float(layoutPosX->value());
	info.pos.y = float(layoutPosY->value());
	info.rot = float(layoutRotation->value());
	crop.left = uint32_t(layoutCropLeft->value());
	crop.right = uint32_t(layoutCropRight->value());
	crop.top = uint32_t(layoutCropTop->value());
	crop.bottom = uint32_t(layoutCropBottom->value());

	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_info2(item, &info);
	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_defer_update_end(item);
	obs_sceneitem_force_update_transform(item);
	OBSDataAutoRelease redoState = obs_scene_save_transform_states(scene, false);
	RegisterTransformUndo(
		scene, undoState, redoState,
		QStringLiteral("Tempest Layout // %1").arg(QString::fromUtf8(obs_source_get_name(source))));
	SetStatus(QStringLiteral("Precision transform applied"));
	RefreshLayoutConsole();
}

void TempestCommandMatrix::NudgeSelectedSource(float deltaX, float deltaY)
{
	obs_sceneitem_t *item = SelectedInspectorItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	if (!item || !source || obs_sceneitem_locked(item) || !main)
		return;
	OBSScene scene = main->GetCurrentScene();
	if (!scene)
		return;
	OBSDataAutoRelease undoState = obs_scene_save_transform_states(scene, false);
	vec2 position;
	obs_sceneitem_get_pos(item, &position);
	position.x += deltaX;
	position.y += deltaY;
	obs_sceneitem_set_pos(item, &position);
	OBSDataAutoRelease redoState = obs_scene_save_transform_states(scene, false);
	RegisterTransformUndo(
		scene, undoState, redoState,
		QStringLiteral("Tempest Nudge // %1").arg(QString::fromUtf8(obs_source_get_name(source))));
	RefreshLayoutConsole();
}

void TempestCommandMatrix::SnapSelectedSource(int horizontal, int vertical)
{
	obs_sceneitem_t *item = SelectedInspectorItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	if (!item || !source || obs_sceneitem_locked(item) || !main)
		return;
	OBSScene scene = main->GetCurrentScene();
	obs_video_info videoInfo = {};
	if (!scene || !obs_get_video_info(&videoInfo))
		return;

	OBSDataAutoRelease undoState = obs_scene_save_transform_states(scene, false);
	vec3 topLeft, bottomRight;
	GetSceneItemBox(item, topLeft, bottomRight);
	const float margin = float(std::clamp(layoutSafeMargin->value(), 0.0, 25.0) / 100.0);
	const float safeLeft = float(videoInfo.base_width) * margin;
	const float safeRight = float(videoInfo.base_width) * (1.0f - margin);
	const float safeTop = float(videoInfo.base_height) * margin;
	const float safeBottom = float(videoInfo.base_height) * (1.0f - margin);
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	if (horizontal < 0)
		offsetX = safeLeft - topLeft.x;
	else if (horizontal > 0)
		offsetX = safeRight - bottomRight.x;
	else
		offsetX = float(videoInfo.base_width) * 0.5f - (topLeft.x + bottomRight.x) * 0.5f;
	if (vertical < 0)
		offsetY = safeTop - topLeft.y;
	else if (vertical > 0)
		offsetY = safeBottom - bottomRight.y;
	else
		offsetY = float(videoInfo.base_height) * 0.5f - (topLeft.y + bottomRight.y) * 0.5f;

	vec2 position;
	obs_sceneitem_get_pos(item, &position);
	position.x += offsetX;
	position.y += offsetY;
	obs_sceneitem_set_pos(item, &position);
	OBSDataAutoRelease redoState = obs_scene_save_transform_states(scene, false);
	RegisterTransformUndo(scene, undoState, redoState,
			      QStringLiteral("Tempest Snap // %1").arg(QString::fromUtf8(obs_source_get_name(source))));
	SetStatus(QStringLiteral("Source snapped to safe-zone anchor"));
	RefreshLayoutConsole();
}

void TempestCommandMatrix::SaveLayoutSnapshot(const char *slot)
{
	if (!main)
		return;
	OBSScene scene = main->GetCurrentScene();
	obs_source_t *sceneSource = scene ? obs_scene_get_source(scene) : nullptr;
	const char *sceneUuid = sceneSource ? obs_source_get_uuid(sceneSource) : nullptr;
	if (!scene || !sceneUuid)
		return;
	OBSDataAutoRelease state = obs_scene_save_transform_states(scene, true);
	const QString key =
		QStringLiteral("LayoutSnapshot_%1_%2").arg(QString::fromUtf8(sceneUuid), QString::fromUtf8(slot));
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, key.toUtf8().constData(), obs_data_get_json(state));
	config_save_safe(config, "tmp", nullptr);
	SetStatus(QStringLiteral("Scene layout saved // SLOT %1").arg(QString::fromUtf8(slot)));
}

void TempestCommandMatrix::RecallLayoutSnapshot(const char *slot)
{
	if (!main)
		return;
	OBSScene scene = main->GetCurrentScene();
	obs_source_t *sceneSource = scene ? obs_scene_get_source(scene) : nullptr;
	const char *sceneUuid = sceneSource ? obs_source_get_uuid(sceneSource) : nullptr;
	if (!scene || !sceneUuid)
		return;
	const QString key =
		QStringLiteral("LayoutSnapshot_%1_%2").arg(QString::fromUtf8(sceneUuid), QString::fromUtf8(slot));
	config_t *config = App()->GetUserConfig();
	const char *savedState = config_get_string(config, ConfigSection, key.toUtf8().constData());
	if (!savedState || !*savedState) {
		SetStatus(QStringLiteral("No scene layout stored in slot %1").arg(QString::fromUtf8(slot)), true);
		return;
	}
	OBSDataAutoRelease undoState = obs_scene_save_transform_states(scene, true);
	obs_scene_load_transform_states(savedState);
	OBSDataAutoRelease redoState = obs_scene_save_transform_states(scene, true);
	RegisterTransformUndo(scene, undoState, redoState,
			      QStringLiteral("Tempest Scene Layout // Slot %1").arg(QString::fromUtf8(slot)));
	SetStatus(QStringLiteral("Scene layout recalled // SLOT %1").arg(QString::fromUtf8(slot)));
	RefreshLayoutConsole();
}

QString TempestCommandMatrix::SelectedReactionKey() const
{
	obs_sceneitem_t *item = SelectedInspectorItem();
	obs_scene_t *scene = item ? obs_sceneitem_get_scene(item) : nullptr;
	obs_source_t *sceneSource = scene ? obs_scene_get_source(scene) : nullptr;
	const char *sceneUuid = sceneSource ? obs_source_get_uuid(sceneSource) : nullptr;
	if (!item || !sceneUuid)
		return {};
	return QStringLiteral("%1:%2").arg(QString::fromUtf8(sceneUuid)).arg(obs_sceneitem_get_id(item));
}

obs_sceneitem_t *TempestCommandMatrix::FindReactionItem(const SourceReaction &reaction) const
{
	if (reaction.sceneUuid.isEmpty() || reaction.itemId <= 0)
		return nullptr;
	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(reaction.sceneUuid.toUtf8().constData());
	obs_scene_t *scene = sceneSource ? obs_group_or_scene_from_source(sceneSource) : nullptr;
	return scene ? obs_scene_find_sceneitem_by_id(scene, reaction.itemId) : nullptr;
}

void TempestCommandMatrix::LoadSourceReactions()
{
	const char *raw = config_get_string(App()->GetUserConfig(), ConfigSection, "SourceReactions");
	const QJsonDocument document = QJsonDocument::fromJson(raw ? QByteArray(raw) : QByteArray());
	if (!document.isArray())
		return;

	static const QStringList validSignals = {QStringLiteral("master"), QStringLiteral("desktop"),
						 QStringLiteral("microphone"), QStringLiteral("beat")};
	static const QStringList validCircuits = {QStringLiteral("core"), QStringLiteral("frame"),
						  QStringLiteral("chat"), QStringLiteral("plates"),
						  QStringLiteral("alerts")};
	for (const QJsonValue &value : document.array()) {
		const QJsonObject object = value.toObject();
		SourceReaction reaction;
		reaction.sceneUuid = object.value(QStringLiteral("sceneUuid")).toString();
		reaction.sourceUuid = object.value(QStringLiteral("sourceUuid")).toString();
		reaction.sourceName = object.value(QStringLiteral("sourceName")).toString();
		reaction.itemId = object.value(QStringLiteral("itemId")).toVariant().toLongLong();
		reaction.signal = object.value(QStringLiteral("signal")).toString(QStringLiteral("master"));
		reaction.preset = object.value(QStringLiteral("preset")).toString(QStringLiteral("custom"));
		reaction.circuit = object.value(QStringLiteral("circuit")).toString(QStringLiteral("core"));
		reaction.threshold = object.value(QStringLiteral("threshold")).toDouble(0.08);
		reaction.enabled = object.value(QStringLiteral("enabled")).toBool(true);
		if (!validSignals.contains(reaction.signal))
			reaction.signal = QStringLiteral("master");
		if (!validCircuits.contains(reaction.circuit))
			reaction.circuit = QStringLiteral("core");
		const QJsonObject effects = object.value(QStringLiteral("effects")).toObject();
		if (!effects.isEmpty()) {
			auto loadEffect = [&effects](const QString &name, bool &enabled, double &amount,
						     double defaultAmount) {
				const QJsonObject effect = effects.value(name).toObject();
				enabled = effect.value(QStringLiteral("enabled")).toBool(false);
				amount = effect.value(QStringLiteral("amount")).toDouble(defaultAmount);
			};
			loadEffect(QStringLiteral("scale"), reaction.scaleEnabled, reaction.scaleAmount, 12.0);
			loadEffect(QStringLiteral("lift"), reaction.liftEnabled, reaction.liftAmount, 24.0);
			loadEffect(QStringLiteral("sway"), reaction.swayEnabled, reaction.swayAmount, 18.0);
			loadEffect(QStringLiteral("rotate"), reaction.rotateEnabled, reaction.rotateAmount, 2.0);
			reaction.visibilityEnabled = effects.value(QStringLiteral("visibility"))
							     .toObject()
							     .value(QStringLiteral("enabled"))
							     .toBool();
		} else {
			const QString legacyEffect =
				object.value(QStringLiteral("effect")).toString(QStringLiteral("scale"));
			const double legacyAmount = object.value(QStringLiteral("amount")).toDouble(12.0);
			reaction.scaleEnabled = legacyEffect == QStringLiteral("scale");
			reaction.liftEnabled = legacyEffect == QStringLiteral("lift");
			reaction.swayEnabled = legacyEffect == QStringLiteral("sway");
			reaction.rotateEnabled = legacyEffect == QStringLiteral("rotate");
			reaction.visibilityEnabled = legacyEffect == QStringLiteral("visibility");
			if (reaction.scaleEnabled)
				reaction.scaleAmount = legacyAmount;
			else if (reaction.liftEnabled)
				reaction.liftAmount = legacyAmount;
			else if (reaction.swayEnabled)
				reaction.swayAmount = legacyAmount;
			else if (reaction.rotateEnabled)
				reaction.rotateAmount = legacyAmount;
		}
		const QJsonObject baseline = object.value(QStringLiteral("baseline")).toObject();
		if (!baseline.isEmpty()) {
			reaction.baseline.pos.x = float(baseline.value(QStringLiteral("x")).toDouble());
			reaction.baseline.pos.y = float(baseline.value(QStringLiteral("y")).toDouble());
			reaction.baseline.rot = float(baseline.value(QStringLiteral("rotation")).toDouble());
			reaction.baseline.scale.x = float(baseline.value(QStringLiteral("scaleX")).toDouble(1.0));
			reaction.baseline.scale.y = float(baseline.value(QStringLiteral("scaleY")).toDouble(1.0));
			reaction.baseline.alignment = uint32_t(baseline.value(QStringLiteral("alignment")).toInt());
			reaction.baseline.bounds_type =
				static_cast<obs_bounds_type>(baseline.value(QStringLiteral("boundsType")).toInt());
			reaction.baseline.bounds_alignment =
				uint32_t(baseline.value(QStringLiteral("boundsAlignment")).toInt());
			reaction.baseline.bounds.x = float(baseline.value(QStringLiteral("boundsX")).toDouble());
			reaction.baseline.bounds.y = float(baseline.value(QStringLiteral("boundsY")).toDouble());
			reaction.baseline.crop_to_bounds = baseline.value(QStringLiteral("cropToBounds")).toBool();
			reaction.baselineVisible = baseline.value(QStringLiteral("visible")).toBool(true);
			reaction.visibilityActive = reaction.baselineVisible;
			reaction.baselineCaptured = true;
		}
		if (!reaction.sceneUuid.isEmpty() && reaction.itemId > 0) {
			const QString key = QStringLiteral("%1:%2").arg(reaction.sceneUuid).arg(reaction.itemId);
			sourceReactions.insert(key, reaction);
		}
	}
}

void TempestCommandMatrix::SaveSourceReactions()
{
	QJsonArray array;
	QStringList keys = sourceReactions.keys();
	keys.sort(Qt::CaseInsensitive);
	for (const QString &key : keys) {
		const SourceReaction &reaction = sourceReactions[key];
		QJsonObject object;
		object.insert(QStringLiteral("sceneUuid"), reaction.sceneUuid);
		object.insert(QStringLiteral("sourceUuid"), reaction.sourceUuid);
		object.insert(QStringLiteral("sourceName"), reaction.sourceName);
		object.insert(QStringLiteral("itemId"), QString::number(reaction.itemId));
		object.insert(QStringLiteral("signal"), reaction.signal);
		object.insert(QStringLiteral("preset"), reaction.preset);
		object.insert(QStringLiteral("circuit"), reaction.circuit);
		object.insert(QStringLiteral("threshold"), reaction.threshold);
		object.insert(QStringLiteral("enabled"), reaction.enabled);
		QJsonObject effects;
		auto saveEffect = [&effects](const QString &name, bool enabled, double amount) {
			QJsonObject effect;
			effect.insert(QStringLiteral("enabled"), enabled);
			effect.insert(QStringLiteral("amount"), amount);
			effects.insert(name, effect);
		};
		saveEffect(QStringLiteral("scale"), reaction.scaleEnabled, reaction.scaleAmount);
		saveEffect(QStringLiteral("lift"), reaction.liftEnabled, reaction.liftAmount);
		saveEffect(QStringLiteral("sway"), reaction.swayEnabled, reaction.swayAmount);
		saveEffect(QStringLiteral("rotate"), reaction.rotateEnabled, reaction.rotateAmount);
		QJsonObject visibilityEffect;
		visibilityEffect.insert(QStringLiteral("enabled"), reaction.visibilityEnabled);
		effects.insert(QStringLiteral("visibility"), visibilityEffect);
		object.insert(QStringLiteral("effects"), effects);
		if (reaction.baselineCaptured) {
			QJsonObject baseline;
			baseline.insert(QStringLiteral("x"), reaction.baseline.pos.x);
			baseline.insert(QStringLiteral("y"), reaction.baseline.pos.y);
			baseline.insert(QStringLiteral("rotation"), reaction.baseline.rot);
			baseline.insert(QStringLiteral("scaleX"), reaction.baseline.scale.x);
			baseline.insert(QStringLiteral("scaleY"), reaction.baseline.scale.y);
			baseline.insert(QStringLiteral("alignment"), int(reaction.baseline.alignment));
			baseline.insert(QStringLiteral("boundsType"), int(reaction.baseline.bounds_type));
			baseline.insert(QStringLiteral("boundsAlignment"), int(reaction.baseline.bounds_alignment));
			baseline.insert(QStringLiteral("boundsX"), reaction.baseline.bounds.x);
			baseline.insert(QStringLiteral("boundsY"), reaction.baseline.bounds.y);
			baseline.insert(QStringLiteral("cropToBounds"), reaction.baseline.crop_to_bounds);
			baseline.insert(QStringLiteral("visible"), reaction.baselineVisible);
			object.insert(QStringLiteral("baseline"), baseline);
		}
		array.append(object);
	}
	const QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "SourceReactions", json.constData());
	config_save_safe(config, "tmp", nullptr);
}

void TempestCommandMatrix::RefreshReactionConsole()
{
	if (!reactionConsolePanel || !reactionEnabled || !reactionPreset || !reactionCircuit || !reactionSignal ||
	    !reactionScaleEnabled || !reactionScaleAmount || !reactionLiftEnabled || !reactionLiftAmount ||
	    !reactionSwayEnabled || !reactionSwayAmount || !reactionRotateEnabled || !reactionRotateAmount ||
	    !reactionVisibilityEnabled || !reactionThreshold || !reactionStatusLabel)
		return;

	const QString key = SelectedReactionKey();
	const auto found = sourceReactions.constFind(key);
	reactionSyncing = true;
	if (found == sourceReactions.cend()) {
		reactionEnabled->setChecked(true);
		reactionPreset->setCurrentIndex(0);
		reactionCircuit->setCurrentIndex(0);
		reactionSignal->setCurrentIndex(0);
		reactionThreshold->setValue(0.08);
		reactionScaleEnabled->setChecked(true);
		reactionScaleAmount->setValue(12.0);
		reactionLiftEnabled->setChecked(false);
		reactionLiftAmount->setValue(24.0);
		reactionSwayEnabled->setChecked(false);
		reactionSwayAmount->setValue(18.0);
		reactionRotateEnabled->setChecked(false);
		reactionRotateAmount->setValue(2.0);
		reactionVisibilityEnabled->setChecked(false);
		reactionStatusLabel->setText(signalReactor ? QStringLiteral("NO BINDING // SELECT SETTINGS AND APPLY")
							   : QStringLiteral("AUDIO REACTOR LINK PENDING"));
	} else {
		const SourceReaction &reaction = found.value();
		reactionEnabled->setChecked(reaction.enabled);
		SetComboData(reactionPreset, reaction.preset);
		SetComboData(reactionCircuit, reaction.circuit);
		SetComboData(reactionSignal, reaction.signal);
		reactionThreshold->setValue(reaction.threshold);
		reactionScaleEnabled->setChecked(reaction.scaleEnabled);
		reactionScaleAmount->setValue(reaction.scaleAmount);
		reactionLiftEnabled->setChecked(reaction.liftEnabled);
		reactionLiftAmount->setValue(reaction.liftAmount);
		reactionSwayEnabled->setChecked(reaction.swayEnabled);
		reactionSwayAmount->setValue(reaction.swayAmount);
		reactionRotateEnabled->setChecked(reaction.rotateEnabled);
		reactionRotateAmount->setValue(reaction.rotateAmount);
		reactionVisibilityEnabled->setChecked(reaction.visibilityEnabled);
		reactionStatusLabel->setText(
			QStringLiteral("BOUND // %1 CIRCUIT // %2 // %3 MODULATOR%4 // BASE %5")
				.arg(reaction.circuit.toUpper())
				.arg(reaction.signal.toUpper())
				.arg(ReactionEffectCount(reaction))
				.arg(ReactionEffectCount(reaction) == 1 ? QString() : QStringLiteral("S"))
				.arg(reaction.baselineCaptured ? QStringLiteral("CAPTURED")
							       : QStringLiteral("PENDING")));
	}
	reactionSyncing = false;
}

void TempestCommandMatrix::ApplyReactionPreset(int index)
{
	if (reactionSyncing || !reactionPreset || index < 0)
		return;
	const QString preset = reactionPreset->itemData(index).toString();
	if (preset == QStringLiteral("custom"))
		return;

	reactionSyncing = true;
	auto configure = [this](const QString &signal, double threshold, bool scale, double scaleAmount, bool lift,
				bool sway, double swayAmount, bool rotate, double rotateAmount, bool visibility,
				double liftAmount) {
		SetComboData(reactionSignal, signal);
		reactionThreshold->setValue(threshold);
		reactionScaleEnabled->setChecked(scale);
		reactionScaleAmount->setValue(scaleAmount);
		reactionLiftEnabled->setChecked(lift);
		reactionLiftAmount->setValue(liftAmount);
		reactionSwayEnabled->setChecked(sway);
		reactionSwayAmount->setValue(swayAmount);
		reactionRotateEnabled->setChecked(rotate);
		reactionRotateAmount->setValue(rotateAmount);
		reactionVisibilityEnabled->setChecked(visibility);
	};
	if (preset == QStringLiteral("mainframe-surge"))
		configure(QStringLiteral("master"), 0.08, true, 6.0, true, true, 8.0, true, 1.2, false, 18.0);
	else if (preset == QStringLiteral("beat-lock"))
		configure(QStringLiteral("beat"), 0.10, true, 10.0, true, false, 18.0, true, 2.0, false, 12.0);
	else if (preset == QStringLiteral("voice-relay"))
		configure(QStringLiteral("microphone"), 0.06, true, 3.5, true, true, 6.0, false, 2.0, false, 16.0);
	else if (preset == QStringLiteral("fractal-drift"))
		configure(QStringLiteral("desktop"), 0.04, true, 4.0, true, true, 24.0, true, 2.5, false, 10.0);
	else if (preset == QStringLiteral("ghost-signal"))
		configure(QStringLiteral("beat"), 0.16, true, 8.0, true, false, 18.0, true, 0.8, true, 8.0);
	reactionSyncing = false;
	SetStatus(QStringLiteral("Motion rig loaded // %1 // apply to bind").arg(reactionPreset->currentText()));
}

void TempestCommandMatrix::MarkReactionPresetCustom()
{
	if (reactionSyncing || !reactionPreset || reactionPreset->currentData().toString() == QStringLiteral("custom"))
		return;
	QSignalBlocker blocker(reactionPreset);
	reactionPreset->setCurrentIndex(0);
}

int TempestCommandMatrix::ReactionEffectCount(const SourceReaction &reaction) const
{
	return int(reaction.scaleEnabled) + int(reaction.liftEnabled) + int(reaction.swayEnabled) +
	       int(reaction.rotateEnabled) + int(reaction.visibilityEnabled);
}

void TempestCommandMatrix::ApplyReactionBinding()
{
	if (reactionSyncing)
		return;
	obs_sceneitem_t *item = SelectedInspectorItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	obs_scene_t *scene = item ? obs_sceneitem_get_scene(item) : nullptr;
	obs_source_t *sceneSource = scene ? obs_scene_get_source(scene) : nullptr;
	const char *sceneUuid = sceneSource ? obs_source_get_uuid(sceneSource) : nullptr;
	const QString key = SelectedReactionKey();
	if (!item || !source || !sceneUuid || key.isEmpty())
		return;

	auto existing = sourceReactions.find(key);
	if (existing != sourceReactions.end())
		RestoreReaction(existing.value());
	SourceReaction reaction;
	reaction.sceneUuid = QString::fromUtf8(sceneUuid);
	const char *sourceUuid = obs_source_get_uuid(source);
	const char *sourceName = obs_source_get_name(source);
	reaction.sourceUuid = sourceUuid ? QString::fromUtf8(sourceUuid) : QString();
	reaction.sourceName = sourceName ? QString::fromUtf8(sourceName) : QStringLiteral("Unnamed source");
	reaction.itemId = obs_sceneitem_get_id(item);
	reaction.signal = reactionSignal->currentData().toString();
	reaction.preset = reactionPreset->currentData().toString();
	reaction.circuit = reactionCircuit->currentData().toString();
	reaction.threshold = reactionThreshold->value();
	reaction.scaleEnabled = reactionScaleEnabled->isChecked();
	reaction.scaleAmount = reactionScaleAmount->value();
	reaction.liftEnabled = reactionLiftEnabled->isChecked();
	reaction.liftAmount = reactionLiftAmount->value();
	reaction.swayEnabled = reactionSwayEnabled->isChecked();
	reaction.swayAmount = reactionSwayAmount->value();
	reaction.rotateEnabled = reactionRotateEnabled->isChecked();
	reaction.rotateAmount = reactionRotateAmount->value();
	reaction.visibilityEnabled = reactionVisibilityEnabled->isChecked();
	reaction.enabled = reactionEnabled->isChecked();
	if (ReactionEffectCount(reaction) == 0) {
		SetStatus(QStringLiteral("Enable at least one motion rig modulator"), true);
		return;
	}
	obs_sceneitem_get_info2(item, &reaction.baseline);
	reaction.baselineVisible = obs_sceneitem_visible(item);
	reaction.visibilityActive = reaction.baselineVisible;
	reaction.baselineCaptured = true;
	sourceReactions.insert(key, reaction);
	SaveSourceReactions();
	UpdateReactionNetworkSummary();
	SetStatus(QStringLiteral("Reactive rig saved // %1 circuit // %2 // %3 modulator%4")
			  .arg(reaction.circuit.toUpper())
			  .arg(reaction.signal.toUpper())
			  .arg(ReactionEffectCount(reaction))
			  .arg(ReactionEffectCount(reaction) == 1 ? QString() : QStringLiteral("s")));
	RefreshReactionConsole();
}

void TempestCommandMatrix::CaptureReactionBaseline()
{
	const QString key = SelectedReactionKey();
	auto found = sourceReactions.find(key);
	obs_sceneitem_t *item = SelectedInspectorItem();
	if (found == sourceReactions.end() || !item) {
		SetStatus(QStringLiteral("Apply a reaction binding before capturing its base"), true);
		return;
	}
	if (found->enabled) {
		SetStatus(QStringLiteral("Disable and apply the binding before capturing a new base"), true);
		return;
	}
	obs_sceneitem_get_info2(item, &found->baseline);
	found->baselineVisible = obs_sceneitem_visible(item);
	found->visibilityActive = found->baselineVisible;
	found->baselineCaptured = true;
	found->runtimeTransformApplied = false;
	found->runtimeVisibilityApplied = false;
	SaveSourceReactions();
	SetStatus(QStringLiteral("Reaction base captured // %1").arg(found->sourceName));
	RefreshReactionConsole();
}

void TempestCommandMatrix::TestReactionBinding()
{
	const QString key = SelectedReactionKey();
	if (!sourceReactions.contains(key)) {
		SetStatus(QStringLiteral("Apply a reaction binding before testing it"), true);
		return;
	}
	reactionTestKey = key;
	reactionTestUntil = QDateTime::currentMSecsSinceEpoch() + 1200;
	ApplyReactionLevels(0.0f, 0.0f, 0.0f, 0.0f);
	QTimer::singleShot(1250, this, [this]() { ApplyReactionLevels(0.0f, 0.0f, 0.0f, 0.0f); });
	SetStatus(QStringLiteral("Reaction test pulse sent"));
}

void TempestCommandMatrix::RemoveReactionBinding()
{
	const QString key = SelectedReactionKey();
	auto found = sourceReactions.find(key);
	if (found == sourceReactions.end()) {
		SetStatus(QStringLiteral("Selected source has no reaction binding"), true);
		return;
	}
	RestoreReaction(found.value());
	const QString sourceName = found->sourceName;
	sourceReactions.erase(found);
	SaveSourceReactions();
	UpdateReactionNetworkSummary();
	SetStatus(QStringLiteral("Reactive binding removed // %1").arg(sourceName));
	RefreshReactionConsole();
	RefreshLayoutConsole();
}

void TempestCommandMatrix::UpdateReactionNetworkSummary()
{
	if (!signalReactor)
		return;
	QHash<QString, int> circuitTotals;
	QHash<QString, int> circuitScoped;
	QHash<QString, int> circuitEnabled;
	for (const QString &circuit : {QStringLiteral("core"), QStringLiteral("frame"), QStringLiteral("chat"),
				       QStringLiteral("plates"), QStringLiteral("alerts")}) {
		circuitTotals.insert(circuit, 0);
		circuitScoped.insert(circuit, 0);
		circuitEnabled.insert(circuit, 0);
	}
	int enabled = 0;
	int active = 0;
	int activeEnabled = 0;
	for (auto it = sourceReactions.cbegin(); it != sourceReactions.cend(); ++it) {
		const bool inScope = !reactionNetworkActiveSceneOnly || it->sceneUuid == reactionActiveSceneUuid;
		if (circuitTotals.contains(it->circuit)) {
			++circuitTotals[it->circuit];
			if (inScope) {
				++circuitScoped[it->circuit];
				if (it->enabled)
					++circuitEnabled[it->circuit];
			}
		}
		if (it->enabled)
			++enabled;
		if (it->sceneUuid == reactionActiveSceneUuid) {
			++active;
			if (it->enabled && ReactionCircuitActive(it->circuit) && ReactionCircuitGain(it->circuit) > 0.0)
				++activeEnabled;
		}
	}
	signalReactor->SetSourceBindingSummary(sourceReactions.size(), enabled, active, activeEnabled);
	for (auto it = circuitTotals.cbegin(); it != circuitTotals.cend(); ++it)
		signalReactor->SetSourceCircuitSummary(it.key(), it.value(), circuitScoped.value(it.key()),
						       circuitEnabled.value(it.key()));
}

void TempestCommandMatrix::SetReactionActiveScene(const QString &sceneUuid)
{
	const bool changed = reactionActiveSceneUuid != sceneUuid;
	reactionActiveSceneUuid = sceneUuid;
	if (!changed)
		return;
	if (reactionNetworkActiveSceneOnly) {
		for (auto it = sourceReactions.begin(); it != sourceReactions.end(); ++it) {
			if (it->sceneUuid != reactionActiveSceneUuid)
				RestoreReaction(it.value());
		}
	}
	UpdateReactionNetworkSummary();
}

bool TempestCommandMatrix::ReactionCircuitActive(const QString &circuit) const
{
	if (!reactionSoloCircuit.isEmpty())
		return circuit == reactionSoloCircuit;
	if (reactionCircuitProfile == QStringLiteral("all"))
		return true;
	if (reactionCircuitProfile == QStringLiteral("core"))
		return circuit == QStringLiteral("core");
	if (reactionCircuitProfile == QStringLiteral("ambient"))
		return circuit == QStringLiteral("core") || circuit == QStringLiteral("frame") ||
		       circuit == QStringLiteral("plates");
	if (reactionCircuitProfile == QStringLiteral("conversation"))
		return circuit == QStringLiteral("core") || circuit == QStringLiteral("frame") ||
		       circuit == QStringLiteral("chat") || circuit == QStringLiteral("plates");
	if (reactionCircuitProfile == QStringLiteral("alert"))
		return circuit == QStringLiteral("core") || circuit == QStringLiteral("frame") ||
		       circuit == QStringLiteral("alerts");
	return true;
}

double TempestCommandMatrix::ReactionCircuitGain(const QString &circuit) const
{
	return std::clamp(reactionCircuitGains.value(circuit, 1.0), 0.0, 2.0);
}

void TempestCommandMatrix::PublishReactionCircuitActivity(const QHash<QString, float> &activity)
{
	if (!signalReactor)
		return;
	for (const QString &circuit : {QStringLiteral("core"), QStringLiteral("frame"), QStringLiteral("chat"),
				       QStringLiteral("plates"), QStringLiteral("alerts")})
		signalReactor->SetSourceCircuitActivity(circuit, activity.value(circuit));
}

void TempestCommandMatrix::TestReactionNetwork()
{
	int enabled = 0;
	for (auto it = sourceReactions.cbegin(); it != sourceReactions.cend(); ++it) {
		if (it->enabled && ReactionCircuitActive(it->circuit) && ReactionCircuitGain(it->circuit) > 0.0 &&
		    (!reactionNetworkActiveSceneOnly || it->sceneUuid == reactionActiveSceneUuid))
			++enabled;
	}
	if (enabled == 0) {
		SetStatus(QStringLiteral("No enabled source reaction rigs to test"), true);
		return;
	}
	reactionNetworkTestCircuit.clear();
	reactionNetworkTestUntil = QDateTime::currentMSecsSinceEpoch() + 1200;
	ApplyReactionLevels(0.0f, 0.0f, 0.0f, 0.0f);
	QTimer::singleShot(1250, this, [this]() { ApplyReactionLevels(0.0f, 0.0f, 0.0f, 0.0f); });
	SetStatus(QStringLiteral("Reaction network test // %1 rig%2")
			  .arg(enabled)
			  .arg(enabled == 1 ? QString() : QStringLiteral("s")));
}

void TempestCommandMatrix::TestReactionCircuit(const QString &circuit)
{
	int enabled = 0;
	for (auto it = sourceReactions.cbegin(); it != sourceReactions.cend(); ++it) {
		if (it->enabled && it->circuit == circuit &&
		    (!reactionNetworkActiveSceneOnly || it->sceneUuid == reactionActiveSceneUuid))
			++enabled;
	}
	if (enabled == 0) {
		SetStatus(QStringLiteral("No enabled %1 circuit rigs to test").arg(circuit.toUpper()), true);
		return;
	}
	reactionNetworkTestCircuit = circuit;
	reactionNetworkTestUntil = QDateTime::currentMSecsSinceEpoch() + 1200;
	ApplyReactionLevels(0.0f, 0.0f, 0.0f, 0.0f);
	QTimer::singleShot(1250, this, [this]() { ApplyReactionLevels(0.0f, 0.0f, 0.0f, 0.0f); });
	SetStatus(QStringLiteral("%1 circuit test // %2 rig%3")
			  .arg(circuit.toUpper())
			  .arg(enabled)
			  .arg(enabled == 1 ? QString() : QStringLiteral("s")));
}

void TempestCommandMatrix::ApplyReactionLevels(float master, float desktop, float microphone, float beat)
{
	reactionPhase = std::fmod(reactionPhase + 0.28, 6.283185307179586);
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const bool testingNetwork = now < reactionNetworkTestUntil;
	const bool externalEventActive = now < reactionExternalEventUntil;
	QHash<QString, float> circuitActivity;
	bool capturedBaseline = false;
	for (auto it = sourceReactions.begin(); it != sourceReactions.end(); ++it) {
		SourceReaction &reaction = it.value();
		obs_sceneitem_t *item = FindReactionItem(reaction);
		if (!item)
			continue;
		if (!reaction.baselineCaptured) {
			obs_sceneitem_get_info2(item, &reaction.baseline);
			reaction.baselineVisible = obs_sceneitem_visible(item);
			reaction.visibilityActive = reaction.baselineVisible;
			reaction.baselineCaptured = true;
			capturedBaseline = true;
		}
		const bool testingSelected = it.key() == reactionTestKey && now < reactionTestUntil;
		const bool testingTargetedCircuit = testingNetwork && !reactionNetworkTestCircuit.isEmpty() &&
						    reaction.circuit == reactionNetworkTestCircuit;
		const bool testingNetworkRig = testingNetwork &&
					       (reactionNetworkTestCircuit.isEmpty() || testingTargetedCircuit);
		const bool externalEventTargets = externalEventActive &&
						  (reactionExternalEventCircuit.isEmpty() ||
						   reactionExternalEventCircuit == QStringLiteral("all") ||
						   reaction.circuit == reactionExternalEventCircuit);
		if (reactionNetworkActiveSceneOnly && reaction.sceneUuid != reactionActiveSceneUuid &&
		    !testingSelected) {
			RestoreReaction(reaction);
			continue;
		}
		if (!ReactionCircuitActive(reaction.circuit) && !testingSelected && !testingTargetedCircuit &&
		    !externalEventTargets) {
			RestoreReaction(reaction);
			continue;
		}
		if (ReactionCircuitGain(reaction.circuit) <= 0.0 && !testingSelected && !testingTargetedCircuit) {
			RestoreReaction(reaction);
			continue;
		}
		if (!reaction.enabled && !testingSelected) {
			RestoreReaction(reaction);
			continue;
		}
		if (!reactionNetworkArmed && !testingSelected && !testingNetworkRig && !externalEventTargets) {
			RestoreReaction(reaction);
			continue;
		}
		float level = master;
		if (reaction.signal == QStringLiteral("desktop"))
			level = desktop;
		else if (reaction.signal == QStringLiteral("microphone"))
			level = microphone;
		else if (reaction.signal == QStringLiteral("beat"))
			level = beat;
		if (externalEventTargets)
			level = std::max(level, reactionExternalEventStrength);
		if (testingSelected || testingNetworkRig)
			level = 1.0f;
		level = std::clamp(level, 0.0f, 1.5f);
		const float circuitGain =
			testingSelected || testingTargetedCircuit ? 1.0f : float(ReactionCircuitGain(reaction.circuit));
		const float networkIntensity = float(std::clamp(reactionNetworkIntensity, 0.0, 2.0)) * circuitGain;

		if (reaction.visibilityEnabled) {
			const float onThreshold = float(reaction.threshold);
			const float offThreshold = onThreshold * 0.75f;
			const float visibilityLevel = level * networkIntensity;
			if (reaction.visibilityActive)
				reaction.visibilityActive = visibilityLevel >= offThreshold;
			else
				reaction.visibilityActive = visibilityLevel >= onThreshold;
			const bool visible = reaction.baselineVisible && reaction.visibilityActive;
			if (obs_sceneitem_visible(item) != visible)
				obs_sceneitem_set_visible(item, visible);
			reaction.runtimeVisibilityApplied = true;
			if (reaction.visibilityActive)
				circuitActivity[reaction.circuit] = std::max(circuitActivity.value(reaction.circuit),
									     std::clamp(visibilityLevel, 0.0f, 2.0f));
		}

		const double denominator = std::max(0.001, 1.0 - reaction.threshold);
		const float response = float(std::clamp((double(level) - reaction.threshold) / denominator, 0.0, 1.0)) *
				       networkIntensity;
		circuitActivity[reaction.circuit] =
			std::max(circuitActivity.value(reaction.circuit), std::clamp(response, 0.0f, 2.0f));
		if (response <= 0.0f) {
			if (reaction.runtimeTransformApplied)
				obs_sceneitem_set_info2(item, &reaction.baseline);
			reaction.runtimeTransformApplied = false;
			continue;
		}
		obs_transform_info transformed = reaction.baseline;
		if (reaction.scaleEnabled) {
			const float multiplier = 1.0f + float(reaction.scaleAmount / 100.0) * response;
			transformed.scale.x *= multiplier;
			transformed.scale.y *= multiplier;
		}
		if (reaction.liftEnabled)
			transformed.pos.y -= float(reaction.liftAmount) * response;
		if (reaction.swayEnabled)
			transformed.pos.x += float(std::sin(reactionPhase) * reaction.swayAmount * response);
		if (reaction.rotateEnabled)
			transformed.rot += float(std::sin(reactionPhase) * reaction.rotateAmount * response);
		if (reaction.scaleEnabled || reaction.liftEnabled || reaction.swayEnabled || reaction.rotateEnabled) {
			obs_sceneitem_set_info2(item, &transformed);
			reaction.runtimeTransformApplied = true;
		}
	}
	if (now >= reactionTestUntil)
		reactionTestKey.clear();
	if (now >= reactionNetworkTestUntil) {
		reactionNetworkTestUntil = 0;
		reactionNetworkTestCircuit.clear();
	}
	PublishReactionCircuitActivity(circuitActivity);
	if (capturedBaseline)
		SaveSourceReactions();
}

void TempestCommandMatrix::RestoreReaction(SourceReaction &reaction)
{
	if ((!reaction.runtimeTransformApplied && !reaction.runtimeVisibilityApplied) || !reaction.baselineCaptured)
		return;
	obs_sceneitem_t *item = FindReactionItem(reaction);
	if (item) {
		if (reaction.runtimeTransformApplied)
			obs_sceneitem_set_info2(item, &reaction.baseline);
		if (reaction.runtimeVisibilityApplied && obs_sceneitem_visible(item) != reaction.baselineVisible)
			obs_sceneitem_set_visible(item, reaction.baselineVisible);
	}
	reaction.visibilityActive = reaction.baselineVisible;
	reaction.runtimeTransformApplied = false;
	reaction.runtimeVisibilityApplied = false;
}

void TempestCommandMatrix::RestoreAllReactions()
{
	for (auto it = sourceReactions.begin(); it != sourceReactions.end(); ++it)
		RestoreReaction(it.value());
}

void TempestCommandMatrix::RelayoutRoutingGrids()
{
	if (!sceneGrid || !protocolGrid)
		return;
	const int availableWidth = std::max(1, basicViewPage ? basicViewPage->width() : width());
	const int preferredButtonWidth = std::max(100, qRound(145.0 * ContentScalePercent() / 100.0));
	const int columns = std::clamp(availableWidth / preferredButtonWidth, 1, 4);
	if (columns == routingColumnCount)
		return;
	routingColumnCount = columns;

	while (QLayoutItem *item = sceneGrid->takeAt(0))
		delete item;
	for (int index = 0; index < currentScenes.size(); ++index) {
		if (QPushButton *button = sceneButtons.value(currentScenes[index].uuid))
			sceneGrid->addWidget(button, index / columns, index % columns);
	}
	if (emptySceneLabel)
		sceneGrid->addWidget(emptySceneLabel, 0, 0, 1, columns);

	while (QLayoutItem *item = protocolGrid->takeAt(0))
		delete item;
	for (int index = 0; index < protocols.size(); ++index)
		protocolGrid->addWidget(protocols[index].button, index / columns, index % columns);
	for (int column = 0; column < 4; ++column) {
		sceneGrid->setColumnStretch(column, column < columns ? 1 : 0);
		protocolGrid->setColumnStretch(column, column < columns ? 1 : 0);
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
	ApplyMediaAction(config.mediaSourceUuid, config.mediaAction);
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

void TempestCommandMatrix::CompleteProtocolRoute(const QString &protocolId, const QString &sceneUuid, quint64 revision,
						 bool launchFailed)
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
	if (hudComposer)
		hudComposer->ApplyProtocolVisibility(sceneSource, protocol->id);
	main->SetCurrentScene(OBSSource(sceneSource.Get()));
	SetReactionActiveScene(sceneUuid);
	const ProtocolActionConfig config = actionConfigs.value(protocolId);
	ApplyReactionNetworkAction(config);
	SetStatus(QStringLiteral("%1 AUTOMATION // %2%3")
			  .arg(protocol->label, obs_source_get_name(sceneSource),
			       launchFailed ? QStringLiteral(" // PROGRAM FAILED") : QString()),
		  launchFailed);
	OBSDataAutoRelease eventData = obs_data_create();
	obs_data_set_string(eventData, "protocol", protocolId.toUtf8().constData());
	obs_data_set_string(eventData, "sceneUuid", sceneUuid.toUtf8().constData());
	obs_data_set_string(eventData, "sceneName", obs_source_get_name(sceneSource));
	obs_data_set_bool(eventData, "programLaunchFailed", launchFailed);
	obs_data_set_string(eventData, "reactionNetworkAction", config.reactionNetworkAction.toUtf8().constData());
	obs_data_set_string(eventData, "reactionScopeAction", config.reactionScopeAction.toUtf8().constData());
	obs_data_set_string(eventData, "reactionCircuitProfile", config.reactionCircuitProfile.toUtf8().constData());
	obs_data_set_bool(eventData, "reactionIntensityApplied", config.reactionIntensityEnabled);
	obs_data_set_int(eventData, "reactionIntensity", config.reactionIntensity);
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

void TempestCommandMatrix::ApplyMediaAction(const QString &sourceUuid, const QString &action)
{
	TempestMediaBay::ApplyMediaAction(sourceUuid, action);
}

void TempestCommandMatrix::ApplyRecordingAction(const QString &action)
{
	if (action == QStringLiteral("start") && !main->RecordingActive())
		main->StartRecording();
	else if (action == QStringLiteral("stop") && main->RecordingActive())
		main->StopRecording();
}

void TempestCommandMatrix::ApplyReactionNetworkAction(const ProtocolActionConfig &config)
{
	if (!signalReactor)
		return;
	if (config.reactionScopeAction == QStringLiteral("active"))
		signalReactor->SetSourceNetworkActiveSceneOnly(true);
	else if (config.reactionScopeAction == QStringLiteral("all"))
		signalReactor->SetSourceNetworkActiveSceneOnly(false);
	if (config.reactionCircuitProfile != QStringLiteral("keep"))
		signalReactor->SetSourceNetworkCircuitProfile(config.reactionCircuitProfile);
	if (config.reactionIntensityEnabled)
		signalReactor->SetSourceNetworkIntensity(float(config.reactionIntensity) / 100.0f);
	if (config.reactionNetworkAction == QStringLiteral("arm")) {
		signalReactor->SetSourceNetworkArmed(true);
	} else if (config.reactionNetworkAction == QStringLiteral("disarm")) {
		signalReactor->DisarmAndRestoreSourceNetwork();
	} else if (config.reactionNetworkAction == QStringLiteral("test")) {
		signalReactor->TestSourceNetwork();
	} else if (config.reactionNetworkAction == QStringLiteral("arm-test")) {
		signalReactor->SetSourceNetworkArmed(true);
		signalReactor->TestSourceNetwork();
	}
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

QVector<TempestCommandMatrix::SourceInfo> TempestCommandMatrix::EnumerateMediaSources() const
{
	QVector<SourceInfo> sources;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA))
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
		actions.mediaSourceUuid = stringValue("MediaSourceUuid");
		actions.mediaAction = stringValue("MediaAction");
		if (actions.mediaAction.isEmpty())
			actions.mediaAction = QStringLiteral("keep");
		actions.recordingAction = stringValue("RecordingAction");
		if (actions.recordingAction.isEmpty())
			actions.recordingAction = QStringLiteral("keep");
		actions.reactionNetworkAction = stringValue("ReactionNetworkAction");
		static const QStringList validNetworkActions = {QStringLiteral("keep"), QStringLiteral("arm"),
								QStringLiteral("disarm"), QStringLiteral("test"),
								QStringLiteral("arm-test")};
		if (!validNetworkActions.contains(actions.reactionNetworkAction))
			actions.reactionNetworkAction = QStringLiteral("keep");
		actions.reactionScopeAction = stringValue("ReactionScopeAction");
		static const QStringList validScopeActions = {QStringLiteral("keep"), QStringLiteral("active"),
							      QStringLiteral("all")};
		if (!validScopeActions.contains(actions.reactionScopeAction))
			actions.reactionScopeAction = QStringLiteral("keep");
		actions.reactionCircuitProfile = stringValue("ReactionCircuitProfile");
		static const QStringList validCircuitProfiles = {
			QStringLiteral("keep"),         QStringLiteral("all"),   QStringLiteral("ambient"),
			QStringLiteral("conversation"), QStringLiteral("alert"), QStringLiteral("core")};
		if (!validCircuitProfiles.contains(actions.reactionCircuitProfile))
			actions.reactionCircuitProfile = QStringLiteral("keep");
		const QByteArray reactionIntensityEnabledKey =
			ActionConfigKey(protocol.id, "ReactionIntensityEnabled").toUtf8();
		actions.reactionIntensityEnabled =
			config_get_bool(config, ConfigSection, reactionIntensityEnabledKey.constData());
		actions.reactionIntensity = std::clamp(intValue("ReactionIntensity", 100), 0, 200);
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
		setString("MediaSourceUuid", actions.mediaSourceUuid);
		setString("MediaAction", actions.mediaAction);
		setString("RecordingAction", actions.recordingAction);
		setString("ReactionNetworkAction", actions.reactionNetworkAction);
		setString("ReactionScopeAction", actions.reactionScopeAction);
		setString("ReactionCircuitProfile", actions.reactionCircuitProfile);
		const QByteArray reactionIntensityEnabledKey =
			ActionConfigKey(protocol.id, "ReactionIntensityEnabled").toUtf8();
		config_set_bool(config, ConfigSection, reactionIntensityEnabledKey.constData(),
				actions.reactionIntensityEnabled);
		setInt("ReactionIntensity", actions.reactionIntensity);
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
		QComboBox *mediaSource = nullptr;
		QComboBox *mediaAction = nullptr;
		QComboBox *recordingAction = nullptr;
		QComboBox *reactionNetworkAction = nullptr;
		QComboBox *reactionScopeAction = nullptr;
		QComboBox *reactionCircuitProfile = nullptr;
		QCheckBox *reactionIntensityEnabled = nullptr;
		QSpinBox *reactionIntensity = nullptr;
		QCheckBox *launchEnabled = nullptr;
		QLineEdit *programPath = nullptr;
		QLineEdit *programArguments = nullptr;
	};

	const QVector<SourceInfo> audioSources = EnumerateAudioSources();
	const QVector<SourceInfo> mediaSources = EnumerateMediaSources();
	const QVector<SourceInfo> transitions = EnumerateTransitions();
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("tempestProtocolActionEditor"));
	dialog.setWindowTitle(QStringLiteral("Tempest Stream Automation"));
	dialog.resize(720, 840);
	dialog.setStyleSheet(QStringLiteral(R"(
		QDialog { background: #07131e; color: #bdf6ff; }
		QTabWidget::pane { border: 1px solid #1f506d; }
		QTabBar::tab { background: #0d2230; color: #748fa4; padding: 9px 18px; border: 1px solid #1f506d; }
		QTabBar::tab:selected { color: #bdf6ff; background: #073c5f; border-color: #45d9ff; }
		QGroupBox { border: 1px solid #1f506d; margin-top: 12px; padding-top: 10px; color: #45d9ff; font-weight: 700; }
		QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
		QScrollArea { border: none; background: #07131e; }
		QScrollArea > QWidget > QWidget { background: #07131e; }
		QLabel, QCheckBox { color: #9eb7c8; }
		QComboBox, QSpinBox, QLineEdit { min-height: 28px; background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; padding: 0 6px; }
		QPushButton { min-height: 30px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; padding: 0 12px; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
	)"));

	auto *dialogLayout = new QVBoxLayout(&dialog);
	auto *intro =
		new QLabel(QStringLiteral("Each protocol can prepare the workstation, then route its assigned scene. "
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
		auto *page = new QScrollArea(tabs);
		page->setWidgetResizable(true);
		page->setFrameShape(QFrame::NoFrame);
		auto *pageContents = new QWidget(page);
		auto *pageLayout = new QVBoxLayout(pageContents);

		auto *routeGroup = new QGroupBox(QStringLiteral("ROUTING SEQUENCE"), pageContents);
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
		auto *audioGroup = new QGroupBox(QStringLiteral("AUDIO SOURCE STATES"), pageContents);
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

		auto *mediaGroup = new QGroupBox(QStringLiteral("MEDIA ACTION"), pageContents);
		auto *mediaGrid = new QGridLayout(mediaGroup);
		mediaGrid->addWidget(new QLabel(QStringLiteral("Source"), mediaGroup), 0, 0);
		mediaGrid->addWidget(new QLabel(QStringLiteral("Action"), mediaGroup), 0, 1);
		fields.mediaSource = new QComboBox(mediaGroup);
		fields.mediaAction = new QComboBox(mediaGroup);
		fields.mediaSource->addItem(QStringLiteral("No source selected"), QString());
		for (const SourceInfo &source : mediaSources)
			fields.mediaSource->addItem(source.name, source.uuid);
		fields.mediaAction->addItem(QStringLiteral("KEEP"), QStringLiteral("keep"));
		fields.mediaAction->addItem(QStringLiteral("PLAY"), QStringLiteral("play"));
		fields.mediaAction->addItem(QStringLiteral("PAUSE"), QStringLiteral("pause"));
		fields.mediaAction->addItem(QStringLiteral("RESTART"), QStringLiteral("restart"));
		fields.mediaAction->addItem(QStringLiteral("STOP"), QStringLiteral("stop"));
		fields.mediaAction->addItem(QStringLiteral("PREVIOUS"), QStringLiteral("previous"));
		fields.mediaAction->addItem(QStringLiteral("NEXT"), QStringLiteral("next"));
		SetComboData(fields.mediaSource, actions.mediaSourceUuid, QStringLiteral("Unavailable media source"));
		SetComboData(fields.mediaAction, actions.mediaAction);
		mediaGrid->addWidget(fields.mediaSource, 1, 0);
		mediaGrid->addWidget(fields.mediaAction, 1, 1);
		pageLayout->addWidget(mediaGroup);

		auto *reactionGroup = new QGroupBox(QStringLiteral("SOURCE REACTION NETWORK"), pageContents);
		auto *reactionForm = new QFormLayout(reactionGroup);
		fields.reactionNetworkAction = new QComboBox(reactionGroup);
		fields.reactionNetworkAction->setAccessibleName(protocol.label +
								QStringLiteral(" reaction network action"));
		fields.reactionNetworkAction->addItem(QStringLiteral("KEEP CURRENT STATE"), QStringLiteral("keep"));
		fields.reactionNetworkAction->addItem(QStringLiteral("ARM NETWORK"), QStringLiteral("arm"));
		fields.reactionNetworkAction->addItem(QStringLiteral("DISARM + RESTORE"), QStringLiteral("disarm"));
		fields.reactionNetworkAction->addItem(QStringLiteral("TEST ENABLED RIGS"), QStringLiteral("test"));
		fields.reactionNetworkAction->addItem(QStringLiteral("ARM + TEST ENABLED RIGS"),
						      QStringLiteral("arm-test"));
		SetComboData(fields.reactionNetworkAction, actions.reactionNetworkAction);
		reactionForm->addRow(QStringLiteral("On scene route"), fields.reactionNetworkAction);
		fields.reactionScopeAction = new QComboBox(reactionGroup);
		fields.reactionScopeAction->setAccessibleName(protocol.label + QStringLiteral(" reaction scene scope"));
		fields.reactionScopeAction->addItem(QStringLiteral("KEEP CURRENT SCOPE"), QStringLiteral("keep"));
		fields.reactionScopeAction->addItem(QStringLiteral("ACTIVE SCENE RIGS ONLY"), QStringLiteral("active"));
		fields.reactionScopeAction->addItem(QStringLiteral("ALL BOUND SCENES"), QStringLiteral("all"));
		SetComboData(fields.reactionScopeAction, actions.reactionScopeAction);
		reactionForm->addRow(QStringLiteral("Scene scope"), fields.reactionScopeAction);
		fields.reactionCircuitProfile = new QComboBox(reactionGroup);
		fields.reactionCircuitProfile->setAccessibleName(protocol.label +
								 QStringLiteral(" reaction circuit profile"));
		fields.reactionCircuitProfile->addItem(QStringLiteral("KEEP CURRENT PROFILE"), QStringLiteral("keep"));
		fields.reactionCircuitProfile->addItem(QStringLiteral("ALL CIRCUITS"), QStringLiteral("all"));
		fields.reactionCircuitProfile->addItem(QStringLiteral("AMBIENT"), QStringLiteral("ambient"));
		fields.reactionCircuitProfile->addItem(QStringLiteral("CONVERSATION"), QStringLiteral("conversation"));
		fields.reactionCircuitProfile->addItem(QStringLiteral("ALERT FOCUS"), QStringLiteral("alert"));
		fields.reactionCircuitProfile->addItem(QStringLiteral("CORE ONLY"), QStringLiteral("core"));
		SetComboData(fields.reactionCircuitProfile, actions.reactionCircuitProfile);
		reactionForm->addRow(QStringLiteral("Circuit profile"), fields.reactionCircuitProfile);
		fields.reactionIntensityEnabled =
			new QCheckBox(QStringLiteral("Set protocol-specific master intensity"), reactionGroup);
		fields.reactionIntensityEnabled->setAccessibleName(
			protocol.label + QStringLiteral(" reaction network intensity enabled"));
		fields.reactionIntensityEnabled->setChecked(actions.reactionIntensityEnabled);
		reactionForm->addRow(QString(), fields.reactionIntensityEnabled);
		fields.reactionIntensity = new QSpinBox(reactionGroup);
		fields.reactionIntensity->setAccessibleName(protocol.label +
							    QStringLiteral(" reaction network intensity"));
		fields.reactionIntensity->setRange(0, 200);
		fields.reactionIntensity->setSingleStep(5);
		fields.reactionIntensity->setSuffix(QStringLiteral(" %"));
		fields.reactionIntensity->setValue(actions.reactionIntensity);
		fields.reactionIntensity->setEnabled(actions.reactionIntensityEnabled);
		connect(fields.reactionIntensityEnabled, &QCheckBox::toggled, fields.reactionIntensity,
			&QWidget::setEnabled);
		reactionForm->addRow(QStringLiteral("Master intensity"), fields.reactionIntensity);
		pageLayout->addWidget(reactionGroup);

		auto *outputGroup = new QGroupBox(QStringLiteral("RECORDING AND PROGRAM"), pageContents);
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
			const QString selected =
				QFileDialog::getOpenFileName(&dialog, QStringLiteral("Select program"), path->text(),
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
		page->setWidget(pageContents);

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
		actions.mediaSourceUuid = fields.mediaSource->currentData().toString();
		actions.mediaAction = fields.mediaAction->currentData().toString();
		actions.recordingAction = fields.recordingAction->currentData().toString();
		actions.reactionNetworkAction = fields.reactionNetworkAction->currentData().toString();
		actions.reactionScopeAction = fields.reactionScopeAction->currentData().toString();
		actions.reactionCircuitProfile = fields.reactionCircuitProfile->currentData().toString();
		actions.reactionIntensityEnabled = fields.reactionIntensityEnabled->isChecked();
		actions.reactionIntensity = fields.reactionIntensity->value();
		actions.launchEnabled = fields.launchEnabled->isChecked();
		actions.programPath = fields.programPath->text().trimmed();
		actions.programArguments = fields.programArguments->text();
		actionConfigs.insert(fields.protocolId, actions);
	}
	SaveActionConfigs();
	SetStatus(QStringLiteral("AUTOMATION ACTIONS SAVED // SCENE CONTROL READY"));
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
	SetReactionActiveScene(currentUuid);
	const QString activeSceneText = QStringLiteral("ACTIVE // %1").arg(currentName.toUpper());
	if (currentSceneLabel->text() != activeSceneText)
		currentSceneLabel->setText(activeSceneText);
	RefreshSourcePanel(currentUuid, currentName);
	const bool editingLayout =
		(layoutPosX && layoutPosX->hasFocus()) || (layoutPosY && layoutPosY->hasFocus()) ||
		(layoutWidth && layoutWidth->hasFocus()) || (layoutHeight && layoutHeight->hasFocus()) ||
		(layoutRotation && layoutRotation->hasFocus()) || (layoutCropLeft && layoutCropLeft->hasFocus()) ||
		(layoutCropRight && layoutCropRight->hasFocus()) || (layoutCropTop && layoutCropTop->hasFocus()) ||
		(layoutCropBottom && layoutCropBottom->hasFocus());
	if (layoutConsolePanel && layoutConsolePanel->isVisible() && !editingLayout)
		RefreshLayoutConsole();
	SetActiveSceneButton(sceneButtons.value(currentUuid));
}

void TempestCommandMatrix::SetActiveSceneButton(QPushButton *button)
{
	if (activeSceneButton == button)
		return;

	if (activeSceneButton) {
		activeSceneButton->setProperty("activeScene", false);
		activeSceneButton->style()->unpolish(activeSceneButton);
		activeSceneButton->style()->polish(activeSceneButton);
		activeSceneButton->update();
	}

	activeSceneButton = button;
	if (!activeSceneButton)
		return;

	activeSceneButton->setProperty("activeScene", true);
	activeSceneButton->style()->unpolish(activeSceneButton);
	activeSceneButton->style()->polish(activeSceneButton);
	activeSceneButton->update();
}

void TempestCommandMatrix::SetStatus(const QString &message, bool error)
{
	statusLabel->setText(message);
	statusLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
					   .arg(error ? QStringLiteral("#ff799c") : QStringLiteral("#45d9ff")));
}
