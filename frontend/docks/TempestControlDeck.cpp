#include "TempestControlDeck.hpp"

#include <OBSApp.hpp>
#include <utility/platform.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QDateTime>
#include <QDesktopServices>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cstring>
#include <array>

namespace {
constexpr char ConfigSection[] = "TempestControlDeck";

struct ModeDefinition {
	const char *id;
	const char *label;
	const char *filename;
	const char *sourceName;
	const char *kicker;
	const char *idleText;
	const char *defaultTitle;
	const char *defaultStatus;
	const char *defaultMessages;
};

constexpr std::array<ModeDefinition, 4> Modes{{
	{"starting", "Starting Soon", "starting-soon.html", "Tempest // Starting Soon", "STREAM STARTING SOON",
	 "COUNTDOWN READY", "WELCOME TO THE STREAM", "BROADCAST SETUP // IN PROGRESS",
	 "CHECKING AUDIO AND VIDEO\nCONNECTING STREAM SERVICES\nTHANKS FOR WAITING"},
	{"brb", "Be Right Back", "brb.html", "Tempest // BRB", "BE RIGHT BACK", "RETURN PENDING", "STREAM PAUSED",
	 "BREAK SCREEN // ACTIVE", "THE STREAM WILL CONTINUE SHORTLY\nMEDIA PLAYBACK CONTINUES\nTHANKS FOR WAITING"},
	{"ending", "Stream Ending", "stream-ending.html", "Tempest // Stream Ending", "STREAM ENDING",
	 "BROADCAST COMPLETE", "THANKS FOR WATCHING", "SESSION COMPLETE",
	 "THANKS FOR JOINING THE STREAM\nFOLLOW FOR FUTURE BROADCASTS\nSEE YOU NEXT TIME"},
	{"live", "Live Overlay", "live-hud.html", "Tempest // Live Overlay", "STREAM LIVE", "LIVE", "TEMPEST BROADCAST",
	 "STREAM CONNECTION // ONLINE", "AUDIO AND VIDEO ONLINE\nCHAT CONNECTION ACTIVE\nSTREAM STATUS NOMINAL"},
}};

const ModeDefinition &FindMode(const QString &id)
{
	for (const ModeDefinition &mode : Modes) {
		if (id == QString::fromUtf8(mode.id))
			return mode;
	}
	return Modes.front();
}

QByteArray ModeKey(const char *base, const QString &mode)
{
	return QByteArray(base) + '_' + mode.toUtf8();
}

QString ConfigString(const char *key, const char *fallback)
{
	config_t *config = App()->GetUserConfig();
	const char *value = config_get_string(config, ConfigSection, key);
	return value && *value ? QString::fromUtf8(value) : QString::fromUtf8(fallback);
}

QString NormalizePlaylistName(const QString &value)
{
	QString name = value.trimmed().toCaseFolded();
	name.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
	name.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
	return name.isEmpty() ? QStringLiteral("common") : name;
}

bool PlaylistDirective(const QString &line, QString *playlist)
{
	static const QRegularExpression directive(QStringLiteral("^@playlist\\s+(.+)$"),
						  QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = directive.match(line.trimmed());
	if (!match.hasMatch())
		return false;
	if (playlist)
		*playlist = NormalizePlaylistName(match.captured(1));
	return true;
}

QStringList PlaylistNames(const QString &text)
{
	QStringList playlists;
	for (const QString &line : text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
		QString playlist;
		if (PlaylistDirective(line, &playlist) && !playlists.contains(playlist, Qt::CaseInsensitive))
			playlists.push_back(playlist);
	}
	return playlists;
}

QStringList ActiveMessageLines(const QString &text, const QString &selection, const QString &modeId)
{
	QStringList messages;
	QString section = QStringLiteral("common");
	const QString selected = NormalizePlaylistName(selection);
	const QString mode = NormalizePlaylistName(modeId);
	const QRegularExpression disabledMarker(QStringLiteral("^\\[\\s*\\]\\s*"));
	const QRegularExpression enabledMarker(QStringLiteral("^\\[\\s*[xX]\\s*\\]\\s*"));
	for (QString line : text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
		line = line.trimmed();
		if (PlaylistDirective(line, &section))
			continue;
		if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || disabledMarker.match(line).hasMatch())
			continue;
		const bool include = selected == QStringLiteral("all") || section == QStringLiteral("common") ||
				     (selected == QStringLiteral("auto") && section == mode) || section == selected;
		if (!include)
			continue;
		line.remove(enabledMarker);
		line = line.trimmed();
		if (!line.isEmpty())
			messages.push_back(line);
	}
	return messages;
}
} // namespace

TempestControlDeck::TempestControlDeck(QWidget *parent) : OBSDock(parent)
{
	setObjectName(QStringLiteral("tempestControlDeck"));
	setWindowTitle(QStringLiteral("Stream Overlay"));
	setMinimumWidth(340);

	BuildInterface();
	EnableContentScaling(objectName());
	EnsureOverlayDirectory();
	LoadState();
	rotationLibraryWatcher = new QFileSystemWatcher(this);
	WatchContentProfileFiles();
	connect(rotationLibraryWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
		QTimer::singleShot(150, this, [this, path]() {
			if (path == ProfileFilePath(activeProfileId)) {
				if (LoadContentProfile(activeProfileId))
					RenderOverlay();
				else {
					SetStatus(
						QStringLiteral(
							"Profile file is not valid JSON; the last valid content remains active."),
						true);
					WatchContentProfileFiles();
				}
			} else {
				RefreshMessagePlaylists();
				RefreshRotationLibrarySummary();
				WatchContentProfileFiles();
			}
		});
	});

	renderDebounce = new QTimer(this);
	renderDebounce->setSingleShot(true);
	renderDebounce->setInterval(350);
	connect(renderDebounce, &QTimer::timeout, this, &TempestControlDeck::RenderOverlay);

	clockTimer = new QTimer(this);
	clockTimer->setInterval(1000);
	connect(clockTimer, &QTimer::timeout, this, &TempestControlDeck::UpdateCountdownPreview);
	clockTimer->start();

	connect(overlayMode, &QComboBox::currentIndexChanged, this, &TempestControlDeck::ChangeOverlayMode);
	connect(streamTitle, &QLineEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(statusLine, &QLineEdit::textChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(rotationSeconds, &QSpinBox::valueChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(messageOrder, &QComboBox::currentIndexChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(messagePlaylist, &QComboBox::currentIndexChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(countdownMinutes, &QSpinBox::valueChanged, this, &TempestControlDeck::QueueOverlayRender);
	connect(startCountdownButton, &QPushButton::clicked, this, &TempestControlDeck::StartCountdown);
	connect(resetCountdownButton, &QPushButton::clicked, this, &TempestControlDeck::ResetCountdown);
	connect(createSourceButton, &QPushButton::clicked, this, &TempestControlDeck::CreateOrUpdateSource);
	connect(composeMessageButton, &QPushButton::clicked, this, &TempestControlDeck::ComposeRotationMessage);
	connect(rotationLibraryButton, &QPushButton::clicked, this, &TempestControlDeck::OpenRotationLibrary);
	connect(messageVariablesButton, &QPushButton::clicked, this, &TempestControlDeck::OpenMessageVariables);
	connect(contentProfile, &QComboBox::currentIndexChanged, this, &TempestControlDeck::ChangeContentProfile);
	connect(newProfileButton, &QPushButton::clicked, this, &TempestControlDeck::NewContentProfile);
	connect(duplicateProfileButton, &QPushButton::clicked, this, &TempestControlDeck::DuplicateContentProfile);
	connect(openProfileFolderButton, &QPushButton::clicked, this, &TempestControlDeck::OpenContentProfileFolder);
	RenderOverlay();
	UpdateCountdownPreview();
}

void TempestControlDeck::ActivateMode(const QString &modeId, bool beginCountdown)
{
	if (!overlayMode)
		return;
	const int index = overlayMode->findData(modeId);
	if (index < 0)
		return;
	overlayMode->setCurrentIndex(index);
	if (beginCountdown)
		StartCountdown();
	raise();
}

void TempestControlDeck::UpdateOverlayText(const QString &modeId, const QString &transmission, const QString &status,
					   const QString &messages)
{
	ActivateMode(modeId);
	if (!transmission.isEmpty())
		streamTitle->setText(transmission);
	if (!status.isEmpty())
		statusLine->setText(status);
	if (!messages.isEmpty())
		AppendRotationMessages(messages);
	QueueOverlayRender();
}

void TempestControlDeck::BuildInterface()
{
	QWidget *body = new QWidget(this);
	body->setObjectName(QStringLiteral("tempestControlRoot"));
	body->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestControlRoot { background: #07131e; }
		QLabel#controlTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#controlSubtitle { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
	)"));
	QVBoxLayout *layout = new QVBoxLayout(body);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(10);

	auto *title = new QLabel(QStringLiteral("TEMPEST // STREAM OVERLAY"), body);
	title->setObjectName(QStringLiteral("controlTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Starting Soon and stream-state graphics"), body);
	subtitle->setObjectName(QStringLiteral("controlSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	QFrame *rule = new QFrame(body);
	rule->setFrameShape(QFrame::HLine);
	rule->setProperty("class", "separator");
	layout->addWidget(rule);

	QFormLayout *form = new QFormLayout();
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	contentProfile = new QComboBox(body);
	contentProfile->setAccessibleName(QStringLiteral("Stream content profile"));
	form->addRow(QStringLiteral("Content profile"), contentProfile);

	QWidget *profileActions = new QWidget(body);
	QHBoxLayout *profileActionLayout = new QHBoxLayout(profileActions);
	profileActionLayout->setContentsMargins(0, 0, 0, 0);
	profileActionLayout->setSpacing(6);
	newProfileButton = new QPushButton(QStringLiteral("New"), profileActions);
	duplicateProfileButton = new QPushButton(QStringLiteral("Duplicate"), profileActions);
	openProfileFolderButton = new QPushButton(QStringLiteral("Open Folder"), profileActions);
	profileActionLayout->addWidget(newProfileButton);
	profileActionLayout->addWidget(duplicateProfileButton);
	profileActionLayout->addWidget(openProfileFolderButton);
	form->addRow(QString(), profileActions);

	overlayMode = new QComboBox(body);
	for (const ModeDefinition &mode : Modes)
		overlayMode->addItem(QString::fromUtf8(mode.label), QString::fromUtf8(mode.id));
	form->addRow(QStringLiteral("Overlay mode"), overlayMode);

	streamTitle = new QLineEdit(body);
	streamTitle->setPlaceholderText(QStringLiteral("Welcome to the stream"));
	form->addRow(QStringLiteral("Heading"), streamTitle);

	statusLine = new QLineEdit(body);
	statusLine->setPlaceholderText(QStringLiteral("Stream status // standby"));
	form->addRow(QStringLiteral("Status"), statusLine);

	QWidget *messageAssets = new QWidget(body);
	QVBoxLayout *messageAssetLayout = new QVBoxLayout(messageAssets);
	messageAssetLayout->setContentsMargins(0, 0, 0, 0);
	messageAssetLayout->setSpacing(6);
	composeMessageButton = new QPushButton(QStringLiteral("Compose Message"), messageAssets);
	composeMessageButton->setToolTip(
		QStringLiteral("Build a profile message with optional timing, accent, and effect controls."));
	rotationLibraryButton = new QPushButton(QStringLiteral("Manage Message Library"), messageAssets);
	rotationLibraryButton->setToolTip(QStringLiteral(
		"Manage profile lines visually, or open the portable text asset for advanced playlist editing."));
	messageVariablesButton = new QPushButton(QStringLiteral("Variables"), messageAssets);
	messageVariablesButton->setToolTip(
		QStringLiteral("Manage shared and profile {{variable}} placeholder values visually."));
	messageAssetLayout->addWidget(composeMessageButton);
	messageAssetLayout->addWidget(rotationLibraryButton);
	messageAssetLayout->addWidget(messageVariablesButton);
	form->addRow(QStringLiteral("Message assets"), messageAssets);

	messagePlaylist = new QComboBox(body);
	messagePlaylist->setToolTip(QStringLiteral(
		"Auto combines ungrouped/common lines with the section matching the current overlay mode."));
	form->addRow(QStringLiteral("Message playlist"), messagePlaylist);

	messageOrder = new QComboBox(body);
	messageOrder->addItem(QStringLiteral("Sequential"), QStringLiteral("sequential"));
	messageOrder->addItem(QStringLiteral("Random"), QStringLiteral("random"));
	messageOrder->addItem(QStringLiteral("Shuffle Bag // No Repeats"), QStringLiteral("shuffle-bag"));
	form->addRow(QStringLiteral("Message order"), messageOrder);

	rotationSeconds = new QSpinBox(body);
	rotationSeconds->setRange(2, 30);
	rotationSeconds->setSuffix(QStringLiteral(" sec"));
	form->addRow(QStringLiteral("Rotation speed"), rotationSeconds);

	countdownMinutes = new QSpinBox(body);
	countdownMinutes->setRange(1, 180);
	countdownMinutes->setSuffix(QStringLiteral(" min"));
	form->addRow(QStringLiteral("Countdown"), countdownMinutes);
	layout->addLayout(form);

	QHBoxLayout *countdownRow = new QHBoxLayout();
	startCountdownButton = new QPushButton(QStringLiteral("Start / Restart"), body);
	resetCountdownButton = new QPushButton(QStringLiteral("Clear"), body);
	countdownRow->addWidget(startCountdownButton, 2);
	countdownRow->addWidget(resetCountdownButton, 1);
	layout->addLayout(countdownRow);

	countdownPreview = new QLabel(QStringLiteral("COUNTDOWN READY"), body);
	countdownPreview->setAlignment(Qt::AlignCenter);
	countdownPreview->setStyleSheet(
		QStringLiteral("QLabel { color:#bdf6ff; background:#06131f; border:1px solid #0c7ccb; padding:10px; "
			       "font-size:22px; font-weight:700; letter-spacing:2px; }"));
	layout->addWidget(countdownPreview);

	createSourceButton = new QPushButton(QStringLiteral("Create / Update Starting Soon Overlay"), body);
	createSourceButton->setMinimumHeight(38);
	createSourceButton->setProperty("class", "primary");
	layout->addWidget(createSourceButton);

	outputPathLabel = new QLabel(body);
	outputPathLabel->setWordWrap(true);
	outputPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	outputPathLabel->setStyleSheet(QStringLiteral("color:#748fa4; font-size:11px;"));
	layout->addWidget(outputPathLabel);

	statusLabel = new QLabel(QStringLiteral("Initializing overlay link..."), body);
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	layout->addStretch(1);

	setWidget(body);
}

void TempestControlDeck::LoadState()
{
	if (!EnsureProfilesDirectory())
		return;
	RefreshContentProfiles(ConfigString("ContentProfile", "default"));
}

void TempestControlDeck::SaveState()
{
	if (loadingProfile || activeProfileId.isEmpty())
		return;
	config_t *config = App()->GetUserConfig();
	SaveModeState(activeModeId);
	activeProfileDocument.insert(QStringLiteral("activeMode"), activeModeId);
	activeProfileDocument.insert(QStringLiteral("rotationSeconds"), rotationSeconds->value());
	activeProfileDocument.insert(QStringLiteral("rotationMode"), messageOrder->currentData().toString());
	activeProfileDocument.insert(QStringLiteral("messagePlaylist"), messagePlaylist->currentData().toString());
	activeProfileDocument.insert(QStringLiteral("countdownMinutes"), countdownMinutes->value());
	WriteProfileDocument(activeProfileId, activeProfileDocument);
	config_set_string(config, ConfigSection, "ContentProfile", activeProfileId.toUtf8().constData());
	config_set_string(config, ConfigSection, "OverlayMode", activeModeId.toUtf8().constData());
	config_set_int(config, ConfigSection, "RotationSeconds", rotationSeconds->value());
	config_set_int(config, ConfigSection, "CountdownMinutes", countdownMinutes->value());
	config_save_safe(config, "tmp", nullptr);
}

void TempestControlDeck::LoadModeState(const QString &modeId)
{
	const ModeDefinition &mode = FindMode(modeId);
	const QJsonObject modes = activeProfileDocument.value(QStringLiteral("modes")).toObject();
	const QJsonObject modeState = modes.value(modeId).toObject();
	streamTitle->setText(modeState.value(QStringLiteral("heading")).toString(QString::fromUtf8(mode.defaultTitle)));
	statusLine->setText(modeState.value(QStringLiteral("status")).toString(QString::fromUtf8(mode.defaultStatus)));
	countdownEndMs = qint64(modeState.value(QStringLiteral("countdownEndMs")).toDouble());
	countdownRunning = modeState.value(QStringLiteral("countdownRunning")).toBool(false);
}

void TempestControlDeck::SaveModeState(const QString &modeId)
{
	if (modeId.isEmpty() || activeProfileDocument.isEmpty())
		return;
	QJsonObject modes = activeProfileDocument.value(QStringLiteral("modes")).toObject();
	QJsonObject modeState = modes.value(modeId).toObject();
	modeState.insert(QStringLiteral("heading"), streamTitle->text().trimmed());
	modeState.insert(QStringLiteral("status"), statusLine->text().trimmed());
	modeState.insert(QStringLiteral("countdownEndMs"), double(countdownEndMs));
	modeState.insert(QStringLiteral("countdownRunning"), countdownRunning);
	modes.insert(modeId, modeState);
	activeProfileDocument.insert(QStringLiteral("modes"), modes);
}

QString TempestControlDeck::ProfileIdForName(const QString &name)
{
	QString id = name.trimmed().toLower();
	id.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
	id.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
	return id.isEmpty() ? QStringLiteral("stream-profile") : id;
}

QString TempestControlDeck::ProfileFilePath(const QString &profileId) const
{
	return QDir(QDir(profilesDirectory).filePath(profileId)).filePath(QStringLiteral("profile.json"));
}

QString TempestControlDeck::ProfileMessagePath(const QString &profileId) const
{
	return QDir(QDir(profilesDirectory).filePath(profileId)).filePath(QStringLiteral("rotating-lines.txt"));
}

QString TempestControlDeck::ProfileVariablesPath(const QString &profileId) const
{
	return QDir(QDir(profilesDirectory).filePath(profileId)).filePath(QStringLiteral("message-variables.json"));
}

bool TempestControlDeck::EnsureMessageFile(const QString &path, const QString &description) const
{
	if (QFileInfo::exists(path))
		return true;
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
		return false;
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	const QByteArray content =
		QStringLiteral(
			"# %1\n"
			"# One message per line. Use [x] for active or [ ] for disabled; unmarked lines are active.\n"
			"# Group lines with @playlist common, starting, live, brb, ending, or a custom name.\n"
			"# Optional: Message text [[duration=8 accent=#45d9ff effect=pulse]]\n")
			.arg(description)
			.toUtf8();
	return file.write(content) == content.size() && file.commit();
}

bool TempestControlDeck::EnsureVariablesFile(const QString &path, const QString &description) const
{
	if (QFileInfo::exists(path))
		return true;
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
		return false;
	QJsonObject variables;
	variables.insert(QStringLiteral("_description"), description);
	variables.insert(QStringLiteral("channel"), QString());
	variables.insert(QStringLiteral("community"), QString());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	const QByteArray content = QJsonDocument(variables).toJson(QJsonDocument::Indented);
	return file.write(content) == content.size() && file.commit();
}

QJsonObject TempestControlDeck::CreateProfileDocument(const QString &name, bool migrateExisting) const
{
	config_t *config = App()->GetUserConfig();
	QJsonObject profile;
	profile.insert(QStringLiteral("schemaVersion"), 1);
	profile.insert(QStringLiteral("name"), name);
	profile.insert(QStringLiteral("includeGlobalMessages"), true);
	const QString configuredMode = migrateExisting ? ConfigString("OverlayMode", "starting")
						       : QStringLiteral("starting");
	profile.insert(QStringLiteral("activeMode"), FindMode(configuredMode).id);
	const int configuredRotation = migrateExisting ? int(config_get_int(config, ConfigSection, "RotationSeconds"))
						       : 6;
	const int configuredCountdown = migrateExisting ? int(config_get_int(config, ConfigSection, "CountdownMinutes"))
							: 10;
	profile.insert(QStringLiteral("rotationSeconds"), configuredRotation > 0 ? configuredRotation : 6);
	profile.insert(QStringLiteral("rotationMode"), QStringLiteral("sequential"));
	profile.insert(QStringLiteral("messagePlaylist"), QStringLiteral("auto"));
	profile.insert(QStringLiteral("countdownMinutes"), configuredCountdown > 0 ? configuredCountdown : 10);

	QJsonObject modes;
	for (const ModeDefinition &mode : Modes) {
		const QString modeId = QString::fromUtf8(mode.id);
		auto legacyString = [config, &modeId, migrateExisting](const char *base, const char *fallback) {
			if (!migrateExisting)
				return QString::fromUtf8(fallback);
			const QByteArray key = ModeKey(base, modeId);
			const char *value = config_get_string(config, ConfigSection, key.constData());
			if (value && *value)
				return QString::fromUtf8(value);
			if (modeId == QStringLiteral("starting")) {
				value = config_get_string(config, ConfigSection, base);
				if (value && *value)
					return QString::fromUtf8(value);
			}
			return QString::fromUtf8(fallback);
		};
		QJsonObject modeState;
		modeState.insert(QStringLiteral("heading"), legacyString("StreamTitle", mode.defaultTitle));
		modeState.insert(QStringLiteral("status"), legacyString("StatusLine", mode.defaultStatus));
		qint64 endMs = 0;
		bool running = false;
		if (migrateExisting) {
			const QByteArray endKey = ModeKey("CountdownEndMs", modeId);
			const QByteArray runningKey = ModeKey("CountdownRunning", modeId);
			endMs = config_get_int(config, ConfigSection, endKey.constData());
			running = config_get_bool(config, ConfigSection, runningKey.constData());
			if (modeId == QStringLiteral("starting") &&
			    !config_has_user_value(config, ConfigSection, endKey.constData())) {
				endMs = config_get_int(config, ConfigSection, "CountdownEndMs");
				running = config_get_bool(config, ConfigSection, "CountdownRunning");
			}
		}
		modeState.insert(QStringLiteral("countdownEndMs"), double(endMs));
		modeState.insert(QStringLiteral("countdownRunning"), running);
		modes.insert(modeId, modeState);
	}
	profile.insert(QStringLiteral("modes"), modes);
	return profile;
}

bool TempestControlDeck::WriteProfileDocument(const QString &profileId, const QJsonObject &document)
{
	if (profileId.isEmpty())
		return false;
	const QString path = ProfileFilePath(profileId);
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
		return false;
	if (rotationLibraryWatcher && rotationLibraryWatcher->files().contains(path))
		rotationLibraryWatcher->removePath(path);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	const QByteArray json = QJsonDocument(document).toJson(QJsonDocument::Indented);
	const bool saved = file.write(json) == json.size() && file.commit();
	if (saved && rotationLibraryWatcher && profileId == activeProfileId)
		rotationLibraryWatcher->addPath(path);
	return saved;
}

bool TempestControlDeck::EnsureProfilesDirectory()
{
	if (overlayDirectory.isEmpty())
		return false;
	profilesDirectory = QDir(overlayDirectory).filePath(QStringLiteral("profiles"));
	if (!QDir().mkpath(profilesDirectory))
		return false;
	QDir profiles(profilesDirectory);
	bool hasProfile = false;
	for (const QString &folder : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (QFileInfo::exists(ProfileFilePath(folder))) {
			hasProfile = true;
			break;
		}
	}
	if (!hasProfile) {
		const QString id = QStringLiteral("default");
		if (!WriteProfileDocument(id, CreateProfileDocument(QStringLiteral("Default"), true)) ||
		    !EnsureMessageFile(ProfileMessagePath(id), QStringLiteral("Default stream profile messages")) ||
		    !EnsureVariablesFile(ProfileVariablesPath(id), QStringLiteral("Default profile message variables")))
			return false;
	}
	return true;
}

void TempestControlDeck::RefreshContentProfiles(const QString &selectProfile)
{
	if (!contentProfile)
		return;
	QString selected = selectProfile.isEmpty() ? activeProfileId : selectProfile;
	QSignalBlocker blocker(contentProfile);
	contentProfile->clear();
	QDir profiles(profilesDirectory);
	for (const QString &folder : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		QFile file(ProfileFilePath(folder));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			continue;
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
		if (!document.isObject())
			continue;
		const QString name = document.object().value(QStringLiteral("name")).toString(folder);
		contentProfile->addItem(name, folder);
	}
	int index = contentProfile->findData(selected);
	if (index < 0)
		index = 0;
	contentProfile->setCurrentIndex(index);
	if (index >= 0)
		LoadContentProfile(contentProfile->itemData(index).toString());
}

bool TempestControlDeck::LoadContentProfile(const QString &profileId)
{
	QFile file(ProfileFilePath(profileId));
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject())
		return false;
	loadingProfile = true;
	activeProfileId = profileId;
	activeProfileDocument = document.object();
	rotationLibraryPath = ProfileMessagePath(profileId);
	messageVariablesPath = ProfileVariablesPath(profileId);
	EnsureMessageFile(rotationLibraryPath,
			  QStringLiteral("%1 stream profile messages")
				  .arg(activeProfileDocument.value(QStringLiteral("name")).toString(profileId)));
	EnsureVariablesFile(messageVariablesPath,
			    QStringLiteral("%1 profile message variables")
				    .arg(activeProfileDocument.value(QStringLiteral("name")).toString(profileId)));
	const QString savedPlaylist =
		activeProfileDocument.value(QStringLiteral("messagePlaylist")).toString(QStringLiteral("auto"));
	RefreshMessagePlaylists(savedPlaylist);
	{
		QSignalBlocker blockProfile(contentProfile);
		QSignalBlocker blockMode(overlayMode);
		QSignalBlocker blockRotation(rotationSeconds);
		QSignalBlocker blockMessageOrder(messageOrder);
		QSignalBlocker blockMessagePlaylist(messagePlaylist);
		QSignalBlocker blockCountdown(countdownMinutes);
		const int profileIndex = contentProfile->findData(profileId);
		if (profileIndex >= 0)
			contentProfile->setCurrentIndex(profileIndex);
		rotationSeconds->setValue(
			std::clamp(activeProfileDocument.value(QStringLiteral("rotationSeconds")).toInt(6), 2, 30));
		const QString savedOrder = activeProfileDocument.value(QStringLiteral("rotationMode"))
						   .toString(QStringLiteral("sequential"));
		const int orderIndex = messageOrder->findData(savedOrder);
		messageOrder->setCurrentIndex(orderIndex >= 0 ? orderIndex : 0);
		const int playlistIndex = messagePlaylist->findData(NormalizePlaylistName(savedPlaylist));
		messagePlaylist->setCurrentIndex(playlistIndex >= 0 ? playlistIndex : 0);
		countdownMinutes->setValue(
			std::clamp(activeProfileDocument.value(QStringLiteral("countdownMinutes")).toInt(10), 1, 180));
		const QString mode =
			activeProfileDocument.value(QStringLiteral("activeMode")).toString(QStringLiteral("starting"));
		const int modeIndex = overlayMode->findData(mode);
		overlayMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
		activeModeId = CurrentModeId();
		QSignalBlocker blockTitle(streamTitle);
		QSignalBlocker blockStatus(statusLine);
		LoadModeState(activeModeId);
	}
	loadingProfile = false;
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "ContentProfile", activeProfileId.toUtf8().constData());
	config_save_safe(config, "tmp", nullptr);
	UpdateOverlayPath();
	UpdateCountdownPreview();
	RefreshRotationLibrarySummary();
	WatchContentProfileFiles();
	return true;
}

bool TempestControlDeck::EnsureOverlayDirectory()
{
	char path[1024];
	if (GetAppConfigPath(path, sizeof(path), "tempest-broadcast-system/control-deck") <= 0) {
		SetStatus(QStringLiteral("Unable to resolve the Tempest configuration directory."), true);
		return false;
	}

	overlayDirectory = QString::fromUtf8(path);
	if (!QDir().mkpath(overlayDirectory)) {
		SetStatus(QStringLiteral("Unable to create the overlay output directory."), true);
		return false;
	}

	UpdateOverlayPath();
	return EnsureRotationLibrary();
}

bool TempestControlDeck::EnsureRotationLibrary()
{
	if (overlayDirectory.isEmpty())
		return false;
	const QString vaultDirectory = QDir(overlayDirectory).filePath(QStringLiteral("vault-elements"));
	if (!QDir().mkpath(vaultDirectory))
		return false;
	globalRotationLibraryPath = QDir(vaultDirectory).filePath(QStringLiteral("rotating-lines.txt"));
	globalMessageVariablesPath = QDir(vaultDirectory).filePath(QStringLiteral("message-variables.json"));
	if (!EnsureVariablesFile(globalMessageVariablesPath, QStringLiteral("Shared message variables")))
		return false;
	if (QFileInfo::exists(globalRotationLibraryPath)) {
		RefreshRotationLibrarySummary();
		return true;
	}

	QStringList migrated;
	QSet<QString> seen;
	auto appendUnique = [&migrated, &seen](const QString &text) {
		const QStringList lines =
			text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
		for (const QString &line : lines) {
			const QString trimmed = line.trimmed();
			const QString key = trimmed.toCaseFolded();
			if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')) && !seen.contains(key)) {
				seen.insert(key);
				migrated.push_back(trimmed);
			}
		}
	};
	config_t *config = App()->GetUserConfig();
	for (const ModeDefinition &mode : Modes) {
		const QString modeId = QString::fromUtf8(mode.id);
		const QByteArray key = ModeKey("RotationMessages", modeId);
		const char *saved = config_get_string(config, ConfigSection, key.constData());
		appendUnique(saved && *saved ? QString::fromUtf8(saved) : QString::fromUtf8(mode.defaultMessages));
	}
	const char *legacy = config_get_string(config, ConfigSection, "RotationMessages");
	if (legacy && *legacy)
		appendUnique(QString::fromUtf8(legacy));

	QSaveFile file(globalRotationLibraryPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	QByteArray content = QByteArrayLiteral(
		"# Tempest Broadcast rotating message library\n"
		"# One message per line. Use [x] for active or [ ] for disabled; unmarked lines are active.\n"
		"# Group lines with @playlist common, starting, live, brb, ending, or a custom name.\n"
		"# Optional: Message text [[duration=8 accent=#45d9ff effect=pulse]]\n\n");
	content += migrated.join(QLatin1Char('\n')).toUtf8();
	content += '\n';
	if (file.write(content) != content.size() || !file.commit())
		return false;
	RefreshRotationLibrarySummary();
	return true;
}

void TempestControlDeck::RefreshMessagePlaylists(const QString &selectPlaylist)
{
	if (!messagePlaylist)
		return;
	QString selected = selectPlaylist.isEmpty() ? messagePlaylist->currentData().toString() : selectPlaylist;
	selected = NormalizePlaylistName(selected.isEmpty() ? QStringLiteral("auto") : selected);
	QStringList discovered;
	auto scanFile = [&discovered](const QString &path) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return;
		for (const QString &playlist : PlaylistNames(QString::fromUtf8(file.readAll()))) {
			if (!discovered.contains(playlist, Qt::CaseInsensitive))
				discovered.push_back(playlist);
		}
	};
	if (activeProfileDocument.value(QStringLiteral("includeGlobalMessages")).toBool(true))
		scanFile(globalRotationLibraryPath);
	scanFile(rotationLibraryPath);

	QSignalBlocker blocker(messagePlaylist);
	messagePlaylist->clear();
	messagePlaylist->addItem(QStringLiteral("Auto // Common + Overlay Mode"), QStringLiteral("auto"));
	messagePlaylist->addItem(QStringLiteral("Common Only"), QStringLiteral("common"));
	messagePlaylist->addItem(QStringLiteral("All Sections"), QStringLiteral("all"));
	for (const ModeDefinition &mode : Modes)
		messagePlaylist->addItem(QString::fromUtf8(mode.label), QString::fromUtf8(mode.id));
	for (const QString &playlist : discovered) {
		if (messagePlaylist->findData(playlist) >= 0)
			continue;
		QString display = playlist;
		display.replace(QLatin1Char('-'), QLatin1Char(' '));
		messagePlaylist->addItem(QStringLiteral("Custom // %1").arg(display.toUpper()), playlist);
	}
	if (messagePlaylist->findData(selected) < 0)
		messagePlaylist->addItem(QStringLiteral("Custom // %1").arg(selected.toUpper()), selected);
	const int index = messagePlaylist->findData(selected);
	messagePlaylist->setCurrentIndex(index >= 0 ? index : 0);
}

QStringList TempestControlDeck::ReadRotationMessages(const QString &fallback, const QString &modeId) const
{
	QStringList messages;
	QSet<QString> seen;
	const QString requestedMode = modeId.isEmpty() ? activeModeId : modeId;
	const QString playlist = messagePlaylist && !messagePlaylist->currentData().toString().isEmpty()
					 ? messagePlaylist->currentData().toString()
					 : activeProfileDocument.value(QStringLiteral("messagePlaylist"))
						   .toString(QStringLiteral("auto"));
	auto readFile = [&messages, &seen, &playlist, &requestedMode](const QString &path) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return;
		for (const QString &line :
		     ActiveMessageLines(QString::fromUtf8(file.readAll()), playlist, requestedMode)) {
			const QString key = line.toCaseFolded();
			if (!seen.contains(key)) {
				seen.insert(key);
				messages.push_back(line);
			}
		}
	};
	if (activeProfileDocument.value(QStringLiteral("includeGlobalMessages")).toBool(true))
		readFile(globalRotationLibraryPath);
	readFile(rotationLibraryPath);
	if (messages.isEmpty() && activeProfileId.isEmpty()) {
		messages = ActiveMessageLines(fallback, playlist, requestedMode);
	}
	return messages;
}

QJsonObject TempestControlDeck::ReadMessageVariables() const
{
	QJsonObject variables;
	auto mergeFile = [&variables](const QString &path) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return;
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
		if (!document.isObject())
			return;
		const QJsonObject incoming = document.object();
		for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it)
			variables.insert(it.key().toCaseFolded(), it.value());
	};
	mergeFile(globalMessageVariablesPath);
	mergeFile(messageVariablesPath);
	return variables;
}

