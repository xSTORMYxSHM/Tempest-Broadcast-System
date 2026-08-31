/******************************************************************************
    Copyright (C) 2026 Tempest Mainframe

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
******************************************************************************/

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <wintrust.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

#define TEMPEST_WIDEN_INNER(value) L##value
#define TEMPEST_WIDEN(value) TEMPEST_WIDEN_INNER(value)

constexpr wchar_t kProductName[] = L"Tempest Broadcast System";
constexpr wchar_t kUpdaterTitle[] = L"Tempest Broadcast System Updater";
constexpr wchar_t kBroadcastExecutable[] = L"tempest-broadcast-system.exe";
constexpr wchar_t kLatestReleaseApi[] =
	L"https://api.github.com/repos/xSTORMYxSHM/Tempest-Broadcast-System/releases/latest";

std::wstring g_resultFile;

class WinHttpHandle {
public:
	WinHttpHandle() = default;
	explicit WinHttpHandle(HINTERNET handle_) : handle(handle_) {}
	~WinHttpHandle()
	{
		if (handle) {
			WinHttpCloseHandle(handle);
		}
	}
	WinHttpHandle(const WinHttpHandle &) = delete;
	WinHttpHandle &operator=(const WinHttpHandle &) = delete;
	WinHttpHandle(WinHttpHandle &&other) noexcept : handle(other.handle) { other.handle = nullptr; }
	WinHttpHandle &operator=(WinHttpHandle &&other) noexcept
	{
		if (this != &other) {
			if (handle) {
				WinHttpCloseHandle(handle);
			}
			handle = other.handle;
			other.handle = nullptr;
		}
		return *this;
	}
	operator HINTERNET() const { return handle; }
	explicit operator bool() const { return handle != nullptr; }

private:
	HINTERNET handle = nullptr;
};

class FileHandle {
public:
	FileHandle() = default;
	explicit FileHandle(HANDLE handle_) : handle(handle_) {}
	~FileHandle()
	{
		if (handle && handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
		}
	}
	FileHandle(const FileHandle &) = delete;
	FileHandle &operator=(const FileHandle &) = delete;
	FileHandle(FileHandle &&other) noexcept : handle(other.handle) { other.handle = nullptr; }
	FileHandle &operator=(FileHandle &&other) noexcept
	{
		if (this != &other) {
			if (handle && handle != INVALID_HANDLE_VALUE) {
				CloseHandle(handle);
			}
			handle = other.handle;
			other.handle = nullptr;
		}
		return *this;
	}
	operator HANDLE() const { return handle; }
	explicit operator bool() const { return handle && handle != INVALID_HANDLE_VALUE; }

private:
	HANDLE handle = nullptr;
};

std::wstring Widen(const std::string &value)
{
	if (value.empty()) {
		return {};
	}
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
					 nullptr, 0);
	if (!length) {
		throw std::runtime_error("The server returned invalid UTF-8 text.");
	}
	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
			    length);
	return result;
}

std::string Narrow(const std::wstring &value)
{
	if (value.empty()) {
		return {};
	}
	int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
					 nullptr, 0, nullptr, nullptr);
	if (!length) {
		throw std::runtime_error("Could not convert Windows text to UTF-8.");
	}
	std::string result(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
			    length, nullptr, nullptr);
	return result;
}

std::string WindowsError(const char *operation)
{
	return std::string(operation) + " failed with Windows error " + std::to_string(GetLastError()) + ".";
}

void WriteResult(const std::string &outcome, const std::string &detail = {})
{
	if (g_resultFile.empty()) {
		return;
	}
	nlohmann::json result = {{"outcome", outcome}, {"detail", detail}};
	std::ofstream stream(std::filesystem::path(g_resultFile), std::ios::binary | std::ios::trunc);
	if (stream) {
		stream << result.dump(2) << '\n';
	}
}

struct Version {
	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;

