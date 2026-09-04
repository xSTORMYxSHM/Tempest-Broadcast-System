#include "TempestAssetVault.hpp"

#include "TempestMediaBay.hpp"
#include "TempestSequenceDirector.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

#include "moc_TempestAssetVault.cpp"

namespace {
constexpr char ConfigSection[] = "TempestAssetVault";
constexpr char AssetBusName[] = "Tempest // Asset Bus";

const QStringList BankNames = {
	QStringLiteral("UNASSIGNED"), QStringLiteral("AUDIO REACTIVE"), QStringLiteral("FRACTAL"),
	QStringLiteral("AVATAR"),     QStringLiteral("TEXT"),           QStringLiteral("ALERT"),
	QStringLiteral("OVERLAY"),
};

QByteArray HtmlPreview(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.read(256 * 1024);
}

bool IsAudioReactiveHtml(const QFileInfo &info, const QByteArray &html)
{
	if (info.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) != 0)
		return false;
	if (info.fileName().startsWith(QStringLiteral("hud-"), Qt::CaseInsensitive) ||
	    info.dir().dirName().compare(QStringLiteral("vault-elements"), Qt::CaseInsensitive) == 0)
		return true;
	return html.contains("tempestTelemetry") || html.contains("telemetry.json");
}

QString FriendlyAssetName(const QFileInfo &info, const QByteArray &html)
{
	if (info.fileName().startsWith(QStringLiteral("hud-"), Qt::CaseInsensitive)) {
		const QRegularExpression stateExpression(
			QStringLiteral("<script\\s+id=[\\\"']hud-state[\\\"'][^>]*>(\\{.*?\\})</script>"),
			QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
		const QRegularExpressionMatch match = stateExpression.match(QString::fromUtf8(html));
		if (match.hasMatch()) {
			const QJsonDocument state = QJsonDocument::fromJson(match.captured(1).toUtf8());
			const QString primary = state.object().value(QStringLiteral("primary")).toString().trimmed();
			if (!primary.isEmpty())
				return primary;
		}
	}

	QString name = info.completeBaseName();
	name.remove(QRegularExpression(QStringLiteral("^[a-z0-9-]+--\\d+-"),
				       QRegularExpression::CaseInsensitiveOption));
	if (name.startsWith(QStringLiteral("hud-"), Qt::CaseInsensitive))
		name.remove(0, 4);
	name.replace(QRegularExpression(QStringLiteral("[-_]+")), QStringLiteral(" "));
	QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for (QString &word : words) {
		const QString lower = word.toLower();
		if (lower == QStringLiteral("hud") || lower == QStringLiteral("brb") ||
		    lower == QStringLiteral("obs"))
			word = lower.toUpper();
		else if (!word.isEmpty())
			word = word.left(1).toUpper() + word.mid(1).toLower();
	}
	return words.isEmpty() ? info.fileName() : words.join(QLatin1Char(' '));
}
} // namespace

TempestAssetVault::TempestAssetVault(OBSBasic *main, TempestSequenceDirector *director, TempestMediaBay *mediaBay,
				     QWidget *parent)
	: OBSDock(parent),
	  main(main),
	  director(director),
	  mediaBay(mediaBay)
{
	setObjectName(QStringLiteral("tempestAssetVault"));
	setWindowTitle(QStringLiteral("Asset Library"));
	setMinimumWidth(380);
	BuildInterface();
	EnableContentScaling(objectName());
	LoadState();
	RebuildRootSelector();

	scanTimer.setInterval(0);
	connect(&scanTimer, &QTimer::timeout, this, &TempestAssetVault::ScanBatch);
	directoryWatcher = new QFileSystemWatcher(this);
	watcherDebounce.setSingleShot(true);
	watcherDebounce.setInterval(650);
	connect(directoryWatcher, &QFileSystemWatcher::directoryChanged, this, &TempestAssetVault::ScheduleRescan);
	connect(directoryWatcher, &QFileSystemWatcher::fileChanged, this, &TempestAssetVault::ScheduleRescan);
	connect(&watcherDebounce, &QTimer::timeout, this, &TempestAssetVault::StartScan);
	if (roots.isEmpty())
		SetStatus(QStringLiteral("ADD A VIDEO FOLDER TO BEGIN INDEXING"));
	else
		StartScan();
}