void TempestControlDeck::ComposeRotationMessage()
{
	const QString activePlaylistSelection = messagePlaylist && !messagePlaylist->currentData().toString().isEmpty()
							? messagePlaylist->currentData().toString()
							: QStringLiteral("auto");
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("tempestMessageComposer"));
	dialog.setWindowTitle(QStringLiteral("Rotating Message Composer"));
	dialog.setMinimumWidth(qRound(560.0 * ContentScalePercent() / 100.0));
	auto *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(14, 14, 14, 14);
	layout->setSpacing(10);

	auto *heading = new QLabel(QStringLiteral("MESSAGE PRESENTATION // PROFILE ASSET"), &dialog);
	heading->setObjectName(QStringLiteral("layoutHeader"));
	auto *description = new QLabel(
		QStringLiteral(
			"Compose one rotating line. Optional presentation controls are stored as portable text metadata."),
		&dialog);
	description->setWordWrap(true);
	layout->addWidget(heading);
	layout->addWidget(description);

	auto *form = new QFormLayout();
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	auto *messageText = new QLineEdit(&dialog);
	messageText->setPlaceholderText(QStringLiteral("Welcome {{channel}}"));
	messageText->setClearButtonEnabled(true);

	auto *playlist = new QComboBox(&dialog);
	playlist->setEditable(true);
	playlist->setInsertPolicy(QComboBox::NoInsert);
	QStringList playlistIds = {QStringLiteral("common"), QStringLiteral("starting"), QStringLiteral("live"),
				   QStringLiteral("brb"), QStringLiteral("ending")};
	auto collectPlaylists = [&playlistIds](const QString &path) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return;
		for (const QString &name : PlaylistNames(QString::fromUtf8(file.readAll()))) {
			if (!playlistIds.contains(name, Qt::CaseInsensitive))
				playlistIds.push_back(name);
		}
	};
	collectPlaylists(globalRotationLibraryPath);
	collectPlaylists(rotationLibraryPath);
	for (const QString &name : playlistIds) {
		const QString label = name == QStringLiteral("common") ? QStringLiteral("COMMON // ALL MODES")
								       : name.toUpper();
		playlist->addItem(label, name);
	}

	auto *activeMessage = new QCheckBox(QStringLiteral("Active in rotation"), &dialog);
	activeMessage->setChecked(true);
	form->addRow(QStringLiteral("Message"), messageText);
	form->addRow(QStringLiteral("Playlist"), playlist);
	form->addRow(QStringLiteral("State"), activeMessage);

	auto *durationRow = new QWidget(&dialog);
	auto *durationLayout = new QHBoxLayout(durationRow);
	durationLayout->setContentsMargins(0, 0, 0, 0);
	durationLayout->setSpacing(8);
	auto *durationOverride = new QCheckBox(QStringLiteral("Override"), durationRow);
	auto *duration = new QDoubleSpinBox(durationRow);
	duration->setRange(2.0, 60.0);
	duration->setDecimals(1);
	duration->setSingleStep(0.5);
	duration->setSuffix(QStringLiteral(" seconds"));
	duration->setValue(rotationSeconds ? rotationSeconds->value() : 6.0);
	duration->setEnabled(false);
	durationLayout->addWidget(durationOverride);
	durationLayout->addWidget(duration, 1);
	connect(durationOverride, &QCheckBox::toggled, duration, &QWidget::setEnabled);
	form->addRow(QStringLiteral("Timing"), durationRow);

	QColor selectedAccent(QStringLiteral("#45d9ff"));
	auto *accentRow = new QWidget(&dialog);
	auto *accentLayout = new QHBoxLayout(accentRow);
	accentLayout->setContentsMargins(0, 0, 0, 0);
	accentLayout->setSpacing(8);
	auto *accentOverride = new QCheckBox(QStringLiteral("Override"), accentRow);
	auto *accentButton = new QPushButton(accentRow);
	accentButton->setEnabled(false);
	accentLayout->addWidget(accentOverride);
	accentLayout->addWidget(accentButton, 1);
	connect(accentOverride, &QCheckBox::toggled, accentButton, &QWidget::setEnabled);
	form->addRow(QStringLiteral("Accent"), accentRow);

	auto *effect = new QComboBox(&dialog);
	effect->addItem(QStringLiteral("NONE // STATIC"), QStringLiteral("none"));
	effect->addItem(QStringLiteral("PULSE // SOFT BEACON"), QStringLiteral("pulse"));
	effect->addItem(QStringLiteral("GLITCH // SIGNAL BREAK"), QStringLiteral("glitch"));
	effect->addItem(QStringLiteral("ALERT // HIGH ENERGY"), QStringLiteral("alert"));
	form->addRow(QStringLiteral("Effect"), effect);
	layout->addLayout(form);

	auto *previewFrame = new QFrame(&dialog);
	previewFrame->setObjectName(QStringLiteral("messagePreviewFrame"));
	auto *previewLayout = new QVBoxLayout(previewFrame);
	previewLayout->setContentsMargins(14, 12, 14, 12);
	auto *previewHeader = new QLabel(QStringLiteral("OUTPUT PREVIEW"), previewFrame);
	previewHeader->setObjectName(QStringLiteral("layoutHeader"));
	auto *preview = new QLabel(QStringLiteral("MESSAGE PREVIEW"), previewFrame);
	preview->setAlignment(Qt::AlignCenter);
	preview->setMinimumHeight(qRound(58.0 * ContentScalePercent() / 100.0));
	preview->setWordWrap(true);
	auto *syntax = new QLabel(previewFrame);
	syntax->setTextInteractionFlags(Qt::TextSelectableByMouse);
	syntax->setWordWrap(true);
	previewLayout->addWidget(previewHeader);
	previewLayout->addWidget(preview);
	previewLayout->addWidget(syntax);
	layout->addWidget(previewFrame);

	auto formattedLine = [&]() {
		QString line = messageText->text().trimmed();
		QStringList attributes;
		if (durationOverride->isChecked()) {
			QString seconds = QString::number(duration->value(), 'f', 1);
			seconds.remove(QRegularExpression(QStringLiteral("\\.0$")));
			attributes.push_back(QStringLiteral("duration=%1").arg(seconds));
		}
		if (accentOverride->isChecked())
			attributes.push_back(
				QStringLiteral("accent=%1").arg(selectedAccent.name(QColor::HexRgb).toUpper()));
		const QString effectId = effect->currentData().toString();
		if (effectId != QStringLiteral("none"))
			attributes.push_back(QStringLiteral("effect=%1").arg(effectId));
		if (!attributes.isEmpty())
			line += QStringLiteral(" [[%1]]").arg(attributes.join(QLatin1Char(' ')));
		if (!activeMessage->isChecked())
			line.prepend(QStringLiteral("[ ] "));
		return line;
	};

	auto updateAccentButton = [&]() {
		accentButton->setText(
			QStringLiteral("SELECTED // %1").arg(selectedAccent.name(QColor::HexRgb).toUpper()));
		accentButton->setStyleSheet(
			QStringLiteral("QPushButton { border-left: 10px solid %1; }").arg(selectedAccent.name()));
	};
	updateAccentButton();
	connect(accentButton, &QPushButton::clicked, &dialog, [&]() {
		const QColor chosen = QColorDialog::getColor(selectedAccent, &dialog,
							     QStringLiteral("Choose Message Accent"),
							     QColorDialog::DontUseNativeDialog);
		if (!chosen.isValid())
			return;
		selectedAccent = chosen;
		updateAccentButton();
		preview->setStyleSheet(QStringLiteral("QLabel { color: %1; border: 1px solid %1; padding: 10px; }")
					       .arg(selectedAccent.name()));
		syntax->setText(formattedLine());
	});

	auto updatePreview = [&]() {
		const QString text = messageText->text().trimmed();
		const QString color = accentOverride->isChecked() ? selectedAccent.name() : QStringLiteral("#45d9ff");
		preview->setText(text.isEmpty() ? QStringLiteral("MESSAGE PREVIEW") : text);
		preview->setStyleSheet(
			QStringLiteral("QLabel { color: %1; border: 1px solid %1; padding: 10px; font-weight: 700; }")
				.arg(color));
		previewHeader->setText(
			QStringLiteral("OUTPUT PREVIEW // %1 // %2")
				.arg(activeMessage->isChecked() ? QStringLiteral("ACTIVE") : QStringLiteral("DISABLED"),
				     effect->currentData().toString().toUpper()));
		syntax->setText(formattedLine());
	};
	connect(messageText, &QLineEdit::textChanged, &dialog, [updatePreview](const QString &) { updatePreview(); });
	connect(activeMessage, &QCheckBox::toggled, &dialog, [updatePreview](bool) { updatePreview(); });
	connect(durationOverride, &QCheckBox::toggled, &dialog, [updatePreview](bool) { updatePreview(); });
	connect(duration, &QDoubleSpinBox::valueChanged, &dialog, [updatePreview](double) { updatePreview(); });
	connect(accentOverride, &QCheckBox::toggled, &dialog, [updatePreview](bool) { updatePreview(); });
	connect(effect, &QComboBox::currentIndexChanged, &dialog, [updatePreview](int) { updatePreview(); });
	updatePreview();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("ADD TO PROFILE"));
	buttons->button(QDialogButtonBox::Save)->setEnabled(false);
	connect(messageText, &QLineEdit::textChanged, buttons->button(QDialogButtonBox::Save),
		[save = buttons->button(QDialogButtonBox::Save)](const QString &text) {
			save->setEnabled(!text.trimmed().isEmpty());
		});
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	messageText->setFocus();

	if (dialog.exec() != QDialog::Accepted)
		return;
	const bool selectedPreset = playlist->currentIndex() >= 0 &&
				    playlist->currentText() == playlist->itemText(playlist->currentIndex());
	const QString targetPlaylist =
		NormalizePlaylistName(selectedPreset ? playlist->currentData().toString() : playlist->currentText());
	if (!AppendRotationMessages(formattedLine(), targetPlaylist))
		return;
	RefreshMessagePlaylists(activePlaylistSelection);
	RefreshRotationLibrarySummary();
	QueueOverlayRender();
	SetStatus(
		QStringLiteral("Message added // %1 playlist // overlay reload queued").arg(targetPlaylist.toUpper()));
}

