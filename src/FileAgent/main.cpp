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
 * 程序入口，解析命令行并执行相应操作
 */

#include "FileAgent.h"
#include "WinUtils/CmdParser.h"
#include "WinUtils/Logger.h"

#include <cwctype>
using namespace std;

using namespace WinUtils;

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int) {
    // 设置日志（日志文件位于程序目录）
    LoggerCore::Inst().SetDefaultStrategies((GetCurrentProcessFSDir() / (APP_NAME + L".log")).wstring());
    LoggerCore::Inst().EnableAllApartments();
    logger.DLog(LogLevel::Info, L"程序启动");

    // 加载配置
    LoadConfig();

    // 解析命令行
    CmdParser parser(false); // 不区分大小写
    wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
    if (!parser.parse(cmdLine, CmdParser::ParseMode::Normal)) {
        logger.Log(LogLevel::Error, L"命令行解析失败");
        return 1;
    }

    if (parser.hasCommand(TS("status")) || parser.hasCommand(TS("isinstalled"))) {
        // 静默查询安装状态，仅通过退出码返回：0 = 已安装，1 = 未安装
        return IsInstalled() ? 0 : 1;
    }

    if (parser.hasCommand(TS("install"))) {
        return Install() ? 0 : 1;
    }
    else if (parser.hasCommand(TS("uninstall"))) {
        return Uninstall() ? 0 : 1;
    }
    else {
        // 无命令时，取第一个参数作为文件路径
        auto params = parser.getParams(TS(""));
        if (!params.empty()) {
            // 兼容不带前缀的裸命令（install / uninstall / status）
            wstring bare = params[0];
            for (auto& c : bare) c = static_cast<wchar_t>(towlower(c));
            if (bare == L"status" || bare == L"isinstalled") {
                return IsInstalled() ? 0 : 1;
            }
            if (bare == L"install") {
                return Install() ? 0 : 1;
            }
            if (bare == L"uninstall") {
                return Uninstall() ? 0 : 1;
            }

            wstring path = params[0];
            // 去除可能的首尾引号
            if (!path.empty() && path.front() == L'\"' && path.back() == L'\"') {
                path = path.substr(1, path.size() - 2);
            }
            if (!path.empty()) {
                AgentOpen(path);
            }
        }
        else {
            logger.Log(LogLevel::Info, L"无有效参数，程序退出");
        }
    }

    logger.DLog(LogLevel::Info, L"程序退出");
    return 0;
}