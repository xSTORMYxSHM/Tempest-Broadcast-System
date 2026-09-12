#include "TempestGuestLayouts.hpp"

#include <widgets/OBSBasic.hpp>

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include <array>
#include <vector>

#include "moc_TempestGuestLayouts.cpp"

namespace {
constexpr char ManagedKey[] = "tempest_guest_layout_managed";
constexpr char RoleKey[] = "tempest_guest_layout_role";
constexpr char ParentKey[] = "tempest_guest_layout_parent";
constexpr char TemplateKey[] = "tempest_guest_layout_template";

struct SlotSpec {
	const char *label;
	float x;
	float y;
	float width;
	float height;
};

struct LayoutSpec {
	const char *id;
	const char *name;
	const char *description;
	std::vector<SlotSpec> participantSlots;
};

const std::array<LayoutSpec, 4> Layouts = {{
	{"split_interview",
	 "Split Interview",
	 "Two equal participant slots for interviews, co-hosts, and side-by-side conversations.",
	 {{"Host", 0.0f, 0.0f, 0.496f, 1.0f}, {"Guest 1", 0.504f, 0.0f, 0.496f, 1.0f}}},
	{"three_person_panel",
	 "Three-Person Panel",
	 "A large host or featured speaker with two stacked guest slots.",
	 {{"Host / Feature", 0.0f, 0.0f, 0.662f, 1.0f},
	  {"Guest 1", 0.670f, 0.0f, 0.330f, 0.496f},
	  {"Guest 2", 0.670f, 0.504f, 0.330f, 0.496f}}},
	{"four_person_grid",
	 "Four-Person Grid",
	 "Four balanced slots for panels, game nights, and roundtables.",
	 {{"Host", 0.0f, 0.0f, 0.496f, 0.496f},
	  {"Guest 1", 0.504f, 0.0f, 0.496f, 0.496f},
	  {"Guest 2", 0.0f, 0.504f, 0.496f, 0.496f},
	  {"Guest 3", 0.504f, 0.504f, 0.496f, 0.496f}}},
	{"focus_rail",
	 "Focus + Guest Rail",
	 "One featured participant above a three-person rail, ready for presentations or rotating focus.",
	 {{"Feature", 0.0f, 0.0f, 1.0f, 0.746f},
	  {"Guest 1", 0.0f, 0.754f, 0.328f, 0.246f},
	  {"Guest 2", 0.336f, 0.754f, 0.328f, 0.246f},
	  {"Guest 3", 0.672f, 0.754f, 0.328f, 0.246f}}},
}};

const LayoutSpec *FindLayout(const QString &id)
{
	for (const LayoutSpec &layout : Layouts) {
		if (id == QString::fromUtf8(layout.id)) {
			return &layout;
		}
	}
	return nullptr;
}

void MarkManaged(obs_source_t *source, const char *role, const QString &parent, const QString &templateId)
{
	if (!source) {
		return;
	}
	OBSDataAutoRelease privateSettings = obs_source_get_private_settings(source);
	obs_data_set_bool(privateSettings, ManagedKey, true);
	obs_data_set_string(privateSettings, RoleKey, role);
	obs_data_set_string(privateSettings, ParentKey, parent.toUtf8().constData());
	obs_data_set_string(privateSettings, TemplateKey, templateId.toUtf8().constData());
}

OBSSourceAutoRelease CreateColor(const QString &name, int width, int height, uint32_t color)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "width", width);
	obs_data_set_int(settings, "height", height);
	obs_data_set_int(settings, "color", color);
	return obs_source_create("color_source", name.toUtf8().constData(), settings, nullptr);
}

OBSSourceAutoRelease CreateSlotLabel(const QString &name, const QString &label, int width, int height)
{
	OBSDataAutoRelease settings = obs_data_create();
	OBSDataAutoRelease font = obs_data_create();
#ifdef _WIN32
	obs_data_set_string(font, "face", "Segoe UI");
#else
	obs_data_set_string(font, "face", "Sans");
#endif
	obs_data_set_int(font, "size", 54);
	obs_data_set_int(font, "flags", 1);
	obs_data_set_obj(settings, "font", font);
	obs_data_set_string(settings, "text",
			    QStringLiteral("%1 SLOT\nAdd a participant, browser source, or video capture here")
				    .arg(label.toUpper())
				    .toUtf8()
				    .constData());
	obs_data_set_bool(settings, "outline", false);
#ifdef _WIN32
	obs_data_set_int(settings, "color", 0x00EAFBFF);
	obs_data_set_bool(settings, "extents", true);
	obs_data_set_bool(settings, "extents_wrap", true);
	obs_data_set_int(settings, "extents_cx", width);
	obs_data_set_int(settings, "extents_cy", height);
	obs_data_set_string(settings, "align", "center");
	obs_data_set_string(settings, "valign", "center");
	const char *sourceId = "text_gdiplus";
#else
	obs_data_set_int(settings, "color1", 0xFFEAFBFF);
	obs_data_set_int(settings, "color2", 0xFFEAFBFF);
	obs_data_set_int(settings, "custom_width", width);
	obs_data_set_bool(settings, "word_wrap", true);
	const char *sourceId = "text_ft2_source";
#endif
	return obs_source_create(sourceId, name.toUtf8().constData(), settings, nullptr);
}