bool TempestControlDeck::AppendRotationMessages(const QString &messages, const QString &playlist)
{
	if (messages.trimmed().isEmpty())
		return true;
	if (rotationLibraryPath.isEmpty() &&
	    (activeProfileId.isEmpty() ||
	     !EnsureMessageFile(ProfileMessagePath(activeProfileId), QStringLiteral("Stream profile messages"))))
		return false;
	QFile source(rotationLibraryPath);
	QByteArray content;
	if (source.open(QIODevice::ReadOnly | QIODevice::Text)) {
		content = source.readAll();
		source.close();
	}
	QSet<QString> existing;
	const QRegularExpression stateMarker(QStringLiteral("^\\[\\s*[xX]?\\s*\\]\\s*"));
	QString appendSection = QStringLiteral("common");
	const QString targetSection = NormalizePlaylistName(playlist);
	for (QString line : QString::fromUtf8(content).split(QRegularExpression(QStringLiteral("[\\r\\n]+")))) {
		line = line.trimmed();
		if (line.startsWith(QLatin1Char('#')))
			continue;
		if (PlaylistDirective(line, &appendSection))
			continue;
		line.remove(stateMarker);
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')))
			existing.insert(appendSection + QLatin1Char('\n') + trimmed.toCaseFolded());
	}
	QStringList additions;
	for (const QString &line :
	     messages.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
		const QString trimmed = line.trimmed();
		const QString key = targetSection + QLatin1Char('\n') + trimmed.toCaseFolded();
		if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')) && !existing.contains(key)) {
			existing.insert(key);
			additions.push_back(trimmed);
		}
	}
	if (additions.isEmpty())
		return true;
	if (!content.isEmpty() && !content.endsWith('\n'))
		content += '\n';
	if (appendSection != targetSection)
		content += QStringLiteral("@playlist %1\n").arg(targetSection).toUtf8();
	content += additions.join(QLatin1Char('\n')).toUtf8();
	content += '\n';
	QSaveFile file(rotationLibraryPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(content) != content.size() ||
	    !file.commit()) {
		SetStatus(QStringLiteral("Unable to update the rotating message library."), true);
		return false;
	}
	RefreshRotationLibrarySummary();
	return true;
}

void TempestControlDeck::RefreshRotationLibrarySummary()
{
	if (!rotationLibraryButton || rotationLibraryPath.isEmpty())
		return;
	const int count = ReadRotationMessages(QString(), activeModeId).size();
	rotationLibraryButton->setText(QStringLiteral("Manage Profile Messages // %1 Line%2")
					       .arg(count)
					       .arg(count == 1 ? QString() : QStringLiteral("s")));
	rotationLibraryButton->setToolTip(
		QStringLiteral(
			"Profile messages: %1\nShared messages: %2\nOne message per line; saved changes reload live.")
			.arg(QDir::toNativeSeparators(rotationLibraryPath),
			     activeProfileDocument.value(QStringLiteral("includeGlobalMessages")).toBool(true)
				     ? QDir::toNativeSeparators(globalRotationLibraryPath)
				     : QStringLiteral("disabled by profile.json")));
	WatchContentProfileFiles();
}

void TempestControlDeck::OpenRotationLibrary()
{
	if (rotationLibraryPath.isEmpty() ||
	    !EnsureMessageFile(rotationLibraryPath, QStringLiteral("Stream profile messages"))) {
		SetStatus(QStringLiteral("Unable to create the rotating message library."), true);
		return;
	}

	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("tempestMessageManager"));
	dialog.setWindowTitle(QStringLiteral("Profile Message Manager"));
	dialog.resize(qRound(860.0 * ContentScalePercent() / 100.0), qRound(560.0 * ContentScalePercent() / 100.0));
	auto *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(14, 14, 14, 14);
	layout->setSpacing(9);

	auto *heading = new QLabel(
		QStringLiteral("PROFILE MESSAGE MANAGER // %1")
			.arg(activeProfileDocument.value(QStringLiteral("name")).toString(activeProfileId).toUpper()),
		&dialog);
	heading->setObjectName(QStringLiteral("layoutHeader"));
	auto *description = new QLabel(
		QStringLiteral(
			"Manage saved profile lines visually. Comments and playlist directives remain untouched."),
		&dialog);
	description->setWordWrap(true);
	auto *countLabel = new QLabel(&dialog);
	countLabel->setObjectName(QStringLiteral("layoutHeader"));
	layout->addWidget(heading);
	layout->addWidget(description);
	layout->addWidget(countLabel);

	auto *tree = new QTreeWidget(&dialog);
	tree->setObjectName(QStringLiteral("profileMessageTree"));
	tree->setColumnCount(4);
	tree->setHeaderLabels({QStringLiteral("STATE"), QStringLiteral("PLAYLIST"), QStringLiteral("MESSAGE"),
			       QStringLiteral("PRESENTATION")});
	tree->setRootIsDecorated(false);
	tree->setAlternatingRowColors(true);
	tree->setSelectionMode(QAbstractItemView::SingleSelection);
	tree->setSelectionBehavior(QAbstractItemView::SelectRows);
	tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
	tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	layout->addWidget(tree, 1);

	auto *primaryActions = new QHBoxLayout();
	primaryActions->setSpacing(7);
	auto *compose = new QPushButton(QStringLiteral("COMPOSE NEW"), &dialog);
	auto *edit = new QPushButton(QStringLiteral("EDIT LINE"), &dialog);
	auto *duplicate = new QPushButton(QStringLiteral("DUPLICATE"), &dialog);
	auto *toggle = new QPushButton(QStringLiteral("ENABLE / DISABLE"), &dialog);
	primaryActions->addWidget(compose);
	primaryActions->addWidget(edit);
	primaryActions->addWidget(duplicate);
	primaryActions->addWidget(toggle);
	layout->addLayout(primaryActions);

	auto *secondaryActions = new QHBoxLayout();
	secondaryActions->setSpacing(7);
	auto *moveUp = new QPushButton(QStringLiteral("MOVE UP"), &dialog);
	auto *moveDown = new QPushButton(QStringLiteral("MOVE DOWN"), &dialog);
	auto *remove = new QPushButton(QStringLiteral("REMOVE"), &dialog);
	auto *openTextFile = new QPushButton(QStringLiteral("OPEN TEXT FILE"), &dialog);
	secondaryActions->addWidget(moveUp);
	secondaryActions->addWidget(moveDown);
	secondaryActions->addWidget(remove);
	secondaryActions->addStretch(1);
	secondaryActions->addWidget(openTextFile);
	layout->addLayout(secondaryActions);

	QStringList documentLines;
	auto loadDocument = [&]() {
		QFile file(rotationLibraryPath);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return false;
		documentLines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::KeepEmptyParts);
		file.close();
		return true;
	};
	auto saveDocument = [&]() {
		const QByteArray content = documentLines.join(QLatin1Char('\n')).toUtf8();
		QSaveFile file(rotationLibraryPath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(content) != content.size() ||
		    !file.commit()) {
			SetStatus(QStringLiteral("Unable to save profile messages."), true);
			return false;
		}
		const QString selectedPlaylist = messagePlaylist ? messagePlaylist->currentData().toString()
								 : QStringLiteral("auto");
		RefreshMessagePlaylists(selectedPlaylist);
		RefreshRotationLibrarySummary();
		WatchContentProfileFiles();
		QueueOverlayRender();
		return true;
	};

	const QRegularExpression stateMarker(QStringLiteral("^\\[\\s*([xX]?)\\s*\\]\\s*"));
	const QRegularExpression metadataBlock(QStringLiteral("\\s*\\[\\[([^\\]]+)\\]\\]\\s*$"));
	auto refreshTree = [&](int preferredLine = -1) {
		tree->clear();
		QString playlist = QStringLiteral("common");
		QTreeWidgetItem *preferredItem = nullptr;
		for (int lineIndex = 0; lineIndex < documentLines.size(); ++lineIndex) {
			QString raw = documentLines[lineIndex].trimmed();
			if (PlaylistDirective(raw, &playlist) || raw.isEmpty() || raw.startsWith(QLatin1Char('#')))
				continue;
			const QRegularExpressionMatch state = stateMarker.match(raw);
			const bool hasState = state.hasMatch();
			const bool enabled = !hasState || !state.captured(1).isEmpty();
			if (hasState)
				raw.remove(stateMarker);
			raw = raw.trimmed();
			QString presentation = QStringLiteral("PROFILE DEFAULT");
			const QRegularExpressionMatch metadata = metadataBlock.match(raw);
			if (metadata.hasMatch()) {
				presentation = metadata.captured(1).toUpper();
				raw = raw.left(metadata.capturedStart()).trimmed();
			}
			auto *item = new QTreeWidgetItem(tree);
			item->setText(0, enabled ? QStringLiteral("ACTIVE") : QStringLiteral("DISABLED"));
			item->setText(1, playlist.toUpper());
			item->setText(2, raw);
			item->setText(3, presentation);
			item->setToolTip(2, documentLines[lineIndex].trimmed());
			item->setData(0, Qt::UserRole, lineIndex);
			item->setData(0, Qt::UserRole + 1, enabled);
			if (lineIndex == preferredLine)
				preferredItem = item;
		}
		countLabel->setText(QStringLiteral("%1 PROFILE LINE%2 // SHARED LIBRARY REMAINS SEPARATE")
					    .arg(tree->topLevelItemCount())
					    .arg(tree->topLevelItemCount() == 1 ? QString() : QStringLiteral("S")));
		if (preferredItem)
			tree->setCurrentItem(preferredItem);
		else if (tree->topLevelItemCount())
			tree->setCurrentItem(tree->topLevelItem(0));
	};

	auto selectedLine = [&]() {
		return tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toInt() : -1;
	};
	auto updateActions = [&]() {
		QTreeWidgetItem *item = tree->currentItem();
		const bool selected = item != nullptr;
		edit->setEnabled(selected);
		duplicate->setEnabled(selected);
		toggle->setEnabled(selected);
		remove->setEnabled(selected);
		if (selected)
			toggle->setText(item->data(0, Qt::UserRole + 1).toBool() ? QStringLiteral("DISABLE")
										 : QStringLiteral("ENABLE"));
		const int row = selected ? tree->indexOfTopLevelItem(item) : -1;
		moveUp->setEnabled(row > 0 && tree->topLevelItem(row - 1)->text(1) == item->text(1));
		moveDown->setEnabled(row >= 0 && row + 1 < tree->topLevelItemCount() &&
				     tree->topLevelItem(row + 1)->text(1) == item->text(1));
	};
	connect(tree, &QTreeWidget::currentItemChanged, &dialog,
		[updateActions](QTreeWidgetItem *, QTreeWidgetItem *) { updateActions(); });

	auto editSelected = [&]() {
		const int lineIndex = selectedLine();
		if (lineIndex < 0 || lineIndex >= documentLines.size())
			return;

		QString raw = documentLines[lineIndex].trimmed();
		const QRegularExpressionMatch state = stateMarker.match(raw);
		const bool explicitActive = state.hasMatch() && !state.captured(1).isEmpty();
		const bool initiallyEnabled = !state.hasMatch() || explicitActive;
		if (state.hasMatch())
			raw.remove(stateMarker);
		raw = raw.trimmed();

		bool hasDuration = false;
		double durationSeconds = rotationSeconds ? rotationSeconds->value() : 6.0;
		bool hasAccent = false;
		QColor accentColor(QStringLiteral("#45d9ff"));
		QString effectId = QStringLiteral("none");
		QStringList preservedAttributes;
		const QRegularExpressionMatch metadata = metadataBlock.match(raw);
		if (metadata.hasMatch()) {
			const QRegularExpression attributeExpression(
				QStringLiteral("([a-z][a-z0-9_-]*)\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s]+)"),
				QRegularExpression::CaseInsensitiveOption);
			QRegularExpressionMatchIterator attributes =
				attributeExpression.globalMatch(metadata.captured(1));
			while (attributes.hasNext()) {
				const QRegularExpressionMatch attribute = attributes.next();
				const QString key = attribute.captured(1).toCaseFolded();
				QString value = attribute.captured(2).trimmed();
				if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) ||
				    (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))
					value = value.mid(1, value.size() - 2);
				bool consumed = false;
				if (key == QStringLiteral("duration")) {
					bool valid = false;
					double amount = 0.0;
					if (value.endsWith(QStringLiteral("ms"), Qt::CaseInsensitive))
						amount = value.left(value.size() - 2).toDouble(&valid) / 1000.0;
					else
						amount = value.toDouble(&valid);
					if (valid) {
						hasDuration = true;
						durationSeconds = qBound(2.0, amount, 60.0);
						consumed = true;
					}
				} else if (key == QStringLiteral("accent") &&
					   QRegularExpression(QStringLiteral("^#[0-9a-f]{3}(?:[0-9a-f]{3})?$"),
							      QRegularExpression::CaseInsensitiveOption)
						   .match(value)
						   .hasMatch()) {
					hasAccent = true;
					accentColor = QColor(value);
					consumed = true;
				} else if (key == QStringLiteral("effect") &&
					   QStringList{QStringLiteral("none"), QStringLiteral("pulse"),
						       QStringLiteral("glitch"), QStringLiteral("alert")}
						   .contains(value.toCaseFolded())) {
					effectId = value.toCaseFolded();
					consumed = true;
				}
				if (!consumed)
					preservedAttributes.push_back(attribute.captured(0));
			}
			raw = raw.left(metadata.capturedStart()).trimmed();
		}

		QDialog editor(&dialog);
		editor.setObjectName(QStringLiteral("tempestVisualMessageEditor"));
		editor.setWindowTitle(QStringLiteral("Visual Message Editor"));
		editor.setMinimumWidth(qRound(580.0 * ContentScalePercent() / 100.0));
		auto *editorLayout = new QVBoxLayout(&editor);
		editorLayout->setContentsMargins(14, 14, 14, 14);
		editorLayout->setSpacing(10);

		auto *editorHeading = new QLabel(
			QStringLiteral("EDIT MESSAGE // %1 PLAYLIST").arg(tree->currentItem()->text(1)), &editor);
		editorHeading->setObjectName(QStringLiteral("layoutHeader"));
		editorLayout->addWidget(editorHeading);
		auto *editorForm = new QFormLayout();
		editorForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		auto *messageText = new QLineEdit(raw, &editor);
		messageText->setClearButtonEnabled(true);
		auto *active = new QCheckBox(QStringLiteral("Active in rotation"), &editor);
		active->setChecked(initiallyEnabled);
		editorForm->addRow(QStringLiteral("Message"), messageText);
		editorForm->addRow(QStringLiteral("State"), active);

		auto *durationRow = new QWidget(&editor);
		auto *durationLayout = new QHBoxLayout(durationRow);
		durationLayout->setContentsMargins(0, 0, 0, 0);
		durationLayout->setSpacing(8);
		auto *durationOverride = new QCheckBox(QStringLiteral("Override"), durationRow);
		durationOverride->setChecked(hasDuration);
		auto *duration = new QDoubleSpinBox(durationRow);
		duration->setRange(2.0, 60.0);
		duration->setDecimals(1);
		duration->setSingleStep(0.5);
		duration->setSuffix(QStringLiteral(" seconds"));
		duration->setValue(durationSeconds);
		duration->setEnabled(hasDuration);
		durationLayout->addWidget(durationOverride);
		durationLayout->addWidget(duration, 1);
		connect(durationOverride, &QCheckBox::toggled, duration, &QWidget::setEnabled);
		editorForm->addRow(QStringLiteral("Timing"), durationRow);

		auto *accentRow = new QWidget(&editor);
		auto *accentLayout = new QHBoxLayout(accentRow);
		accentLayout->setContentsMargins(0, 0, 0, 0);
		accentLayout->setSpacing(8);
		auto *accentOverride = new QCheckBox(QStringLiteral("Override"), accentRow);
		accentOverride->setChecked(hasAccent);
		auto *accentButton = new QPushButton(accentRow);
		accentButton->setEnabled(hasAccent);
		accentLayout->addWidget(accentOverride);
		accentLayout->addWidget(accentButton, 1);
		connect(accentOverride, &QCheckBox::toggled, accentButton, &QWidget::setEnabled);
		editorForm->addRow(QStringLiteral("Accent"), accentRow);

		auto *effect = new QComboBox(&editor);
		effect->addItem(QStringLiteral("NONE // STATIC"), QStringLiteral("none"));
		effect->addItem(QStringLiteral("PULSE // SOFT BEACON"), QStringLiteral("pulse"));
		effect->addItem(QStringLiteral("GLITCH // SIGNAL BREAK"), QStringLiteral("glitch"));
		effect->addItem(QStringLiteral("ALERT // HIGH ENERGY"), QStringLiteral("alert"));
		effect->setCurrentIndex(std::max(0, effect->findData(effectId)));
		editorForm->addRow(QStringLiteral("Effect"), effect);
		editorLayout->addLayout(editorForm);

		if (!preservedAttributes.isEmpty()) {
			auto *preserved = new QLabel(QStringLiteral("PRESERVED METADATA // %1")
							     .arg(preservedAttributes.join(QLatin1Char(' '))),
						     &editor);
			preserved->setObjectName(QStringLiteral("layoutHeader"));
			preserved->setWordWrap(true);
			preserved->setToolTip(QStringLiteral(
				"These unsupported attributes will be retained unchanged when the line is saved."));
			editorLayout->addWidget(preserved);
		}

		auto *preview = new QLabel(&editor);
		preview->setAlignment(Qt::AlignCenter);
		preview->setWordWrap(true);
		preview->setMinimumHeight(qRound(58.0 * ContentScalePercent() / 100.0));
		auto *syntax = new QLabel(&editor);
		syntax->setTextInteractionFlags(Qt::TextSelectableByMouse);
		syntax->setWordWrap(true);
		editorLayout->addWidget(preview);
		editorLayout->addWidget(syntax);

		auto buildEditedLine = [&]() {
			QString line = messageText->text().trimmed();
			QStringList attributes;
			if (durationOverride->isChecked()) {
				QString seconds = QString::number(duration->value(), 'f', 1);
				seconds.remove(QRegularExpression(QStringLiteral("\\.0$")));
				attributes.push_back(QStringLiteral("duration=%1").arg(seconds));
			}
			if (accentOverride->isChecked())
				attributes.push_back(
					QStringLiteral("accent=%1").arg(accentColor.name(QColor::HexRgb).toUpper()));
			if (effect->currentData().toString() != QStringLiteral("none"))
				attributes.push_back(QStringLiteral("effect=%1").arg(effect->currentData().toString()));
			attributes.append(preservedAttributes);
			if (!attributes.isEmpty())
				line += QStringLiteral(" [[%1]]").arg(attributes.join(QLatin1Char(' ')));
			if (!active->isChecked())
				line.prepend(QStringLiteral("[ ] "));
			else if (explicitActive)
				line.prepend(QStringLiteral("[x] "));
			return line;
		};
		auto updateAccentButton = [&]() {
			accentButton->setText(
				QStringLiteral("SELECTED // %1").arg(accentColor.name(QColor::HexRgb).toUpper()));
			accentButton->setStyleSheet(
				QStringLiteral("QPushButton { border-left: 10px solid %1; }").arg(accentColor.name()));
		};
		auto updatePreview = [&]() {
			const QString color = accentOverride->isChecked() ? accentColor.name()
									  : QStringLiteral("#45d9ff");
			preview->setText(messageText->text().trimmed().isEmpty() ? QStringLiteral("MESSAGE PREVIEW")
										 : messageText->text().trimmed());
			preview->setStyleSheet(
				QStringLiteral(
					"QLabel { color: %1; border: 1px solid %1; padding: 10px; font-weight: 700; }")
					.arg(color));
			syntax->setText(buildEditedLine());
		};
		updateAccentButton();
		updatePreview();
		connect(accentButton, &QPushButton::clicked, &editor, [&]() {
			const QColor chosen = QColorDialog::getColor(accentColor, &editor,
								     QStringLiteral("Choose Message Accent"),
								     QColorDialog::DontUseNativeDialog);
			if (!chosen.isValid())
				return;
			accentColor = chosen;
			updateAccentButton();
			updatePreview();
		});
		connect(messageText, &QLineEdit::textChanged, &editor,
			[updatePreview](const QString &) { updatePreview(); });
		connect(active, &QCheckBox::toggled, &editor, [updatePreview](bool) { updatePreview(); });
		connect(durationOverride, &QCheckBox::toggled, &editor, [updatePreview](bool) { updatePreview(); });
		connect(duration, &QDoubleSpinBox::valueChanged, &editor, [updatePreview](double) { updatePreview(); });
		connect(accentOverride, &QCheckBox::toggled, &editor, [updatePreview](bool) { updatePreview(); });
		connect(effect, &QComboBox::currentIndexChanged, &editor, [updatePreview](int) { updatePreview(); });

		auto *editorButtons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);
		editorButtons->button(QDialogButtonBox::Save)->setText(QStringLiteral("SAVE MESSAGE"));
		editorButtons->button(QDialogButtonBox::Save)->setEnabled(!messageText->text().trimmed().isEmpty());
		connect(messageText, &QLineEdit::textChanged, editorButtons->button(QDialogButtonBox::Save),
			[save = editorButtons->button(QDialogButtonBox::Save)](const QString &text) {
				save->setEnabled(!text.trimmed().isEmpty());
			});
		connect(editorButtons, &QDialogButtonBox::accepted, &editor, &QDialog::accept);
		connect(editorButtons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
		editorLayout->addWidget(editorButtons);
		messageText->setFocus();
		if (editor.exec() != QDialog::Accepted)
			return;
		documentLines[lineIndex] = buildEditedLine();
		if (saveDocument()) {
			refreshTree(lineIndex);
			SetStatus(QStringLiteral("Profile message updated // overlay reload queued"));
		}
	};
	connect(tree, &QTreeWidget::itemDoubleClicked, &dialog,
		[editSelected](QTreeWidgetItem *, int) { editSelected(); });
	connect(edit, &QPushButton::clicked, &dialog, editSelected);
	connect(compose, &QPushButton::clicked, &dialog, [&]() {
		ComposeRotationMessage();
		if (loadDocument())
			refreshTree();
	});
	connect(duplicate, &QPushButton::clicked, &dialog, [&]() {
		const int lineIndex = selectedLine();
		if (lineIndex < 0 || lineIndex >= documentLines.size())
			return;
		documentLines.insert(lineIndex + 1, documentLines[lineIndex]);
		if (saveDocument()) {
			refreshTree(lineIndex + 1);
			SetStatus(QStringLiteral("Profile message duplicated // overlay reload queued"));
		}
	});
	connect(toggle, &QPushButton::clicked, &dialog, [&]() {
		const int lineIndex = selectedLine();
		if (lineIndex < 0 || lineIndex >= documentLines.size())
			return;
		QString raw = documentLines[lineIndex].trimmed();
		const bool enabled = tree->currentItem()->data(0, Qt::UserRole + 1).toBool();
		raw.remove(stateMarker);
		documentLines[lineIndex] = enabled ? QStringLiteral("[ ] %1").arg(raw.trimmed())
						   : QStringLiteral("[x] %1").arg(raw.trimmed());
		if (saveDocument()) {
			refreshTree(lineIndex);
			SetStatus(enabled ? QStringLiteral("Profile message disabled // overlay reload queued")
					  : QStringLiteral("Profile message enabled // overlay reload queued"));
		}
	});
	auto moveSelected = [&](int direction) {
		QTreeWidgetItem *item = tree->currentItem();
		if (!item)
			return;
		const int row = tree->indexOfTopLevelItem(item);
		const int targetRow = row + direction;
		if (targetRow < 0 || targetRow >= tree->topLevelItemCount())
			return;
		QTreeWidgetItem *targetItem = tree->topLevelItem(targetRow);
		if (targetItem->text(1) != item->text(1))
			return;
		const int lineIndex = item->data(0, Qt::UserRole).toInt();
		const int targetLine = targetItem->data(0, Qt::UserRole).toInt();
		documentLines.swapItemsAt(lineIndex, targetLine);
		if (saveDocument()) {
			refreshTree(targetLine);
			SetStatus(QStringLiteral("Profile message order updated // overlay reload queued"));
		}
	};
	connect(moveUp, &QPushButton::clicked, &dialog, [&]() { moveSelected(-1); });
	connect(moveDown, &QPushButton::clicked, &dialog, [&]() { moveSelected(1); });
	connect(remove, &QPushButton::clicked, &dialog, [&]() {
		const int lineIndex = selectedLine();
		if (lineIndex < 0 || lineIndex >= documentLines.size())
			return;
		if (QMessageBox::question(&dialog, QStringLiteral("Remove Profile Message"),
					  QStringLiteral("Remove this line from the active profile?")) !=
		    QMessageBox::Yes)
			return;
		documentLines.removeAt(lineIndex);
		if (saveDocument()) {
			refreshTree();
			SetStatus(QStringLiteral("Profile message removed // overlay reload queued"));
		}
	});
	connect(openTextFile, &QPushButton::clicked, &dialog, [&]() {
		if (!QDesktopServices::openUrl(QUrl::fromLocalFile(rotationLibraryPath))) {
			SetStatus(QStringLiteral("Unable to open the rotating message library."), true);
			return;
		}
		SetStatus(QStringLiteral("Message text asset opened // save changes to update live"));
		dialog.accept();
	});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (!loadDocument()) {
		SetStatus(QStringLiteral("Unable to read the rotating message library."), true);
		return;
	}
	refreshTree();
	updateActions();
	dialog.exec();
}