	static Version Parse(std::string value)
	{
		constexpr char prefix[] = "tempest-v";
		if (value.rfind(prefix, 0) == 0) {
			value.erase(0, sizeof(prefix) - 1);
		}

		Version version;
		size_t start = 0;
		uint32_t *parts[] = {&version.major, &version.minor, &version.patch};
		for (size_t index = 0; index < 3; ++index) {
			size_t end = value.find('.', start);
			if (index == 2) {
				end = std::string::npos;
			}
			if (start >= value.size() || (index < 2 && end == std::string::npos)) {
				throw std::runtime_error("An update used an invalid version number.");
			}
			std::string part = value.substr(start, end == std::string::npos ? end : end - start);
			if (part.empty() ||
			    !std::all_of(part.begin(), part.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
				throw std::runtime_error("An update used an invalid version number.");
			}
			unsigned long parsed = std::stoul(part);
			if (parsed > UINT32_MAX) {
				throw std::runtime_error("An update version component was too large.");
			}
			*parts[index] = static_cast<uint32_t>(parsed);
			start = end == std::string::npos ? value.size() : end + 1;
		}
		return version;
	}

	std::string ToString() const
	{
		return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
	}

	friend bool operator<(const Version &left, const Version &right)
	{
		if (left.major != right.major) {
			return left.major < right.major;
		}
		if (left.minor != right.minor) {
			return left.minor < right.minor;
		}
		return left.patch < right.patch;
	}
};

struct HttpResponse {
	DWORD status = 0;
	uint64_t contentLength = 0;
	std::vector<uint8_t> body;
};

using DataSink = std::function<bool(const uint8_t *, DWORD, uint64_t, uint64_t)>;

HttpResponse HttpGet(const std::wstring &url, const DataSink &sink = {})
{
	if (url.rfind(L"https://", 0) != 0) {
		throw std::runtime_error("The updater refused a non-HTTPS download URL.");
	}

	std::array<wchar_t, 512> host{};
	std::array<wchar_t, 4096> path{};
	std::array<wchar_t, 4096> extra{};
	URL_COMPONENTS components{};
	components.dwStructSize = sizeof(components);
	components.lpszHostName = host.data();
	components.dwHostNameLength = static_cast<DWORD>(host.size());
	components.lpszUrlPath = path.data();
	components.dwUrlPathLength = static_cast<DWORD>(path.size());
	components.lpszExtraInfo = extra.data();
	components.dwExtraInfoLength = static_cast<DWORD>(extra.size());
	if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
		throw std::runtime_error(WindowsError("Parsing the update URL"));
	}
	if (components.nScheme != INTERNET_SCHEME_HTTPS) {
		throw std::runtime_error("The updater refused a non-HTTPS download URL.");
	}

	std::wstring userAgent = L"Tempest-Broadcast-Updater/" TEMPEST_WIDEN(TEMPEST_PRODUCT_VERSION);
	WinHttpHandle session(WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
					  WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session) {
		throw std::runtime_error(WindowsError("Opening the network session"));
	}
	WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);

	std::wstring hostName(host.data(), components.dwHostNameLength);
	WinHttpHandle connection(WinHttpConnect(session, hostName.c_str(), components.nPort, 0));
	if (!connection) {
		throw std::runtime_error(WindowsError("Connecting to the update server"));
	}

	std::wstring requestPath(path.data(), components.dwUrlPathLength);
	requestPath.append(extra.data(), components.dwExtraInfoLength);
	const wchar_t *acceptTypes[] = {L"application/vnd.github+json", L"application/octet-stream", nullptr};
	WinHttpHandle request(WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr, WINHTTP_NO_REFERER,
						 acceptTypes, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH));
	if (!request) {
		throw std::runtime_error(WindowsError("Creating the update request"));
	}

	constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
	if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(request, nullptr)) {
		throw std::runtime_error(WindowsError("Downloading update information"));
	}

	HttpResponse response;
	DWORD statusSize = sizeof(response.status);
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				 WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &statusSize,
				 WINHTTP_NO_HEADER_INDEX)) {
		throw std::runtime_error(WindowsError("Reading the update server response"));
	}

	wchar_t lengthText[64]{};
	DWORD lengthSize = sizeof(lengthText);
	if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
				&lengthSize, WINHTTP_NO_HEADER_INDEX)) {
		response.contentLength = _wcstoui64(lengthText, nullptr, 10);
	}

	uint64_t received = 0;
	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available)) {
			throw std::runtime_error(WindowsError("Reading update data"));
		}
		if (!available) {
			break;
		}
		std::vector<uint8_t> buffer(std::min<DWORD>(available, 64 * 1024));
		DWORD read = 0;
		if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
			throw std::runtime_error(WindowsError("Reading update data"));
		}
		if (!read) {
			break;
		}
		received += read;
		if (sink) {
			if (!sink(buffer.data(), read, received, response.contentLength)) {
				throw std::runtime_error("The update download was cancelled.");
			}
		} else {
			if (response.body.size() + read > 4 * 1024 * 1024) {
				throw std::runtime_error("The update server response was unexpectedly large.");
			}
			response.body.insert(response.body.end(), buffer.begin(), buffer.begin() + read);
		}
	}
	return response;
}

