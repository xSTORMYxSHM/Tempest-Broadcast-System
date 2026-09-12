/******************************************************************************
    Copyright (C) 2019-2020 by Dillon Pentz <dillon@vodbox.io>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSImporter.hpp"
#include "ImporterEntryPathItemDelegate.hpp"
#include "ImporterModel.hpp"

#include <importers/importers.hpp>
#include <models/SceneCollection.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "moc_OBSImporter.cpp"

namespace {
constexpr std::string_view OBSSceneCollectionPath = "tempest-broadcast-system/basic/scenes";
constexpr std::string_view OBSProfilePath = "tempest-broadcast-system/basic/profiles";

struct ImportReport {
	int collectionsImported = 0;
	int profilesImported = 0;
	int renamed = 0;
	int failures = 0;
	QSet<QString> missingAssets;
	QSet<QString> missingSourceTypes;
	QStringList failureDetails;
};

QString CanonicalOrAbsolutePath(const QString &path)
{
	const QFileInfo info(path);
	const QString canonical = info.canonicalFilePath();
	return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool IsOBSConfigurationRoot(const QString &path)
{
	const QDir root(path);
	return root.exists(QStringLiteral("basic/scenes")) || root.exists(QStringLiteral("basic/profiles"));
}

QString ResolveOBSConfigurationRoot(const QString &selectedPath)
{
	QStringList candidates;
	QDir cursor(CanonicalOrAbsolutePath(selectedPath));
	for (int level = 0; level < 5; ++level) {
		const QString base = cursor.absolutePath();
		candidates.append(base);
		candidates.append(QDir(base).filePath(QStringLiteral("obs-studio")));
		candidates.append(QDir(base).filePath(QStringLiteral("config/obs-studio")));
		if (!cursor.cdUp()) {
			break;
		}
	}

	QSet<QString> visited;
	for (const QString &candidate : candidates) {
		const QString normalized = CanonicalOrAbsolutePath(candidate);
		const QString key = normalized.toCaseFolded();
		if (visited.contains(key)) {
			continue;
		}
		visited.insert(key);
		if (IsOBSConfigurationRoot(normalized)) {
			return normalized;
		}
	}

	return {};
}

QString DefaultOBSConfigurationRoot()
{
	char path[1024];
	if (os_get_config_path(path, sizeof(path), "obs-studio") <= 0) {
		return QDir::homePath();
	}
	return CanonicalOrAbsolutePath(QString::fromUtf8(path));
}

QFileInfoList FindSceneCollections(const QString &root)
{
	QDir scenes(QDir(root).filePath(QStringLiteral("basic/scenes")));
	return scenes.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name);
}

QFileInfoList FindProfiles(const QString &root)
{
	QFileInfoList result;
	QDir profiles(QDir(root).filePath(QStringLiteral("basic/profiles")));
	const QFileInfoList candidates =
		profiles.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
	for (const QFileInfo &candidate : candidates) {
		if (QFileInfo(QDir(candidate.absoluteFilePath()).filePath(QStringLiteral("basic.ini"))).isReadable()) {
			result.append(candidate);
		}
	}
	return result;
}

QSet<QString> ExistingSceneNames()
{
	QSet<QString> names;
	if (OBSBasic *main = OBSBasic::Get()) {
		for (const auto &[name, collection] : main->GetSceneCollectionCache()) {
			UNUSED_PARAMETER(collection);
			names.insert(QString::fromStdString(name).toCaseFolded());
		}
	}
	return names;
}

QSet<QString> ExistingProfileNames()
{
	QSet<QString> names;
	if (OBSBasic *main = OBSBasic::Get()) {
		for (const auto &[name, profile] : main->GetProfileCache()) {
			UNUSED_PARAMETER(profile);
			names.insert(QString::fromStdString(name).toCaseFolded());
		}
	}
	return names;
}

QString ReserveUniqueName(const QString &requestedName, const QString &fallback, QSet<QString> &reserved, bool &renamed)
{
	QString base = requestedName.trimmed();
	if (base.isEmpty()) {
		base = fallback;
	}
	QString candidate = base;
	if (reserved.contains(candidate.toCaseFolded())) {
		candidate = QStringLiteral("%1 (OBS Import)").arg(base);
		int suffix = 2;
		while (reserved.contains(candidate.toCaseFolded())) {
			candidate = QStringLiteral("%1 (OBS Import %2)").arg(base).arg(suffix++);
		}
		renamed = true;
	}
	reserved.insert(candidate.toCaseFolded());
	return candidate;
}

void AnalyzeStrings(const json11::Json &value, QSet<QString> &missingAssets)
{
	if (value.is_string()) {
		const QString text = QString::fromStdString(value.string_value()).trimmed();
		if (text.size() <= 2048 && !text.contains(QStringLiteral("://")) && QDir::isAbsolutePath(text) &&
		    !QFileInfo::exists(text)) {
			missingAssets.insert(QDir::toNativeSeparators(text));
		}
		return;
	}
	if (value.is_array()) {
		for (const json11::Json &item : value.array_items()) {
			AnalyzeStrings(item, missingAssets);
		}
		return;
	}
	if (value.is_object()) {
		for (const auto &[key, item] : value.object_items()) {
			UNUSED_PARAMETER(key);
			AnalyzeStrings(item, missingAssets);
		}
	}
}

void AnalyzeCollection(const json11::Json &collection, ImportReport &report)
{
	for (const json11::Json &sourceValue : collection["sources"].array_items()) {
		const std::string sourceId = sourceValue["id"].string_value();
		if (!sourceId.empty() && !obs_get_latest_input_type_id(sourceId.c_str())) {
			report.missingSourceTypes.insert(QString::fromStdString(sourceId));
		}
	}
	AnalyzeStrings(collection, report.missingAssets);
}

bool WriteSceneCollection(const QString &sourcePath, const QString &requestedName,
			  const std::filesystem::path &destinationDirectory, QSet<QString> &reservedNames,
			  ImportReport &report)
{
	const QByteArray sourcePathUtf8 = sourcePath.toUtf8();
	const std::string sourcePathString = sourcePathUtf8.constData();
	std::string importName = requestedName.toUtf8().constData();
	if (importName.empty()) {
		const std::string program = DetectProgram(sourcePathString);
		if (program != "Null") {
			importName = GetSCName(sourcePathString, program);
		}
	}
	json11::Json converted;
	const int importResult = ImportSC(sourcePathString, importName, converted);
	if (importResult != IMPORTER_SUCCESS || converted == json11::Json()) {
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("Scene collection could not be converted: %1").arg(sourcePath));
		return false;
	}

	bool renamed = false;
	const QString originalName = QString::fromStdString(converted["name"].string_value());
	const QString uniqueName =
		ReserveUniqueName(originalName, QStringLiteral("OBS Studio Import"), reservedNames, renamed);
	json11::Json::object output = converted.object_items();
	output["name"] = uniqueName.toStdString();
	converted = output;

	std::string safeName;
	if (!GetFileSafeName(uniqueName.toUtf8().constData(), safeName)) {
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("A safe filename could not be created for %1.").arg(uniqueName));
		return false;
	}

	std::filesystem::path destination = destinationDirectory / std::filesystem::u8path(safeName);
	std::string destinationString = destination.u8string();
	if (!GetClosestUnusedFileName(destinationString, "json")) {
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("A destination filename could not be reserved for %1.").arg(uniqueName));
		return false;
	}

	QSaveFile file(QString::fromUtf8(destinationString.c_str()));
	if (!file.open(QIODevice::WriteOnly)) {
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("Scene collection could not be written: %1")
				.arg(QDir::toNativeSeparators(QString::fromStdString(destinationString))));
		return false;
	}
	const QByteArray document = QByteArray::fromStdString(converted.dump());
	if (file.write(document) != document.size() || !file.commit()) {
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("Scene collection write did not complete: %1").arg(uniqueName));
		return false;
	}

	AnalyzeCollection(converted, report);
	++report.collectionsImported;
	if (renamed) {
		++report.renamed;
	}
	blog(LOG_INFO, "Import Scene Collection: %s (%s) - SUCCESS", uniqueName.toUtf8().constData(),
	     destinationString.c_str());
	return true;
}

QString ReadProfileName(const QFileInfo &profileDirectory)
{
	const QString basicIni = QDir(profileDirectory.absoluteFilePath()).filePath(QStringLiteral("basic.ini"));
	ConfigFile config;
	if (config.Open(basicIni.toUtf8().constData(), CONFIG_OPEN_EXISTING) != CONFIG_SUCCESS) {
		return profileDirectory.fileName();
	}
	const char *name = config_get_string(config, "General", "Name");
	return name && *name ? QString::fromUtf8(name) : profileDirectory.fileName();
}

bool CopyProfileTree(const std::filesystem::path &source, const std::filesystem::path &destination, QString *error)
{
	try {
		std::filesystem::create_directories(destination);
		for (const auto &entry : std::filesystem::recursive_directory_iterator(source)) {
			if (entry.is_symlink()) {
				continue;
			}
			const std::filesystem::path relative = std::filesystem::relative(entry.path(), source);
			const std::filesystem::path target = destination / relative;
			if (entry.is_directory()) {
				std::filesystem::create_directories(target);
			} else if (entry.is_regular_file()) {
				std::filesystem::create_directories(target.parent_path());
				std::filesystem::copy_file(entry.path(), target,
							   std::filesystem::copy_options::overwrite_existing);
			}
		}
		return true;
	} catch (const std::filesystem::filesystem_error &exception) {
		if (error) {
			*error = QString::fromUtf8(exception.what());
		}
		return false;
	}
}

bool ImportProfile(const QFileInfo &sourceProfile, const std::filesystem::path &destinationDirectory,
		   QSet<QString> &reservedNames, ImportReport &report)
{
	bool renamed = false;
	const QString profileName = ReserveUniqueName(ReadProfileName(sourceProfile), QStringLiteral("OBS Profile"),
						      reservedNames, renamed);
	std::string safeName;
	if (!GetFileSafeName(profileName.toUtf8().constData(), safeName)) {
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("A safe folder name could not be created for profile %1.").arg(profileName));
		return false;
	}

	std::filesystem::path finalPath = destinationDirectory / std::filesystem::u8path(safeName);
	std::string finalPathString = finalPath.u8string();
	if (!GetClosestUnusedFileName(finalPathString, nullptr)) {
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("A destination folder could not be reserved for profile %1.").arg(profileName));
		return false;
	}
	finalPath = std::filesystem::u8path(finalPathString);
	const QString stagingName =
		QStringLiteral(".obs-import-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	const std::filesystem::path stagingPath =
		destinationDirectory / std::filesystem::u8path(stagingName.toUtf8().constData());

	QString error;
	if (!CopyProfileTree(std::filesystem::u8path(sourceProfile.absoluteFilePath().toUtf8().constData()),
			     stagingPath, &error)) {
		std::error_code cleanupError;
		std::filesystem::remove_all(stagingPath, cleanupError);
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("Profile %1 could not be copied: %2").arg(profileName, error));
		return false;
	}

	const std::filesystem::path basicIni = stagingPath / std::filesystem::u8path("basic.ini");
	ConfigFile config;
	if (config.Open(basicIni.u8string().c_str(), CONFIG_OPEN_EXISTING) != CONFIG_SUCCESS) {
		std::error_code cleanupError;
		std::filesystem::remove_all(stagingPath, cleanupError);
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("Imported profile %1 has an unreadable basic.ini.").arg(profileName));
		return false;
	}
	config_set_string(config, "General", "Name", profileName.toUtf8().constData());
	if (config.SaveSafe("tmp", "bak") != CONFIG_SUCCESS) {
		config.Close();
		std::error_code cleanupError;
		std::filesystem::remove_all(stagingPath, cleanupError);
		++report.failures;
		report.failureDetails.append(
			QStringLiteral("Imported profile %1 could not be finalized.").arg(profileName));
		return false;
	}
	config.Close();

	try {
		std::filesystem::rename(stagingPath, finalPath);
	} catch (const std::filesystem::filesystem_error &exception) {
		std::error_code cleanupError;
		std::filesystem::remove_all(stagingPath, cleanupError);
		++report.failures;
		report.failureDetails.append(QStringLiteral("Profile %1 could not be installed: %2")
						     .arg(profileName, QString::fromUtf8(exception.what())));
		return false;
	}

	++report.profilesImported;
	if (renamed) {
		++report.renamed;
	}
	blog(LOG_INFO, "Import OBS Profile: %s (%s) - SUCCESS", profileName.toUtf8().constData(),
	     finalPath.u8string().c_str());
	return true;
}

QString FormatImportReport(const ImportReport &report, const QStringList &pluginDataFolders = {})
{
	QStringList lines{
		QStringLiteral("Scene collections imported: %1").arg(report.collectionsImported),
		QStringLiteral("Profiles imported: %1").arg(report.profilesImported),
	};
	if (report.renamed > 0) {
		lines.append(QStringLiteral("Name conflicts safely renamed: %1").arg(report.renamed));
	}
	if (report.failures > 0) {
		lines.append(QStringLiteral("Items that failed: %1").arg(report.failures));
	}
	if (!report.missingSourceTypes.isEmpty()) {
		QStringList types = report.missingSourceTypes.values();
		types.sort(Qt::CaseInsensitive);
		lines.append(QStringLiteral("\nSources needing compatible plugins:\n- %1")
				     .arg(types.join(QStringLiteral("\n- "))));
	}
	if (!report.missingAssets.isEmpty()) {
		QStringList assets = report.missingAssets.values();
		assets.sort(Qt::CaseInsensitive);
		const qsizetype remaining = std::max<qsizetype>(0, assets.size() - 8);
		assets = assets.mid(0, 8);
		lines.append(QStringLiteral("\nMissing asset paths:\n- %1").arg(assets.join(QStringLiteral("\n- "))));
		if (remaining) {
			lines.append(QStringLiteral("...and %1 more missing path(s).").arg(remaining));
		}
	}
	if (!pluginDataFolders.isEmpty()) {
		lines.append(
			QStringLiteral(
				"\nPlugin data was detected but not copied. Reinstall compatible plugins first:\n- %1")
				.arg(pluginDataFolders.join(QStringLiteral("\n- "))));
	}
	if (!report.failureDetails.isEmpty()) {
		lines.append(QStringLiteral("\nImport errors:\n- %1")
				     .arg(report.failureDetails.join(QStringLiteral("\n- "))));
	}
	return lines.join(QLatin1Char('\n'));
}
} // namespace

OBSImporter::OBSImporter(QWidget *parent) : QDialog(parent), optionsModel(new ImporterModel), ui(new Ui::OBSImporter)
{
	setAcceptDrops(true);

	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	ui->setupUi(this);

	ui->tableView->setModel(optionsModel);
	ui->tableView->setItemDelegateForColumn(ImporterColumn::Path, new ImporterEntryPathItemDelegate());
	ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeMode::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(ImporterColumn::Name, QHeaderView::ResizeMode::Stretch);
	ui->tableView->horizontalHeader()->setSectionResizeMode(ImporterColumn::Path, QHeaderView::ResizeMode::Stretch);

	connect(optionsModel, &ImporterModel::dataChanged, this, &OBSImporter::dataChanged);

	ui->tableView->setEditTriggers(QAbstractItemView::EditTrigger::CurrentChanged);

	ui->buttonBox->button(QDialogButtonBox::Ok)->setText(QTStr("Import"));

	connect(ui->buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this,
		&OBSImporter::importCollections);
	connect(ui->importerSelectFiles, &QPushButton::clicked, this, &OBSImporter::browseImport);
	connect(ui->importOBSInstallation, &QPushButton::clicked, this, &OBSImporter::browseOBSInstallation);
	connect(ui->buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &OBSImporter::close);

	ImportersInit();

	bool autoSearchPrompt = config_get_bool(App()->GetUserConfig(), "General", "AutoSearchPrompt");

	if (!autoSearchPrompt) {
		QMessageBox::StandardButton button = OBSMessageBox::question(
			parent, QTStr("Importer.AutomaticCollectionPrompt"), QTStr("Importer.AutomaticCollectionText"));

		if (button == QMessageBox::Yes) {
			config_set_bool(App()->GetUserConfig(), "General", "AutomaticCollectionSearch", true);
		} else {
			config_set_bool(App()->GetUserConfig(), "General", "AutomaticCollectionSearch", false);
		}

		config_set_bool(App()->GetUserConfig(), "General", "AutoSearchPrompt", true);
	}

	bool autoSearch = config_get_bool(App()->GetUserConfig(), "General", "AutomaticCollectionSearch");

	OBSImporterFiles f;
	if (autoSearch) {
		f = ImportersFindFiles();
	}

	for (size_t i = 0; i < f.size(); i++) {
		QString path = f[i].c_str();
		path.replace("\\", "/");
		addImportOption(path, true);
	}

	f.clear();

	ui->tableView->resizeColumnsToContents();

	QModelIndex index = optionsModel->createIndex(optionsModel->rowCount() - 1, 2);
	QMetaObject::invokeMethod(ui->tableView, "setCurrentIndex", Qt::QueuedConnection,
				  Q_ARG(const QModelIndex &, index));
}

void OBSImporter::addImportOption(QString path, bool automatic)
{
	QStringList list;

	list.append(path);

	QModelIndex insertIndex = optionsModel->index(optionsModel->rowCount() - 1, ImporterColumn::Path);

	optionsModel->setData(insertIndex, list, automatic ? ImporterEntryRole::AutoPath : ImporterEntryRole::NewPath);
}

void OBSImporter::dropEvent(QDropEvent *ev)
{
	for (QUrl url : ev->mimeData()->urls()) {
		QFileInfo fileInfo(url.toLocalFile());
		if (fileInfo.isDir()) {

			QDirIterator dirIter(fileInfo.absoluteFilePath(), QDir::Files);

			while (dirIter.hasNext()) {
				addImportOption(dirIter.next(), false);
			}
		} else {
			addImportOption(fileInfo.canonicalFilePath(), false);
		}
	}
}

void OBSImporter::dragEnterEvent(QDragEnterEvent *ev)
{
	if (ev->mimeData()->hasUrls()) {
		ev->accept();
	}
}

void OBSImporter::browseImport()
{
	QString Pattern = "(*.json *.bpres *.xml *.xconfig)";

	QStringList paths = OpenFiles(this, QTStr("Importer.SelectCollection"), "",
				      QTStr("Importer.Collection") + QString(" ") + Pattern);

	if (!paths.empty()) {
		for (int i = 0; i < paths.count(); i++) {
			addImportOption(paths[i], false);
		}
	}
}

void OBSImporter::browseOBSInstallation()
{
	const QString selected =
		SelectDirectory(this, QTStr("Importer.SelectOBSInstallation"), DefaultOBSConfigurationRoot());
	if (selected.isEmpty()) {
		return;
	}

	const QString root = ResolveOBSConfigurationRoot(selected);
	if (root.isEmpty()) {
		OBSMessageBox::warning(this, QTStr("Importer.ImportOBSInstallation"),
				       QTStr("Importer.InvalidOBSInstallation"));
		return;
	}

	const QFileInfoList collections = FindSceneCollections(root);
	const QFileInfoList profiles = FindProfiles(root);
	if (collections.isEmpty() && profiles.isEmpty()) {
		OBSMessageBox::information(this, QTStr("Importer.ImportOBSInstallation"),
					   QTStr("Importer.EmptyOBSInstallation"));
		return;
	}

	const QString confirmation = QTStr("Importer.ConfirmOBSInstallation")
					     .arg(collections.size())
					     .arg(profiles.size())
					     .arg(QDir::toNativeSeparators(root));
	if (OBSMessageBox::question(this, QTStr("Importer.ImportOBSInstallation"), confirmation,
				    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
		return;
	}

	setEnabled(false);
	ImportReport report;
	QSet<QString> sceneNames = ExistingSceneNames();
	QSet<QString> profileNames = ExistingProfileNames();
	const std::filesystem::path sceneDestination =
		App()->userScenesLocation / std::filesystem::u8path(OBSSceneCollectionPath);
	const std::filesystem::path profileDestination =
		App()->userProfilesLocation / std::filesystem::u8path(OBSProfilePath);
	try {
		std::filesystem::create_directories(sceneDestination);
		std::filesystem::create_directories(profileDestination);
	} catch (const std::filesystem::filesystem_error &exception) {
		setEnabled(true);
		OBSMessageBox::critical(this, QTStr("Importer.ImportOBSInstallation"),
					QString::fromUtf8(exception.what()));
		return;
	}

	for (const QFileInfo &collection : collections) {
		WriteSceneCollection(collection.absoluteFilePath(), {}, sceneDestination, sceneNames, report);
	}
	for (const QFileInfo &profile : profiles) {
		ImportProfile(profile, profileDestination, profileNames, report);
	}

	profilesImported = report.profilesImported > 0;
	QStringList pluginDataFolders;
	QDir pluginConfig(QDir(root).filePath(QStringLiteral("plugin_config")));
	for (const QFileInfo &entry :
	     pluginConfig.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name)) {
		pluginDataFolders.append(entry.fileName());
	}

	OBSMessageBox::information(this, QTStr("Importer.OBSInstallationResult"),
				   FormatImportReport(report, pluginDataFolders));
	accept();
}

void OBSImporter::importCollections()
{
	setEnabled(false);

	const std::filesystem::path sceneCollectionLocation =
		App()->userScenesLocation / std::filesystem::u8path(OBSSceneCollectionPath);
	try {
		std::filesystem::create_directories(sceneCollectionLocation);
	} catch (const std::filesystem::filesystem_error &exception) {
		setEnabled(true);
		OBSMessageBox::critical(this, QTStr("Importer.SelectCollection"), QString::fromUtf8(exception.what()));
		return;
	}
	QSet<QString> reservedNames = ExistingSceneNames();
	ImportReport report;

	for (int i = 0; i < optionsModel->rowCount() - 1; i++) {
		int selected = optionsModel->index(i, ImporterColumn::Selected).data(Qt::CheckStateRole).value<int>();

		if (selected == Qt::Unchecked) {
			continue;
		}

		std::string pathStr = optionsModel->index(i, ImporterColumn::Path)
					      .data(Qt::DisplayRole)
					      .value<QString>()
					      .toStdString();
		std::string nameStr = optionsModel->index(i, ImporterColumn::Name)
					      .data(Qt::DisplayRole)
					      .value<QString>()
					      .toStdString();

		WriteSceneCollection(QString::fromStdString(pathStr), QString::fromStdString(nameStr),
				     sceneCollectionLocation, reservedNames, report);
	}

	OBSMessageBox::information(this, QTStr("Import"), FormatImportReport(report));
	accept();
}

void OBSImporter::dataChanged()
{
	ui->tableView->resizeColumnToContents(ImporterColumn::Name);

	bool enableImportButton = false;

	for (int i = 0; i < optionsModel->rowCount() - 1; i++) {
		int selected = optionsModel->index(i, ImporterColumn::Selected).data(Qt::CheckStateRole).value<int>();
		if (selected == Qt::Checked) {
			enableImportButton = true;
			break;
		}
	}

	ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enableImportButton);
}