struct ManagedSceneInfo {
	QString name;
	QString role;
	QString parent;
};

bool EnumerateManagedScene(void *data, obs_source_t *source)
{
	OBSDataAutoRelease privateSettings = obs_source_get_private_settings(source);
	if (!obs_data_get_bool(privateSettings, ManagedKey)) {
		return true;
	}
	auto *scenes = static_cast<std::vector<ManagedSceneInfo> *>(data);
	scenes->push_back({QString::fromUtf8(obs_source_get_name(source)),
			   QString::fromUtf8(obs_data_get_string(privateSettings, RoleKey)),
			   QString::fromUtf8(obs_data_get_string(privateSettings, ParentKey))});
	return true;
}
} // namespace

TempestGuestLayouts::TempestGuestLayouts(OBSBasic *main, QWidget *parent) : OBSDock(parent), main(main)
{
	setObjectName(QStringLiteral("tempestGuestLayouts"));
	setWindowTitle(QStringLiteral("Guest Layouts"));
	setMinimumWidth(390);
	BuildInterface();
	EnableContentScaling(objectName());
}

void TempestGuestLayouts::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestGuestLayoutsRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestGuestLayoutsRoot { background: #07131e; }
		QLabel#guestLayoutTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#guestLayoutHint, QLabel#guestLayoutSummary { color: #88a8bb; font-size: 10px; }
		QLabel#guestLayoutStatus { color: #45d9ff; padding: 8px; border: 1px solid #1f506d; background: #06101a; font-weight: 700; }
		QComboBox, QListWidget { color: #bdf6ff; background: #06101a; border: 1px solid #1f506d; }
		QComboBox { min-height: 31px; padding: 0 7px; }
		QListWidget::item { min-height: 28px; padding: 3px 6px; }
		QListWidget::item:selected { background: #0c456b; color: #ffffff; }
		QPushButton { min-height: 31px; padding: 0 9px; color: #bdf6ff; background: #0d2230; border: 1px solid #1f506d; font-weight: 700; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
	)"));

	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	auto *title = new QLabel(QStringLiteral("STREAM TOGETHER LAYOUTS"), root);
	title->setObjectName(QStringLiteral("guestLayoutTitle"));
	layout->addWidget(title);

	auto *hint = new QLabel(
		QStringLiteral(
			"Create reusable participant slots without changing an existing scene. Each slot accepts any "
			"OBS source, including Stream Together, browser, camera, or capture sources."),
		root);
	hint->setObjectName(QStringLiteral("guestLayoutHint"));
	hint->setWordWrap(true);
	layout->addWidget(hint);

	templateSelector = new QComboBox(root);
	templateSelector->setAccessibleName(QStringLiteral("Guest layout template"));
	for (const LayoutSpec &spec : Layouts) {
		templateSelector->addItem(QString::fromUtf8(spec.name), QString::fromUtf8(spec.id));
	}
	layout->addWidget(templateSelector);

	templateSummary = new QLabel(root);
	templateSummary->setObjectName(QStringLiteral("guestLayoutSummary"));
	templateSummary->setWordWrap(true);
	layout->addWidget(templateSummary);

	createButton = new QPushButton(QStringLiteral("CREATE NON-DESTRUCTIVE LAYOUT"), root);
	layout->addWidget(createButton);

	auto *sceneLabel = new QLabel(QStringLiteral("GENERATED SCENES // DOUBLE-CLICK TO OPEN"), root);
	sceneLabel->setObjectName(QStringLiteral("guestLayoutHint"));
	layout->addWidget(sceneLabel);

	managedScenes = new QListWidget(root);
	managedScenes->setAccessibleName(QStringLiteral("Generated guest layout scenes"));
	managedScenes->setMinimumHeight(170);
	layout->addWidget(managedScenes, 1);

	refreshButton = new QPushButton(QStringLiteral("REFRESH SCENE LIST"), root);
	layout->addWidget(refreshButton);

	statusLabel = new QLabel(QStringLiteral("SELECT A TEMPLATE TO BEGIN"), root);
	statusLabel->setObjectName(QStringLiteral("guestLayoutStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	setWidget(root);

	connect(templateSelector, &QComboBox::currentIndexChanged, this, &TempestGuestLayouts::UpdateTemplateSummary);
	connect(createButton, &QPushButton::clicked, this, &TempestGuestLayouts::CreateSelectedLayout);
	connect(refreshButton, &QPushButton::clicked, this, &TempestGuestLayouts::RefreshManagedScenes);
	connect(managedScenes, &QListWidget::itemDoubleClicked, this, &TempestGuestLayouts::OpenManagedScene);
	UpdateTemplateSummary();
}

void TempestGuestLayouts::showEvent(QShowEvent *event)
{
	OBSDock::showEvent(event);
	RefreshManagedScenes();
}

void TempestGuestLayouts::UpdateTemplateSummary()
{
	const LayoutSpec *spec = FindLayout(templateSelector ? templateSelector->currentData().toString() : QString());
	if (!spec) {
		templateSummary->clear();
		return;
	}
	templateSummary->setText(QStringLiteral("%1 participant slots // %2")
					 .arg(spec->participantSlots.size())
					 .arg(QString::fromUtf8(spec->description)));
}

QString TempestGuestLayouts::UniqueSourceName(const QString &baseName) const
{
	QString candidate = baseName;
	for (int suffix = 2;; ++suffix) {
		OBSSourceAutoRelease source = obs_get_source_by_name(candidate.toUtf8().constData());
		if (!source) {
			return candidate;
		}
		candidate = QStringLiteral("%1 (%2)").arg(baseName).arg(suffix);
	}
}

bool TempestGuestLayouts::CreatePlaceholderScene(const QString &sceneName, const QString &slotLabel, int slotIndex,
						 int canvasWidth, int canvasHeight)
{
	OBSSceneAutoRelease scene = obs_scene_create(sceneName.toUtf8().constData());
	if (!scene) {
		return false;
	}
	obs_source_t *sceneSource = obs_scene_get_source(scene);
	MarkManaged(sceneSource, "slot", QString(), QString());

	static constexpr std::array<uint32_t, 4> SlotColors = {0xFF122A3A, 0xFF102E35, 0xFF182640, 0xFF11313D};
	const QString backgroundName = UniqueSourceName(QStringLiteral("%1 // Placeholder").arg(sceneName));
	OBSSourceAutoRelease background =
		CreateColor(backgroundName, canvasWidth, canvasHeight, SlotColors[slotIndex % SlotColors.size()]);
	if (!background) {
		return false;
	}
	obs_sceneitem_t *backgroundItem = obs_scene_add(scene, background);
	if (!backgroundItem) {
		return false;
	}
	obs_sceneitem_set_locked(backgroundItem, true);

	const QString labelName = UniqueSourceName(QStringLiteral("%1 // Guide").arg(sceneName));
	OBSSourceAutoRelease label = CreateSlotLabel(labelName, slotLabel, canvasWidth, canvasHeight);
	if (label) {
		obs_sceneitem_t *labelItem = obs_scene_add(scene, label);
		if (labelItem) {
			obs_sceneitem_set_locked(labelItem, true);
		}
	}
	return true;
}

void TempestGuestLayouts::CreateSelectedLayout()
{
	if (!main || !templateSelector) {
		return;
	}
	const QString templateId = templateSelector->currentData().toString();
	const LayoutSpec *spec = FindLayout(templateId);
	if (!spec) {
		SetStatus(QStringLiteral("SELECT A VALID TEMPLATE"), true);
		return;
	}

	obs_video_info videoInfo{};
	if (!obs_get_video_info(&videoInfo) || videoInfo.base_width == 0 || videoInfo.base_height == 0) {
		SetStatus(QStringLiteral("VIDEO CANVAS IS NOT READY"), true);
		return;
	}
	if (!obs_get_latest_input_type_id("color_source")) {
		SetStatus(QStringLiteral("COLOR SOURCE MODULE IS UNAVAILABLE"), true);
		return;
	}

	const QString masterName =
		UniqueSourceName(QStringLiteral("Stream Together // %1").arg(QString::fromUtf8(spec->name)));
	std::vector<QString> slotNames;
	slotNames.reserve(spec->participantSlots.size());
	for (size_t index = 0; index < spec->participantSlots.size(); ++index) {
		const QString slotName = UniqueSourceName(
			QStringLiteral("%1 // %2")
				.arg(masterName, QString::fromUtf8(spec->participantSlots[index].label)));
		if (!CreatePlaceholderScene(slotName, QString::fromUtf8(spec->participantSlots[index].label),
					    static_cast<int>(index), static_cast<int>(videoInfo.base_width),
					    static_cast<int>(videoInfo.base_height))) {
			SetStatus(QStringLiteral("COULD NOT CREATE PARTICIPANT SLOT // %1").arg(slotName), true);
			return;
		}
		slotNames.push_back(slotName);
	}

	OBSSceneAutoRelease master = obs_scene_create(masterName.toUtf8().constData());
	if (!master) {
		SetStatus(QStringLiteral("COULD NOT CREATE MASTER LAYOUT"), true);
		return;
	}
	obs_source_t *masterSource = obs_scene_get_source(master);
	MarkManaged(masterSource, "layout", masterName, templateId);

	const QString backdropName = UniqueSourceName(QStringLiteral("%1 // Backdrop").arg(masterName));
	OBSSourceAutoRelease backdrop = CreateColor(backdropName, static_cast<int>(videoInfo.base_width),
						    static_cast<int>(videoInfo.base_height), 0xFF03080D);
	if (backdrop) {
		obs_sceneitem_t *backdropItem = obs_scene_add(master, backdrop);
		if (backdropItem) {
			obs_sceneitem_set_locked(backdropItem, true);
		}
	}

	for (size_t index = 0; index < spec->participantSlots.size(); ++index) {
		OBSSourceAutoRelease slotSource = obs_get_source_by_name(slotNames[index].toUtf8().constData());
		if (!slotSource) {
			continue;
		}
		MarkManaged(slotSource, "slot", masterName, templateId);
		obs_sceneitem_t *item = obs_scene_add(master, slotSource);
		if (!item) {
			continue;
		}
		const SlotSpec &slot = spec->participantSlots[index];
		vec2 position = {slot.x * videoInfo.base_width, slot.y * videoInfo.base_height};
		vec2 bounds = {slot.width * videoInfo.base_width, slot.height * videoInfo.base_height};
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
		obs_sceneitem_set_pos(item, &position);
		obs_sceneitem_set_bounds(item, &bounds);
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_OUTER);
		obs_sceneitem_set_locked(item, true);
	}

	main->SetCurrentScene(OBSSource(masterSource), true);
	main->SaveProject();
	RefreshManagedScenes();
	SetStatus(QStringLiteral("CREATED // %1 // DOUBLE-CLICK A SLOT TO ADD ITS FEED").arg(masterName));
}

void TempestGuestLayouts::RefreshManagedScenes()
{
	if (!managedScenes) {
		return;
	}
	const QString selectedName =
		managedScenes->currentItem() ? managedScenes->currentItem()->data(Qt::UserRole).toString() : QString();
	managedScenes->clear();
	std::vector<ManagedSceneInfo> scenes;
	obs_enum_scenes(EnumerateManagedScene, &scenes);
	for (const ManagedSceneInfo &scene : scenes) {
		const QString prefix = scene.role == QStringLiteral("layout") ? QStringLiteral("LAYOUT")
									      : QStringLiteral("SLOT");
		auto *item = new QListWidgetItem(QStringLiteral("%1 // %2").arg(prefix, scene.name), managedScenes);
		item->setData(Qt::UserRole, scene.name);
		item->setToolTip(scene.role == QStringLiteral("slot") && !scene.parent.isEmpty()
					 ? QStringLiteral("Participant slot in %1").arg(scene.parent)
					 : QStringLiteral("Generated guest layout"));
		if (scene.name == selectedName) {
			managedScenes->setCurrentItem(item);
		}
	}
}

void TempestGuestLayouts::OpenManagedScene(QListWidgetItem *item)
{
	if (!main || !item) {
		return;
	}
	const QString name = item->data(Qt::UserRole).toString();
	OBSSourceAutoRelease source = obs_get_source_by_name(name.toUtf8().constData());
	if (!source || !obs_scene_from_source(source)) {
		SetStatus(QStringLiteral("SCENE IS NO LONGER AVAILABLE"), true);
		RefreshManagedScenes();
		return;
	}
	main->SetCurrentScene(OBSSource(source.Get()), true);
	SetStatus(QStringLiteral("OPENED // %1").arg(name));
}

void TempestGuestLayouts::SetStatus(const QString &message, bool error)
{
	if (!statusLabel) {
		return;
	}
	statusLabel->setText(message);
	statusLabel->setStyleSheet(error ? QStringLiteral("color:#ff7085;") : QString());
}