class ProgressDialog {
public:
	ProgressDialog(bool enabled, const std::wstring &line) : active(enabled)
	{
		if (!active) {
			return;
		}
		HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		comInitialized = SUCCEEDED(result);
		if (FAILED(CoCreateInstance(CLSID_ProgressDialog, nullptr, CLSCTX_INPROC_SERVER,
					    IID_PPV_ARGS(&dialog)))) {
			dialog = nullptr;
			return;
		}
		dialog->SetTitle(kUpdaterTitle);
		dialog->SetLine(1, line.c_str(), FALSE, nullptr);
		dialog->SetCancelMsg(L"Cancelling the update download...", nullptr);
		dialog->StartProgressDialog(nullptr, nullptr, PROGDLG_AUTOTIME | PROGDLG_NOMINIMIZE, nullptr);
	}

	~ProgressDialog()
	{
		if (dialog) {
			dialog->StopProgressDialog();
			dialog->Release();
		}
		if (comInitialized) {
			CoUninitialize();
		}
	}

	bool Update(uint64_t completed, uint64_t total)
	{
		if (!dialog) {
			return true;
		}
		if (total) {
			dialog->SetProgress64(completed, total);
		}
		return !dialog->HasUserCancelled();
	}

private:
	IProgressDialog *dialog = nullptr;
	bool active = false;
	bool comInitialized = false;
};

struct ReleaseAsset {
	Version version;
	std::string versionText;
	std::string name;
	std::wstring url;
	std::string digest;
	uint64_t size = 0;
};

ReleaseAsset GetLatestRelease()
{
	HttpResponse response = HttpGet(kLatestReleaseApi);
	if (response.status != 200) {
		throw std::runtime_error("GitHub returned HTTP " + std::to_string(response.status) +
					 ". The update check could not be completed.");
	}
	nlohmann::json release = nlohmann::json::parse(response.body.begin(), response.body.end());
	if (release.value("draft", true) || release.value("prerelease", true)) {
		throw std::runtime_error("The latest GitHub release is not a final public release.");
	}

	ReleaseAsset result;
	result.version = Version::Parse(release.at("tag_name").get<std::string>());
	result.versionText = result.version.ToString();
	result.name = "tempest-broadcast-system-" + result.versionText + "-windows-x64-installer.exe";
	for (const auto &asset : release.at("assets")) {
		if (asset.value("name", std::string()) != result.name) {
			continue;
		}
		result.url = Widen(asset.at("browser_download_url").get<std::string>());
		result.digest = asset.value("digest", std::string());
		result.size = asset.at("size").get<uint64_t>();
		break;
	}
	if (result.url.empty()) {
		throw std::runtime_error("The latest Tempest release does not contain the expected Windows installer.");
	}
	if (result.digest.rfind("sha256:", 0) != 0 || result.digest.size() != 71) {
		throw std::runtime_error("The latest installer does not publish a valid SHA-256 digest.");
	}
	return result;
}

