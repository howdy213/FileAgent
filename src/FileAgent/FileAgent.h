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
 * 声明所有公共函数、常量和全局变量
 */
#pragma once
#include "WinUtils/WinPch.h"

#include <string>
#include <vector>
#include <filesystem>
#include <Windows.h>
#include "WinUtils/WinUtils.h"
#include "WinUtils/Logger.h"

// ==================== 常量 ====================
inline const std::wstring APP_NAME = L"FileAgent";
inline const std::wstring DEFAULT_BACKUP_DIR_NAME = L"AutoSaveBackup";
inline const std::vector<std::wstring> DEFAULT_EXTENSIONS = {
	L".doc", L".docx", L".ppt", L".pptx",L".xls", L".xlsx",  L".pdf"
};
inline const std::wstring REG_KEY_PATH = L"Software\\" + APP_NAME;
inline const std::wstring REG_INSTALLED_VALUE = L"Installed";

inline const std::wstring USER_CHOICE_BASE = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts";
inline const std::wstring USER_CHOICE_SUB = L"UserChoice";
inline const std::wstring USER_CHOICE_PROGID = L"Progid";

// ==================== 全局变量（extern 声明） ====================
extern WinUtils::Logger logger;
extern bool g_overwrite;
extern std::vector<std::wstring> g_skipBackupRoots;   // 若文件在此目录下，则跳过备份（可为空）
extern std::wstring g_backupStorePath;  // 备份存储目录（绝对路径）
extern std::vector<std::wstring> g_extensions; // 要处理的扩展名列表
extern bool g_openCopy;                    // 是否打开副本
extern bool g_addTimestamp;                // 是否在备份文件名中添加时间戳
extern std::wstring g_enaPath;             // 备份成功后用于弹出通知的 exe 路径（可留空）

// ==================== 函数声明 ====================

// 配置加载
void LoadConfig();

// 路径工具
std::wstring MakeBackupPath(const std::wstring& sourceFile);
void EnsureBackupDir();

// 注册表辅助
std::wstring ReadRegString(HKEY hKeyParent, const std::wstring& subKey, const std::wstring& valueName);
bool WriteRegString(HKEY hKeyParent, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& data);
std::vector<std::wstring> EnumRegValueNames(HKEY hKeyParent, const std::wstring& subKey);
bool CopyRegistryKey(HKEY hSrcParent, const std::wstring& srcSubKey, HKEY hDstParent, const std::wstring& dstSubKey);
std::wstring GetProgID(const std::wstring& ext);

// 核心功能
bool IsInstalled();
bool Install();
bool Uninstall();
void AgentOpen(const std::wstring& filePath);