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
 * 程序入口：无参数时显示别名配置界面，有参数时按命令行修改 FileAgent.ini
 */
#include "FAConfig.h"
#include "WinUtils/CmdParser.h"
#include "WinUtils/WinUtils.h"

#include <commctrl.h>
#include <shobjidl.h>
#include <cwctype>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace std;

#define MAX_LOADSTRING 100

// 全局变量
static HINSTANCE g_hInst;
static WCHAR szTitle[MAX_LOADSTRING];
static WCHAR szWindowClass[MAX_LOADSTRING];

static HWND g_hList = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hEmptyHint = nullptr;
static HWND g_hChkEnabled = nullptr;
static vector<AliasEntry> g_aliases;
static bool g_pendingNewRow = false;
static HFONT g_hFont = nullptr;
static HFONT g_hFontBold = nullptr;

// 前向声明
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
static void BeginCellEdit(int item, int subItem);
static void FinishCellEdit(bool save);

static wstring ToLowerCopy(const wstring& text) {
	wstring result = text;
	for (auto& c : result) c = towlower(c);
	return result;
}

static wstring TrimCopy(const wstring& text) {
	const wstring whitespace = L" \t\r\n";
	size_t begin = text.find_first_not_of(whitespace);
	if (begin == wstring::npos) return L"";
	size_t end = text.find_last_not_of(whitespace);
	return text.substr(begin, end - begin + 1);
}

static wstring GetEditText(HWND hEdit) {
	int length = GetWindowTextLengthW(hEdit);
	if (length <= 0) return L"";
	wstring buffer(static_cast<size_t>(length) + 1, L'\0');
	GetWindowTextW(hEdit, buffer.data(), length + 1);
	buffer.resize(static_cast<size_t>(length));
	return buffer;
}

static int GetSelectedIndex() {
	return ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
}

static bool g_displayedRegistered = false;
static wstring g_registrationSummary = L"未知";
static wstring g_fileAgentHint;

static void UpdateStatus();

// 查询注册状态并刷新按钮/状态栏。
// 查询需要启动 FileAgent.exe，开销较大，因此只在启动、手动刷新、
// 注册/卸载之后调用，而不是每次刷新列表都调用。
static void RefreshRegistration() {
	const bool available = std::filesystem::exists(GetConfigDirectory() / FILE_AGENT_EXE_NAME);
	g_displayedRegistered = available && IsRegistered();
	g_registrationSummary = available
		? (g_displayedRegistered ? L"FileAgent 已安装" : L"FileAgent 未安装")
		: L"未找到 FileAgent.exe";

	HWND button = g_hList ? GetDlgItem(GetParent(g_hList), IDC_BTN_REGISTER) : nullptr;
	if (button) {
		SetWindowTextW(button, g_displayedRegistered ? L"卸载注册" : L"注册");
		EnableWindow(button, available);
	}
	UpdateStatus();
}

static void UpdateStatus() {
	if (!g_hStatus) return;
	const wstring countText = L"别名数量: " + to_wstring(g_aliases.size());
	const wstring stateText = IsCommandEnabled()
		? L"命令行: 已启用"
		: L"命令行: 已禁用，需 enable";
	const wstring regText = L"注册: " + g_registrationSummary;
	SendMessageW(g_hStatus, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(countText.c_str()));
	SendMessageW(g_hStatus, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(stateText.c_str()));
	SendMessageW(g_hStatus, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(regText.c_str()));
	SendMessageW(g_hStatus, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(g_fileAgentHint.c_str()));
}

