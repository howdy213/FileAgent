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
 * 实现安装、卸载、代理打开及路径辅助函数
 */
#include "FileAgent.h"
#include "WinUtils/WinUtils.h"
#include <ShlObj.h>
#include <chrono>
#include <ctime>
#include <format>
#include <WinUtils/WinReg.h>
using namespace std;
using namespace WinUtils;
namespace fs = filesystem;

// ==================== 路径辅助 ====================
wstring MakeBackupPath(const wstring& sourceFile) {
    fs::path src(sourceFile);
    wstring backupName;
    if (g_addTimestamp) {
        auto now = chrono::system_clock::now();
        auto time = chrono::system_clock::to_time_t(now);
        tm tm;
        localtime_s(&tm, &time);
        wstring timestamp = format(L"{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
        backupName = format(L"{}_{}{}", src.stem().wstring(), timestamp, src.extension().wstring());
    }
    else {
        backupName = src.filename().wstring();
    }
    return (fs::path(g_backupStorePath) / backupName).wstring();
}

void EnsureBackupDir() {
    fs::path dir = g_backupStorePath;
    if (!fs::exists(dir)) fs::create_directories(dir);
}

// ==================== 安装状态 ====================
bool IsInstalled() {
    return ReadRegString(HKEY_CURRENT_USER, REG_KEY_PATH, REG_INSTALLED_VALUE) == L"1";
}

bool SetInstalledFlag() {
    return WriteRegString(HKEY_CURRENT_USER, REG_KEY_PATH, REG_INSTALLED_VALUE, L"1");
}

void RemoveInstalledFlag() {
    try {
        RegKey key;
        key.Open(HKEY_CURRENT_USER, REG_KEY_PATH, KEY_WRITE);
        key.DeleteValue(REG_INSTALLED_VALUE);
    }
    catch (...) { /* ignore */ }
}

// ==================== 安装 ====================
bool Install() {
    if (IsInstalled()) {
        MessageBoxW(nullptr, L"FileAgent 已经安装，无需重复操作。若需更新，请先执行 /uninstall。",
            L"提示", MB_OK | MB_ICONINFORMATION);
        return true;
    }

    wstring myPath = GetCurrentProcessPath();
    wstring myCommand = L"\"" + myPath + L"\" \"%1\"";

    for (const auto& ext : g_extensions) {
        wstring progId = GetProgID(ext);   // 优先从 UserChoice 读取

        if (!progId.empty()) {
            // ---- 有 ProgID：修改该 ProgID 的 command ----
            wstring userProgKey = L"Software\\Classes\\" + progId;
            wstring userCommandKey = userProgKey + L"\\shell\\open\\command";
            wstring userIconKey = userProgKey + L"\\DefaultIcon";

            // 确保 HKCU 中存在命令和图标键（若没有则从 HKCR 复制）
            HKEY hTest;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, userCommandKey.c_str(), 0, KEY_READ, &hTest) != ERROR_SUCCESS) {
                CopyRegistryKey(HKEY_CLASSES_ROOT, progId + L"\\shell\\open\\command",
                    HKEY_CURRENT_USER, userCommandKey);
            }
            else {
                RegCloseKey(hTest);
            }
            if (RegOpenKeyExW(HKEY_CURRENT_USER, userIconKey.c_str(), 0, KEY_READ, &hTest) != ERROR_SUCCESS) {
                CopyRegistryKey(HKEY_CLASSES_ROOT, progId + L"\\DefaultIcon",
                    HKEY_CURRENT_USER, userIconKey);
            }
            else {
                RegCloseKey(hTest);
            }

            // 备份原始命令
            if (ReadRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand").empty()) {
                wstring current = ReadRegString(HKEY_CURRENT_USER, userCommandKey, L"");
                WriteRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand", current);
            }

            // 写入新命令
            if (!WriteRegString(HKEY_CURRENT_USER, userCommandKey, L"", myCommand)) {
                logger.Log(LogLevel::Error, format(L"安装失败：无法写入命令键 {}", userCommandKey));
                return false;
            }
            logger.Log(LogLevel::Info, format(L"已为扩展 {} 通过 ProgID {} 设置命令", ext, progId));

            // 存储实际使用的 ProgID，用于卸载时恢复
            wstring storedProgidKey = L"InstalledProgid_" + ext.substr(1);
            WriteRegString(HKEY_CURRENT_USER, REG_KEY_PATH, storedProgidKey, progId);
        }
        else {
            // ---- 无 ProgID：仅修改 HKCU\Classes\<ext>\shell\open\command ----
            wstring userCommandKey = L"Software\\Classes\\" + ext + L"\\shell\\open\\command";

            wstring original = ReadRegString(HKEY_CURRENT_USER, userCommandKey, L"");
            if (original.empty()) {
                original = ReadRegString(HKEY_CLASSES_ROOT, userCommandKey, L"");
            }
            if (!original.empty()) {
                WriteRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand", original);
            }
            else {
                WriteRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand", L"");
            }

            if (!WriteRegString(HKEY_CURRENT_USER, userCommandKey, L"", myCommand)) {
                logger.Log(LogLevel::Error, format(L"安装失败：无法为扩展 {} 写入命令", ext));
                return false;
            }
            logger.Log(LogLevel::Info, format(L"已为扩展 {} 直接设置命令（无 ProgID）", ext));
        }
    }

    SetInstalledFlag();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    logger.Log(LogLevel::Info, L"安装成功");

    MessageBoxW(nullptr, (L"FileAgent 安装成功。\n双击文档时会自动备份到 " + g_backupStorePath + L"。").c_str(),
        L"安装", MB_OK | MB_ICONINFORMATION);
    return true;
}

