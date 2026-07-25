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
 *  实现配置加载，定义全局变量
 */
#include "FileAgent.h"
#include "WinUtils/INI.h"
#include "WinUtils/WinUtils.h"
using namespace std;

using namespace WinUtils;
namespace fs = filesystem;

// 全局变量定义
WinUtils::Logger logger(APP_NAME);
vector<wstring> g_skipBackupRoots;
wstring g_backupStorePath;
vector<wstring> g_extensions = DEFAULT_EXTENSIONS;
bool g_openCopy = false;
bool g_overwrite = true;
bool g_addTimestamp = true;

static wstring JoinPaths(const vector<wstring>& paths) {
	wstring result;
	for (const auto& p : paths) {
		if (!result.empty()) result += L", ";
		result += p;
	}
	return result;
}

void LoadConfig() {
	fs::path iniPath = GetCurrentProcessFSDir() / (APP_NAME + L".ini");
	INIFile ini(iniPath);
	INIStructure data;

	if (!fs::exists(iniPath)) {
		data[TS("Settings")][TS("Overwrite")] = TS("true");
		data[TS("Settings")][TS("BackupRoot")] = TS("");   // 空表示不跳过
		data[TS("Settings")][TS("BackupDir")] = (wstring)L".\\" + DEFAULT_BACKUP_DIR_NAME;
		data[TS("Settings")][TS("OpenCopy")] = TS("false");
		data[TS("Settings")][TS("AddTimestamp")] = TS("true");
		wstring extStr;
		for (const auto& ext : DEFAULT_EXTENSIONS) {
			if (!extStr.empty()) extStr += L",";
			extStr += ext;
		}
		data[TS("Settings")][TS("Extensions")] = extStr;
		ini.generate(data, true);
		logger.DLog(LogLevel::Info, L"创建默认配置文件: " + iniPath.wstring());

		// 设置默认值
		g_overwrite = true;
		g_skipBackupRoots.clear();
		g_backupStorePath = (GetCurrentProcessFSDir() / DEFAULT_BACKUP_DIR_NAME).wstring();
		g_extensions = DEFAULT_EXTENSIONS;
		g_openCopy = false;
		g_addTimestamp = true;
		return;
	}

	if (!ini.read(data)) {
		logger.DLog(LogLevel::Error, L"读取配置文件失败，使用默认配置（不跳过任何目录）");
		g_overwrite = true;
		g_skipBackupRoots.clear();
		g_backupStorePath = (GetCurrentProcessFSDir() / DEFAULT_BACKUP_DIR_NAME).wstring();
		g_extensions = DEFAULT_EXTENSIONS;
		g_openCopy = false;
		g_addTimestamp = true;
		return;
	}

	auto& settings = data[TS("Settings")];

	wstring skipRoot = settings.get(TS("BackupRoot"));
	g_skipBackupRoots.clear();

	if (!skipRoot.empty()) {
		vector<wstring> tokens;
		size_t start = 0;
		while (true) {
			size_t delim = skipRoot.find_first_of(L";,", start);
			wstring token = (delim == wstring::npos) ? skipRoot.substr(start) : skipRoot.substr(start, delim - start);
			token.erase(0, token.find_first_not_of(L" \t"));
			token.erase(token.find_last_not_of(L" \t") + 1);
			if (!token.empty()) {
				fs::path root = ResolveFSPath(token, GetCurrentProcessFSDir());
				wstring path = root.wstring();
				if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
					path += L'\\';
				}
				if (!path.empty()) {
					g_skipBackupRoots.push_back(path);
				}
			}
			if (delim == wstring::npos) break;
			start = delim + 1;
		}
		logger.DLog(LogLevel::Info, L"手动设置跳过根目录: " + JoinPaths(g_skipBackupRoots));
	}
	else {
		logger.DLog(LogLevel::Info, L"BackupRoot 为空，不跳过任何目录");
	}

	// ---------- BackupDir ----------
	wstring storeDir = settings.get(TS("BackupDir"));
	if (storeDir.empty()) storeDir = DEFAULT_BACKUP_DIR_NAME;
	fs::path storePath = ResolveFSPath(storeDir, GetCurrentProcessFSDir());
	g_backupStorePath = storePath.wstring();

	// --------- Overwrite ----------
	g_overwrite = settings.get_as<bool>(TS("Overwrite"), true);

	// ---------- OpenCopy ----------
	g_openCopy = settings.get_as<bool>(TS("OpenCopy"), false);

	// ---------- AddTimestamp ----------
	g_addTimestamp = settings.get_as<bool>(TS("AddTimestamp"), true);

	// ---------- Extensions ----------
	wstring extStr = settings.get(TS("Extensions"));
	if (!extStr.empty()) {
		g_extensions.clear();
		size_t start = 0;
		while (true) {
			size_t comma = extStr.find(L',', start);
			wstring token = extStr.substr(start, comma - start);
			token.erase(0, token.find_first_not_of(L" \t"));
			token.erase(token.find_last_not_of(L" \t") + 1);
			if (!token.empty()) {
				if (token[0] != L'.') token = L"." + token;
				g_extensions.push_back(token);
			}
			if (comma == wstring::npos) break;
			start = comma + 1;
		}
	}
	if (g_extensions.empty()) g_extensions = DEFAULT_EXTENSIONS;

	logger.DLog(LogLevel::Info, L"配置加载成功，跳过根目录列表: " + JoinPaths(g_skipBackupRoots) +
		L"，备份存储目录: " + g_backupStorePath +
		L"，打开副本: " + (g_openCopy ? L"true" : L"false") +
		L"，添加时间戳: " + (g_addTimestamp ? L"true" : L"false"));
}