TempestAssetVault::~TempestAssetVault() = default;

void TempestAssetVault::BuildInterface()
{
	auto *root = new QWidget(this);
	root->setObjectName(QStringLiteral("tempestAssetRoot"));
	root->setStyleSheet(QStringLiteral(R"(
		QWidget#tempestAssetRoot { background: #07131e; }
		QLabel#vaultTitle { color: #45d9ff; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
		QLabel#vaultSubtitle { color: #748fa4; font-size: 10px; letter-spacing: 1px; }
		QLabel#vaultDetails { color: #9eb7c8; padding: 7px; border: 1px solid #1f506d; background: #06101a; }
		QLabel#vaultStatus { color: #45d9ff; font-size: 10px; }
		QComboBox, QLineEdit, QListWidget { background: #06101a; border: 1px solid #1f506d; color: #bdf6ff; }
		QComboBox, QLineEdit { min-height: 29px; padding: 0 7px; }
		QListWidget::item { min-height: 39px; border-bottom: 1px solid #102c3e; padding: 4px; }
		QListWidget::item:selected { background: #073c5f; border: 1px solid #45d9ff; }
		QPushButton { min-height: 31px; border: 1px solid #1f506d; background: #0d2230; color: #bdf6ff; font-weight: 700; padding: 0 8px; }
		QPushButton:hover { border-color: #45d9ff; background: #0c456b; }
		QPushButton:disabled { color: #40576a; border-color: #172d3d; background: #091721; }
		QCheckBox { color: #9eb7c8; }
	)"));
	auto *layout = new QVBoxLayout(root);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(7);
	auto *title = new QLabel(QStringLiteral("ASSET LIBRARY"), root);
	title->setObjectName(QStringLiteral("vaultTitle"));
	auto *subtitle = new QLabel(QStringLiteral("Indexed media, overlays, and reusable stream assets"), root);
	subtitle->setObjectName(QStringLiteral("vaultSubtitle"));
	layout->addWidget(title);
	layout->addWidget(subtitle);

	auto *rootRow = new QHBoxLayout();
	rootsSelector = new QComboBox(root);
	rootsSelector->setObjectName(QStringLiteral("tempestVaultRoots"));
	auto *addRoot = new QPushButton(QStringLiteral("ADD FOLDER"), root);
	auto *removeRoot = new QPushButton(QStringLiteral("REMOVE"), root);
	connect(addRoot, &QPushButton::clicked, this, &TempestAssetVault::AddRootFolder);
	connect(removeRoot, &QPushButton::clicked, this, &TempestAssetVault::RemoveRootFolder);
	rootRow->addWidget(rootsSelector, 1);
	rootRow->addWidget(addRoot);
	rootRow->addWidget(removeRoot);
	layout->addLayout(rootRow);

	auto *filterRow = new QHBoxLayout();
	searchField = new QLineEdit(root);
	searchField->setObjectName(QStringLiteral("tempestVaultSearch"));
	searchField->setPlaceholderText(QStringLiteral("Search filename or folder..."));
	bankFilter = new QComboBox(root);
	bankFilter->addItem(QStringLiteral("ALL COLLECTIONS"), QString());
	for (const QString &bank : BankNames)
		bankFilter->addItem(bank, bank);
	connect(searchField, &QLineEdit::textChanged, this, &TempestAssetVault::RebuildAssetList);
	connect(bankFilter, &QComboBox::currentIndexChanged, this, &TempestAssetVault::RebuildAssetList);
	filterRow->addWidget(searchField, 1);
	filterRow->addWidget(bankFilter);
	layout->addLayout(filterRow);

	assetList = new QListWidget(root);
	assetList->setObjectName(QStringLiteral("tempestVaultAssetList"));
	assetList->setAccessibleName(QStringLiteral("Indexed media and overlay assets"));
	connect(assetList, &QListWidget::currentRowChanged, this, &TempestAssetVault::SelectAsset);
	connect(assetList, &QListWidget::itemDoubleClicked, this, &TempestAssetVault::LoadSelectedAsset);
	layout->addWidget(assetList, 1);

	detailLabel = new QLabel(QStringLiteral("NO ASSET SELECTED"), root);
	detailLabel->setObjectName(QStringLiteral("vaultDetails"));
	detailLabel->setWordWrap(true);
	layout->addWidget(detailLabel);

	auto *bankRow = new QHBoxLayout();
	assignBankSelector = new QComboBox(root);
	for (const QString &bank : BankNames)
		assignBankSelector->addItem(bank, bank);
	auto *assign = new QPushButton(QStringLiteral("ASSIGN COLLECTION"), root);
	connect(assign, &QPushButton::clicked, this, &TempestAssetVault::AssignBank);
	bankRow->addWidget(assignBankSelector, 1);
	bankRow->addWidget(assign);
	layout->addLayout(bankRow);

	loopOnBus = new QCheckBox(QStringLiteral("Loop when loaded on Asset Bus"), root);
	layout->addWidget(loopOnBus);
	loadButton = new QPushButton(QStringLiteral("PREVIEW SELECTED ASSET"), root);
	queueButton = new QPushButton(QStringLiteral("ADD TO CURRENT SEQUENCE"), root);
	auto *openFolder = new QPushButton(QStringLiteral("OPEN FILE LOCATION"), root);
	connect(loadButton, &QPushButton::clicked, this, &TempestAssetVault::LoadSelectedAsset);
	connect(queueButton, &QPushButton::clicked, this, &TempestAssetVault::AddSelectedToSequence);
	connect(openFolder, &QPushButton::clicked, this, &TempestAssetVault::OpenSelectedFolder);
	layout->addWidget(loadButton);
	layout->addWidget(queueButton);
	layout->addWidget(openFolder);

	auto *refresh = new QPushButton(QStringLiteral("RESCAN LIBRARY"), root);
	connect(refresh, &QPushButton::clicked, this, &TempestAssetVault::StartScan);
	layout->addWidget(refresh);
	statusLabel = new QLabel(QStringLiteral("LIBRARY INITIALIZING"), root);
	statusLabel->setObjectName(QStringLiteral("vaultStatus"));
	statusLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	setWidget(root);
	loadButton->setEnabled(false);
	queueButton->setEnabled(false);
}

void TempestAssetVault::LoadState()
{
	config_t *config = App()->GetUserConfig();
	const QJsonDocument rootDocument =
		QJsonDocument::fromJson(QByteArray(config_get_string(config, ConfigSection, "Roots")));
	if (rootDocument.isArray()) {
		for (const QJsonValue &value : rootDocument.array()) {
			const QString path = NormalizePath(value.toString());
			if (!path.isEmpty() && QDir(path).exists() && !roots.contains(path, Qt::CaseInsensitive))
				roots.push_back(path);
		}
	}
	const QJsonDocument bankDocument =
		QJsonDocument::fromJson(QByteArray(config_get_string(config, ConfigSection, "Banks")));
	if (bankDocument.isObject()) {
		const QJsonObject object = bankDocument.object();
		for (auto it = object.begin(); it != object.end(); ++it)
			banks.insert(NormalizePath(it.key()), it.value().toString(QStringLiteral("UNASSIGNED")));
	}
	char controlDeckPath[1024];
	if (GetAppConfigPath(controlDeckPath, sizeof(controlDeckPath),
			     "tempest-broadcast-system/control-deck") > 0) {
		const QString path = NormalizePath(QString::fromUtf8(controlDeckPath));
		const QString profilesPath = NormalizePath(QDir(path).filePath(QStringLiteral("profiles")));
		const QString elementsPath = NormalizePath(QDir(path).filePath(QStringLiteral("vault-elements")));
		for (qsizetype index = roots.size() - 1; index >= 0; --index) {
			if (roots[index].compare(profilesPath, Qt::CaseInsensitive) == 0 ||
			    roots[index].compare(elementsPath, Qt::CaseInsensitive) == 0)
				roots.removeAt(index);
		}
		QDir().mkpath(path);
		if (!roots.contains(path, Qt::CaseInsensitive))
			roots.prepend(path);
	}
}

void TempestAssetVault::SaveRoots()
{
	QJsonArray array;
	for (const QString &root : roots)
		array.append(root);
	const QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "Roots", json.constData());
	config_save_safe(config, "tmp", nullptr);
}

void TempestAssetVault::SaveBanks()
{
	QJsonObject object;
	for (auto it = banks.cbegin(); it != banks.cend(); ++it)
		object.insert(it.key(), it.value());
	const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
	config_t *config = App()->GetUserConfig();
	config_set_string(config, ConfigSection, "Banks", json.constData());
	config_save_safe(config, "tmp", nullptr);
}

void TempestAssetVault::RebuildRootSelector()
{
	const QString selected = rootsSelector->currentData().toString();
	QSignalBlocker blocker(rootsSelector);
	rootsSelector->clear();
	if (roots.isEmpty())
		rootsSelector->addItem(QStringLiteral("No indexed folders"), QString());
	else
		for (const QString &root : roots)
			rootsSelector->addItem(QDir::toNativeSeparators(root), root);
	const int index = rootsSelector->findData(selected);
	if (index >= 0)
		rootsSelector->setCurrentIndex(index);
}

void TempestAssetVault::AddRootFolder()
{
	const QString initial = QDir(QStringLiteral("G:/")).exists() ? QStringLiteral("G:/") : QDir::homePath();
	const QString selected =
		QFileDialog::getExistingDirectory(this, QStringLiteral("Add Asset Library Folder"), initial);
	const QString normalized = NormalizePath(selected);
	if (normalized.isEmpty())
		return;
	if (!roots.contains(normalized, Qt::CaseInsensitive))
		roots.push_back(normalized);
	SaveRoots();
	RebuildRootSelector();
	rootsSelector->setCurrentIndex(rootsSelector->findData(normalized));
	StartScan();
}

void TempestAssetVault::RemoveRootFolder()
{
	const QString selected = rootsSelector->currentData().toString();
	if (selected.isEmpty())
		return;
	for (int index = 0; index < roots.size(); ++index) {
		if (roots[index].compare(selected, Qt::CaseInsensitive) == 0) {
			roots.removeAt(index);
			break;
		}
	}
	SaveRoots();
	RebuildRootSelector();
	StartScan();
}

void TempestAssetVault::StartScan()
{
	watcherDebounce.stop();
	scanTimer.stop();
	iterator.reset();
	assets.clear();
	scannedPaths.clear();
	scanRootIndex = 0;
	scanning = !roots.isEmpty();
	RebuildAssetList();
	if (!scanning) {
		RefreshWatchPaths();
		SetStatus(QStringLiteral("ADD A VIDEO FOLDER TO BEGIN INDEXING"));
		return;
	}
	SetStatus(QStringLiteral("INDEXING ASSET LIBRARY // 0 FILES"));
	scanTimer.start();
}

void TempestAssetVault::ScanBatch()
{
	constexpr int BatchSize = 250;
	int processed = 0;
	const QStringList filters = {QStringLiteral("*.mp4"),  QStringLiteral("*.mov"),  QStringLiteral("*.mkv"),
				     QStringLiteral("*.webm"), QStringLiteral("*.avi"),  QStringLiteral("*.m4v"),
				     QStringLiteral("*.gif"),  QStringLiteral("*.html"), QStringLiteral("*.txt"),
				     QStringLiteral("*.json")};
	while (processed < BatchSize && scanRootIndex < roots.size()) {
		if (!iterator)
			iterator = std::make_unique<QDirIterator>(roots[scanRootIndex], filters, QDir::Files,
								  QDirIterator::Subdirectories);
		if (iterator->hasNext()) {
			const QString path = NormalizePath(iterator->next());
			++processed;
			const QString pathKey = path.toCaseFolded();
			if (scannedPaths.contains(pathKey))
				continue;
			scannedPaths.insert(pathKey);
			const QFileInfo info(path);
			if (info.fileName().compare(QStringLiteral("telemetry.json"), Qt::CaseInsensitive) == 0)
				continue;
			const bool browserAsset = info.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) ==
						  0;
			const bool textAsset = info.suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0 ||
					       info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0;
			const QByteArray html = browserAsset ? HtmlPreview(path) : QByteArray();
			const QString defaultBank = IsAudioReactiveHtml(info, html) ? QStringLiteral("AUDIO REACTIVE")
						    : browserAsset ? QStringLiteral("OVERLAY")
						    : textAsset  ? QStringLiteral("TEXT")
								 : QStringLiteral("UNASSIGNED");
			assets.push_back(
				{path, info.fileName(), FriendlyAssetName(info, html), banks.value(path, defaultBank), info.size()});
		} else {
			iterator.reset();
			++scanRootIndex;
		}
	}
	SetStatus(QStringLiteral("INDEXING ASSET LIBRARY // %1 FILES").arg(assets.size()));
	if (scanRootIndex < roots.size())
		return;

	scanTimer.stop();
	iterator.reset();
	scanning = false;
	std::sort(assets.begin(), assets.end(), [](const Asset &a, const Asset &b) {
		const int bankOrder = a.bank.compare(b.bank, Qt::CaseInsensitive);
		return bankOrder == 0 ? a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0 : bankOrder < 0;
	});
	RebuildAssetList();
	RefreshWatchPaths();
	SetStatus(QStringLiteral("LIBRARY READY // %1 ASSET%2")
			  .arg(assets.size())
			  .arg(assets.size() == 1 ? QString() : QStringLiteral("S")));
}

void TempestAssetVault::ScheduleRescan(const QString &changedPath)
{
	(void)changedPath;
	if (!watcherDebounce.isActive())
		SetStatus(QStringLiteral("LIBRARY CHANGE DETECTED // REFRESH PENDING"));
	watcherDebounce.start();
}

void TempestAssetVault::RefreshWatchPaths()
{
	if (!directoryWatcher)
		return;
	const QStringList watchedDirectories = directoryWatcher->directories();
	const QStringList watchedFiles = directoryWatcher->files();
	if (!watchedDirectories.isEmpty())
		directoryWatcher->removePaths(watchedDirectories);
	if (!watchedFiles.isEmpty())
		directoryWatcher->removePaths(watchedFiles);

	constexpr int MaximumWatchDirectories = 2048;
	QStringList paths;
	for (const QString &root : roots) {
		if (!QDir(root).exists())
			continue;
		paths.push_back(root);
		QDirIterator directories(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (directories.hasNext() && paths.size() < MaximumWatchDirectories)
			paths.push_back(NormalizePath(directories.next()));
		if (paths.size() >= MaximumWatchDirectories)
			break;
	}
	paths.removeDuplicates();
	if (!paths.isEmpty())
		directoryWatcher->addPaths(paths);
}

void TempestAssetVault::RebuildAssetList()
{
	const QString selectedPath = SelectedAsset() ? SelectedAsset()->path : QString();
	const QString search = searchField->text().trimmed();
	const QString bank = bankFilter->currentData().toString();
	QSignalBlocker blocker(assetList);
	assetList->clear();
	for (int index = 0; index < assets.size(); ++index) {
		const Asset &asset = assets[index];
		if (!bank.isEmpty() && asset.bank != bank)
			continue;
		if (!search.isEmpty() && !asset.name.contains(search, Qt::CaseInsensitive) &&
		    !asset.displayName.contains(search, Qt::CaseInsensitive) &&
		    !asset.path.contains(search, Qt::CaseInsensitive))
			continue;
		auto *item = new QListWidgetItem(QStringLiteral("%1 // %2\n%3")
							 .arg(asset.bank, asset.displayName,
							      QDir::toNativeSeparators(QFileInfo(asset.path).path())),
						 assetList);
		item->setData(Qt::UserRole, index);
		item->setToolTip(QDir::toNativeSeparators(asset.path));
		if (asset.path == selectedPath)
			assetList->setCurrentItem(item);
	}
	if (assetList->currentRow() < 0 && assetList->count() > 0)
		assetList->setCurrentRow(0);
	SelectAsset();
}

const TempestAssetVault::Asset *TempestAssetVault::SelectedAsset() const
{
	if (!assetList || !assetList->currentItem())
		return nullptr;
	const int index = assetList->currentItem()->data(Qt::UserRole).toInt();
	return index >= 0 && index < assets.size() ? &assets[index] : nullptr;
}

void TempestAssetVault::SelectAsset()
{
	const Asset *asset = SelectedAsset();
	loadButton->setEnabled(asset != nullptr);
	const bool browserAsset =
		asset && QFileInfo(asset->path).suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0;
	const bool textAsset =
		asset && (QFileInfo(asset->path).suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0 ||
			  QFileInfo(asset->path).suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0);
	queueButton->setEnabled(asset && !browserAsset && !textAsset);
	loopOnBus->setEnabled(asset && !browserAsset && !textAsset);
	loadButton->setText(browserAsset ? QStringLiteral("ADD / UPDATE BROWSER ELEMENT")
			    : textAsset  ? QStringLiteral("EDIT TEXT ASSET")
					 : QStringLiteral("LOAD / PREVIEW ON ASSET BUS"));
	if (!asset) {
		detailLabel->setText(scanning ? QStringLiteral("INDEXING...") : QStringLiteral("NO ASSET SELECTED"));
		return;
	}
	detailLabel->setText(QStringLiteral("%1\n%2\n%3 // %4")
				     .arg(asset->displayName, QDir::toNativeSeparators(asset->path), asset->bank,
					  FormatBytes(asset->bytes)));
	const int bankIndex = assignBankSelector->findData(asset->bank);
	if (bankIndex >= 0) {
		QSignalBlocker blocker(assignBankSelector);
		assignBankSelector->setCurrentIndex(bankIndex);
	}
}

void TempestAssetVault::AssignBank()
{
	const Asset *selected = SelectedAsset();
	if (!selected)
		return;
	const QString path = selected->path;
	const QString bank = assignBankSelector->currentData().toString();
	banks.insert(path, bank);
	for (Asset &asset : assets) {
		if (asset.path == path) {
			asset.bank = bank;
			break;
		}
	}
	SaveBanks();
	RebuildAssetList();
	SetStatus(QStringLiteral("COLLECTION ASSIGNED // %1").arg(bank));
}

QString TempestAssetVault::EnsureAssetBus(const QString &filePath, bool playNow)
{
	if (!main)
		return {};
	OBSScene scene = main->GetCurrentScene();
	if (!scene) {
		SetStatus(QStringLiteral("ASSET BUS FAILED // NO ACTIVE SCENE"), true);
		return {};
	}

	OBSSourceAutoRelease source = obs_get_source_by_name(AssetBusName);
	bool created = false;
	if (source) {
		if (strcmp(obs_source_get_unversioned_id(source), "ffmpeg_source") != 0) {
			SetStatus(QStringLiteral("ASSET BUS NAME IS USED BY A NON-MEDIA SOURCE"), true);
			return {};
		}
	} else {
		const char *sourceType = obs_get_latest_input_type_id("ffmpeg_source");
		if (!sourceType) {
			SetStatus(QStringLiteral("ASSET BUS FAILED // MEDIA SOURCE MODULE UNAVAILABLE"), true);
			return {};
		}
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_bool(settings, "is_local_file", true);
		obs_data_set_string(settings, "local_file", filePath.toUtf8().constData());
		obs_data_set_bool(settings, "looping", loopOnBus->isChecked());
		obs_data_set_bool(settings, "restart_on_activate", true);
		obs_data_set_bool(settings, "clear_on_media_end", true);
		obs_data_set_bool(settings, "close_when_inactive", false);
		source = obs_source_create(sourceType, AssetBusName, settings, nullptr);
		created = source != nullptr;
		if (!source) {
			SetStatus(QStringLiteral("ASSET BUS CREATION FAILED"), true);
			return {};
		}
	}

	if (!obs_scene_find_source_recursive(scene, AssetBusName))
		obs_scene_add(scene, source);
	const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	if (playNow || created)
		TempestMediaBay::LoadMediaFile(uuid, filePath, loopOnBus->isChecked(), playNow);
	if (!playNow)
		obs_source_media_stop(source);
	main->SaveProject();
	if (mediaBay)
		mediaBay->SelectSourceUuid(uuid);
	return uuid;
}

QString TempestAssetVault::EnsureBrowserAsset(const QString &filePath)
{
	if (!main)
		return {};
	OBSScene scene = main->GetCurrentScene();
	if (!scene) {
		SetStatus(QStringLiteral("BROWSER ELEMENT FAILED // NO ACTIVE SCENE"), true);
		return {};
	}
	QFile file(filePath);
	int width = 1920;
	int height = 1080;
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		const QString html = QString::fromUtf8(file.read(16384));
		const QRegularExpressionMatch size =
			QRegularExpression(
				QStringLiteral(
					"<meta\\s+name=[\\\"']tempest-size[\\\"']\\s+content=[\\\"'](\\d+)x(\\d+)[\\\"']"),
				QRegularExpression::CaseInsensitiveOption)
				.match(html);
		if (size.hasMatch()) {
			width = std::clamp(size.captured(1).toInt(), 64, 4096);
			height = std::clamp(size.captured(2).toInt(), 64, 4096);
		}
	}
	const QString sourceName = QStringLiteral("Tempest Vault // %1").arg(QFileInfo(filePath).completeBaseName());
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.toUtf8().constData());
	bool created = false;
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", QDir::toNativeSeparators(filePath).toUtf8().constData());
	obs_data_set_int(settings, "width", width);
	obs_data_set_int(settings, "height", height);
	obs_data_set_bool(settings, "fps_custom", true);
	obs_data_set_int(settings, "fps", 30);
	obs_data_set_bool(settings, "shutdown", true);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_string(settings, "css", "body { background-color: rgba(0, 0, 0, 0); overflow: hidden; }");
	if (source) {
		if (strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0) {
			SetStatus(QStringLiteral("BROWSER ELEMENT NAME IS USED BY ANOTHER SOURCE TYPE"), true);
			return {};
		}
		obs_source_update(source, settings);
	} else {
		const char *sourceType = obs_get_latest_input_type_id("browser_source");
		if (!sourceType)
			return {};
		source = obs_source_create(sourceType, sourceName.toUtf8().constData(), settings, nullptr);
		created = source != nullptr;
	}
	if (!source)
		return {};
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, sourceName.toUtf8().constData());
	if (!item) {
		item = obs_scene_add(scene, source);
		obs_video_info video{};
		if (item && obs_get_video_info(&video)) {
			vec2 position{(float(video.base_width) - float(width)) / 2.0f,
				      (float(video.base_height) - float(height)) / 2.0f};
			obs_sceneitem_set_pos(item, &position);
		}
	}
	main->SaveProject();
	return created ? QStringLiteral("created") : QStringLiteral("updated");
}

void TempestAssetVault::LoadSelectedAsset()
{
	const Asset *asset = SelectedAsset();
	if (!asset)
		return;
	const QString suffix = QFileInfo(asset->path).suffix();
	if (suffix.compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0 ||
	    suffix.compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0) {
		if (QDesktopServices::openUrl(QUrl::fromLocalFile(asset->path)))
			SetStatus(QStringLiteral("TEXT ASSET OPENED // %1").arg(asset->name.toUpper()));
		else
			SetStatus(QStringLiteral("TEXT ASSET COULD NOT BE OPENED"), true);
		return;
	}
	if (QFileInfo(asset->path).suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
		const QString result = EnsureBrowserAsset(asset->path);
		if (!result.isEmpty())
			SetStatus(
				QStringLiteral("BROWSER ELEMENT %1 // %2").arg(result.toUpper(), asset->name.toUpper()));
		return;
	}
	const QString uuid = EnsureAssetBus(asset->path, true);
	if (!uuid.isEmpty())
		SetStatus(QStringLiteral("ASSET BUS PLAYING // %1").arg(asset->name.toUpper()));
}

void TempestAssetVault::AddSelectedToSequence()
{
	const Asset *asset = SelectedAsset();
	if (!asset || !director)
		return;
	const QString path = asset->path;
	const QString name = QFileInfo(path).completeBaseName();
	const QString uuid = EnsureAssetBus(path, false);
	if (uuid.isEmpty())
		return;
	if (director->AddAssetCue(path, name, uuid))
		SetStatus(QStringLiteral("SEQUENCE CUE CREATED // %1").arg(name.toUpper()));
}

void TempestAssetVault::OpenSelectedFolder()
{
	const Asset *asset = SelectedAsset();
	if (asset)
		QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(asset->path).absolutePath()));
}

void TempestAssetVault::SetStatus(const QString &message, bool error)
{
	statusLabel->setText(message);
	statusLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
					   .arg(error ? QStringLiteral("#ff799c") : QStringLiteral("#45d9ff")));
}

QString TempestAssetVault::NormalizePath(const QString &path)
{
	return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString TempestAssetVault::FormatBytes(qint64 bytes)
{
	if (bytes >= 1024LL * 1024LL * 1024LL)
		return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
	if (bytes >= 1024LL * 1024LL)
		return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
	return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}
