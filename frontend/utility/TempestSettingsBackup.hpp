/******************************************************************************
    Copyright (C) 2026 Tempest Mainframe

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace TempestRecovery {

struct BackupSummary {
	std::filesystem::path directory;
	uint64_t fileCount = 0;
	uint64_t totalBytes = 0;
	uint64_t sceneFilesChecked = 0;
	uint64_t invalidSceneFiles = 0;
};

enum class UpdateStartupState {
	None,
	FirstStart,
	PreviousStartFailed,
};

struct UpdateStartupStatus {
	UpdateStartupState state = UpdateStartupState::None;
	std::filesystem::path backupDirectory;
	std::string fromVersion;
	std::string targetVersion;
	uint32_t attempt = 0;
};

BackupSummary CreateSettingsBackup(const std::filesystem::path &configDirectory, const std::string &reason,
				   const std::string &fromVersion, const std::string &targetVersion);

void WritePendingUpdate(const std::filesystem::path &configDirectory, const BackupSummary &backup,
			const std::string &fromVersion, const std::string &targetVersion);

UpdateStartupStatus BeginUpdateStartup(const std::filesystem::path &configDirectory, const std::string &currentVersion);

void CompleteUpdateStartup(const std::filesystem::path &configDirectory, const std::string &currentVersion);

} // namespace TempestRecovery
