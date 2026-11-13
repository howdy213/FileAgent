/*
 * Copyright 2026 howdy213
 *
 * This file is part of FileAgent.
 *
 * FileAgent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FileAgent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FileAgent. If not, see <https://www.gnu.org/licenses/>.
 *
 * 实现 FAConfig.ini / FileAgent.ini 的读写与命令行处理
 */
#include "FAConfig.h"
#include "WinUtils/INI.h"
#include "WinUtils/CmdParser.h"
#include "WinUtils/WinUtils.h"

#include <algorithm>

using namespace std;
using namespace WinUtils;
namespace fs = filesystem;

static wstring ToLowerCopy(const wstring& text) {
	wstring result = text;
	INIStringUtil::toLower(result);
	return result;
}

static wstring TrimCopy(const wstring& text) {
	wstring result = text;
	INIStringUtil::trim(result);
	return result;
}

// ==================== 路径 ====================
fs::path GetConfigDirectory() {
	return GetCurrentProcessFSDir();
}

fs::path GetFAConfigPath() {
	return GetConfigDirectory() / FA_CONFIG_FILE_NAME;
}

fs::path GetFileAgentConfigPath() {
	return GetConfigDirectory() / FILE_AGENT_INI_NAME;
}

static void CreateDefaultFileAgentIni(const fs::path& iniPath) {
	INIStructure data;
	data[L"Settings"][L"Overwrite"] = L"true";
	data[L"Settings"][L"BackupRoot"] = L"";
	data[L"Settings"][L"BackupDir"] = L".\\AutoSaveBackup";
	data[L"Settings"][L"OpenCopy"] = L"false";
	data[L"Settings"][L"AddTimestamp"] = L"true";
	data[L"Settings"][L"Extensions"] = L".doc,.docx,.ppt,.pptx,.xls,.xlsx,.pdf";
	data[L"Settings"][L"EnaPath"] = L"";
	INIFile ini(iniPath);
	static_cast<void>(ini.generate(data, true));
}

void EnsureConfigFiles() {
	fs::path faPath = GetFAConfigPath();
	if (!fs::exists(faPath)) {
		INIStructure data;
		data[FA_CONFIG_SETTINGS][L"Enabled"] = L"true";
		INIFile ini(faPath);
		static_cast<void>(ini.generate(data, true));
	}

	fs::path agentPath = GetFileAgentConfigPath();
	if (!fs::exists(agentPath)) {
		CreateDefaultFileAgentIni(agentPath);
	}
}

// ==================== 启用状态 ====================
bool IsCommandEnabled() {
	INIStructure data;
	INIFile ini(GetFAConfigPath());
	if (!ini.read(data)) return true;
	return data[FA_CONFIG_SETTINGS].get_as<bool>(L"Enabled", true);
}

bool SetCommandEnabled(bool enabled) {
	INIStructure data;
	INIFile ini(GetFAConfigPath());
	static_cast<void>(ini.read(data));
	data[FA_CONFIG_SETTINGS][L"Enabled"] = enabled ? L"true" : L"false";
	return ini.generate(data, true);
}

// ==================== 别名 ====================
vector<AliasEntry> LoadAliases() {
	vector<AliasEntry> aliases;
	INIStructure data;
	INIFile ini(GetFAConfigPath());
	if (!ini.read(data)) return aliases;

	auto& section = data[FA_CONFIG_ALIASES];
	for (auto const& item : section) {
		if (item.first.empty()) continue;
		aliases.push_back({ item.first, item.second });
	}
	return aliases;
}

bool SaveAliases(const vector<AliasEntry>& aliases) {
	INIStructure data;
	INIFile ini(GetFAConfigPath());
	static_cast<void>(ini.read(data));

	auto& settings = data[FA_CONFIG_SETTINGS];
	if (!settings.has(L"Enabled")) settings[L"Enabled"] = L"true";

	data[FA_CONFIG_ALIASES].clear();
	for (const auto& alias : aliases) {
		data[FA_CONFIG_ALIASES][alias.name] = alias.path;
	}
	return ini.generate(data, true);
}

bool ResolveAliasValue(const wstring& name, wstring& value) {
	INIStructure data;
	INIFile ini(GetFAConfigPath());
	if (!ini.read(data)) return false;

	auto& section = data[FA_CONFIG_ALIASES];
	const wstring target = ToLowerCopy(TrimCopy(name));
	for (auto const& item : section) {
		if (ToLowerCopy(item.first) == target) {
			value = item.second;
			return true;
		}
	}
	return false;
}

