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
 * 声明 FAConfig 的配置读写、命令行处理与界面入口
 */
#pragma once

#include "framework.h"
#include "resource.h"

#include <filesystem>
#include <string>
#include <vector>

// ==================== 常量 ====================
inline const std::wstring FA_CONFIG_FILE_NAME = L"FAConfig.ini";
inline const std::wstring FILE_AGENT_INI_NAME = L"FileAgent.ini";
inline const std::wstring FA_CONFIG_SETTINGS = L"Settings";
inline const std::wstring FA_CONFIG_ALIASES = L"Aliases";

// 别名条目：英文名 -> 路径
struct AliasEntry {
	std::wstring name;
	std::wstring path;
};

// ==================== 配置文件 ====================
std::filesystem::path GetConfigDirectory();
std::filesystem::path GetFAConfigPath();
std::filesystem::path GetFileAgentConfigPath();

// 确保 FAConfig.ini 与 FileAgent.ini 存在（不存在时写入默认内容）
void EnsureConfigFiles();

// ==================== 别名 ====================
std::vector<AliasEntry> LoadAliases();
bool SaveAliases(const std::vector<AliasEntry>& aliases);
// 按别名（不区分大小写）解析路径；未找到时返回 false
bool ResolveAliasValue(const std::wstring& name, std::wstring& value);

// ==================== 启用状态 ====================
bool IsCommandEnabled();
bool SetCommandEnabled(bool enabled);

// ==================== FileAgent.ini 修改 ====================
bool WriteFileAgentSetting(const std::wstring& key, const std::wstring& value);

// ==================== 入口 ====================
// 处理命令行；返回进程退出码
int RunCommandLine(const std::wstring& cmdLine);
// 显示原生配置界面；返回进程退出码
int RunConfigUI(HINSTANCE instance, int showCmd);