std::filesystem::path DefaultDownloadDirectory()
{
	PWSTR localAppData = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
		throw std::runtime_error("Windows could not locate the Local AppData folder.");
	}
	std::filesystem::path directory(localAppData);
	CoTaskMemFree(localAppData);
	directory /= L"Tempest Mainframe";
	directory /= L"Tempest Broadcast System";
	directory /= L"Updates";
	return directory;
}

void DownloadInstaller(const ReleaseAsset &asset, const std::filesystem::path &destination, bool showProgress)
{
	std::filesystem::create_directories(destination.parent_path());
	std::filesystem::path partial = destination;
	partial += L".download";
	std::error_code ignored;
	std::filesystem::remove(partial, ignored);
	FileHandle file(CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
				    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
	if (!file) {
		throw std::runtime_error(WindowsError("Creating the installer download"));
	}

	ProgressDialog progress(showProgress, L"Downloading Tempest Broadcast System " + Widen(asset.versionText));
	try {
		HttpResponse response =
			HttpGet(asset.url, [&](const uint8_t *data, DWORD size, uint64_t completed, uint64_t total) {
				DWORD written = 0;
				if (!WriteFile(file, data, size, &written, nullptr) || written != size) {
					throw std::runtime_error(WindowsError("Writing the installer download"));
				}
				return progress.Update(completed, total ? total : asset.size);
			});
		if (response.status != 200) {
			throw std::runtime_error("GitHub returned HTTP " + std::to_string(response.status) +
						 ". The installer could not be downloaded.");
		}
	} catch (...) {
		std::filesystem::remove(partial, ignored);
		throw;
	}
	file = FileHandle();
	std::filesystem::remove(destination, ignored);
	std::filesystem::rename(partial, destination);
}

std::string Sha256(const std::filesystem::path &path)
{
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
		throw std::runtime_error("Windows could not initialize SHA-256 verification.");
	}
	DWORD objectLength = 0;
	DWORD returned = 0;
	if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
			      sizeof(objectLength), &returned, 0) < 0) {
		BCryptCloseAlgorithmProvider(algorithm, 0);
		throw std::runtime_error("Windows could not initialize SHA-256 verification.");
	}
	std::vector<uint8_t> object(objectLength);
	if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) {
		BCryptCloseAlgorithmProvider(algorithm, 0);
		throw std::runtime_error("Windows could not initialize SHA-256 verification.");
	}

	FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				    FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
	if (!file) {
		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		throw std::runtime_error(WindowsError("Opening the downloaded installer"));
	}
	std::array<uint8_t, 64 * 1024> buffer{};
	for (;;) {
		DWORD read = 0;
		if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
			BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			throw std::runtime_error(WindowsError("Reading the downloaded installer"));
		}
		if (!read) {
			break;
		}
		if (BCryptHashData(hash, buffer.data(), read, 0) < 0) {
			BCryptDestroyHash(hash);
			BCryptCloseAlgorithmProvider(algorithm, 0);
			throw std::runtime_error("Windows could not verify the installer SHA-256 digest.");
		}
	}
	std::array<uint8_t, 32> digest{};
	if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		throw std::runtime_error("Windows could not verify the installer SHA-256 digest.");
	}
	BCryptDestroyHash(hash);
	BCryptCloseAlgorithmProvider(algorithm, 0);

	constexpr char hex[] = "0123456789abcdef";
	std::string result = "sha256:";
	result.reserve(71);
	for (uint8_t byte : digest) {
		result.push_back(hex[byte >> 4]);
		result.push_back(hex[byte & 0x0f]);
	}
	return result;
}

Version ReadFileVersion(const std::filesystem::path &path)
{
	DWORD ignored = 0;
	DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
	if (!size) {
		throw std::runtime_error("The downloaded installer has no Windows version information.");
	}
	std::vector<uint8_t> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
		throw std::runtime_error(WindowsError("Reading the installer version"));
	}
	VS_FIXEDFILEINFO *info = nullptr;
	UINT infoSize = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &infoSize) || !info) {
		throw std::runtime_error("The downloaded installer has invalid Windows version information.");
	}
	return {HIWORD(info->dwProductVersionMS), LOWORD(info->dwProductVersionMS), HIWORD(info->dwProductVersionLS)};
}