// ==================== 注册状态（委托 FileAgent） ====================
// 向同目录的 FileAgent.exe 传参，通过其退出码获取结果：
//   status    -> 0 已安装 / 1 未安装
//   install   -> 0 成功 / 1 失败
//   uninstall -> 0 成功 / 1 失败
static bool RunFileAgent(const wstring& arguments, DWORD& exitCode) {
	const fs::path agentPath = GetConfigDirectory() / FILE_AGENT_EXE_NAME;
	if (!fs::exists(agentPath)) return false;

	const wstring commandLine = L"\"" + agentPath.wstring() + L"\" " + arguments;
	vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
	buffer.push_back(L'\0');

	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo = {};

	if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
		0, nullptr, agentPath.parent_path().c_str(), &startupInfo, &processInfo)) {
		return false;
	}

	// 最多等待 30 秒，避免 FileAgent.exe 异常时阻塞界面
	if (WaitForSingleObject(processInfo.hProcess, 30000) == WAIT_TIMEOUT) {
		TerminateProcess(processInfo.hProcess, 1);
	}
	exitCode = 1;
	GetExitCodeProcess(processInfo.hProcess, &exitCode);
	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	return true;
}

bool IsRegistered() {
	DWORD exitCode = 1;
	return RunFileAgent(L"--status", exitCode) && exitCode == 0;
}

bool RegisterToSystem() {
	DWORD exitCode = 1;
	return RunFileAgent(L"--install", exitCode) && exitCode == 0;
}

bool UnregisterFromSystem() {
	DWORD exitCode = 1;
	return RunFileAgent(L"--uninstall", exitCode) && exitCode == 0;
}

wstring GetRegistrationSummary() {
	if (!fs::exists(GetConfigDirectory() / FILE_AGENT_EXE_NAME)) return L"未找到 FileAgent.exe";
	return IsRegistered() ? L"FileAgent 已安装" : L"FileAgent 未安装";
}

// ==================== FileAgent.ini 修改 ====================
bool WriteFileAgentSetting(const wstring& key, const wstring& value) {
	fs::path iniPath = GetFileAgentConfigPath();
	if (!fs::exists(iniPath)) {
		CreateDefaultFileAgentIni(iniPath);
	}

	INIStructure data;
	INIFile ini(iniPath);
	if (!ini.read(data)) return false;

	data[L"Settings"][key] = value;
	return ini.write(data, true);
}

// ==================== FileAgent.ini 设置 ====================
FileAgentSettings LoadFileAgentSettings() {
	FileAgentSettings settings;
	fs::path iniPath = GetFileAgentConfigPath();
	if (!fs::exists(iniPath)) {
		CreateDefaultFileAgentIni(iniPath);
	}

	INIStructure data;
	INIFile ini(iniPath);
	if (!ini.read(data)) return settings;

	auto& section = data[L"Settings"];

	const wstring backupDir = section.get(L"BackupDir");
	if (!backupDir.empty()) settings.backupDir = backupDir;
	settings.backupRoot = section.get(L"BackupRoot");
	settings.enaPath = section.get(L"EnaPath");
	const wstring extensions = section.get(L"Extensions");
	if (!extensions.empty()) settings.extensions = extensions;
	settings.overwrite = section.get_as<bool>(L"Overwrite", true);
	settings.openCopy = section.get_as<bool>(L"OpenCopy", false);
	settings.addTimestamp = section.get_as<bool>(L"AddTimestamp", true);
	return settings;
}

bool SaveFileAgentSettings(const FileAgentSettings& settings) {
	fs::path iniPath = GetFileAgentConfigPath();
	if (!fs::exists(iniPath)) {
		CreateDefaultFileAgentIni(iniPath);
	}

	INIStructure data;
	INIFile ini(iniPath);
	if (!ini.read(data)) return false;

	auto& section = data[L"Settings"];
	section[L"BackupDir"] = settings.backupDir;
	section[L"BackupRoot"] = settings.backupRoot;
	section[L"Extensions"] = settings.extensions;
	section[L"EnaPath"] = settings.enaPath;
	section[L"Overwrite"] = settings.overwrite ? L"true" : L"false";
	section[L"OpenCopy"] = settings.openCopy ? L"true" : L"false";
	section[L"AddTimestamp"] = settings.addTimestamp ? L"true" : L"false";
	return ini.write(data, true);
}