static void RefreshList() {
	if (!g_hList) return;
	ListView_DeleteAllItems(g_hList);
	for (size_t i = 0; i < g_aliases.size(); ++i) {
		LVITEMW item = {};
		item.mask = LVIF_TEXT;
		item.iItem = static_cast<int>(i);
		item.pszText = const_cast<LPWSTR>(g_aliases[i].name.c_str());
		ListView_InsertItem(g_hList, &item);
		ListView_SetItemText(g_hList, static_cast<int>(i), 1,
			const_cast<LPWSTR>(g_aliases[i].path.c_str()));
	}
	if (g_hEmptyHint) {
		ShowWindow(g_hEmptyHint, g_aliases.empty() ? SW_SHOW : SW_HIDE);
	}
	if (g_hChkEnabled) {
		SendMessageW(g_hChkEnabled, BM_SETCHECK, IsCommandEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
	}
	UpdateStatus();
}

static bool IsDuplicateName(const wstring& name, int ignoreIndex) {
	const wstring target = ToLowerCopy(name);
	for (size_t i = 0; i < g_aliases.size(); ++i) {
		if (static_cast<int>(i) == ignoreIndex) continue;
		if (ToLowerCopy(g_aliases[i].name) == target) return true;
	}
	return false;
}

static void OnAdd() {
	if (g_pendingNewRow && !g_aliases.empty()) {
		int index = static_cast<int>(g_aliases.size()) - 1;
		ListView_SetItemState(g_hList, index, LVIS_SELECTED | LVIS_FOCUSED,
			LVIS_SELECTED | LVIS_FOCUSED);
		ListView_EnsureVisible(g_hList, index, FALSE);
		BeginCellEdit(index, 0);
		return;
	}

	g_aliases.push_back({ L"", L"" });
	g_pendingNewRow = true;
	RefreshList();

	int index = static_cast<int>(g_aliases.size()) - 1;
	ListView_SetItemState(g_hList, index, LVIS_SELECTED | LVIS_FOCUSED,
		LVIS_SELECTED | LVIS_FOCUSED);
	ListView_EnsureVisible(g_hList, index, FALSE);
	BeginCellEdit(index, 0);
}

static void OnDelete() {
	int index = GetSelectedIndex();
	if (index < 0) {
		MessageBoxW(g_hList ? GetParent(g_hList) : nullptr, L"请先在表格中选择要删除的别名。",
			L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	if (g_pendingNewRow && index == static_cast<int>(g_aliases.size()) - 1) {
		g_aliases.pop_back();
		g_pendingNewRow = false;
		RefreshList();
		return;
	}

	AliasEntry removed = g_aliases[static_cast<size_t>(index)];
	g_aliases.erase(g_aliases.begin() + index);
	if (!SaveAliases(g_aliases)) {
		MessageBoxW(g_hList ? GetParent(g_hList) : nullptr, L"保存 FAConfig.ini 失败。",
			L"错误", MB_OK | MB_ICONERROR);
		g_aliases.insert(g_aliases.begin() + index, removed);
		return;
	}

	RefreshList();
}

static bool PickFolder(HWND owner, const wchar_t* title, wstring& selected) {
	IFileOpenDialog* dialog = nullptr;
	const HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
	if (FAILED(hr) || !dialog) {
		MessageBoxW(owner, L"无法打开路径选择对话框。", L"错误", MB_OK | MB_ICONERROR);
		return false;
	}

	DWORD options = 0;
	if (SUCCEEDED(dialog->GetOptions(&options))) {
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
	}
	dialog->SetTitle(title);

	selected.clear();
	if (SUCCEEDED(dialog->Show(owner))) {
		IShellItem* item = nullptr;
		if (SUCCEEDED(dialog->GetResult(&item)) && item) {
			LPWSTR path = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
				selected = path;
				CoTaskMemFree(path);
			}
			item->Release();
		}
	}
	dialog->Release();
	return !selected.empty();
}

// 选择 exe 文件（用于 ENA 路径等）
static bool PickExecutable(HWND owner, const wchar_t* title, wstring& selected) {
	IFileOpenDialog* dialog = nullptr;
	const HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
	if (FAILED(hr) || !dialog) {
		MessageBoxW(owner, L"无法打开文件选择对话框。", L"错误", MB_OK | MB_ICONERROR);
		return false;
	}

	DWORD options = 0;
	if (SUCCEEDED(dialog->GetOptions(&options))) {
		dialog->SetOptions((options & ~FOS_PICKFOLDERS) |
			FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
	}
	const COMDLG_FILTERSPEC filter[] = { { L"可执行文件", L"*.exe" } };
	dialog->SetFileTypes(1, filter);
	dialog->SetTitle(title);

	selected.clear();
	if (SUCCEEDED(dialog->Show(owner))) {
		IShellItem* item = nullptr;
		if (SUCCEEDED(dialog->GetResult(&item)) && item) {
			LPWSTR path = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
				selected = path;
				CoTaskMemFree(path);
			}
			item->Release();
		}
	}
	dialog->Release();
	return !selected.empty();
}

static void OnBrowsePath() {	FinishCellEdit(true);

	HWND owner = g_hList ? GetParent(g_hList) : nullptr;
	int index = GetSelectedIndex();
	if (index < 0 || index >= static_cast<int>(g_aliases.size())) {
		MessageBoxW(owner, L"请先在表格中选择要设置路径的别名。", L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (g_pendingNewRow && index == static_cast<int>(g_aliases.size()) - 1
		&& g_aliases[static_cast<size_t>(index)].name.empty()) {
		MessageBoxW(owner, L"请先填写英文名，再浏览选择路径。", L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	wstring selected;
	if (!PickFolder(owner, L"选择别名对应的路径", selected)) return;

	AliasEntry old = g_aliases[static_cast<size_t>(index)];
	g_aliases[static_cast<size_t>(index)].path = selected;
	if (!SaveAliases(g_aliases)) {
		g_aliases[static_cast<size_t>(index)] = old;
		MessageBoxW(owner, L"保存 FAConfig.ini 失败。", L"错误", MB_OK | MB_ICONERROR);
		return;
	}
	if (g_pendingNewRow && index == static_cast<int>(g_aliases.size()) - 1) {
		g_pendingNewRow = false;
	}
	RefreshList();
	ListView_SetItemState(g_hList, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_EnsureVisible(g_hList, index, FALSE);
}

// ==================== FileAgent.ini 设置 ====================
static HWND g_hFaTitle = nullptr;
static HWND g_hFaLabelDir = nullptr;
static HWND g_hFaLabelRoot = nullptr;
static HWND g_hFaLabelExt = nullptr;
static HWND g_hFaLabelEna = nullptr;

static void SetCheckState(HWND check, bool checked) {
	if (check) SendMessageW(check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

static bool GetCheckState(HWND check) {
	return check && SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void SetFileAgentControls(HWND hWnd) {
	const FileAgentSettings settings = LoadFileAgentSettings();
	if (HWND edit = GetDlgItem(hWnd, IDC_FA_BACKUPDIR)) {
		SetWindowTextW(edit, settings.backupDir.c_str());
	}
	if (HWND edit = GetDlgItem(hWnd, IDC_FA_BACKUPROOT)) {
		SetWindowTextW(edit, settings.backupRoot.c_str());
	}
	if (HWND edit = GetDlgItem(hWnd, IDC_FA_EXTENSIONS)) {
		SetWindowTextW(edit, settings.extensions.c_str());
	}
	if (HWND edit = GetDlgItem(hWnd, IDC_FA_ENAPATH)) {
		SetWindowTextW(edit, settings.enaPath.c_str());
	}
	SetCheckState(GetDlgItem(hWnd, IDC_CHK_OVERWRITE), settings.overwrite);
	SetCheckState(GetDlgItem(hWnd, IDC_CHK_OPENCOPY), settings.openCopy);
	SetCheckState(GetDlgItem(hWnd, IDC_CHK_ADDTIMESTAMP), settings.addTimestamp);
}

static void OnSaveFileAgentSettings(HWND hWnd) {
	FileAgentSettings settings;
	settings.backupDir = GetEditText(GetDlgItem(hWnd, IDC_FA_BACKUPDIR));
	settings.backupRoot = GetEditText(GetDlgItem(hWnd, IDC_FA_BACKUPROOT));
	settings.extensions = GetEditText(GetDlgItem(hWnd, IDC_FA_EXTENSIONS));
	settings.enaPath = GetEditText(GetDlgItem(hWnd, IDC_FA_ENAPATH));
	settings.overwrite = GetCheckState(GetDlgItem(hWnd, IDC_CHK_OVERWRITE));
	settings.openCopy = GetCheckState(GetDlgItem(hWnd, IDC_CHK_OPENCOPY));
	settings.addTimestamp = GetCheckState(GetDlgItem(hWnd, IDC_CHK_ADDTIMESTAMP));

	g_fileAgentHint = SaveFileAgentSettings(settings)
		? L"FileAgent.ini: 已保存"
		: L"FileAgent.ini: 保存失败";
	UpdateStatus();
	if (g_fileAgentHint.find(L"失败") != wstring::npos) {
		MessageBoxW(hWnd, L"保存 FileAgent.ini 失败。", L"错误", MB_OK | MB_ICONERROR);
	}
}

static void OnReloadFileAgentSettings(HWND hWnd) {
	SetFileAgentControls(hWnd);
	g_fileAgentHint = L"FileAgent.ini: 已重新加载";
	UpdateStatus();
}

static void OnBrowseBackupDir(HWND hWnd) {
	wstring selected;
	if (!PickFolder(hWnd, L"选择备份存储目录", selected)) return;
	if (HWND edit = GetDlgItem(hWnd, IDC_FA_BACKUPDIR)) {
		SetWindowTextW(edit, selected.c_str());
	}
}

static void OnBrowseEnaPath(HWND hWnd) {
	wstring selected;
	if (!PickExecutable(hWnd, L"选择备份成功后弹出通知的程序", selected)) return;
	if (HWND edit = GetDlgItem(hWnd, IDC_FA_ENAPATH)) {
		SetWindowTextW(edit, selected.c_str());
	}
}

// ==================== 单元格内联编辑 ====================
#define IDC_CELL_EDIT 2000

static HWND g_hCellEdit = nullptr;
static WNDPROC g_cellEditOldProc = nullptr;
static int g_cellEditItem = -1;
static int g_cellEditSubItem = -1;
static bool g_cellEditClosing = false;

static void FinishCellEdit(bool save);

static LRESULT CALLBACK CellEditProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_KEYDOWN:
		if (wParam == VK_RETURN) {
			FinishCellEdit(true);
			return 0;
		}
		if (wParam == VK_ESCAPE) {
			FinishCellEdit(false);
			return 0;
		}
		break;
	case WM_KILLFOCUS:
		FinishCellEdit(true);
		return 0;
	}
	if (!g_cellEditOldProc) return DefWindowProcW(hWnd, message, wParam, lParam);
	return CallWindowProcW(g_cellEditOldProc, hWnd, message, wParam, lParam);
}

static void CloseCellEdit() {
	if (!g_hCellEdit) return;
	HWND edit = g_hCellEdit;
	g_hCellEdit = nullptr;
	g_cellEditItem = -1;
	g_cellEditSubItem = -1;
	if (g_cellEditOldProc) {
		SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_cellEditOldProc));
		g_cellEditOldProc = nullptr;
	}
	DestroyWindow(edit);
}

static void FinishCellEdit(bool save) {
	if (!g_hCellEdit || g_cellEditClosing) return;
	g_cellEditClosing = true;

	HWND edit = g_hCellEdit;
	const int item = g_cellEditItem;
	const int subItem = g_cellEditSubItem;
	const wstring text = TrimCopy(GetEditText(edit));
	CloseCellEdit();

	HWND owner = g_hList ? GetParent(g_hList) : nullptr;
	bool continuePath = false;

	if (item >= 0 && item < static_cast<int>(g_aliases.size())) {
		const bool pending = g_pendingNewRow && item == static_cast<int>(g_aliases.size()) - 1;
		AliasEntry old = g_aliases[static_cast<size_t>(item)];
		AliasEntry& entry = g_aliases[static_cast<size_t>(item)];

		if (!save) {
			// 取消编辑：新增中的空行直接丢弃
			if (pending) {
				g_aliases.pop_back();
				g_pendingNewRow = false;
				RefreshList();
			}
		} else {
			bool valid = true;
			if (subItem == 0) {
				if (text.empty()) {
					// 英文名为空：直接取消本次编辑，不弹提示
					valid = false;
				} else if (IsDuplicateName(text, item)) {
					MessageBoxW(owner, L"其他行已使用该英文名。", L"提示", MB_OK | MB_ICONINFORMATION);
					valid = false;
				} else {
					entry.name = text;
				}
				if (valid && pending) {
					// 新行填完英文名后自动继续编辑路径
					continuePath = true;
				}
			} else if (subItem == 1) {
				entry.path = text;
			}

			if (valid && !continuePath && (pending || entry.name != old.name || entry.path != old.path)) {
				if (!SaveAliases(g_aliases)) {
					g_aliases[static_cast<size_t>(item)] = old;
					if (pending) {
						g_aliases.pop_back();
						g_pendingNewRow = false;
					}
					MessageBoxW(owner, L"保存 FAConfig.ini 失败。", L"错误", MB_OK | MB_ICONERROR);
					RefreshList();
				} else {
					if (pending) g_pendingNewRow = false;
					RefreshList();
					ListView_SetItemState(g_hList, item, LVIS_SELECTED | LVIS_FOCUSED,
						LVIS_SELECTED | LVIS_FOCUSED);
					ListView_EnsureVisible(g_hList, item, FALSE);
				}
			} else if (!valid && pending && text.empty()) {
				// 新行必填项为空，直接丢弃该空行
				g_aliases.pop_back();
				g_pendingNewRow = false;
				RefreshList();
			}
		}
	}

	g_cellEditClosing = false;
	if (g_hList) SetFocus(g_hList);

	if (continuePath) {
		RefreshList();
		ListView_SetItemState(g_hList, item, LVIS_SELECTED | LVIS_FOCUSED,
			LVIS_SELECTED | LVIS_FOCUSED);
		ListView_EnsureVisible(g_hList, item, FALSE);
		BeginCellEdit(item, 1);
	}
}

static void BeginCellEdit(int item, int subItem) {
	if (!g_hList || item < 0 || item >= static_cast<int>(g_aliases.size())) return;
	if (subItem < 0 || subItem > 1) return;

	FinishCellEdit(true);

	ListView_SetItemState(g_hList, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_EnsureVisible(g_hList, item, FALSE);

	RECT rect = { subItem, LVIR_BOUNDS, 0, 0 };
	if (!ListView_GetSubItemRect(g_hList, item, subItem, LVIR_BOUNDS, &rect)) return;

	const wstring text = subItem == 0
		? g_aliases[static_cast<size_t>(item)].name
		: g_aliases[static_cast<size_t>(item)].path;

	g_hCellEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
		WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
		rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
		g_hList, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CELL_EDIT)), g_hInst, nullptr);
	if (!g_hCellEdit) return;

	SendMessageW(g_hCellEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
	g_cellEditItem = item;
	g_cellEditSubItem = subItem;
	g_cellEditOldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
		g_hCellEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CellEditProc)));
	SendMessageW(g_hCellEdit, EM_SETSEL, 0, -1);
	SetFocus(g_hCellEdit);
}

static void LayoutControls(HWND hWnd) {
	RECT rc = {};
	GetClientRect(hWnd, &rc);
	const int clientW = rc.right - rc.left;
	const int clientH = rc.bottom - rc.top;

	int contentBottom = clientH;
	if (g_hStatus) {
		SendMessageW(g_hStatus, WM_SIZE, 0, 0);
		RECT statusRect = {};
		GetWindowRect(g_hStatus, &statusRect);
		ScreenToClient(hWnd, reinterpret_cast<LPPOINT>(&statusRect));
		contentBottom = statusRect.top;
	}

	const int margin = 12;
	const int headerH = 56;
	const int buttonH = 30;
	const int buttonW = 84;
	const int gap = 8;

	const int buttonY = contentBottom - margin - buttonH;
	const int listX = margin;
	const int listY = headerH;
	const int listW = clientW - margin * 2;

	// FileAgent.ini 设置区（位于别名列表与底部按钮之间）
	const int faRowH = 26;
	const int faPanelH = 24 + (faRowH + 4) * 4 + 28;
	const int faY = buttonY - 12 - faPanelH;
	const int listH = max(0, faY - 12 - listY);

	if (g_hList) {
		SetWindowPos(g_hList, nullptr, listX, listY, listW, listH, SWP_NOZORDER);
		ListView_SetColumnWidth(g_hList, 0, 200);
		ListView_SetColumnWidth(g_hList, 1, max(120, listW - 200 - 8));
	}

	if (HWND hTitle = GetDlgItem(hWnd, IDC_TITLE)) {
		SetWindowPos(hTitle, nullptr, margin + 2, 10, clientW - margin * 2 - 4, 26, SWP_NOZORDER);
	}
	if (HWND hSubtitle = GetDlgItem(hWnd, IDC_SUBTITLE)) {
		SetWindowPos(hSubtitle, nullptr, margin + 2, 34, clientW - margin * 2 - 4, 18, SWP_NOZORDER);
	}

	// FileAgent.ini 设置区
	{
		const int labelW = 112;
		const int editX = margin + labelW;
		const int editW = clientW - margin - editX;
		const int browseW = buttonW;
		int rowY = faY;

		if (g_hFaTitle) {
			SetWindowPos(g_hFaTitle, nullptr, margin + 2, rowY, clientW - margin * 2 - 4, 20, SWP_NOZORDER);
		}
		rowY += 24;

		if (g_hFaLabelDir) {
			SetWindowPos(g_hFaLabelDir, nullptr, margin, rowY + 4, labelW - 8, 20, SWP_NOZORDER);
		}
		if (HWND edit = GetDlgItem(hWnd, IDC_FA_BACKUPDIR)) {
			SetWindowPos(edit, nullptr, editX, rowY, max(80, editW - browseW - gap), faRowH, SWP_NOZORDER);
		}
		if (HWND button = GetDlgItem(hWnd, IDC_BTN_FA_BROWSEDIR)) {
			SetWindowPos(button, nullptr, clientW - margin - browseW, rowY - 1,
				browseW, faRowH + 2, SWP_NOZORDER);
		}
		rowY += faRowH + 4;

		if (g_hFaLabelRoot) {
			SetWindowPos(g_hFaLabelRoot, nullptr, margin, rowY + 4, labelW - 8, 20, SWP_NOZORDER);
		}
		if (HWND edit = GetDlgItem(hWnd, IDC_FA_BACKUPROOT)) {
			SetWindowPos(edit, nullptr, editX, rowY, editW, faRowH, SWP_NOZORDER);
		}
		rowY += faRowH + 4;

		if (g_hFaLabelExt) {
			SetWindowPos(g_hFaLabelExt, nullptr, margin, rowY + 4, labelW - 8, 20, SWP_NOZORDER);
		}
		if (HWND edit = GetDlgItem(hWnd, IDC_FA_EXTENSIONS)) {
			SetWindowPos(edit, nullptr, editX, rowY, editW, faRowH, SWP_NOZORDER);
		}
		rowY += faRowH + 4;

		if (g_hFaLabelEna) {
			SetWindowPos(g_hFaLabelEna, nullptr, margin, rowY + 4, labelW - 8, 20, SWP_NOZORDER);
		}
		if (HWND edit = GetDlgItem(hWnd, IDC_FA_ENAPATH)) {
			SetWindowPos(edit, nullptr, editX, rowY, max(80, editW - browseW - gap), faRowH, SWP_NOZORDER);
		}
		if (HWND button = GetDlgItem(hWnd, IDC_BTN_FA_BROWSEENA)) {
			SetWindowPos(button, nullptr, clientW - margin - browseW, rowY - 1,
				browseW, faRowH + 2, SWP_NOZORDER);
		}
		rowY += faRowH + 6;

		int checkX = margin;
		if (HWND check = GetDlgItem(hWnd, IDC_CHK_OVERWRITE)) {
			SetWindowPos(check, nullptr, checkX, rowY, 140, 22, SWP_NOZORDER);
		}
		checkX += 140;
		if (HWND check = GetDlgItem(hWnd, IDC_CHK_OPENCOPY)) {
			SetWindowPos(check, nullptr, checkX, rowY, 140, 22, SWP_NOZORDER);
		}
		checkX += 140;
		if (HWND check = GetDlgItem(hWnd, IDC_CHK_ADDTIMESTAMP)) {
			SetWindowPos(check, nullptr, checkX, rowY, 160, 22, SWP_NOZORDER);
		}

		if (HWND button = GetDlgItem(hWnd, IDC_BTN_FA_SAVE)) {
			SetWindowPos(button, nullptr, clientW - margin - buttonW, rowY - 3,
				buttonW, faRowH, SWP_NOZORDER);
		}
		if (HWND button = GetDlgItem(hWnd, IDC_BTN_FA_RELOAD)) {
			SetWindowPos(button, nullptr, clientW - margin - buttonW * 2 - gap, rowY - 3,
				buttonW, faRowH, SWP_NOZORDER);
		}
	}

	const int buttonX = clientW - margin - buttonW * 5 - gap * 4;
	if (g_hChkEnabled) {
		SetWindowPos(g_hChkEnabled, nullptr, margin, buttonY + 5, 180, 22, SWP_NOZORDER);
	}
	HWND buttons[] = {
		GetDlgItem(hWnd, IDC_BTN_ADD),
		GetDlgItem(hWnd, IDC_BTN_DELETE),
		GetDlgItem(hWnd, IDC_BTN_BROWSE),
		GetDlgItem(hWnd, IDC_BTN_REFRESH),
		GetDlgItem(hWnd, IDC_BTN_REGISTER)
	};
	for (int i = 0; i < 5; ++i) {
		if (buttons[i]) {
			SetWindowPos(buttons[i], nullptr, buttonX + i * (buttonW + gap), buttonY,
				buttonW, buttonH, SWP_NOZORDER);
		}
	}

	if (g_hEmptyHint) {
		SetWindowPos(g_hEmptyHint, nullptr, listX, listY + listH / 2 - 10,
			listW, 20, SWP_NOZORDER);
	}
}

static void CreateControls(HWND hWnd) {
	g_hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
	g_hFontBold = CreateFontW(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

	CreateWindowW(L"STATIC", L"别名配置", WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TITLE)), g_hInst, nullptr);
	CreateWindowW(L"STATIC", L"定义英文名与路径的映射，命令行使用 --英文名 参数即可写入 FileAgent.ini",
		WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUBTITLE)), g_hInst, nullptr);

	g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ALIAS_LIST)), g_hInst, nullptr);
	ListView_SetExtendedListViewStyle(g_hList,
		LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	LVCOLUMNW column = {};
	column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
	column.pszText = const_cast<LPWSTR>(L"英文名");
	column.cx = 200;
	column.iSubItem = 0;
	ListView_InsertColumn(g_hList, 0, &column);
	column.pszText = const_cast<LPWSTR>(L"路径");
	column.cx = 440;
	column.iSubItem = 1;
	ListView_InsertColumn(g_hList, 1, &column);

	g_hEmptyHint = CreateWindowW(L"STATIC", L"暂无别名，点击“新增”添加；双击单元格可编辑，路径可稍后填写。",
		WS_CHILD | WS_VISIBLE | SS_CENTER,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EMPTY_HINT)), g_hInst, nullptr);

	CreateWindowW(L"BUTTON", L"新增", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_DELETE)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REFRESH)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"注册", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REGISTER)), g_hInst, nullptr);

	g_hChkEnabled = CreateWindowW(L"BUTTON", L"启用命令行参数", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_ENABLED)), g_hInst, nullptr);
	SendMessageW(g_hChkEnabled, BM_SETCHECK, IsCommandEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

	// ---- FileAgent.ini 设置 ----
	g_hFaTitle = CreateWindowW(L"STATIC", L"FileAgent.ini 设置（保存后 FileAgent 下次启动生效）",
		WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_FA_TITLE)), g_hInst, nullptr);
	g_hFaLabelDir = CreateWindowW(L"STATIC", L"备份目录", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC)), g_hInst, nullptr);
	g_hFaLabelRoot = CreateWindowW(L"STATIC", L"跳过根目录", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC)), g_hInst, nullptr);
	g_hFaLabelExt = CreateWindowW(L"STATIC", L"扩展名", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC)), g_hInst, nullptr);
	g_hFaLabelEna = CreateWindowW(L"STATIC", L"ENA 路径", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC)), g_hInst, nullptr);

	CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FA_BACKUPDIR)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_FA_BROWSEDIR)), g_hInst, nullptr);
	CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FA_BACKUPROOT)), g_hInst, nullptr);
	CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FA_EXTENSIONS)), g_hInst, nullptr);
	CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FA_ENAPATH)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_FA_BROWSEENA)), g_hInst, nullptr);

	CreateWindowW(L"BUTTON", L"覆盖已有备份", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_OVERWRITE)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"打开副本", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_OPENCOPY)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"文件名添加时间戳", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_ADDTIMESTAMP)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_FA_SAVE)), g_hInst, nullptr);
	CreateWindowW(L"BUTTON", L"重新加载", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_FA_RELOAD)), g_hInst, nullptr);

	g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
		WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
		0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS_BAR)), g_hInst, nullptr);
	int statusParts[4] = { 100, 330, 540, -1 };
	SendMessageW(g_hStatus, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(statusParts));

	// 统一使用界面字体，标题使用加粗大号字体
	for (HWND child = GetWindow(hWnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
	}
	if (HWND hTitle = GetDlgItem(hWnd, IDC_TITLE)) {
		SendMessageW(hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontBold), TRUE);
	}

	LayoutControls(hWnd);
	RefreshList();
	SetFileAgentControls(hWnd);
	g_fileAgentHint = L"FileAgent.ini: 已加载";
	RefreshRegistration();
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	// 必须在任何窗口创建之前启用 DPI 感知，避免界面被系统拉伸导致模糊
	WinUtils::EnableDpiAwareness();
	EnsureConfigFiles();

	const wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
	const wstring trimmed = TrimCopy(cmdLine);
	if (trimmed.empty()) {
		return RunConfigUI(hInstance, nCmdShow);
	}
	return RunCommandLine(trimmed);
}