void TempestControlDeck::OpenMessageVariables()
{
	if (messageVariablesPath.isEmpty() ||
	    !EnsureVariablesFile(messageVariablesPath, QStringLiteral("Profile message variables"))) {
		SetStatus(QStringLiteral("Unable to create the message variable asset."), true);
		return;
	}
	if (globalMessageVariablesPath.isEmpty() ||
	    !EnsureVariablesFile(globalMessageVariablesPath, QStringLiteral("Shared message variables"))) {
		SetStatus(QStringLiteral("Unable to create the shared message variable asset."), true);
		return;
	}

	QJsonObject sharedVariables;
	QJsonObject profileVariables;
	auto readVariables = [](const QString &path, QJsonObject &variables) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return false;
		QJsonParseError error;
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject())
			return false;
		variables = document.object();
		return true;
	};
	if (!readVariables(globalMessageVariablesPath, sharedVariables) ||
	    !readVariables(messageVariablesPath, profileVariables)) {
		SetStatus(QStringLiteral("A message variable file is not valid JSON; no changes were made."), true);
		return;
	}

	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("tempestVariableManager"));
	dialog.setWindowTitle(QStringLiteral("Message Variable Manager"));
	dialog.resize(qRound(780.0 * ContentScalePercent() / 100.0), qRound(520.0 * ContentScalePercent() / 100.0));
	auto *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(14, 14, 14, 14);
	layout->setSpacing(9);

	auto *heading = new QLabel(
		QStringLiteral("MESSAGE VARIABLE MANAGER // %1")
			.arg(activeProfileDocument.value(QStringLiteral("name")).toString(activeProfileId).toUpper()),
		&dialog);
	heading->setObjectName(QStringLiteral("layoutHeader"));
	auto *description = new QLabel(
		QStringLiteral("Shared values are available to every content profile. Profile values override a shared "
			       "value with the same name. Changes are picked up by active overlays automatically."),
		&dialog);
	description->setWordWrap(true);
	layout->addWidget(heading);
	layout->addWidget(description);

	auto *builtIns = new QLabel(
		QStringLiteral("BUILT-IN READ-ONLY VARIABLES // {{time}}  {{date}}  {{profile}}  {{mode}}  {{title}}  "
			       "{{status}}"),
		&dialog);
	builtIns->setObjectName(QStringLiteral("layoutHeader"));
	builtIns->setWordWrap(true);
	layout->addWidget(builtIns);

	auto *tree = new QTreeWidget(&dialog);
	tree->setObjectName(QStringLiteral("tempestVariableTree"));
	tree->setColumnCount(4);
	tree->setHeaderLabels(
		{QStringLiteral("VARIABLE"), QStringLiteral("VALUE"), QStringLiteral("TYPE"), QStringLiteral("SCOPE")});
	tree->setRootIsDecorated(false);
	tree->setAlternatingRowColors(true);
	tree->setSelectionMode(QAbstractItemView::SingleSelection);
	tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	layout->addWidget(tree, 1);

	auto *actionRow = new QWidget(&dialog);
	auto *actionLayout = new QHBoxLayout(actionRow);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(7);
	auto *add = new QPushButton(QStringLiteral("ADD VARIABLE"), actionRow);
	auto *edit = new QPushButton(QStringLiteral("EDIT"), actionRow);
	auto *moveScope = new QPushButton(QStringLiteral("MOVE SCOPE"), actionRow);
	auto *remove = new QPushButton(QStringLiteral("REMOVE"), actionRow);
	actionLayout->addWidget(add);
	actionLayout->addWidget(edit);
	actionLayout->addWidget(moveScope);
	actionLayout->addWidget(remove);
	layout->addWidget(actionRow);

	auto *advancedRow = new QWidget(&dialog);
	auto *advancedLayout = new QHBoxLayout(advancedRow);
	advancedLayout->setContentsMargins(0, 0, 0, 0);
	advancedLayout->setSpacing(7);
	auto *openProfile = new QPushButton(QStringLiteral("OPEN PROFILE JSON"), advancedRow);
	auto *openShared = new QPushButton(QStringLiteral("OPEN SHARED JSON"), advancedRow);
	openProfile->setToolTip(QDir::toNativeSeparators(messageVariablesPath));
	openShared->setToolTip(QDir::toNativeSeparators(globalMessageVariablesPath));
	advancedLayout->addWidget(openProfile);
	advancedLayout->addWidget(openShared);
	layout->addWidget(advancedRow);

	constexpr int KeyRole = Qt::UserRole;
	constexpr int ScopeRole = Qt::UserRole + 1;
	constexpr int EditableRole = Qt::UserRole + 2;
	const QSet<QString> builtInNames{QStringLiteral("time"), QStringLiteral("date"),  QStringLiteral("profile"),
					 QStringLiteral("mode"), QStringLiteral("title"), QStringLiteral("status")};

	auto valueDescription = [](const QJsonValue &value, QString *type, bool *editable) {
		*editable = true;
		if (value.isString()) {
			*type = QStringLiteral("Text");
			return value.toString();
		}
		if (value.isDouble()) {
			*type = QStringLiteral("Number");
			return QString::number(value.toDouble(), 'g', 15);
		}
		if (value.isBool()) {
			*type = QStringLiteral("Boolean");
			return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
		}
		if (value.isNull()) {
			*editable = false;
			*type = QStringLiteral("Null");
			return QStringLiteral("null");
		}
		*editable = false;
		*type = value.isArray() ? QStringLiteral("Advanced array") : QStringLiteral("Advanced object");
		const QJsonDocument document(value.isArray() ? QJsonDocument(value.toArray())
							     : QJsonDocument(value.toObject()));
		return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
	};

	auto refreshTree = [&](const QString &preferredKey = QString(), const QString &preferredScope = QString()) {
		tree->clear();
		auto addDocument = [&](const QJsonObject &variables, const QString &scope) {
			QStringList keys = variables.keys();
			keys.sort(Qt::CaseInsensitive);
			for (const QString &key : keys) {
				if (key.startsWith(QLatin1Char('_')))
					continue;
				QString type;
				bool editable = false;
				const QString value = valueDescription(variables.value(key), &type, &editable);
				auto *item = new QTreeWidgetItem(tree, {QStringLiteral("{{%1}}").arg(key), value, type,
									scope == QStringLiteral("profile")
										? QStringLiteral("Current profile")
										: QStringLiteral("Shared")});
				item->setData(0, KeyRole, key);
				item->setData(0, ScopeRole, scope);
				item->setData(0, EditableRole, editable);
				if (scope == QStringLiteral("profile") && sharedVariables.contains(key))
					item->setToolTip(
						3, QStringLiteral("Overrides the shared value for this profile."));
				if (!editable)
					item->setToolTip(2, QStringLiteral("Use Open JSON to edit structured values."));
				if (key.compare(preferredKey, Qt::CaseInsensitive) == 0 && scope == preferredScope)
					tree->setCurrentItem(item);
			}
		};
		addDocument(sharedVariables, QStringLiteral("shared"));
		addDocument(profileVariables, QStringLiteral("profile"));
		if (!tree->currentItem() && tree->topLevelItemCount())
			tree->setCurrentItem(tree->topLevelItem(0));
	};

	auto writeVariables = [&](const QString &path, const QJsonObject &variables) {
		QSaveFile file(path);
		const QByteArray data = QJsonDocument(variables).toJson(QJsonDocument::Indented);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(data) != data.size() ||
		    !file.commit()) {
			SetStatus(QStringLiteral("Unable to save message variables."), true);
			return false;
		}
		WatchContentProfileFiles();
		return true;
	};
	auto commitVariables = [&](const QJsonObject &beforeShared, const QJsonObject &beforeProfile) {
		const bool sharedChanged = sharedVariables != beforeShared;
		const bool profileChanged = profileVariables != beforeProfile;
		if (sharedChanged && !writeVariables(globalMessageVariablesPath, sharedVariables))
			return false;
		if (profileChanged && !writeVariables(messageVariablesPath, profileVariables)) {
			if (sharedChanged)
				writeVariables(globalMessageVariablesPath, beforeShared);
			return false;
		}
		return true;
	};

	auto selectedDocument = [&](const QString &scope) -> QJsonObject * {
		return scope == QStringLiteral("profile") ? &profileVariables : &sharedVariables;
	};
	auto selectedPath = [&](const QString &scope) {
		return scope == QStringLiteral("profile") ? messageVariablesPath : globalMessageVariablesPath;
	};

	auto updateActions = [&]() {
		QTreeWidgetItem *item = tree->currentItem();
		const bool selected = item != nullptr;
		edit->setEnabled(selected && item->data(0, EditableRole).toBool());
		moveScope->setEnabled(selected);
		remove->setEnabled(selected);
	};
	connect(tree, &QTreeWidget::currentItemChanged, &dialog,
		[updateActions](QTreeWidgetItem *, QTreeWidgetItem *) { updateActions(); });

	auto editVariable = [&](bool create) {
		QTreeWidgetItem *item = create ? nullptr : tree->currentItem();
		if (!create && (!item || !item->data(0, EditableRole).toBool()))
			return;
		const QString originalKey = item ? item->data(0, KeyRole).toString() : QString();
		const QString originalScope = item ? item->data(0, ScopeRole).toString() : QStringLiteral("profile");
		const QJsonValue originalValue = item ? selectedDocument(originalScope)->value(originalKey)
						      : QJsonValue(QString());

		QDialog editor(&dialog);
		editor.setObjectName(QStringLiteral("tempestVariableEditor"));
		editor.setWindowTitle(create ? QStringLiteral("Add Message Variable")
					     : QStringLiteral("Edit Message Variable"));
		editor.setMinimumWidth(qRound(500.0 * ContentScalePercent() / 100.0));
		auto *editorLayout = new QVBoxLayout(&editor);
		editorLayout->setContentsMargins(14, 14, 14, 14);
		editorLayout->setSpacing(10);
		auto *editorHeading = new QLabel(create ? QStringLiteral("ADD VARIABLE")
							: QStringLiteral("EDIT VARIABLE // {{%1}}").arg(originalKey),
						 &editor);
		editorHeading->setObjectName(QStringLiteral("layoutHeader"));
		editorLayout->addWidget(editorHeading);
		auto *form = new QFormLayout();
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		auto *name = new QLineEdit(originalKey, &editor);
		name->setPlaceholderText(QStringLiteral("channel-name"));
		name->setToolTip(QStringLiteral("Letters, numbers, dots, dashes, and underscores are supported."));
		auto *scope = new QComboBox(&editor);
		scope->addItem(QStringLiteral("CURRENT PROFILE // OVERRIDE"), QStringLiteral("profile"));
		scope->addItem(QStringLiteral("SHARED // EVERY PROFILE"), QStringLiteral("shared"));
		scope->setCurrentIndex(std::max(0, scope->findData(originalScope)));
		auto *type = new QComboBox(&editor);
		type->addItem(QStringLiteral("TEXT"), QStringLiteral("text"));
		type->addItem(QStringLiteral("NUMBER"), QStringLiteral("number"));
		type->addItem(QStringLiteral("BOOLEAN"), QStringLiteral("boolean"));
		auto *value = new QLineEdit(&editor);
		if (originalValue.isDouble()) {
			type->setCurrentIndex(type->findData(QStringLiteral("number")));
			value->setText(QString::number(originalValue.toDouble(), 'g', 15));
		} else if (originalValue.isBool()) {
			type->setCurrentIndex(type->findData(QStringLiteral("boolean")));
			value->setText(originalValue.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
		} else {
			value->setText(originalValue.isNull() ? QString() : originalValue.toString());
		}
		form->addRow(QStringLiteral("Variable name"), name);
		form->addRow(QStringLiteral("Scope"), scope);
		form->addRow(QStringLiteral("Value type"), type);
		form->addRow(QStringLiteral("Value"), value);
		editorLayout->addLayout(form);
		auto *preview = new QLabel(&editor);
		preview->setAlignment(Qt::AlignCenter);
		preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
		preview->setMinimumHeight(qRound(54.0 * ContentScalePercent() / 100.0));
		editorLayout->addWidget(preview);
		auto *editorButtons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);
		editorButtons->button(QDialogButtonBox::Save)->setText(QStringLiteral("SAVE VARIABLE"));
		editorLayout->addWidget(editorButtons);
		const QRegularExpression validName(QStringLiteral("^[a-z0-9_.-]+$"),
						   QRegularExpression::CaseInsensitiveOption);
		auto updateEditor = [&]() {
			const QString key = name->text().trimmed().toCaseFolded();
			const bool validKey = validName.match(key).hasMatch() && !key.startsWith(QLatin1Char('_')) &&
					      !builtInNames.contains(key);
			bool validValue = true;
			const QString kind = type->currentData().toString();
			if (kind == QStringLiteral("number")) {
				bool validNumber = false;
				value->text().trimmed().toDouble(&validNumber);
				validValue = validNumber;
			} else if (kind == QStringLiteral("boolean")) {
				const QString boolean = value->text().trimmed().toCaseFolded();
				validValue = boolean == QStringLiteral("true") || boolean == QStringLiteral("false");
			}
			preview->setText(validKey ? QStringLiteral("{{%1}}  →  %2").arg(key, value->text())
						  : QStringLiteral("Enter a valid non-built-in variable name"));
			preview->setStyleSheet(
				QStringLiteral(
					"QLabel { color: %1; border: 1px solid %1; padding: 10px; font-weight: 700; }")
					.arg(validKey && validValue ? QStringLiteral("#45d9ff")
								    : QStringLiteral("#ff668c")));
			editorButtons->button(QDialogButtonBox::Save)->setEnabled(validKey && validValue);
		};
		connect(name, &QLineEdit::textChanged, &editor, [updateEditor](const QString &) { updateEditor(); });
		connect(value, &QLineEdit::textChanged, &editor, [updateEditor](const QString &) { updateEditor(); });
		connect(type, &QComboBox::currentIndexChanged, &editor, [updateEditor](int) { updateEditor(); });
		connect(editorButtons, &QDialogButtonBox::accepted, &editor, &QDialog::accept);
		connect(editorButtons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
		updateEditor();
		name->setFocus();
		if (editor.exec() != QDialog::Accepted)
			return;

		const QString key = name->text().trimmed().toCaseFolded();
		const QString targetScope = scope->currentData().toString();
		QJsonObject *target = selectedDocument(targetScope);
		if ((create || key != originalKey || targetScope != originalScope) && target->contains(key) &&
		    QMessageBox::question(&editor, QStringLiteral("Replace Message Variable"),
					  QStringLiteral("{{%1}} already exists in that scope. Replace it?").arg(key)) !=
			    QMessageBox::Yes)
			return;
		QJsonValue newValue;
		const QString kind = type->currentData().toString();
		if (kind == QStringLiteral("number"))
			newValue = value->text().trimmed().toDouble();
		else if (kind == QStringLiteral("boolean"))
			newValue = value->text().trimmed().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
		else
			newValue = value->text();

		QJsonObject beforeShared = sharedVariables;
		QJsonObject beforeProfile = profileVariables;
		if (!create)
			selectedDocument(originalScope)->remove(originalKey);
		target->insert(key, newValue);
		if (!commitVariables(beforeShared, beforeProfile)) {
			sharedVariables = beforeShared;
			profileVariables = beforeProfile;
			return;
		}
		refreshTree(key, targetScope);
		SetStatus(QStringLiteral("Message variable saved // active overlays update live"));
	};

	connect(add, &QPushButton::clicked, &dialog, [&]() { editVariable(true); });
	connect(edit, &QPushButton::clicked, &dialog, [&]() { editVariable(false); });
	connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem *item, int) {
		if (item && item->data(0, EditableRole).toBool())
			editVariable(false);
	});
	connect(moveScope, &QPushButton::clicked, &dialog, [&]() {
		QTreeWidgetItem *item = tree->currentItem();
		if (!item)
			return;
		const QString key = item->data(0, KeyRole).toString();
		const QString oldScope = item->data(0, ScopeRole).toString();
		const QString targetScope = oldScope == QStringLiteral("profile") ? QStringLiteral("shared")
										  : QStringLiteral("profile");
		QJsonObject *source = selectedDocument(oldScope);
		QJsonObject *target = selectedDocument(targetScope);
		if (target->contains(key) &&
		    QMessageBox::question(
			    &dialog, QStringLiteral("Replace Message Variable"),
			    QStringLiteral("{{%1}} already exists in the target scope. Replace it?").arg(key)) !=
			    QMessageBox::Yes)
			return;
		const QJsonObject beforeShared = sharedVariables;
		const QJsonObject beforeProfile = profileVariables;
		target->insert(key, source->take(key));
		if (!commitVariables(beforeShared, beforeProfile)) {
			sharedVariables = beforeShared;
			profileVariables = beforeProfile;
			return;
		}
		refreshTree(key, targetScope);
		SetStatus(QStringLiteral("Message variable moved // active overlays update live"));
	});
	connect(remove, &QPushButton::clicked, &dialog, [&]() {
		QTreeWidgetItem *item = tree->currentItem();
		if (!item)
			return;
		const QString key = item->data(0, KeyRole).toString();
		const QString scope = item->data(0, ScopeRole).toString();
		if (QMessageBox::question(&dialog, QStringLiteral("Remove Message Variable"),
					  QStringLiteral("Remove {{%1}} from the %2 scope?")
						  .arg(key, scope == QStringLiteral("profile")
								    ? QStringLiteral("current profile")
								    : QStringLiteral("shared"))) != QMessageBox::Yes)
			return;
		QJsonObject *document = selectedDocument(scope);
		const QJsonObject before = *document;
		document->remove(key);
		if (!writeVariables(selectedPath(scope), *document)) {
			*document = before;
			return;
		}
		refreshTree();
		SetStatus(QStringLiteral("Message variable removed // active overlays update live"));
	});
	auto openJson = [&](const QString &path) {
		if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
			SetStatus(QStringLiteral("Unable to open the message variable asset."), true);
			return;
		}
		SetStatus(QStringLiteral("Message variable JSON opened // save changes to update live"));
	};
	connect(openProfile, &QPushButton::clicked, &dialog, [&]() { openJson(messageVariablesPath); });
	connect(openShared, &QPushButton::clicked, &dialog, [&]() { openJson(globalMessageVariablesPath); });

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	refreshTree();
	updateActions();
	dialog.exec();
}