// 将命令名映射到 FileAgent.ini 的配置键
static wstring MapCommandToSettingKey(const wstring& command) {
	const wstring lower = ToLowerCopy(command);
	if (lower == L"subject" || lower == L"backupdir") return L"BackupDir";
	if (lower == L"backuproot") return L"BackupRoot";
	if (lower == L"extensions") return L"Extensions";
	if (lower == L"overwrite") return L"Overwrite";
	if (lower == L"opencopy") return L"OpenCopy";
	if (lower == L"addtimestamp") return L"AddTimestamp";
	return command;
}

static bool IsCommandToken(const wstring& token) {
	if (token.empty()) return false;
	if (token.size() >= 2 && token[0] == L'-' && token[1] == L'-') return true;
	return token[0] == L'-' || token[0] == L'/';
}

static size_t GetCommandPrefixLength(const wstring& token) {
	if (token.size() >= 2 && token[0] == L'-' && token[1] == L'-') return 2;
	return 1;
}

// 输出到调用方的控制台（若存在）；无控制台时静默
static void WriteStdoutLine(const wstring& text) {
	static int consoleState = -1; // -1 未检测，0 无控制台，1 有控制台
	if (consoleState < 0) {
		consoleState = GetConsoleWindow() ? 1 : 0;
		if (consoleState == 0 && AttachConsole(ATTACH_PARENT_PROCESS)) {
			consoleState = 1;
		}
	}
	if (consoleState == 0) return;

	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!output || output == INVALID_HANDLE_VALUE) return;

	const wstring line = text + L"\r\n";
	DWORD written = 0;
	WriteConsoleW(output, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
}

int RunCommandLine(const wstring& cmdLine) {
	EnsureConfigFiles();

	const vector<wstring> tokens = CmdParser(false).tokenize(cmdLine);
	bool enabled = IsCommandEnabled();
	int exitCode = 0;

	for (size_t i = 0; i < tokens.size(); ++i) {
		if (!IsCommandToken(tokens[i])) continue;

		const wstring rest = tokens[i].substr(GetCommandPrefixLength(tokens[i]));
		wstring name = rest;
		wstring inlineValue;
		bool hasInlineValue = false;

		const size_t eq = rest.find(L'=');
		if (eq != wstring::npos) {
			name = rest.substr(0, eq);
			inlineValue = rest.substr(eq + 1);
			hasInlineValue = true;
		}
		name = TrimCopy(name);
		if (name.empty()) continue;

		const wstring lowerName = ToLowerCopy(name);

		// 注册管理命令：始终可用，不受启用状态限制
		if (lowerName == L"register") {
			const bool ok = RegisterToSystem();
			WriteStdoutLine(ok ? L"FAConfig 已注册。" : L"FAConfig 注册失败。");
			if (!ok) exitCode = 1;
			continue;
		}
		if (lowerName == L"unregister") {
			const bool ok = UnregisterFromSystem();
			WriteStdoutLine(ok ? L"FAConfig 已卸载注册。" : L"FAConfig 卸载注册失败。");
			if (!ok) exitCode = 1;
			continue;
		}
		if (lowerName == L"status" || lowerName == L"isregistered") {
			const bool registered = IsRegistered();
			WriteStdoutLine(registered ? L"已注册" : L"未注册");
			exitCode = registered ? 0 : 1;
			continue;
		}

		// 特殊命令：不受启用状态限制的方向切换
		if (lowerName == L"disable") {
			enabled = false;
			if (!SetCommandEnabled(false)) exitCode = 1;
			continue;
		}
		if (lowerName == L"enable") {
			enabled = true;
			if (!SetCommandEnabled(true)) exitCode = 1;
			continue;
		}

		// 收集该命令的参数，直到下一个命令
		vector<wstring> values;
		if (hasInlineValue) values.push_back(inlineValue);
		size_t next = i + 1;
		while (next < tokens.size() && !IsCommandToken(tokens[next])) {
			values.push_back(tokens[next]);
			++next;
		}
		i = next - 1;

		if (!enabled) continue;
		if (values.empty()) continue;

		wstring rawValue;
		for (const auto& part : values) {
			if (!rawValue.empty()) rawValue += L" ";
			rawValue += part;
		}

		wstring value = rawValue;
		wstring aliasValue;
		if (ResolveAliasValue(rawValue, aliasValue)) {
			value = aliasValue;
		}

		if (!WriteFileAgentSetting(MapCommandToSettingKey(name), value)) {
			exitCode = 1;
		}
	}

	return exitCode;
}