// ==================== 窗口 ====================
ATOM MyRegisterClass(HINSTANCE hInstance) {
	WNDCLASSEXW wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FACONFIG));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_FACONFIG);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
	return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
	g_hInst = hInstance;

	HWND hWnd = CreateWindowW(szWindowClass, L"FAConfig - 别名配置", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, 860, 700, nullptr, nullptr, hInstance, nullptr);
	if (!hWnd) return FALSE;

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	return TRUE;
}

int RunConfigUI(HINSTANCE instance, int showCmd) {
	g_hInst = instance;

	INITCOMMONCONTROLSEX icc = {};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
	InitCommonControlsEx(&icc);

	LoadStringW(instance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(instance, IDC_FACONFIG, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(instance);

	if (!InitInstance(instance, showCmd)) {
		return 1;
	}

	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	HACCEL hAccelTable = LoadAccelerators(instance, MAKEINTRESOURCE(IDC_FACONFIG));

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0)) {
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	if (SUCCEEDED(comResult)) CoUninitialize();
	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_CREATE:
		g_aliases = LoadAliases();
		CreateControls(hWnd);
		return 0;

	case WM_SIZE:
		FinishCellEdit(true);
		LayoutControls(hWnd);
		return 0;

	case WM_GETMINMAXINFO:
	{
		auto info = reinterpret_cast<MINMAXINFO*>(lParam);
		info->ptMinTrackSize.x = 680;
		info->ptMinTrackSize.y = 620;
		return 0;
	}

	case WM_CTLCOLORSTATIC:
	{
		HDC hdc = reinterpret_cast<HDC>(wParam);
		HWND child = reinterpret_cast<HWND>(lParam);
		const int id = GetDlgCtrlID(child);
		if (id == IDC_SUBTITLE || id == IDC_EMPTY_HINT || id == IDC_LBL_FA_TITLE) {
			SetTextColor(hdc, RGB(128, 128, 128));
		}
		SetBkMode(hdc, TRANSPARENT);
		return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
	}

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDM_ABOUT:
			DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			return 0;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			return 0;
		case IDC_BTN_ADD:
			OnAdd();
			return 0;
		case IDC_BTN_DELETE:
			OnDelete();
			return 0;
		case IDC_BTN_BROWSE:
			OnBrowsePath();
			return 0;
		case IDC_BTN_REFRESH:
			g_pendingNewRow = false;
			g_aliases = LoadAliases();
			RefreshList();
			RefreshRegistration();
			return 0;
		case IDC_BTN_FA_BROWSEDIR:
			OnBrowseBackupDir(hWnd);
			return 0;
		case IDC_BTN_FA_BROWSEENA:
			OnBrowseEnaPath(hWnd);
			return 0;
		case IDC_BTN_FA_SAVE:
			OnSaveFileAgentSettings(hWnd);
			return 0;
		case IDC_BTN_FA_RELOAD:
			OnReloadFileAgentSettings(hWnd);
			return 0;
		case IDC_BTN_REGISTER:
		{
			const bool actual = IsRegistered();
			// 多个实例同时使用时，界面显示的状态可能已过期，
			// 此时只刷新状态，避免误执行相反的操作。
			if (actual != g_displayedRegistered) {
				RefreshRegistration();
				MessageBoxW(hWnd,
					L"注册状态已被其他程序修改，已刷新为最新状态，请确认后重新操作。",
					L"提示", MB_OK | MB_ICONINFORMATION);
				return 0;
			}
			const bool ok = actual ? UnregisterFromSystem() : RegisterToSystem();
			if (!ok) {
				MessageBoxW(hWnd, actual ? L"卸载注册失败。" : L"注册失败。",
					L"错误", MB_OK | MB_ICONERROR);
			}
			RefreshRegistration();
			return 0;
		}
		case IDC_CHK_ENABLED:
			if (HIWORD(wParam) == BN_CLICKED) {
				const bool enable = SendMessageW(g_hChkEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
				if (!SetCommandEnabled(enable)) {
					SendMessageW(g_hChkEnabled, BM_SETCHECK, enable ? BST_UNCHECKED : BST_CHECKED, 0);
					MessageBoxW(hWnd, L"保存 FAConfig.ini 失败。", L"错误", MB_OK | MB_ICONERROR);
				}
				UpdateStatus();
				return 0;
			}
			break;
		}
		break;

	case WM_NOTIFY:
	{
		auto header = reinterpret_cast<LPNMHDR>(lParam);
		if (header->idFrom == IDC_ALIAS_LIST) {
			if (header->code == NM_DBLCLK) {
				auto activate = reinterpret_cast<LPNMITEMACTIVATE>(lParam);
				if (activate->iItem >= 0 && activate->iSubItem >= 0 && activate->iSubItem <= 1) {
					BeginCellEdit(activate->iItem, activate->iSubItem);
				}
				return 0;
			}
		}
		break;
	}

	case WM_DESTROY:
		if (g_hFont) DeleteObject(g_hFont);
		if (g_hFontBold) DeleteObject(g_hFontBold);
		g_hFont = nullptr;
		g_hFontBold = nullptr;
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	UNREFERENCED_PARAMETER(lParam);
	switch (message) {
	case WM_INITDIALOG:
		return static_cast<INT_PTR>(TRUE);
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
			EndDialog(hDlg, LOWORD(wParam));
			return static_cast<INT_PTR>(TRUE);
		}
		break;
	}
	return static_cast<INT_PTR>(FALSE);
}
