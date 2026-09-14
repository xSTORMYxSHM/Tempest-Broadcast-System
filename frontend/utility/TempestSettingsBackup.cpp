/******************************************************************************
    Copyright (C) 2026 Tempest Mainframe

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "TempestSettingsBackup.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace TempestRecovery {
namespace {

constexpr size_t kBackupRetention = 5;
constexpr char kBackupDirectoryName[] = "recovery-backups";
constexpr char kPendingUpdatePath[] = "updates/pending-update.json";
constexpr char kVerifiedUpdatePath[] = "updates/last-verified-update.json";

const std::set<std::string> kExcludedRootEntries = {".sentinel",          "crashes", "logs", "profiler_data",
						    kBackupDirectoryName, "updates"};

std::string Timestamp(bool filenameSafe)
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm utc{};
#ifdef _WIN32
	gmtime_s(&utc, &time);
#else
	gmtime_r(&time, &utc);
#endif
	std::ostringstream text;
	text << std::put_time(&utc, filenameSafe ? "%Y%m%d-%H%M%SZ" : "%Y-%m-%dT%H:%M:%SZ");
	return text.str();
}

std::string SanitizeName(std::string value)
{
	for (char &character : value) {
		const unsigned char byte = static_cast<unsigned char>(character);
		if (!std::isalnum(byte) && character != '-' && character != '_') {
			character = '-';
		}
	}
	while (value.find("--") != std::string::npos) {
		value.replace(value.find("--"), 2, "-");
	}
	return value.empty() ? "settings" : value;
}

bool IsExcluded(const std::filesystem::path &relative)
{
	if (relative.empty()) {
		return false;
	}
	std::string root = relative.begin()->u8string();
	std::transform(root.begin(), root.end(), root.begin(),
		       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return kExcludedRootEntries.count(root) != 0;
}

bool IsSceneCollection(const std::filesystem::path &relative)
{
	auto component = relative.begin();
	if (component == relative.end() || component->u8string() != "basic") {
		return false;
	}
	if (++component == relative.end() || component->u8string() != "scenes") {
		return false;
	}
	return relative.extension() == ".json";
}

nlohmann::json ReadJson(const std::filesystem::path &path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		throw std::runtime_error("Could not read " + path.u8string() + ".");
	}
	nlohmann::json value;
	input >> value;
	return value;
}

void WriteJsonAtomic(const std::filesystem::path &path, const nlohmann::json &value)
{
	std::filesystem::create_directories(path.parent_path());
	std::filesystem::path temporary = path;
	temporary += ".tmp";
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output) {
			throw std::runtime_error("Could not create " + temporary.u8string() + ".");
		}
		output << value.dump(2) << '\n';
		if (!output) {
			throw std::runtime_error("Could not finish writing " + temporary.u8string() + ".");
		}
	}
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	std::filesystem::rename(temporary, path);
}

std::filesystem::path UniqueBackupPath(const std::filesystem::path &root, const std::string &name)
{
	std::filesystem::path candidate = root / name;
	for (unsigned int suffix = 2; std::filesystem::exists(candidate); ++suffix) {
		candidate = root / (name + "-" + std::to_string(suffix));
	}
	return candidate;
}

void PruneBackups(const std::filesystem::path &backupRoot)
{
	std::vector<std::filesystem::path> backups;
	for (const auto &entry : std::filesystem::directory_iterator(backupRoot)) {
		if (entry.is_directory() && entry.path().filename().u8string().rfind(".incomplete-", 0) != 0 &&
		    std::filesystem::is_regular_file(entry.path() / "manifest.json")) {
			backups.push_back(entry.path());
		}
	}
	std::sort(backups.begin(), backups.end());
	while (backups.size() > kBackupRetention) {
		std::error_code ignored;
		std::filesystem::remove_all(backups.front(), ignored);
		backups.erase(backups.begin());
	}
}

std::filesystem::path PendingUpdateFile(const std::filesystem::path &configDirectory)
{
	return configDirectory / std::filesystem::u8path(kPendingUpdatePath);
}

} // namespace

BackupSummary CreateSettingsBackup(const std::filesystem::path &configDirectory, const std::string &reason,
				   const std::string &fromVersion, const std::string &targetVersion)
{
	const std::filesystem::path source = std::filesystem::absolute(configDirectory).lexically_normal();
	if (source.empty() || source == source.root_path()) {
		throw std::runtime_error("The settings backup refused an unsafe configuration directory.");
	}
	const std::filesystem::path backupRoot = source / kBackupDirectoryName;
	std::filesystem::create_directories(backupRoot);

	const std::string backupName = Timestamp(true) + "-" + SanitizeName(reason) + "-" + SanitizeName(fromVersion) +
				       "-to-" + SanitizeName(targetVersion);
	const std::filesystem::path destination = UniqueBackupPath(backupRoot, backupName);
	const std::filesystem::path staging = backupRoot / (".incomplete-" + destination.filename().u8string());
	std::error_code ignored;
	std::filesystem::remove_all(staging, ignored);

	BackupSummary summary;
	summary.directory = destination;
	std::vector<std::string> invalidScenes;
	try {
		std::filesystem::create_directories(staging / "settings");
		for (std::filesystem::recursive_directory_iterator iterator(source), end; iterator != end; ++iterator) {
			const std::filesystem::path relative = std::filesystem::relative(iterator->path(), source);
			if (IsExcluded(relative)) {
				if (iterator->is_directory()) {
					iterator.disable_recursion_pending();
				}
				continue;
			}
			if (iterator->is_symlink()) {
				if (iterator->is_directory()) {
					iterator.disable_recursion_pending();
				}
				continue;
			}

			const std::filesystem::path target = staging / "settings" / relative;
			if (iterator->is_directory()) {
				std::filesystem::create_directories(target);
				continue;
			}
			if (!iterator->is_regular_file()) {
				continue;
			}

			std::filesystem::create_directories(target.parent_path());
			std::filesystem::copy_file(iterator->path(), target,
						   std::filesystem::copy_options::overwrite_existing);
			const uint64_t sourceSize = iterator->file_size();
			if (std::filesystem::file_size(target) != sourceSize) {
				throw std::runtime_error("Backup verification failed for " + relative.u8string() + ".");
			}
			++summary.fileCount;
			summary.totalBytes += sourceSize;

			if (IsSceneCollection(relative)) {
				++summary.sceneFilesChecked;
				try {
					ReadJson(target);
				} catch (const std::exception &) {
					invalidScenes.push_back(relative.u8string());
					++summary.invalidSceneFiles;
				}
			}
		}

		nlohmann::json manifest = {
			{"schema", 1},
			{"product", "Tempest Broadcast System"},
			{"status", "complete"},
			{"created_utc", Timestamp(false)},
			{"reason", reason},
			{"from_version", fromVersion},
			{"target_version", targetVersion},
			{"source_directory", source.u8string()},
			{"file_count", summary.fileCount},
			{"total_bytes", summary.totalBytes},
			{"scene_files_checked", summary.sceneFilesChecked},
			{"invalid_scene_files", invalidScenes},
			{"excluded_root_entries", kExcludedRootEntries},
		};
		WriteJsonAtomic(staging / "manifest.json", manifest);
		std::filesystem::rename(staging, destination);
		PruneBackups(backupRoot);
		return summary;
	} catch (...) {
		std::filesystem::remove_all(staging, ignored);
		throw;
	}
}

void WritePendingUpdate(const std::filesystem::path &configDirectory, const BackupSummary &backup,
			const std::string &fromVersion, const std::string &targetVersion)
{
	nlohmann::json pending = {
		{"schema", 1},
		{"product", "Tempest Broadcast System"},
		{"created_utc", Timestamp(false)},
		{"from_version", fromVersion},
		{"target_version", targetVersion},
		{"backup_directory", std::filesystem::absolute(backup.directory).u8string()},
		{"backup_file_count", backup.fileCount},
		{"backup_total_bytes", backup.totalBytes},
		{"launch_state", "awaiting-first-start"},
		{"launch_attempt", 0},
	};
	WriteJsonAtomic(PendingUpdateFile(configDirectory), pending);
}

UpdateStartupStatus BeginUpdateStartup(const std::filesystem::path &configDirectory, const std::string &currentVersion)
{
	UpdateStartupStatus status;
	const std::filesystem::path pendingPath = PendingUpdateFile(configDirectory);
	std::error_code pendingError;
	if (!std::filesystem::is_regular_file(pendingPath, pendingError)) {
		return status;
	}

	try {
		nlohmann::json pending = ReadJson(pendingPath);
		status.fromVersion = pending.value("from_version", std::string());
		status.targetVersion = pending.value("target_version", std::string());
		if (status.targetVersion != currentVersion) {
			return {};
		}
		status.backupDirectory = std::filesystem::u8path(pending.value("backup_directory", std::string()));
		status.attempt = pending.value("launch_attempt", 0U) + 1;
		const std::string launchState = pending.value("launch_state", std::string());
		status.state = launchState == "first-start-running" ? UpdateStartupState::PreviousStartFailed
								    : UpdateStartupState::FirstStart;
		pending["launch_state"] = "first-start-running";
		pending["launch_attempt"] = status.attempt;
		pending["last_attempt_utc"] = Timestamp(false);
		WriteJsonAtomic(pendingPath, pending);
	} catch (const std::exception &) {
		return {};
	}
	return status;
}

void CompleteUpdateStartup(const std::filesystem::path &configDirectory, const std::string &currentVersion)
{
	const std::filesystem::path pendingPath = PendingUpdateFile(configDirectory);
	std::error_code pendingError;
	if (!std::filesystem::is_regular_file(pendingPath, pendingError)) {
		return;
	}

	try {
		nlohmann::json pending = ReadJson(pendingPath);
		if (pending.value("target_version", std::string()) != currentVersion) {
			return;
		}
		pending["verified_utc"] = Timestamp(false);
		pending["launch_state"] = "verified";
		WriteJsonAtomic(configDirectory / std::filesystem::u8path(kVerifiedUpdatePath), pending);
		std::filesystem::remove(pendingPath);
	} catch (const std::exception &) {
		// A stale diagnostic must never turn a successful application start into a failure.
	}
}

} // namespace TempestRecovery