bool HasTrustedSignature(const std::filesystem::path &path)
{
	WINTRUST_FILE_INFO file{};
	file.cbStruct = sizeof(file);
	file.pcwszFilePath = path.c_str();
	WINTRUST_DATA trust{};
	trust.cbStruct = sizeof(trust);
	trust.dwUIChoice = WTD_UI_NONE;
	trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
	trust.dwUnionChoice = WTD_CHOICE_FILE;
	trust.pFile = &file;
	trust.dwStateAction = WTD_STATEACTION_VERIFY;
	trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
	GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	LONG status = WinVerifyTrust(nullptr, &policy, &trust);
	trust.dwStateAction = WTD_STATEACTION_CLOSE;
	WinVerifyTrust(nullptr, &policy, &trust);
	return status == ERROR_SUCCESS;
}

void VerifyInstaller(const ReleaseAsset &asset, const std::filesystem::path &path)
{
	std::error_code error;
	uint64_t size = std::filesystem::file_size(path, error);
	if (error || size != asset.size) {
		throw std::runtime_error("The downloaded installer size does not match the GitHub release.");
	}
	if (Sha256(path) != asset.digest) {
		throw std::runtime_error("The downloaded installer failed SHA-256 verification.");
	}
	Version fileVersion = ReadFileVersion(path);
	if (fileVersion.ToString() != asset.versionText) {
		throw std::runtime_error("The downloaded installer version does not match the GitHub release.");
	}
	if (!HasTrustedSignature(path)) {
		throw std::runtime_error("The downloaded installer does not have a trusted Authenticode signature.");
	}
}

bool ProcessRunning(DWORD processId)
{
	FileHandle process(OpenProcess(SYNCHRONIZE, FALSE, processId));
	return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

std::vector<DWORD> FindBroadcastProcesses(DWORD preferredProcess)
{
	std::vector<DWORD> processes;
	if (preferredProcess && ProcessRunning(preferredProcess)) {
		processes.push_back(preferredProcess);
	}
	FileHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
	if (!snapshot) {
		return processes;
	}
	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	if (!Process32FirstW(snapshot, &entry)) {
		return processes;
	}
	do {
		if (_wcsicmp(entry.szExeFile, kBroadcastExecutable) == 0 &&
		    std::find(processes.begin(), processes.end(), entry.th32ProcessID) == processes.end()) {
			processes.push_back(entry.th32ProcessID);
		}
	} while (Process32NextW(snapshot, &entry));
	return processes;
}

BOOL CALLBACK CloseBroadcastWindow(HWND window, LPARAM parameter)
{
	auto *processes = reinterpret_cast<std::vector<DWORD> *>(parameter);
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (std::find(processes->begin(), processes->end(), processId) != processes->end()) {
		PostMessageW(window, WM_CLOSE, 0, 0);
	}
	return TRUE;
}

bool CloseBroadcastProcesses(DWORD preferredProcess)
{
	std::vector<DWORD> processes = FindBroadcastProcesses(preferredProcess);
	if (processes.empty()) {
		return true;
	}

	for (;;) {
		EnumWindows(CloseBroadcastWindow, reinterpret_cast<LPARAM>(&processes));
		for (int attempt = 0; attempt < 120; ++attempt) {
			if (std::none_of(processes.begin(), processes.end(), ProcessRunning)) {
				return true;
			}
			Sleep(250);
		}
		int choice = MessageBoxW(
			nullptr,
			L"Tempest Broadcast System is still running. Finish or stop any active output, close "
			L"the application, and then choose Retry. The updater will never force-terminate it.",
			kUpdaterTitle, MB_RETRYCANCEL | MB_ICONWARNING | MB_SETFOREGROUND);
		if (choice != IDRETRY) {
			return false;
		}
	}
}

void LaunchInstaller(const std::filesystem::path &installer)
{
	SHELLEXECUTEINFOW execute{};
	execute.cbSize = sizeof(execute);
	execute.fMask = SEE_MASK_NOCLOSEPROCESS;
	execute.lpVerb = L"open";
	execute.lpFile = installer.c_str();
	execute.lpParameters = L"/S /UPDATE";
	execute.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&execute)) {
		throw std::runtime_error(WindowsError("Starting the Tempest installer"));
	}
	if (execute.hProcess) {
		CloseHandle(execute.hProcess);
	}
}