// ==================== 卸载 ====================
bool Uninstall() {
    if (!IsInstalled()) {
        MessageBoxW(nullptr, L"FileAgent 尚未安装。", L"提示", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    for (const auto& ext : g_extensions) {
        // 优先读取安装时存储的 ProgID
        wstring storedProgidKey = L"InstalledProgid_" + ext.substr(1);
        wstring progId = ReadRegString(HKEY_CURRENT_USER, REG_KEY_PATH, storedProgidKey);
        if (progId.empty()) {
            // 兼容旧版本（未存储的情况）或用户手动删除存储值
            progId = GetProgID(ext);
        }

        if (!progId.empty()) {
            wstring userCommandKey = L"Software\\Classes\\" + progId + L"\\shell\\open\\command";
            wstring original = ReadRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand");
            if (!original.empty()) {
                WriteRegString(HKEY_CURRENT_USER, userCommandKey, L"", original);
                try {
                    RegKey key;
                    key.Open(HKEY_CURRENT_USER, userCommandKey, KEY_SET_VALUE);
                    key.DeleteValue(L"OriginalCommand");
                }
                catch (...) {}
            }
            logger.Log(LogLevel::Info, format(L"已恢复 ProgID {} 的原始命令", progId));

            // 删除存储的 ProgID 值
            try {
                RegKey key;
                key.Open(HKEY_CURRENT_USER, REG_KEY_PATH, KEY_WRITE);
                key.DeleteValue(storedProgidKey);
            }
            catch (...) {}
        }
        else {
            // 无 ProgID：恢复扩展本身的命令
            wstring userCommandKey = L"Software\\Classes\\" + ext + L"\\shell\\open\\command";
            wstring original = ReadRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand");
            if (!original.empty()) {
                if (original == L"") {
                    // 原始为空，则删除 open 键
                    try {
                        RegKey key;
                        key.Open(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext + L"\\shell", KEY_WRITE);
                        key.DeleteKey(L"open", 0);
                    }
                    catch (...) {}
                }
                else {
                    WriteRegString(HKEY_CURRENT_USER, userCommandKey, L"", original);
                    try {
                        RegKey key;
                        key.Open(HKEY_CURRENT_USER, userCommandKey, KEY_SET_VALUE);
                        key.DeleteValue(L"OriginalCommand");
                    }
                    catch (...) {}
                }
                logger.Log(LogLevel::Info, format(L"已恢复扩展 {} 的原始命令", ext));
            }
        }
    }

    RemoveInstalledFlag();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    logger.Log(LogLevel::Info, L"卸载成功");
    MessageBoxW(nullptr, L"FileAgent 已卸载，文件关联已恢复。", L"卸载", MB_OK | MB_ICONINFORMATION);
    return true;
}

static bool IsPathUnderAnyRoot(const fs::path& path, const vector<wstring>& roots) {
    for (const auto& root : roots) {
        if (root.empty()) continue;
        if (path.wstring().find(root) == 0) {
            return true;
        }
    }
    return false;
}

// ==================== 代理打开 ====================
void AgentOpen(const wstring& filePath) {
    if (!fs::exists(filePath)) {
        MessageBoxW(nullptr, L"文件不存在或无法访问。", L"错误", MB_ICONERROR);
        return;
    }

    // ---- 合并跳过备份判断 ----
    bool shouldSkipBackup = false;
    if (!g_skipBackupRoots.empty() && IsPathUnderAnyRoot(filePath, g_skipBackupRoots)) {
        shouldSkipBackup = true;
    }
    if (!shouldSkipBackup && !g_backupStorePath.empty() && IsPathUnderAnyRoot(filePath, { g_backupStorePath })) {
        shouldSkipBackup = true;
        logger.DLog(LogLevel::Info, L"文件位于备份存储目录，跳过备份: " + filePath);
    }

    wstring targetFile = filePath; // 默认打开原件

    if (shouldSkipBackup) {
        logger.DLog(LogLevel::Info, L"跳过备份，直接打开原件: " + filePath);
    }
    else {
        // 执行备份
        EnsureBackupDir();
        wstring backupPath = MakeBackupPath(filePath);
        bool backupOk = CopyFileW(filePath.c_str(), backupPath.c_str(), FALSE); // FALSE = 覆盖
        if (!backupOk) {
            logger.DLog(LogLevel::Warn, L"备份文件失败，将继续尝试打开原件");
            backupPath = filePath; // 备份失败，回退
        }
        else {
            logger.DLog(LogLevel::Info, L"备份成功: " + backupPath);
            if (g_openCopy) {
                targetFile = backupPath;
                logger.DLog(LogLevel::Info, L"配置为打开副本: " + targetFile);
            }
        }
    }

    fs::path p(filePath);
    wstring ext = p.extension().wstring();
    for (auto& c : ext) c = towlower(c);

    wstring progId = GetProgID(ext);
    wstring originalCommand;
    if (!progId.empty()) {
        wstring userCommandKey = L"Software\\Classes\\" + progId + L"\\shell\\open\\command";
        originalCommand = ReadRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand");
        if (originalCommand.empty()) {
            originalCommand = ReadRegString(HKEY_CLASSES_ROOT, progId + L"\\shell\\open\\command", L"");
        }
    }
    else {
        wstring userCommandKey = L"Software\\Classes\\" + ext + L"\\shell\\open\\command";
        originalCommand = ReadRegString(HKEY_CURRENT_USER, userCommandKey, L"OriginalCommand");
        if (originalCommand.empty()) {
            originalCommand = ReadRegString(HKEY_CLASSES_ROOT, userCommandKey, L"");
        }
    }

    if (originalCommand.empty()) {
        logger.DLog(LogLevel::Error, L"无法找到原始打开程序");
        return;
    }

    // 替换 %1
    wstring command = originalCommand;
    wstring targetWithQuotes = L"\"" + targetFile + L"\"";

    size_t pos = command.find(L"\"%1\"");
    if (pos != wstring::npos) {
        command.replace(pos, 4, targetWithQuotes);
    }
    else {
        pos = command.find(L"%1");
        if (pos != wstring::npos) {
            command.replace(pos, 2, targetWithQuotes);
        }
        else {
            command += L" " + targetWithQuotes;
        }
    }

    // 启动程序
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;

    vector<WCHAR> cmdLine(command.begin(), command.end());
    cmdLine.push_back(0);

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
        CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr, &si, &pi)) {
        DWORD err = GetLastError();
        wstring errMsg = format(L"启动原始程序失败，错误码: {}", err);
        MessageBoxW(nullptr, errMsg.c_str(), L"错误", MB_ICONERROR);
        logger.DLog(LogLevel::Error, errMsg);
        return;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}