QStringList TempestControlDeck::MessageLibraryRelativePaths(bool vaultElement) const
{
	QStringList libraries;
	if (activeProfileDocument.value(QStringLiteral("includeGlobalMessages")).toBool(true))
		libraries.push_back(vaultElement ? QStringLiteral("rotating-lines.txt")
						 : QStringLiteral("vault-elements/rotating-lines.txt"));
	if (!activeProfileId.isEmpty()) {
		const QString relative = QStringLiteral("profiles/%1/rotating-lines.txt").arg(activeProfileId);
		libraries.push_back(vaultElement ? QStringLiteral("../%1").arg(relative) : relative);
	}
	return libraries;
}

QStringList TempestControlDeck::VariableLibraryRelativePaths(bool vaultElement) const
{
	QStringList libraries;
	libraries.push_back(vaultElement ? QStringLiteral("message-variables.json")
					 : QStringLiteral("vault-elements/message-variables.json"));
	if (!activeProfileId.isEmpty()) {
		const QString relative = QStringLiteral("profiles/%1/message-variables.json").arg(activeProfileId);
		libraries.push_back(vaultElement ? QStringLiteral("../%1").arg(relative) : relative);
	}
	return libraries;
}

void TempestControlDeck::WatchContentProfileFiles()
{
	if (!rotationLibraryWatcher)
		return;
	const QStringList watched = rotationLibraryWatcher->files();
	if (!watched.isEmpty())
		rotationLibraryWatcher->removePaths(watched);
	const QStringList candidates = {globalRotationLibraryPath, globalMessageVariablesPath, rotationLibraryPath,
					messageVariablesPath, ProfileFilePath(activeProfileId)};
	for (const QString &path : candidates) {
		if (!path.isEmpty() && QFileInfo::exists(path))
			rotationLibraryWatcher->addPath(path);
	}
}

void TempestControlDeck::ChangeContentProfile(int index)
{
	if (loadingProfile || !contentProfile || index < 0)
		return;
	const QString profileId = contentProfile->itemData(index).toString();
	if (profileId.isEmpty() || profileId == activeProfileId)
		return;
	SaveState();
	if (!LoadContentProfile(profileId)) {
		SetStatus(QStringLiteral("Unable to load the selected content profile."), true);
		return;
	}
	RenderOverlay();
	SetStatus(QStringLiteral("Content profile active // %1").arg(contentProfile->currentText()));
}

void TempestControlDeck::NewContentProfile()
{
	bool accepted = false;
	const QString name = QInputDialog::getText(this, QStringLiteral("New Stream Content Profile"),
						   QStringLiteral("Profile name"), QLineEdit::Normal, QString(),
						   &accepted)
				     .trimmed();
	if (!accepted || name.isEmpty())
		return;
	QString profileId = ProfileIdForName(name);
	const QString baseId = profileId;
	for (int suffix = 2; QFileInfo::exists(ProfileFilePath(profileId)); ++suffix)
		profileId = QStringLiteral("%1-%2").arg(baseId).arg(suffix);
	if (!WriteProfileDocument(profileId, CreateProfileDocument(name, false)) ||
	    !EnsureMessageFile(ProfileMessagePath(profileId), QStringLiteral("%1 stream profile messages").arg(name)) ||
	    !EnsureVariablesFile(ProfileVariablesPath(profileId),
				 QStringLiteral("%1 profile message variables").arg(name))) {
		SetStatus(QStringLiteral("Unable to create the content profile."), true);
		return;
	}
	RefreshContentProfiles(profileId);
	RenderOverlay();
	SetStatus(QStringLiteral("Content profile created // %1").arg(name));
}

void TempestControlDeck::DuplicateContentProfile()
{
	if (activeProfileId.isEmpty())
		return;
	const QString currentName = activeProfileDocument.value(QStringLiteral("name")).toString(activeProfileId);
	bool accepted = false;
	const QString name = QInputDialog::getText(this, QStringLiteral("Duplicate Stream Content Profile"),
						   QStringLiteral("New profile name"), QLineEdit::Normal,
						   QStringLiteral("%1 Copy").arg(currentName), &accepted)
				     .trimmed();
	if (!accepted || name.isEmpty())
		return;
	QString profileId = ProfileIdForName(name);
	const QString baseId = profileId;
	for (int suffix = 2; QFileInfo::exists(ProfileFilePath(profileId)); ++suffix)
		profileId = QStringLiteral("%1-%2").arg(baseId).arg(suffix);
	QJsonObject duplicate = activeProfileDocument;
	duplicate.insert(QStringLiteral("name"), name);
	QJsonObject modes = duplicate.value(QStringLiteral("modes")).toObject();
	for (auto it = modes.begin(); it != modes.end(); ++it) {
		QJsonObject mode = it.value().toObject();
		mode.insert(QStringLiteral("countdownEndMs"), 0.0);
		mode.insert(QStringLiteral("countdownRunning"), false);
		it.value() = mode;
	}
	duplicate.insert(QStringLiteral("modes"), modes);
	if (!WriteProfileDocument(profileId, duplicate)) {
		SetStatus(QStringLiteral("Unable to duplicate the content profile."), true);
		return;
	}
	const QString destinationMessages = ProfileMessagePath(profileId);
	if (!QFile::copy(rotationLibraryPath, destinationMessages) &&
	    !EnsureMessageFile(destinationMessages, QStringLiteral("%1 stream profile messages").arg(name))) {
		SetStatus(QStringLiteral("Profile created, but its message library could not be copied."), true);
		return;
	}
	const QString destinationVariables = ProfileVariablesPath(profileId);
	if (!QFile::copy(messageVariablesPath, destinationVariables) &&
	    !EnsureVariablesFile(destinationVariables, QStringLiteral("%1 profile message variables").arg(name))) {
		SetStatus(QStringLiteral("Profile created, but its message variables could not be copied."), true);
		return;
	}
	RefreshContentProfiles(profileId);
	RenderOverlay();
	SetStatus(QStringLiteral("Content profile duplicated // %1").arg(name));
}

void TempestControlDeck::OpenContentProfileFolder()
{
	if (activeProfileId.isEmpty())
		return;
	const QString folder = QFileInfo(ProfileFilePath(activeProfileId)).absolutePath();
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
		SetStatus(QStringLiteral("Unable to open the content profile folder."), true);
}

QString TempestControlDeck::CurrentModeId() const
{
	return overlayMode ? overlayMode->currentData().toString() : QStringLiteral("starting");
}

QString TempestControlDeck::CurrentModeLabel() const
{
	return QString::fromUtf8(FindMode(activeModeId).label);
}

QString TempestControlDeck::CurrentSourceName() const
{
	return QString::fromUtf8(FindMode(activeModeId).sourceName);
}

void TempestControlDeck::UpdateOverlayPath()
{
	if (overlayDirectory.isEmpty())
		return;
	const ModeDefinition &mode = FindMode(activeModeId);
	overlayPath = QDir(overlayDirectory).filePath(QString::fromUtf8(mode.filename));
	outputPathLabel->setText(QStringLiteral("Overlay file: %1").arg(QDir::toNativeSeparators(overlayPath)));
	createSourceButton->setText(
		QStringLiteral("Create / Update %1 Overlay Source").arg(QString::fromUtf8(mode.label)));
}

void TempestControlDeck::ChangeOverlayMode(int)
{
	SaveModeState(activeModeId);
	activeModeId = CurrentModeId();
	{
		QSignalBlocker blockTitle(streamTitle);
		QSignalBlocker blockStatus(statusLine);
		LoadModeState(activeModeId);
	}
	UpdateOverlayPath();
	UpdateCountdownPreview();
	RenderOverlay();
}

void TempestControlDeck::QueueOverlayRender()
{
	if (renderDebounce)
		renderDebounce->start();
}

void TempestControlDeck::RenderOverlay()
{
	SaveState();
	if (overlayPath.isEmpty() && !EnsureOverlayDirectory())
		return;

	for (const ModeDefinition &mode : Modes) {
		const QString modeId = QString::fromUtf8(mode.id);
		const QString modePath = QDir(overlayDirectory).filePath(QString::fromUtf8(mode.filename));
		QSaveFile file(modePath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			SetStatus(QStringLiteral("%1 overlay write failed: %2")
					  .arg(QString::fromUtf8(mode.label), file.errorString()),
				  true);
			return;
		}

		QByteArray html = BuildOverlayHtml(modeId).toUtf8();
		if (file.write(html) != html.size() || !file.commit()) {
			SetStatus(QStringLiteral("%1 overlay save failed: %2")
					  .arg(QString::fromUtf8(mode.label), file.errorString()),
				  true);
			return;
		}
	}
	RenderVaultElements();

	++renderRevision;
	RefreshExistingSource();
	SetStatus(QStringLiteral("Overlay synchronized // revision %1").arg(renderRevision));
}

QString TempestControlDeck::BuildOverlayStateJson(const QString &modeId, bool vaultElement) const
{
	const QString requestedMode = modeId.isEmpty() ? activeModeId : modeId;
	const ModeDefinition &mode = FindMode(requestedMode);
	const QJsonObject modes = activeProfileDocument.value(QStringLiteral("modes")).toObject();
	const QJsonObject modeState = modes.value(requestedMode).toObject();
	const bool useEditor = requestedMode == activeModeId;
	const QString titleText = useEditor ? streamTitle->text().trimmed()
					    : modeState.value(QStringLiteral("heading"))
						      .toString(QString::fromUtf8(mode.defaultTitle))
						      .trimmed();
	const QString statusText = useEditor ? statusLine->text().trimmed()
					     : modeState.value(QStringLiteral("status"))
						       .toString(QString::fromUtf8(mode.defaultStatus))
						       .trimmed();
	QJsonArray messages;
	const QStringList lines = ReadRotationMessages(QString::fromUtf8(mode.defaultMessages), requestedMode);
	for (const QString &line : lines) {
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty())
			messages.append(trimmed);
	}
	QJsonObject state;
	state.insert(QStringLiteral("mode"), requestedMode);
	state.insert(QStringLiteral("kicker"), QString::fromUtf8(mode.kicker));
	state.insert(QStringLiteral("idleText"), QString::fromUtf8(mode.idleText));
	state.insert(QStringLiteral("title"), titleText);
	state.insert(QStringLiteral("status"), statusText);
	state.insert(QStringLiteral("messages"), messages);
	state.insert(QStringLiteral("profileName"),
		     activeProfileDocument.value(QStringLiteral("name")).toString(activeProfileId));
	state.insert(QStringLiteral("variables"), ReadMessageVariables());
	QJsonArray messageLibraries;
	for (const QString &library : MessageLibraryRelativePaths(vaultElement))
		messageLibraries.append(library);
	state.insert(QStringLiteral("messageLibraries"), messageLibraries);
	if (!messageLibraries.isEmpty())
		state.insert(QStringLiteral("messageLibrary"), messageLibraries.first());
	QJsonArray variableLibraries;
	for (const QString &library : VariableLibraryRelativePaths(vaultElement))
		variableLibraries.append(library);
	state.insert(QStringLiteral("variableLibraries"), variableLibraries);
	state.insert(QStringLiteral("rotationMs"), rotationSeconds->value() * 1000);
	state.insert(QStringLiteral("rotationMode"), messageOrder->currentData().toString());
	state.insert(QStringLiteral("messagePlaylist"), messagePlaylist->currentData().toString());
	const qint64 requestedEnd = useEditor ? countdownEndMs
					      : qint64(modeState.value(QStringLiteral("countdownEndMs")).toDouble());
	const bool requestedRunning = useEditor ? countdownRunning
						: modeState.value(QStringLiteral("countdownRunning")).toBool(false);
	state.insert(QStringLiteral("countdownEndMs"), requestedRunning ? double(requestedEnd) : 0.0);
	QString stateJson = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
	stateJson.replace(QStringLiteral("</"), QStringLiteral("<\\/"));
	return stateJson;
}