struct Options {
	bool quiet = false;
	bool downloadOnly = false;
	Version currentVersion = Version::Parse(TEMPEST_PRODUCT_VERSION);
	DWORD parentProcess = 0;
	std::optional<std::filesystem::path> downloadDirectory;
};

Options ParseOptions()
{
	Options options;
	int argc = 0;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) {
		throw std::runtime_error(WindowsError("Reading updater arguments"));
	}
	for (int index = 1; index < argc; ++index) {
		std::wstring argument = argv[index];
		if (argument == L"--quiet") {
			options.quiet = true;
		} else if (argument == L"--download-only") {
			options.downloadOnly = true;
		} else if (argument == L"--current-version" && index + 1 < argc) {
			options.currentVersion = Version::Parse(Narrow(argv[++index]));
		} else if (argument == L"--parent-pid" && index + 1 < argc) {
			options.parentProcess = wcstoul(argv[++index], nullptr, 10);
		} else if (argument == L"--download-directory" && index + 1 < argc) {
			options.downloadDirectory = std::filesystem::path(argv[++index]);
		} else if (argument == L"--result-file" && index + 1 < argc) {
			g_resultFile = argv[++index];
		} else {
			LocalFree(argv);
			throw std::runtime_error("The updater received an invalid command-line option.");
		}
	}
	LocalFree(argv);
	return options;
}

int RunUpdater(const Options &options)
{
	ReleaseAsset latest = GetLatestRelease();
	if (!(options.currentVersion < latest.version)) {
		WriteResult("current", latest.versionText);
		if (!options.quiet) {
			std::wstring message = L"Tempest Broadcast System " + Widen(options.currentVersion.ToString()) +
					       L" is up to date.";
			MessageBoxW(nullptr, message.c_str(), kUpdaterTitle,
				    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
		}
		return 0;
	}

	if (!options.downloadOnly) {
		std::wstring message =
			L"Tempest Broadcast System " + Widen(latest.versionText) +
			L" is available.\n\nThe signed installer will be downloaded and verified. "
			L"Broadcast will then close and restart on the new version.\n\nInstall this update now?";
		if (MessageBoxW(nullptr, message.c_str(), kUpdaterTitle,
				MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND) != IDYES) {
			WriteResult("cancelled", "The user declined the update.");
			return 0;
		}
	}

	std::filesystem::path directory = options.downloadDirectory.value_or(DefaultDownloadDirectory());
	std::filesystem::path installer = directory / Widen(latest.name);
	DownloadInstaller(latest, installer, !options.quiet && !options.downloadOnly);
	VerifyInstaller(latest, installer);

	if (options.downloadOnly) {
		WriteResult("downloaded", Narrow(installer.wstring()));
		return 0;
	}

	if (!CloseBroadcastProcesses(options.parentProcess)) {
		WriteResult("cancelled", "Broadcast remained open.");
		return 0;
	}
	WriteResult("installer-started", Narrow(installer.wstring()));
	LaunchInstaller(installer);
	return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	bool quiet = false;
	try {
		Options options = ParseOptions();
		quiet = options.quiet;
		return RunUpdater(options);
	} catch (const std::exception &error) {
		WriteResult("error", error.what());
		if (!quiet) {
			MessageBoxW(nullptr, Widen(error.what()).c_str(), kUpdaterTitle,
				    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
		}
		return 1;
	}
}
