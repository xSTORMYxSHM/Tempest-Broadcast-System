#include "TempestCommandMatrix.hpp"

#include "TempestControlDeck.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
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

	connect(isolateOverlay, &QCheckBox::toggled, this, &TempestCommandMatrix::SaveAssignments);
	connect(startCountdown, &QCheckBox::toggled, this, &TempestCommandMatrix::SaveAssignments);

	refreshTimer = new QTimer(this);
	refreshTimer->setInterval(500);
	connect(refreshTimer, &QTimer::timeout, this, &TempestCommandMatrix::RefreshScenes);
	refreshTimer->start();
	RefreshScenes();
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

	if (isolateOverlay->isChecked())
		ApplyProtocolOverlay(sceneSource.Get(), protocol->sourceName);
	controlDeck->ActivateMode(protocol->id,
				  protocol->id == QStringLiteral("starting") && startCountdown->isChecked());
	main->SetCurrentScene(OBSSource(sceneSource.Get()));
	SetStatus(QStringLiteral("%1 PROTOCOL // %2").arg(protocol->label, obs_source_get_name(sceneSource)));
	UpdateActiveScene();
}

void TempestCommandMatrix::SwitchScene(const QString &uuid, const QString &name)
{
	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!sceneSource || obs_source_get_type(sceneSource) != OBS_SOURCE_TYPE_SCENE) {
		SetStatus(QStringLiteral("Scene route unavailable: %1").arg(name), true);
		RefreshScenes();
		return;
	}
	main->SetCurrentScene(OBSSource(sceneSource.Get()));
	SetStatus(QStringLiteral("DIRECT ROUTE // %1").arg(name));
	UpdateActiveScene();
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