QString TempestControlDeck::BuildOverlayHtml(const QString &modeId) const
{
	const QString stateJson = BuildOverlayStateJson(modeId, false);

	QString html = QString::fromUtf8(R"TEMPEST(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--void:#03090f;--panel:rgba(5,16,26,.84);--cyan:#45d9ff;--ice:#bdf6ff;--blue:#0c7ccb;--deep:#06345d;--muted:#748fa4;--danger:#ff4b70;--audio:0;--glow:12px;--audioAlpha:.04;--coreScale:1;--reactorScale:.88;--reactorOpacity:.14;--titleScale:1;--audioScale:1;--audioLift:0px;--scanlineOpacity:.18;--footerPulse:.35;--audioFill:20%}
*{box-sizing:border-box}html,body{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;color:var(--ice);font-family:"Segoe UI",Arial,sans-serif}
body:before{content:"";position:absolute;inset:0;background:repeating-linear-gradient(0deg,color-mix(in srgb,var(--cyan) 2.5%,transparent) 0,color-mix(in srgb,var(--cyan) 2.5%,transparent) 1px,transparent 1px,transparent 5px);opacity:var(--scanlineOpacity);filter:drop-shadow(0 0 var(--glow) var(--cyan));pointer-events:none}
.frame{position:absolute;inset:4.5%;border:1px solid color-mix(in srgb,var(--cyan) 24%,transparent);box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--cyan) 18%,transparent);clip-path:polygon(0 0,18% 0,19% 1px,81% 1px,82% 0,100% 0,100% 100%,82% 100%,81% calc(100% - 1px),19% calc(100% - 1px),18% 100%,0 100%)}
.frame:before,.frame:after{content:"";position:absolute;width:16%;height:2px;top:-1px;background:linear-gradient(90deg,transparent,var(--cyan),transparent);animation:sweep 4s linear infinite}.frame:after{top:auto;bottom:-1px;right:0;animation-direction:reverse}
.top{position:absolute;left:6.5%;right:6.5%;top:6.5%;display:flex;justify-content:space-between;align-items:center;font-size:clamp(11px,1vw,18px);letter-spacing:.22em;text-transform:uppercase;color:var(--muted)}
.sig{display:flex;align-items:center;gap:12px;color:var(--cyan);font-weight:700}.core{width:14px;height:14px;border:2px solid var(--cyan);transform:rotate(45deg) scale(var(--coreScale));box-shadow:0 0 var(--glow) var(--cyan);animation:pulse 1.8s ease-in-out infinite}
.status{padding:8px 14px;border:1px solid color-mix(in srgb,var(--cyan) 35%,transparent);background:rgba(3,9,15,.66)}
.hero{position:absolute;left:10%;right:10%;top:24%;text-align:center;text-transform:uppercase}
.kicker{font-size:clamp(13px,1.2vw,22px);letter-spacing:.48em;color:var(--cyan);margin-bottom:18px}
.title{color:var(--ice);font-size:clamp(40px,6vw,112px);line-height:.92;font-weight:800;letter-spacing:.06em;text-shadow:0 0 var(--glow) color-mix(in srgb,var(--cyan) 52%,transparent);transform:scale(var(--titleScale))}
.rule{height:1px;width:72%;margin:28px auto;background:linear-gradient(90deg,transparent,var(--blue),var(--cyan),var(--blue),transparent);position:relative}.rule:after{content:"";position:absolute;left:50%;top:-4px;width:9px;height:9px;background:var(--ice);transform:rotate(45deg);box-shadow:0 0 16px var(--cyan)}
.countdown{font-variant-numeric:tabular-nums;font-size:clamp(54px,8vw,148px);line-height:1;font-weight:300;letter-spacing:.12em;color:var(--ice);text-shadow:0 0 var(--glow) var(--cyan);transform:scale(var(--audioScale))}
.message{margin-top:22px;min-height:1.5em;font-size:clamp(15px,1.5vw,28px);letter-spacing:.3em;color:var(--lineAccent,var(--cyan));text-shadow:0 0 var(--glow) var(--lineAccent,var(--cyan));transform:translateY(var(--audioLift));transition:opacity .24s ease,transform 70ms linear}.message[data-effect="pulse"]{animation:messagePulse 1.2s ease-in-out infinite}.message[data-effect="glitch"]{animation:messageGlitch .32s steps(2,end) infinite}.message[data-effect="alert"]{animation:messageAlert .7s ease-in-out infinite alternate}
.bottom{position:absolute;left:7%;right:7%;bottom:7%;display:flex;align-items:center;gap:18px;color:var(--muted);font-size:clamp(10px,.9vw,16px);letter-spacing:.18em;opacity:var(--footerPulse)}.line{height:1px;flex:1;background:linear-gradient(90deg,var(--deep),var(--cyan),var(--deep));box-shadow:0 0 var(--glow) var(--cyan)}.packet{color:var(--cyan);text-shadow:0 0 var(--glow) var(--cyan)}
.corner{position:absolute;width:54px;height:54px;border-color:var(--cyan);opacity:.72}.c1{left:4.5%;top:4.5%;border-left:3px solid;border-top:3px solid}.c2{right:4.5%;top:4.5%;border-right:3px solid;border-top:3px solid}.c3{left:4.5%;bottom:4.5%;border-left:3px solid;border-bottom:3px solid}.c4{right:4.5%;bottom:4.5%;border-right:3px solid;border-bottom:3px solid}
.reactor{position:absolute;left:50%;top:54%;width:56vw;height:56vw;max-width:900px;max-height:900px;transform:translate(-50%,-50%) scale(var(--reactorScale));border:1px solid color-mix(in srgb,var(--cyan) 25%,transparent);border-radius:50%;opacity:var(--reactorOpacity);box-shadow:0 0 var(--glow) color-mix(in srgb,var(--blue) 30%,transparent);pointer-events:none}.reactor:before,.reactor:after{content:"";position:absolute;border:1px dashed color-mix(in srgb,var(--cyan) 35%,transparent);border-radius:50%;inset:12%;animation:orbit 18s linear infinite}.reactor:after{inset:27%;animation-duration:11s;animation-direction:reverse}
.eq{position:absolute;left:17%;right:17%;bottom:12%;height:34px;display:flex;gap:5px;align-items:flex-end;justify-content:center;opacity:.72}.eq i{display:block;width:3px;height:3px;background:var(--cyan);box-shadow:0 0 8px var(--cyan);transition:height 45ms linear}body.reduced-motion *{animation:none!important;transition:none!important}
@keyframes pulse{50%{opacity:.55}}@keyframes sweep{from{transform:translateX(-15%)}to{transform:translateX(620%)}}@keyframes orbit{to{transform:rotate(360deg)}}@keyframes messagePulse{50%{opacity:.55;filter:brightness(1.35)}}@keyframes messageGlitch{0%,100%{filter:none}25%{filter:drop-shadow(3px 0 var(--lineAccent,var(--cyan)))}50%{filter:drop-shadow(-3px 0 var(--lineAccent,var(--cyan)))}75%{filter:brightness(1.6)}}@keyframes messageAlert{to{filter:brightness(1.65);letter-spacing:.36em}}
</style>
</head>
<body>
<div class="frame"></div><div class="reactor"></div><div class="eq" id="eq"></div><i class="corner c1"></i><i class="corner c2"></i><i class="corner c3"></i><i class="corner c4"></i>
<header class="top"><div class="sig"><i class="core"></i><span>TEMPEST BROADCAST // STREAM OVERLAY</span></div><div class="status" id="status"></div></header>
<main class="hero"><div class="kicker" id="kicker"></div><div class="title" id="title"></div><div class="rule"></div><div class="countdown" id="countdown"></div><div class="message" id="message"></div></main>
<footer class="bottom"><span>LIVE PRODUCTION</span><i class="line"></i><span class="packet">STREAM READY</span><i class="line"></i><span>TEMPEST BROADCAST</span></footer>
<script id="tempest-state" type="application/json">{{STATE_JSON}}</script>
<script>
const state=JSON.parse(document.getElementById('tempest-state').textContent);const root=document.documentElement,title=document.getElementById('title'),status=document.getElementById('status'),countdown=document.getElementById('countdown'),message=document.getElementById('message'),kicker=document.getElementById('kicker'),eq=document.getElementById('eq');
title.textContent=state.title||'WELCOME TO THE STREAM';status.textContent=state.status||'STREAM STATUS // STANDBY';kicker.textContent=state.kicker;let index=0,audio=0,rotatingMessages=state.messages||[],shuffleBag=[],lastMessage='',messageVariables=state.variables||{},currentRawMessage='',rotationTimer=0;for(let i=0;i<32;i++)eq.appendChild(document.createElement('i'));
function playlistName(value){return String(value||'common').trim().toLocaleLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^-+|-+$/g,'')||'common'}function messageLines(text){const selected=playlistName(state.messagePlaylist||'auto'),mode=playlistName(state.mode),messages=[];let section='common';text.split(/\r?\n/).forEach(raw=>{let line=raw.trim();const directive=line.match(/^@playlist\s+(.+)$/i);if(directive){section=playlistName(directive[1]);return}if(!line||line.startsWith('#')||/^\[\s*\]\s*/.test(line))return;const include=selected==='all'||section==='common'||(selected==='auto'&&section===mode)||section===selected;if(!include)return;line=line.replace(/^\[\s*x\s*\]\s*/i,'').trim();if(line)messages.push(line)});return messages}function builtinVariables(){const now=new Date();return{...messageVariables,time:now.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}),date:now.toLocaleDateString(),profile:state.profileName||'',mode:state.mode||'',title:state.title||'',status:state.status||''}}function expandMessage(value){const variables=builtinVariables();return String(value||'').replace(/\{\{\s*([a-z0-9_.-]+)\s*\}\}/gi,(match,key)=>Object.prototype.hasOwnProperty.call(variables,key.toLocaleLowerCase())?String(variables[key.toLocaleLowerCase()]??''):match)}function updateCurrentMessage(){if(message&&currentRawMessage)message.textContent=expandMessage(currentRawMessage)}async function refreshVariables(){const libraries=Array.isArray(state.variableLibraries)?state.variableLibraries:[];if(!libraries.length)return;const groups=await Promise.all(libraries.map(async path=>{try{const response=await fetch(encodeURI(path)+'?t='+Date.now(),{cache:'no-store'});if(!response.ok)return{};const value=await response.json();return value&&typeof value==='object'&&!Array.isArray(value)?value:{}}catch(_){return{}}}));messageVariables={};groups.forEach(group=>Object.entries(group).forEach(([key,value])=>messageVariables[key.toLocaleLowerCase()]=value));updateCurrentMessage()}async function refreshMessages(){const libraries=Array.isArray(state.messageLibraries)?state.messageLibraries:(state.messageLibrary?[state.messageLibrary]:[]);if(!libraries.length)return;try{const groups=await Promise.all(libraries.map(async path=>{const response=await fetch(encodeURI(path)+'?t='+Date.now(),{cache:'no-store'});return response.ok?messageLines(await response.text()):[]})),seen=new Set(),updated=[];groups.flat().forEach(line=>{const key=line.toLocaleLowerCase();if(!seen.has(key)){seen.add(key);updated.push(line)}});const wasEmpty=!rotatingMessages.length;rotatingMessages=updated;index=rotatingMessages.length?index%rotatingMessages.length:0;shuffleBag=[];if(!rotatingMessages.length){clearTimeout(rotationTimer);message.textContent='';message.style.opacity='0';currentRawMessage=''}else if(wasEmpty)rotate()}catch(_){}}function parseMessageEntry(value){let text=String(value||''),durationMs=Math.max(2000,Number(state.rotationMs)||6000),accent='',effect='none';const block=text.match(/\s*\[\[([^\]]+)\]\]\s*$/);if(block){text=text.slice(0,block.index).trim();const attributes=block[1].match(/[a-z][a-z0-9_-]*\s*=\s*(?:"[^"]*"|'[^']*'|[^\s]+)/gi)||[];attributes.forEach(attribute=>{const split=attribute.indexOf('='),key=attribute.slice(0,split).trim().toLocaleLowerCase(),raw=attribute.slice(split+1).trim().replace(/^["']|["']$/g,'');if(key==='duration'){const amount=parseFloat(raw);if(Number.isFinite(amount))durationMs=Math.max(2000,Math.min(60000,/ms$/i.test(raw)?amount:amount*1000))}else if(key==='accent'&&/^#[0-9a-f]{3}(?:[0-9a-f]{3})?$/i.test(raw))accent=raw;else if(key==='effect'&&['none','pulse','glitch','alert'].includes(raw.toLocaleLowerCase()))effect=raw.toLocaleLowerCase()})}return{text,durationMs,accent,effect}}function applyMessagePresentation(entry){if(entry.accent)root.style.setProperty('--lineAccent',entry.accent);else root.style.removeProperty('--lineAccent');message.dataset.effect=entry.effect}function nextMessage(){if(!rotatingMessages.length)return'';if(state.rotationMode==='random')return rotatingMessages[Math.floor(Math.random()*rotatingMessages.length)];if(state.rotationMode==='shuffle-bag'){if(!shuffleBag.length){shuffleBag=[...rotatingMessages];for(let i=shuffleBag.length-1;i>0;i--){const j=Math.floor(Math.random()*(i+1));[shuffleBag[i],shuffleBag[j]]=[shuffleBag[j],shuffleBag[i]]}if(shuffleBag.length>1&&shuffleBag[shuffleBag.length-1]===lastMessage)[shuffleBag[0],shuffleBag[shuffleBag.length-1]]=[shuffleBag[shuffleBag.length-1],shuffleBag[0]]}lastMessage=shuffleBag.pop();return lastMessage}return rotatingMessages[index++%rotatingMessages.length]}function rotate(){if(!message)return;clearTimeout(rotationTimer);const entry=parseMessageEntry(nextMessage());if(!entry.text){rotationTimer=setTimeout(rotate,entry.durationMs);return}currentRawMessage=entry.text;applyMessagePresentation(entry);message.style.opacity='0';setTimeout(()=>{message.textContent=expandMessage(currentRawMessage);message.style.opacity='1'},240);rotationTimer=setTimeout(rotate,entry.durationMs)}rotate();refreshVariables();refreshMessages();setInterval(refreshMessages,10000);setInterval(refreshVariables,5000);setInterval(updateCurrentMessage,1000);
function tick(){if(!state.countdownEndMs){countdown.textContent=state.idleText;return}const remaining=Math.max(0,state.countdownEndMs-Date.now()),seconds=Math.ceil(remaining/1000),minutes=Math.floor(seconds/60),secs=seconds%60;countdown.textContent=remaining<=0?(state.mode==='brb'?'RETURNING SOON':'STREAM READY'):String(minutes).padStart(2,'0')+':'+String(secs).padStart(2,'0')}tick();setInterval(tick,250);
async function telemetry(){let data={};try{const response=await fetch('./telemetry.json?t='+Date.now(),{cache:'no-store'});if(response.ok)data=await response.json()}catch(_){}const number=(value,fallback)=>Number.isFinite(Number(value))?Number(value):fallback,eventActive=!!data.externalEventActive,raw=Math.max(number(data.level,0),number(data.pulse,0),eventActive?number(data.externalEventStrength,0):0);audio=Math.max(raw,audio*.74);const reduced=!!data.reducedMotion,motion=reduced?0:Math.max(0,Math.min(2,number(data.reactionMotion,1))),glowGain=Math.max(0,Math.min(2,number(data.reactionGlow,1)));let palette=String(data.reactionPalette||'tempest'),base=190,span=105;if(eventActive&&String(data.externalEventEffect)==='spectrum')palette='spectrum';if(palette==='ultraviolet'){base=250;span=55}else if(palette==='ember'){base=12;span=38}else if(palette==='verdant'){base=135;span=55}let hue=palette==='spectrum'?(Date.now()/18+audio*140)%360:base+audio*span;const accent=eventActive&&/^#[0-9A-Fa-f]{6}$/.test(String(data.externalEventAccent||''))?String(data.externalEventAccent):'',cyan=accent||`hsl(${hue} 100% ${64+audio*8}%)`,ice=accent||`hsl(${(hue+8)%360} 100% ${86+audio*8}%)`,blue=accent?`color-mix(in srgb,${accent} 72%,#06101a)`:`hsl(${(hue+15)%360} 84% ${44+audio*8}%)`,deep=accent?`color-mix(in srgb,${accent} 28%,#02070b)`:`hsl(${(hue+20)%360} 75% ${18+audio*5}%)`;document.body.classList.toggle('reduced-motion',reduced);root.style.setProperty('--audio',audio.toFixed(3));root.style.setProperty('--cyan',cyan);root.style.setProperty('--ice',ice);root.style.setProperty('--blue',blue);root.style.setProperty('--deep',deep);root.style.setProperty('--glow',(12+audio*92*glowGain)+'px');root.style.setProperty('--audioAlpha',(.04+audio*.24*glowGain).toFixed(3));root.style.setProperty('--coreScale',(1+audio*.55*motion).toFixed(3));root.style.setProperty('--reactorScale',(.88+audio*.16*motion).toFixed(3));root.style.setProperty('--reactorOpacity',(.14+audio*.5*glowGain).toFixed(3));root.style.setProperty('--titleScale',(1+audio*.012*motion).toFixed(4));root.style.setProperty('--audioScale',(1+audio*.04*motion).toFixed(3));root.style.setProperty('--audioLift',(-audio*8*motion).toFixed(2)+'px');root.style.setProperty('--scanlineOpacity',(.18+audio*.7*glowGain).toFixed(3));root.style.setProperty('--footerPulse',(.35+audio*.65*glowGain).toFixed(3));root.style.setProperty('--audioFill',(20+audio*80*motion).toFixed(1)+'%');[...eq.children].forEach((bar,i)=>{const wave=.28+.72*Math.abs(Math.sin(performance.now()/180+i*.61));bar.style.height=(3+audio*31*wave*motion)+'px'})}telemetry();setInterval(telemetry,60);
</script>
</body>
</html>)TEMPEST");
	html.replace(QStringLiteral("{{STATE_JSON}}"), stateJson);
	// Poll local telemetry at the reactor's 10 Hz publication rate. The adjusted
	// envelope decay preserves the previous 60 ms visual response curve.
	html.replace(QStringLiteral("audio*.74"), QStringLiteral("audio*.606"));
	html.replace(QStringLiteral("setInterval(telemetry,60)"), QStringLiteral("setInterval(telemetry,100)"));
	html.replace(
		QStringLiteral(
			"async function telemetry(){let data={};try{const response=await fetch('./telemetry.json?t='+Date.now(),{cache:'no-store'});if(response.ok)data=await response.json()}catch(_){}"),
		QStringLiteral(
			"let tempestTelemetry={};window.addEventListener('tempestTelemetry',event=>{tempestTelemetry=event.detail||{}});function telemetry(){const data=tempestTelemetry;"));
	return html;
}

