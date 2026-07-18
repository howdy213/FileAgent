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
 * 实现所有注册表辅助函数
 */

#include "FileAgent.h"
#include "WinUtils/WinReg.h"
#include "WinUtils/StrConvert.h"
using namespace std;
using namespace WinUtils;

wstring ReadRegString(HKEY hKeyParent, const wstring& subKey, const wstring& valueName) {
    try {
        RegKey key;
        key.Open(hKeyParent, subKey, KEY_READ);
        return key.GetStringValue(valueName);
    }
    catch (const RegException& e) {
        logger.DLog(LogLevel::Warn, format(L"读取注册表失败: {}", ConvertString(e.what())));
        return L"";
    }
}

bool WriteRegString(HKEY hKeyParent, const wstring& subKey, const wstring& valueName, const wstring& data) {
    try {
        RegKey key;
        key.Create(hKeyParent, subKey, KEY_WRITE);
        key.SetStringValue(valueName, data);
        return true;
    }
    catch (const RegException& e) {
        logger.DLog(LogLevel::Error, format(L"写入注册表失败: {}", ConvertString(e.what())));
        return false;
    }
}

vector<wstring> EnumRegValueNames(HKEY hKeyParent, const wstring& subKey) {
    vector<wstring> names;
    try {
        RegKey key;
        key.Open(hKeyParent, subKey, KEY_READ);
        for (auto& [name, type] : key.EnumValues()) {
            if (!name.empty()) names.push_back(name);
        }
    }
    catch (const RegException& e) {
        logger.DLog(LogLevel::Warn, format(L"枚举注册表值失败: {}", ConvertString(e.what())));
    }
    return names;
}

bool CopyRegistryKey(HKEY hSrcParent, const wstring& srcSubKey,
    HKEY hDstParent, const wstring& dstSubKey) {
    HKEY hSrc, hDst;
    if (RegOpenKeyExW(hSrcParent, srcSubKey.c_str(), 0, KEY_READ, &hSrc) != ERROR_SUCCESS)
        return false;
    if (RegCreateKeyExW(hDstParent, dstSubKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hDst, nullptr) != ERROR_SUCCESS) {
        RegCloseKey(hSrc);
        return false;
    }

    DWORD index = 0;
    WCHAR valueName[256];
    DWORD nameSize, type, dataSize;
    BYTE data[4096];
    while (true) {
        nameSize = 256;
        dataSize = sizeof(data);
        LONG ret = RegEnumValueW(hSrc, index++, valueName, &nameSize, nullptr,
            &type, data, &dataSize);
        if (ret == ERROR_NO_MORE_ITEMS) break;
        if (ret == ERROR_SUCCESS)
            RegSetValueExW(hDst, valueName, 0, type, data, dataSize);
    }
    RegCloseKey(hSrc);
    RegCloseKey(hDst);
    return true;
}

wstring GetProgID(const wstring& ext) {
    // 1. 优先从 UserChoice 读取（用户最近选择的关联）
    wstring choiceKey = USER_CHOICE_BASE + L"\\" + ext + L"\\" + USER_CHOICE_SUB;
    wstring progId = ReadRegString(HKEY_CURRENT_USER, choiceKey, USER_CHOICE_PROGID);
    if (!progId.empty()) return progId;

    // 2. 其次从 HKCU\Classes\<ext> 读取
    progId = ReadRegString(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext, L"");
    if (!progId.empty()) return progId;

    // 3. 再从 HKCR\<ext> 读取
    progId = ReadRegString(HKEY_CLASSES_ROOT, ext, L"");
    if (!progId.empty()) return progId;

    // 4. 最后从 OpenWithProgids 中枚举
    wstring openWithKey = L"Software\\Classes\\" + ext + L"\\OpenWithProgids";
    auto ids = EnumRegValueNames(HKEY_CURRENT_USER, openWithKey);
    if (!ids.empty()) return ids[0];
    ids = EnumRegValueNames(HKEY_CLASSES_ROOT, openWithKey);
    if (!ids.empty()) return ids[0];

    return L"";
}