QString TempestControlDeck::BuildVaultElementHtml(const QString &elementId, int width, int height) const
{
	QString body;
	QString componentCss;
	if (elementId == QStringLiteral("scanlines")) {
		body = QStringLiteral("<div class=\"scanlines\"></div>");
		componentCss = QStringLiteral(
			".scanlines{position:absolute;inset:-8px;background:repeating-linear-gradient(0deg,color-mix(in srgb,var(--cyan) 3.5%,transparent) 0,color-mix(in srgb,var(--cyan) 3.5%,transparent) 1px,transparent 1px,transparent 5px);opacity:var(--scanlineOpacity);filter:drop-shadow(0 0 var(--glow) var(--cyan));transform:translateY(var(--audioLift));transition:opacity 70ms linear,transform 70ms linear}.scanlines:after{content:'';position:absolute;inset:0;background:linear-gradient(90deg,transparent 0,var(--cyan) 50%,transparent 100%);opacity:var(--audioAlpha);animation:sweep 3.2s linear infinite}");
	} else if (elementId == QStringLiteral("signal-frame")) {
		body = QStringLiteral(
			"<div class=\"frame\"></div><i class=\"corner c1\"></i><i class=\"corner c2\"></i><i class=\"corner c3\"></i><i class=\"corner c4\"></i>");
		componentCss = QStringLiteral(
			".frame{position:absolute;inset:3px;border:1px solid color-mix(in srgb,var(--cyan) 24%,transparent);box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--cyan) 18%,transparent)}.frame:before,.frame:after{content:'';position:absolute;width:16%;height:2px;top:-1px;background:linear-gradient(90deg,transparent,var(--cyan),transparent);animation:sweep 4s linear infinite}.frame:after{top:auto;bottom:-1px;right:0;animation-direction:reverse}.corner{position:absolute;width:54px;height:54px;opacity:.72}.c1{left:0;top:0;border-left:3px solid var(--cyan);border-top:3px solid var(--cyan)}.c2{right:0;top:0;border-right:3px solid var(--cyan);border-top:3px solid var(--cyan)}.c3{left:0;bottom:0;border-left:3px solid var(--cyan);border-bottom:3px solid var(--cyan)}.c4{right:0;bottom:0;border-right:3px solid var(--cyan);border-bottom:3px solid var(--cyan)}");
	} else if (elementId == QStringLiteral("reactor-core")) {
		body = QStringLiteral("<div class=\"reactor\"></div>");
		componentCss = QStringLiteral(
			".reactor{position:absolute;inset:1px;border:1px solid color-mix(in srgb,var(--cyan) 25%,transparent);border-radius:50%;opacity:var(--reactorOpacity);box-shadow:0 0 var(--glow) color-mix(in srgb,var(--blue) 30%,transparent);transform:scale(var(--reactorScale))}.reactor:before,.reactor:after{content:'';position:absolute;border:1px dashed color-mix(in srgb,var(--cyan) 35%,transparent);border-radius:50%;inset:12%;animation:orbit 18s linear infinite}.reactor:after{inset:27%;animation-duration:11s;animation-direction:reverse}");
	} else if (elementId == QStringLiteral("uplink-header")) {
		body = QStringLiteral(
			"<header><div class=\"sig\"><i class=\"core\"></i><span>TEMPEST BROADCAST // STREAM OVERLAY</span></div><div class=\"status\" id=\"status\"></div></header>");
		componentCss = QStringLiteral(
			"header{position:absolute;inset:0;display:flex;justify-content:space-between;align-items:center;font-size:18px;letter-spacing:.22em;text-transform:uppercase;color:var(--muted)}.sig{display:flex;align-items:center;gap:12px;color:var(--cyan);font-weight:700}.core{width:14px;height:14px;border:2px solid var(--cyan);transform:rotate(45deg) scale(var(--coreScale));box-shadow:0 0 var(--glow) var(--cyan);animation:pulse 1.8s ease-in-out infinite}.status{padding:8px 14px;border:1px solid color-mix(in srgb,var(--cyan) 35%,transparent);background:rgba(3,9,15,.66)}");
	} else if (elementId == QStringLiteral("title-plate")) {
		body = QStringLiteral(
			"<main><div class=\"kicker\" id=\"kicker\"></div><div class=\"title\" id=\"title\"></div><div class=\"rule\"></div></main>");
		componentCss = QStringLiteral(
			"main{position:absolute;inset:0;text-align:center;text-transform:uppercase}.kicker{font-size:22px;letter-spacing:.48em;color:var(--cyan);margin-bottom:18px}.title{color:var(--ice);font-size:96px;line-height:.92;font-weight:800;letter-spacing:.06em;text-shadow:0 0 var(--glow) color-mix(in srgb,var(--cyan) 52%,transparent);transform:scale(var(--titleScale))}.rule{height:1px;width:72%;margin:28px auto;background:linear-gradient(90deg,transparent,var(--blue),var(--cyan),var(--blue),transparent);position:relative}.rule:after{content:'';position:absolute;left:50%;top:-4px;width:9px;height:9px;background:var(--ice);transform:rotate(45deg);box-shadow:0 0 16px var(--cyan)}");
	} else if (elementId == QStringLiteral("countdown")) {
		body = QStringLiteral("<div class=\"countdown\" id=\"countdown\"></div>");
		componentCss = QStringLiteral(
			".countdown{position:absolute;inset:0;text-align:center;font-variant-numeric:tabular-nums;font-size:132px;line-height:1.2;font-weight:300;letter-spacing:.12em;color:var(--ice);text-shadow:0 0 var(--glow) var(--cyan);transform:scale(var(--audioScale));transition:transform 70ms linear}");
	} else if (elementId == QStringLiteral("rotation-message")) {
		body = QStringLiteral("<div class=\"message\" id=\"message\"></div>");
		componentCss = QStringLiteral(
			".message{position:absolute;inset:0;text-align:center;font-size:28px;line-height:70px;letter-spacing:.3em;color:var(--lineAccent,var(--cyan));text-shadow:0 0 var(--glow) var(--lineAccent,var(--cyan));transform:translateY(var(--audioLift)) scale(var(--audioScale));transition:opacity .24s ease,transform 70ms linear}.message[data-effect=\"pulse\"]{animation:messagePulse 1.2s ease-in-out infinite}.message[data-effect=\"glitch\"]{animation:messageGlitch .32s steps(2,end) infinite}.message[data-effect=\"alert\"]{animation:messageAlert .7s ease-in-out infinite alternate}");
	} else if (elementId == QStringLiteral("spectrum")) {
		body = QStringLiteral("<div class=\"eq\" id=\"eq\"></div>");
		componentCss = QStringLiteral(
			".eq{position:absolute;inset:0;display:flex;gap:5px;align-items:flex-end;justify-content:center;opacity:.72}.eq i{display:block;width:3px;height:3px;background:var(--cyan);box-shadow:0 0 8px var(--cyan);transition:height 45ms linear}");
	} else if (elementId == QStringLiteral("footer-plate")) {
		body = QStringLiteral(
			"<footer><span>LIVE PRODUCTION</span><i></i><span class=\"packet\">STREAM READY</span><i></i><span>TEMPEST BROADCAST</span></footer>");
		componentCss = QStringLiteral(
			"footer{position:absolute;inset:0;display:flex;align-items:center;gap:18px;color:var(--muted);font-size:16px;letter-spacing:.18em;opacity:var(--footerPulse);transform:scaleX(var(--audioScale));transition:opacity 70ms linear,transform 70ms linear}footer i{height:1px;flex:1;background:linear-gradient(90deg,var(--deep),var(--cyan),var(--deep));box-shadow:0 0 var(--glow) var(--cyan)}.packet{color:var(--cyan);text-shadow:0 0 var(--glow) var(--cyan)}");
	} else if (elementId == QStringLiteral("orbit-badge")) {
		body = QStringLiteral(
			"<div class=\"orbit-badge\"><i></i><strong>TM</strong><span id=\"status\"></span></div>");
		componentCss = QStringLiteral(
			".orbit-badge{position:absolute;inset:8px;border:1px solid color-mix(in srgb,var(--cyan) 42%,transparent);border-radius:50%;box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--cyan) 18%,transparent),0 0 var(--glow) color-mix(in srgb,var(--cyan) 12%,transparent);display:grid;place-items:center;text-align:center}.orbit-badge:before,.orbit-badge:after,.orbit-badge i{content:'';position:absolute;border:1px dashed color-mix(in srgb,var(--cyan) 38%,transparent);border-radius:50%;inset:13%;animation:orbit 13s linear infinite}.orbit-badge:after{inset:27%;animation-duration:8s;animation-direction:reverse}.orbit-badge i{inset:39%;border-style:solid;animation-duration:5s}.orbit-badge strong{font-size:68px;letter-spacing:.12em;color:var(--ice);text-shadow:0 0 var(--glow) var(--cyan)}.orbit-badge span{position:absolute;bottom:22%;font-size:9px;letter-spacing:.18em;color:var(--cyan);max-width:70%}");
	} else if (elementId == QStringLiteral("telemetry-plate")) {
		body = QStringLiteral(
			"<section class=\"telemetry-plate\"><header><b>STREAM TELEMETRY</b><span id=\"clock\"></span></header><h2 id=\"title\"></h2><p id=\"status\"></p><div class=\"meter\"><i></i></div><footer><span>AUDIO REACTOR</span><strong id=\"level\">0%</strong></footer></section>");
		componentCss = QStringLiteral(
			".telemetry-plate{position:absolute;inset:2px;padding:22px 26px;border:1px solid color-mix(in srgb,var(--cyan) 48%,transparent);background:linear-gradient(135deg,rgba(4,15,24,.94),rgba(4,10,17,.74));clip-path:polygon(0 0,94% 0,100% 16%,100% 100%,6% 100%,0 84%)}header,footer{display:flex;justify-content:space-between;align-items:center;color:var(--muted);font-size:10px;letter-spacing:.18em}header b{color:var(--cyan)}h2{margin:28px 0 8px;font-size:28px;letter-spacing:.13em;color:var(--ice)}p{margin:0 0 20px;color:var(--cyan);font-size:11px;letter-spacing:.13em}.meter{height:4px;background:color-mix(in srgb,var(--cyan) 12%,transparent);overflow:hidden}.meter i{display:block;width:var(--meter,4%);height:100%;background:linear-gradient(90deg,var(--blue),var(--cyan),var(--ice));box-shadow:0 0 var(--glow) var(--cyan);transition:width 70ms linear}footer{margin-top:12px}footer strong{color:var(--cyan)}");
	} else if (elementId == QStringLiteral("archive-ticker")) {
		body = QStringLiteral(
			"<div class=\"archive-ticker\"><b>STREAM FEED</b><i></i><span id=\"message\"></span><em>ONLINE</em></div>");
		componentCss = QStringLiteral(
			".archive-ticker{position:absolute;inset:2px;display:grid;grid-template-columns:auto 80px 1fr auto;gap:20px;align-items:center;padding:0 22px;border-top:1px solid var(--cyan);border-bottom:1px solid var(--blue);background:rgba(3,11,18,.88);box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--cyan) 18%,transparent);font-size:12px;letter-spacing:.18em;transform:translateY(var(--audioLift));transition:transform 70ms linear}.archive-ticker b,.archive-ticker em{color:var(--cyan);font-style:normal;text-shadow:0 0 var(--glow) var(--cyan)}.archive-ticker i{height:2px;width:var(--audioFill);background:linear-gradient(90deg,transparent,var(--cyan));box-shadow:0 0 var(--glow) var(--cyan);transition:width 70ms linear}.archive-ticker span{color:var(--ice);transition:opacity .24s ease}.archive-ticker em{font-size:9px;color:var(--muted)}");
	} else if (elementId == QStringLiteral("alert-popup")) {
		body = QStringLiteral(
			"<aside class=\"alert-popup\"><div class=\"beacon\"><i></i></div><div><small>STREAM EVENT</small><h2 id=\"title\"></h2><p id=\"status\"></p></div></aside>");
		componentCss = QStringLiteral(
			".alert-popup{position:absolute;inset:3px;display:grid;grid-template-columns:120px 1fr;align-items:center;gap:24px;padding:20px 28px;border:1px solid color-mix(in srgb,var(--cyan) 50%,transparent);border-left:4px solid var(--cyan);background:linear-gradient(100deg,rgba(4,17,27,.96),rgba(4,10,16,.78));box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--cyan) 10%,transparent);clip-path:polygon(0 0,94% 0,100% 24%,100% 100%,0 100%)}.beacon{width:82px;height:82px;border:1px solid var(--cyan);transform:rotate(45deg);display:grid;place-items:center;box-shadow:0 0 var(--glow) color-mix(in srgb,var(--cyan) 50%,transparent)}.beacon i{width:34px;height:34px;background:var(--cyan);animation:pulse .8s ease-in-out infinite;box-shadow:0 0 var(--glow) var(--cyan)}small{color:var(--cyan);font-size:10px;letter-spacing:.28em}h2{margin:10px 0 7px;color:var(--ice);font-size:30px;letter-spacing:.12em}p{margin:0;color:var(--muted);font-size:12px;letter-spacing:.16em}");
	} else if (elementId == QStringLiteral("signal-rail")) {
		body = QStringLiteral(
			"<aside class=\"signal-rail\"><b>T<br>M</b><div class=\"track\"><i></i></div><span id=\"status\"></span><em id=\"clock\"></em></aside>");
		componentCss = QStringLiteral(
			".signal-rail{position:absolute;inset:2px;display:flex;flex-direction:column;align-items:center;gap:26px;padding:28px 18px;border-left:2px solid var(--cyan);border-right:1px solid color-mix(in srgb,var(--cyan) 25%,transparent);background:linear-gradient(90deg,rgba(4,17,27,.92),rgba(4,10,16,.6));text-align:center}.signal-rail b{font-size:42px;line-height:.86;letter-spacing:.1em;color:var(--ice);text-shadow:0 0 var(--glow) var(--cyan)}.track{position:relative;width:3px;flex:1;background:color-mix(in srgb,var(--cyan) 15%,transparent)}.track:before,.track:after{content:'';position:absolute;left:-9px;width:21px;height:1px;background:var(--cyan)}.track:before{top:0}.track:after{bottom:0}.track i{position:absolute;left:0;bottom:0;width:100%;height:var(--meter,4%);background:var(--cyan);box-shadow:0 0 var(--glow) var(--cyan);transition:height 70ms linear}.signal-rail span{writing-mode:vertical-rl;transform:rotate(180deg);max-height:220px;overflow:hidden;color:var(--cyan);font-size:10px;letter-spacing:.16em}.signal-rail em{font-style:normal;color:var(--muted);font-size:10px;letter-spacing:.12em}");
	} else if (elementId == QStringLiteral("operator-lower-third")) {
		body = QStringLiteral(
			"<section class=\"operator-lower-third\"><div class=\"mark\">TB</div><div><small>TEMPEST BROADCAST // CREATOR CHANNEL</small><h2 id=\"title\"></h2><p id=\"status\"></p></div><time id=\"clock\"></time></section>");
		componentCss = QStringLiteral(
			".operator-lower-third{position:absolute;inset:3px;display:grid;grid-template-columns:110px 1fr auto;gap:24px;align-items:center;padding:18px 26px;border-bottom:3px solid var(--cyan);background:linear-gradient(90deg,rgba(3,13,22,.96),rgba(3,9,15,.72) 78%,transparent);clip-path:polygon(0 0,94% 0,100% 32%,100% 100%,0 100%)}.mark{width:76px;height:76px;display:grid;place-items:center;border:1px solid var(--cyan);color:var(--ice);font-size:26px;font-weight:800;letter-spacing:.08em;box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--cyan) 20%,transparent)}small{color:var(--cyan);font-size:9px;letter-spacing:.24em}h2{margin:10px 0 5px;color:var(--ice);font-size:30px;letter-spacing:.12em}p{margin:0;color:var(--muted);font-size:11px;letter-spacing:.15em}time{align-self:start;color:var(--cyan);font-size:11px;letter-spacing:.14em}");
	} else if (elementId == QStringLiteral("radio-player-template")) {
		body = QString::fromUtf8(
			R"TEMPESTRADIO(<section class="radio-player"><div class="radio-mark"><img id="shrArt" alt=""><i></i></div><div class="radio-info"><header><strong>RADIO PLAYER</strong><span id="shrState">SETUP REQUIRED</span></header><h2 id="shrTrack">ADD A STATION IN OVERLAY DESIGNER</h2><p id="shrArtist">AZURACAST PLAYER TEMPLATE</p><div class="radio-meta"><span id="shrListeners">-- LIVE</span><span id="shrClock">--:-- / --:--</span></div><div class="radio-progress"><i id="shrProgress"></i></div></div><aside><button id="shrToggle" type="button" aria-label="Play radio">▶</button><small>AUDIO INPUT</small></aside><audio id="shrAudio" preload="none" autoplay></audio></section><script>
const shrApi='',shrAudio=document.getElementById('shrAudio'),shrToggle=document.getElementById('shrToggle'),shrTrack=document.getElementById('shrTrack'),shrArtist=document.getElementById('shrArtist'),shrState=document.getElementById('shrState'),shrListeners=document.getElementById('shrListeners'),shrClock=document.getElementById('shrClock'),shrProgress=document.getElementById('shrProgress'),shrArt=document.getElementById('shrArt');let shrElapsed=0,shrDuration=0,shrSynced=Date.now();
function shrTime(value){const seconds=Math.max(0,Math.floor(Number(value)||0));return String(Math.floor(seconds/60)).padStart(2,'0')+':'+String(seconds%60).padStart(2,'0')}
function shrTick(){const elapsed=Math.max(0,shrElapsed+(Date.now()-shrSynced)/1000),shown=shrDuration>0?Math.min(elapsed,shrDuration):elapsed;shrClock.textContent=shrTime(shown)+' / '+(shrDuration>0?shrTime(shrDuration):'LIVE');shrProgress.style.width=(shrDuration>0?Math.min(100,shown/shrDuration*100):0).toFixed(1)+'%'}
function shrButton(){shrToggle.textContent=shrAudio.paused?'▶':'Ⅱ';shrToggle.setAttribute('aria-label',shrAudio.paused?'Play radio':'Pause radio')}
async function shrRefresh(){try{const response=await fetch(shrApi+'?t='+Date.now(),{cache:'no-store'});if(!response.ok)throw new Error('api '+response.status);const data=await response.json(),song=data.now_playing?.song||{},station=data.station||{};shrTrack.textContent=song.title||song.text||'TRACK DATA UNAVAILABLE';shrArtist.textContent=song.artist||'AUTOMATED ROTATION';shrState.textContent=data.live?.is_live?'● LIVE DJ':'● LIVE // AUTO DJ';shrListeners.textContent=String(data.listeners?.current??0).padStart(2,'0')+' LISTENERS';shrElapsed=Number(data.now_playing?.elapsed)||0;shrDuration=Number(data.now_playing?.duration)||0;shrSynced=Date.now();shrArt.src=song.art||station.art||'';if(station.listen_url&&shrAudio.src!==station.listen_url){shrAudio.src=station.listen_url;shrAudio.load();shrAudio.play().catch(()=>{});}shrTick();shrButton()}catch(_){shrState.textContent='● DATA RETRY'}}
shrToggle.addEventListener('click',()=>{if(shrAudio.paused)shrAudio.play().catch(()=>{});else shrAudio.pause()});shrAudio.addEventListener('play',shrButton);shrAudio.addEventListener('pause',shrButton);if(shrApi){shrRefresh();setInterval(shrRefresh,15000)}else{shrState.textContent='STATION URL REQUIRED'}setInterval(shrTick,1000);
</script>)TEMPESTRADIO");
		componentCss = QString::fromUtf8(
			R"TEMPESTCSS(.radio-player{position:absolute;inset:2px;display:grid;grid-template-columns:82px minmax(0,1fr) 58px;gap:15px;align-items:center;padding:13px 18px;border:1px solid color-mix(in srgb,var(--cyan) 48%,transparent);border-left:3px solid var(--cyan);background:linear-gradient(105deg,rgba(3,13,22,.96),rgba(3,9,15,.78));box-shadow:inset 0 0 var(--glow) color-mix(in srgb,var(--cyan) 12%,transparent),0 0 var(--glow) color-mix(in srgb,var(--cyan) 10%,transparent);clip-path:polygon(0 0,96% 0,100% 18%,100% 100%,0 100%);transform:translateY(var(--audioLift)) scale(var(--audioScale));transition:transform 70ms linear}.radio-mark{justify-self:center;width:58px;height:58px;position:relative;overflow:hidden;border:2px solid var(--cyan);background:color-mix(in srgb,var(--cyan) 12%,#04101a);box-shadow:0 0 var(--glow) var(--cyan);transform:rotate(45deg) scale(var(--coreScale))}.radio-mark img{position:absolute;inset:-16px;width:88px;height:88px;object-fit:cover;transform:rotate(-45deg);opacity:.78}.radio-mark i{position:absolute;inset:12px;border:1px solid var(--cyan);background:color-mix(in srgb,var(--cyan) 18%,transparent);box-shadow:0 0 var(--glow) var(--cyan)}.radio-info{min-width:0;display:grid;grid-template-rows:20px 30px 17px 15px 4px;gap:2px}.radio-info header{display:flex;align-items:center;justify-content:space-between;gap:12px;color:var(--muted);font-size:8px;letter-spacing:.15em}.radio-info header strong,.radio-info header span{color:var(--cyan);white-space:nowrap}.radio-info h2{margin:0;color:var(--ice);font-size:17px;line-height:30px;letter-spacing:.055em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;text-shadow:0 0 var(--glow) color-mix(in srgb,var(--cyan) 35%,transparent)}.radio-info p{margin:0;color:var(--cyan);font-size:10px;line-height:17px;font-weight:700;letter-spacing:.13em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.radio-meta{display:flex;justify-content:space-between;gap:12px;color:var(--muted);font-size:8px;line-height:15px;letter-spacing:.1em}.radio-progress{height:3px;background:color-mix(in srgb,var(--cyan) 12%,transparent);overflow:hidden}.radio-progress i{display:block;width:0;height:100%;background:var(--cyan);box-shadow:0 0 var(--glow) var(--cyan);transition:width .8s linear}.radio-player aside{display:grid;justify-items:center;gap:10px}.radio-player button{width:38px;height:38px;padding:0;border:1px solid var(--cyan);border-radius:50%;background:color-mix(in srgb,var(--cyan) 8%,#04101a);color:var(--ice);font:800 15px/1 "Segoe UI",Arial,sans-serif;box-shadow:0 0 var(--glow) color-mix(in srgb,var(--cyan) 38%,transparent)}.radio-player small{color:var(--muted);font-size:7px;letter-spacing:.09em;white-space:nowrap}.radio-player audio{display:none})TEMPESTCSS");
	}

	QString html = QString::fromUtf8(
		R"TEMPEST(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="tempest-size" content="{{WIDTH}}x{{HEIGHT}}"><meta name="viewport" content="width=device-width,initial-scale=1"><style>
:root{--cyan:#45d9ff;--ice:#bdf6ff;--blue:#0c7ccb;--deep:#06345d;--muted:#748fa4;--glow:12px;--audioAlpha:.04;--coreScale:1;--reactorScale:.88;--reactorOpacity:.14;--titleScale:1;--audioScale:1;--audioLift:0px;--scanlineOpacity:.18;--footerPulse:.35;--audioFill:20%}*{box-sizing:border-box}html,body{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;color:var(--ice);font-family:"Segoe UI",Arial,sans-serif}{{COMPONENT_CSS}}body.reduced-motion *{animation:none!important;transition:none!important}@keyframes pulse{50%{opacity:.55}}@keyframes sweep{from{transform:translateX(-15%)}to{transform:translateX(620%)}}@keyframes orbit{to{transform:rotate(360deg)}}@keyframes messagePulse{50%{opacity:.55;filter:brightness(1.35)}}@keyframes messageGlitch{0%,100%{filter:none}25%{filter:drop-shadow(3px 0 var(--lineAccent,var(--cyan)))}50%{filter:drop-shadow(-3px 0 var(--lineAccent,var(--cyan)))}75%{filter:brightness(1.6)}}@keyframes messageAlert{to{filter:brightness(1.65);letter-spacing:.36em}}
</style></head><body>{{BODY}}<script id="tempest-state" type="application/json">{{STATE_JSON}}</script><script>
const state=JSON.parse(document.getElementById('tempest-state').textContent),root=document.documentElement,title=document.getElementById('title'),status=document.getElementById('status'),countdown=document.getElementById('countdown'),message=document.getElementById('message'),kicker=document.getElementById('kicker'),eq=document.getElementById('eq');if(title)title.textContent=state.title||'STORM HORIZON RADIO';if(status)status.textContent=state.status||'OPERATOR LINK // STANDBY';if(kicker)kicker.textContent=state.kicker;if(eq)for(let i=0;i<32;i++)eq.appendChild(document.createElement('i'));let index=0,audio=0;function rotate(){if(!message)return;message.style.opacity='0';setTimeout(()=>{message.textContent=state.messages[index++%state.messages.length];message.style.opacity='1'},240)}rotate();if(message)setInterval(rotate,Math.max(2000,state.rotationMs||6000));function tick(){if(!countdown)return;if(!state.countdownEndMs){countdown.textContent=state.idleText;return}const remaining=Math.max(0,state.countdownEndMs-Date.now()),seconds=Math.ceil(remaining/1000),minutes=Math.floor(seconds/60),secs=seconds%60;countdown.textContent=remaining<=0?'UPLINK READY':String(minutes).padStart(2,'0')+':'+String(secs).padStart(2,'0')}tick();if(countdown)setInterval(tick,250);async function telemetry(){let data={};try{const response=await fetch('../telemetry.json?t='+Date.now(),{cache:'no-store'});if(response.ok)data=await response.json()}catch(_){}const number=(value,fallback)=>Number.isFinite(Number(value))?Number(value):fallback,eventActive=!!data.externalEventActive,raw=Math.max(number(data.level,0),number(data.pulse,0),eventActive?number(data.externalEventStrength,0):0);audio=Math.max(raw,audio*.74);const reduced=!!data.reducedMotion,motion=reduced?0:Math.max(0,Math.min(2,number(data.reactionMotion,1))),glowGain=Math.max(0,Math.min(2,number(data.reactionGlow,1)));let palette=String(data.reactionPalette||'tempest'),base=190,span=105;if(eventActive&&String(data.externalEventEffect)==='spectrum')palette='spectrum';if(palette==='ultraviolet'){base=250;span=55}else if(palette==='ember'){base=12;span=38}else if(palette==='verdant'){base=135;span=55}let hue=palette==='spectrum'?(Date.now()/18+audio*140)%360:base+audio*span;const accent=eventActive&&/^#[0-9A-Fa-f]{6}$/.test(String(data.externalEventAccent||''))?String(data.externalEventAccent):'',cyan=accent||`hsl(${hue} 100% ${64+audio*8}%)`,ice=accent||`hsl(${(hue+8)%360} 100% ${86+audio*8}%)`,blue=accent?`color-mix(in srgb,${accent} 72%,#06101a)`:`hsl(${(hue+15)%360} 84% ${44+audio*8}%)`,deep=accent?`color-mix(in srgb,${accent} 28%,#02070b)`:`hsl(${(hue+20)%360} 75% ${18+audio*5}%)`;document.body.classList.toggle('reduced-motion',reduced);root.style.setProperty('--audio',audio.toFixed(3));root.style.setProperty('--cyan',cyan);root.style.setProperty('--ice',ice);root.style.setProperty('--blue',blue);root.style.setProperty('--deep',deep);root.style.setProperty('--glow',(12+audio*92*glowGain)+'px');root.style.setProperty('--audioAlpha',(.04+audio*.24*glowGain).toFixed(3));root.style.setProperty('--coreScale',(1+audio*.55*motion).toFixed(3));root.style.setProperty('--reactorScale',(.88+audio*.16*motion).toFixed(3));root.style.setProperty('--reactorOpacity',(.14+audio*.5*glowGain).toFixed(3));root.style.setProperty('--titleScale',(1+audio*.012*motion).toFixed(4));root.style.setProperty('--audioScale',(1+audio*.04*motion).toFixed(3));root.style.setProperty('--audioLift',(-audio*8*motion).toFixed(2)+'px');root.style.setProperty('--scanlineOpacity',(.18+audio*.7*glowGain).toFixed(3));root.style.setProperty('--footerPulse',(.35+audio*.65*glowGain).toFixed(3));root.style.setProperty('--audioFill',(20+audio*80*motion).toFixed(1)+'%');if(eq)[...eq.children].forEach((bar,i)=>{const wave=.28+.72*Math.abs(Math.sin(performance.now()/180+i*.61));bar.style.height=(3+audio*31*wave*motion)+'px'})}telemetry();setInterval(telemetry,60);
</script><script>const clockNode=document.getElementById('clock'),levelNode=document.getElementById('level');function updateLocalClock(){if(clockNode)clockNode.textContent=new Date().toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'})}updateLocalClock();if(clockNode)setInterval(updateLocalClock,1000);setInterval(()=>{root.style.setProperty('--meter',Math.max(4,Math.min(100,audio*100))+'%');if(levelNode)levelNode.textContent=Math.round(Math.min(1,audio)*100)+'%'},60);</script></body></html>)TEMPEST");
	html.replace(QStringLiteral("{{WIDTH}}"), QString::number(width));
	html.replace(QStringLiteral("{{HEIGHT}}"), QString::number(height));
	html.replace(QStringLiteral("{{COMPONENT_CSS}}"), componentCss);
	html.replace(QStringLiteral("{{BODY}}"), body);
	html.replace(QStringLiteral("{{STATE_JSON}}"), BuildOverlayStateJson(QStringLiteral("starting"), true));
	html.replace(
		QStringLiteral(
			"let index=0,audio=0;function rotate(){if(!message)return;message.style.opacity='0';setTimeout(()=>{message.textContent=state.messages[index++%state.messages.length];message.style.opacity='1'},240)}rotate();if(message)setInterval(rotate,Math.max(2000,state.rotationMs||6000));"),
		QStringLiteral(
			"let index=0,audio=0,rotatingMessages=state.messages||[],shuffleBag=[],lastMessage='',messageVariables=state.variables||{},currentRawMessage='';function playlistName(value){return String(value||'common').trim().toLocaleLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^-+|-+$/g,'')||'common'}function messageLines(text){const selected=playlistName(state.messagePlaylist||'auto'),mode=playlistName(state.mode),messages=[];let section='common';text.split(/\\r?\\n/).forEach(raw=>{let line=raw.trim();const directive=line.match(/^@playlist\\s+(.+)$/i);if(directive){section=playlistName(directive[1]);return}if(!line||line.startsWith('#')||/^\\[\\s*\\]\\s*/.test(line))return;const include=selected==='all'||section==='common'||(selected==='auto'&&section===mode)||section===selected;if(!include)return;line=line.replace(/^\\[\\s*x\\s*\\]\\s*/i,'').trim();if(line)messages.push(line)});return messages}function builtinVariables(){const now=new Date();return{...messageVariables,time:now.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}),date:now.toLocaleDateString(),profile:state.profileName||'',mode:state.mode||'',title:state.title||'',status:state.status||''}}function expandMessage(value){const variables=builtinVariables();return String(value||'').replace(/\\{\\{\\s*([a-z0-9_.-]+)\\s*\\}\\}/gi,(match,key)=>Object.prototype.hasOwnProperty.call(variables,key.toLocaleLowerCase())?String(variables[key.toLocaleLowerCase()]??''):match)}function updateCurrentMessage(){if(message&&currentRawMessage)message.textContent=expandMessage(currentRawMessage)}async function refreshVariables(){const libraries=Array.isArray(state.variableLibraries)?state.variableLibraries:[];if(!libraries.length)return;const groups=await Promise.all(libraries.map(async path=>{try{const response=await fetch(encodeURI(path)+'?t='+Date.now(),{cache:'no-store'});if(!response.ok)return{};const value=await response.json();return value&&typeof value==='object'&&!Array.isArray(value)?value:{}}catch(_){return{}}}));messageVariables={};groups.forEach(group=>Object.entries(group).forEach(([key,value])=>messageVariables[key.toLocaleLowerCase()]=value));updateCurrentMessage()}async function refreshMessages(){const libraries=Array.isArray(state.messageLibraries)?state.messageLibraries:(state.messageLibrary?[state.messageLibrary]:[]);if(!message||!libraries.length)return;try{const groups=await Promise.all(libraries.map(async path=>{const response=await fetch(encodeURI(path)+'?t='+Date.now(),{cache:'no-store'});return response.ok?messageLines(await response.text()):[]})),seen=new Set(),updated=[];groups.flat().forEach(line=>{const key=line.toLocaleLowerCase();if(!seen.has(key)){seen.add(key);updated.push(line)}});const wasEmpty=!rotatingMessages.length;rotatingMessages=updated;index=rotatingMessages.length?index%rotatingMessages.length:0;shuffleBag=[];if(!rotatingMessages.length){message.textContent='';message.style.opacity='0';currentRawMessage=''}else if(wasEmpty)rotate()}catch(_){}}function nextMessage(){if(!rotatingMessages.length)return'';if(state.rotationMode==='random')return rotatingMessages[Math.floor(Math.random()*rotatingMessages.length)];if(state.rotationMode==='shuffle-bag'){if(!shuffleBag.length){shuffleBag=[...rotatingMessages];for(let i=shuffleBag.length-1;i>0;i--){const j=Math.floor(Math.random()*(i+1));[shuffleBag[i],shuffleBag[j]]=[shuffleBag[j],shuffleBag[i]]}if(shuffleBag.length>1&&shuffleBag[shuffleBag.length-1]===lastMessage)[shuffleBag[0],shuffleBag[shuffleBag.length-1]]=[shuffleBag[shuffleBag.length-1],shuffleBag[0]]}lastMessage=shuffleBag.pop();return lastMessage}return rotatingMessages[index++%rotatingMessages.length]}function rotate(){if(!message)return;const next=nextMessage();if(!next)return;currentRawMessage=next;message.style.opacity='0';setTimeout(()=>{message.textContent=expandMessage(currentRawMessage);message.style.opacity='1'},240)}rotate();refreshVariables();refreshMessages();if(message){setInterval(rotate,Math.max(2000,state.rotationMs||6000));setInterval(refreshMessages,10000);setInterval(refreshVariables,5000);setInterval(updateCurrentMessage,1000)};"));
	html.replace(QStringLiteral("currentRawMessage='';function playlistName"),
		     QStringLiteral("currentRawMessage='',rotationTimer=0;function playlistName"));
	html.replace(
		QStringLiteral(
			"if(!rotatingMessages.length){message.textContent='';message.style.opacity='0';currentRawMessage=''}else if(wasEmpty)rotate()"),
		QStringLiteral(
			"if(!rotatingMessages.length){clearTimeout(rotationTimer);message.textContent='';message.style.opacity='0';currentRawMessage=''}else if(wasEmpty)rotate()"));
	html.replace(
		QStringLiteral("}catch(_){}}function nextMessage(){"),
		QStringLiteral(
			"}catch(_){}}function parseMessageEntry(value){let text=String(value||''),durationMs=Math.max(2000,Number(state.rotationMs)||6000),accent='',effect='none';const block=text.match(/\\s*\\[\\[([^\\]]+)\\]\\]\\s*$/);if(block){text=text.slice(0,block.index).trim();const attributes=block[1].match(/[a-z][a-z0-9_-]*\\s*=\\s*(?:\"[^\"]*\"|'[^']*'|[^\\s]+)/gi)||[];attributes.forEach(attribute=>{const split=attribute.indexOf('='),key=attribute.slice(0,split).trim().toLocaleLowerCase(),raw=attribute.slice(split+1).trim().replace(/^[\"']|[\"']$/g,'');if(key==='duration'){const amount=parseFloat(raw);if(Number.isFinite(amount))durationMs=Math.max(2000,Math.min(60000,/ms$/i.test(raw)?amount:amount*1000))}else if(key==='accent'&&/^#[0-9a-f]{3}(?:[0-9a-f]{3})?$/i.test(raw))accent=raw;else if(key==='effect'&&['none','pulse','glitch','alert'].includes(raw.toLocaleLowerCase()))effect=raw.toLocaleLowerCase()})}return{text,durationMs,accent,effect}}function applyMessagePresentation(entry){if(entry.accent)root.style.setProperty('--lineAccent',entry.accent);else root.style.removeProperty('--lineAccent');message.dataset.effect=entry.effect}function nextMessage(){"));
	html.replace(
		QStringLiteral(
			"function rotate(){if(!message)return;const next=nextMessage();if(!next)return;currentRawMessage=next;message.style.opacity='0';setTimeout(()=>{message.textContent=expandMessage(currentRawMessage);message.style.opacity='1'},240)}rotate();refreshVariables();refreshMessages();if(message){setInterval(rotate,Math.max(2000,state.rotationMs||6000));setInterval(refreshMessages,10000);setInterval(refreshVariables,5000);setInterval(updateCurrentMessage,1000)};"),
		QStringLiteral(
			"function rotate(){if(!message)return;clearTimeout(rotationTimer);const entry=parseMessageEntry(nextMessage());if(!entry.text){rotationTimer=setTimeout(rotate,entry.durationMs);return}currentRawMessage=entry.text;applyMessagePresentation(entry);message.style.opacity='0';setTimeout(()=>{message.textContent=expandMessage(currentRawMessage);message.style.opacity='1'},240);rotationTimer=setTimeout(rotate,entry.durationMs)}rotate();refreshVariables();refreshMessages();if(message){setInterval(refreshMessages,10000);setInterval(refreshVariables,5000);setInterval(updateCurrentMessage,1000)};"));
	html.replace(QStringLiteral("STORM HORIZON RADIO"), QStringLiteral("RADIO PLAYER"));
	html.replace(QStringLiteral("STORM HORIZON ROTATION"), QStringLiteral("AUTOMATED ROTATION"));
	html.replace(QStringLiteral("OPERATOR LINK // STANDBY"), QStringLiteral("STREAM STATUS // STANDBY"));
	html.replace(QStringLiteral("UPLINK READY"), QStringLiteral("STREAM READY"));
	html.replace(QStringLiteral("audio*.74"), QStringLiteral("audio*.606"));
	html.replace(QStringLiteral("setInterval(telemetry,60)"), QStringLiteral("setInterval(telemetry,100)"));
	html.replace(
		QStringLiteral(
			"async function telemetry(){let data={};try{const response=await fetch('../telemetry.json?t='+Date.now(),{cache:'no-store'});if(response.ok)data=await response.json()}catch(_){}"),
		QStringLiteral(
			"let tempestTelemetry={};window.addEventListener('tempestTelemetry',event=>{tempestTelemetry=event.detail||{}});function telemetry(){const data=tempestTelemetry;"));
	html.replace(QStringLiteral("+'%'},60);</script>"), QStringLiteral("+'%'},100);</script>"));
	return html;
}

bool TempestControlDeck::RenderVaultElements()
{
	struct ElementDefinition {
		const char *id;
		const char *filename;
		int width;
		int height;
	};
	static constexpr ElementDefinition elements[] = {
		{"scanlines", "starting-soon--01-scanlines.html", 1920, 1080},
		{"signal-frame", "starting-soon--02-signal-frame.html", 1750, 990},
		{"reactor-core", "starting-soon--03-reactor-core.html", 900, 900},
		{"uplink-header", "starting-soon--04-uplink-header.html", 1660, 90},
		{"title-plate", "starting-soon--05-title-plate.html", 1540, 250},
		{"countdown", "starting-soon--06-countdown.html", 1100, 180},
		{"rotation-message", "starting-soon--07-rotation-message.html", 1400, 70},
		{"spectrum", "starting-soon--08-spectrum.html", 1260, 40},
		{"footer-plate", "starting-soon--09-footer-plate.html", 1650, 70},
		{"orbit-badge", "sandbox--10-orbit-badge.html", 360, 360},
		{"telemetry-plate", "sandbox--11-telemetry-plate.html", 560, 240},
		{"archive-ticker", "sandbox--12-archive-ticker.html", 1280, 70},
		{"alert-popup", "sandbox--13-alert-popup.html", 720, 190},
		{"signal-rail", "sandbox--14-signal-rail.html", 260, 760},
		{"operator-lower-third", "sandbox--15-operator-lower-third.html", 960, 170},
		{"radio-player-template", "radio--16-radio-player-template.html", 720, 160},
	};
	const QString directory = QDir(overlayDirectory).filePath(QStringLiteral("vault-elements"));
	if (!QDir().mkpath(directory))
		return false;
	for (const ElementDefinition &element : elements) {
		QSaveFile file(QDir(directory).filePath(QString::fromUtf8(element.filename)));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
			return false;
		const QByteArray html =
			BuildVaultElementHtml(QString::fromUtf8(element.id), element.width, element.height).toUtf8();
		if (file.write(html) != html.size() || !file.commit())
			return false;
	}
	return true;
}

void TempestControlDeck::StartCountdown()
{
	countdownEndMs = QDateTime::currentMSecsSinceEpoch() + (qint64)countdownMinutes->value() * 60 * 1000;
	countdownRunning = true;
	RenderOverlay();
	UpdateCountdownPreview();
}

void TempestControlDeck::ResetCountdown()
{
	countdownRunning = false;
	countdownEndMs = 0;
	RenderOverlay();
	UpdateCountdownPreview();
}

void TempestControlDeck::UpdateCountdownPreview()
{
	const ModeDefinition &mode = FindMode(activeModeId);
	if (!countdownRunning || countdownEndMs <= 0) {
		countdownPreview->setText(QString::fromUtf8(mode.idleText));
		return;
	}

	qint64 remaining = countdownEndMs - QDateTime::currentMSecsSinceEpoch();
	if (remaining <= 0) {
		countdownPreview->setText(activeModeId == QStringLiteral("starting")
						  ? QStringLiteral("STREAM READY")
						  : QString::fromUtf8(mode.idleText));
		return;
	}

	qint64 seconds = (remaining + 999) / 1000;
	qint64 minutes = seconds / 60;
	seconds %= 60;
	countdownPreview->setText(
		QStringLiteral("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0')));
}

void TempestControlDeck::ApplySourceSettings(obs_source_t *source)
{
	if (!source)
		return;

	obs_video_info ovi{};
	int width = 1920;
	int height = 1080;
	if (obs_get_video_info(&ovi)) {
		width = (int)ovi.base_width;
		height = (int)ovi.base_height;
	}

	OBSDataAutoRelease settings = obs_data_create();
	QByteArray path = QDir::toNativeSeparators(overlayPath).toUtf8();
	QByteArray css =
		QStringLiteral("body{background:rgba(0,0,0,0);margin:0;overflow:hidden;}/* tempest-revision:%1 */")
			.arg(renderRevision)
			.toUtf8();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", path.constData());
	obs_data_set_int(settings, "width", width);
	obs_data_set_int(settings, "height", height);
	obs_data_set_bool(settings, "fps_custom", true);
	obs_data_set_int(settings, "fps", 30);
	obs_data_set_bool(settings, "shutdown", true);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_bool(settings, "reroute_audio", false);
	obs_data_set_string(settings, "css", css.constData());
	obs_source_update(source, settings);
}

void TempestControlDeck::RefreshExistingSource()
{
	QByteArray sourceName = CurrentSourceName().toUtf8();
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.constData());
	if (source && strcmp(obs_source_get_unversioned_id(source), "browser_source") == 0)
		ApplySourceSettings(source);
}

void TempestControlDeck::CreateOrUpdateSource()
{
	RenderOverlay();
	OBSBasic *main = OBSBasic::Get();
	OBSScene scene = main ? main->GetCurrentScene() : nullptr;
	if (!scene) {
		SetStatus(QStringLiteral("No active scene is available for the overlay source."), true);
		return;
	}

	QByteArray sourceName = CurrentSourceName().toUtf8();
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.constData());
	if (source) {
		if (strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) {
			SetStatus(QStringLiteral("A non-browser source already uses the name '%1'.")
					  .arg(CurrentSourceName()),
				  true);
			return;
		}
		ApplySourceSettings(source);
	} else {
		const char *sourceType = obs_get_latest_input_type_id("browser_source");
		if (!sourceType) {
			SetStatus(QStringLiteral("The OBS Browser Source module is not available."), true);
			return;
		}

		OBSDataAutoRelease settings = obs_data_create();
		source = obs_source_create(sourceType, sourceName.constData(), settings, nullptr);
		if (!source) {
			SetStatus(QStringLiteral("Unable to create the %1 Browser Source.").arg(CurrentModeLabel()),
				  true);
			return;
		}
		ApplySourceSettings(source);
	}

	if (!obs_scene_find_source(scene, sourceName.constData()))
		obs_scene_add(scene, source);
	main->SaveProject();
	SetStatus(QStringLiteral("%1 source linked to the active scene.").arg(CurrentModeLabel()));
}

void TempestControlDeck::SetStatus(const QString &message, bool isError)
{
	if (!statusLabel)
		return;
	statusLabel->setText(message);
	statusLabel->setStyleSheet(isError ? QStringLiteral("color:#ff4b70;") : QStringLiteral("color:#45d9ff;"));
}
