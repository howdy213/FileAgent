#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FAConfig.ini 编辑器 + ClassIsland 自动化配置生成/导入。

说明：
- 本脚本只编辑 FAConfig.ini（别名表与命令行启用状态），不直接修改 FileAgent.ini；
  对 FileAgent.ini 的写入仍由 FAConfig.exe 在 ClassIsland 触发时完成。
- 可根据别名表生成 ClassIsland 可识别的 Automations JSON，并在选择 ClassIsland.exe
  后自动定位到 <ClassIsland 目录>\\data\\Config\\Automations 进行导入。

编码说明：
- FAConfig.exe 在 Windows 上通过 ANSI 代码页读取 ini，所以本脚本以 ANSI 读写 FAConfig.ini。
"""

from __future__ import annotations

import configparser
import io
import json
import locale
import shutil
import subprocess
import sys
import threading
import uuid
from datetime import datetime
from pathlib import Path
from typing import Optional

import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, messagebox, ttk

SCRIPT_DIR = Path(__file__).resolve().parent

# 脚本自身的数据（窗口设置等）统一保存在脚本同目录
SETTINGS_FILE = SCRIPT_DIR / "settings.json"
NO_SUBJECT = "（不生成）"

# 常见英文别名到 ClassIsland 科目名的自动匹配
EN_TO_CN = {
    "chinese": "语文",
    "math": "数学",
    "mathematics": "数学",
    "english": "英语",
    "physics": "物理",
    "chemistry": "化学",
    "biology": "生物",
    "politics": "政治",
    "history": "历史",
    "geography": "地理",
    "other": "其他",
    "selfstudy": "自习",
    "self-study": "自习",
}

TRIGGER_CURRENT_SUBJECT_CHANGED = "classisland.lessons.currentTimeStateChanged"
RULE_CURRENT_SUBJECT = "classisland.lessons.currentSubject"
RULE_TIME_STATE = "classisland.lessons.timeState"
RULE_NEXT_SUBJECT = "classisland.lessons.nextSubject"
ACTION_RUN = "classisland.os.run"
TIME_STATE_PREPARE = 3  # 预备铃


# ==================== 基础工具 ====================

def set_dpi_awareness() -> None:
    """在 Windows 上启用 DPI 感知，避免高 DPI 缩放下界面模糊。

    必须在创建任何窗口（包括 Tk 根窗口）之前调用。
    - Windows 8.1+：优先使用 shcore.SetProcessDpiAwareness(1)（PROCESS_SYSTEM_DPI_AWARE）。
    - 更早系统：回退到 user32.SetProcessDPIAware()。
    - 非 Windows 平台直接跳过。
    """
    if sys.platform != "win32":
        return
    try:
        import ctypes
    except ImportError:
        return

    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)  # PROCESS_SYSTEM_DPI_AWARE
        return
    except (AttributeError, OSError):
        pass

    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except (AttributeError, OSError):
        pass


def apply_tk_dpi_scaling(app: tk.Tk) -> None:
    """启用 DPI 感知后，让 tkinter 按实际 DPI 缩放，避免控件变得过小。

    tk 的 scaling 表示“每磅多少像素”，默认约 96/72。若系统 DPI 高于 96，
    不调整会导致界面偏小。
    """
    if sys.platform != "win32":
        return
    try:
        import ctypes
        dpi = ctypes.windll.user32.GetDpiForWindow(app.winfo_id())
        if dpi:
            app.tk.call("tk", "scaling", dpi / 72.0)
    except Exception:
        pass


def load_settings() -> dict:
    try:
        with SETTINGS_FILE.open("r", encoding="utf-8") as f:
            data = json.load(f)
            return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_settings(data: dict) -> None:
    try:
        SETTINGS_FILE.parent.mkdir(parents=True, exist_ok=True)
        with SETTINGS_FILE.open("w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    except OSError:
        pass


def default_ini_path() -> Path:
    # 自动使用脚本所在目录的 FAConfig.ini
    return SCRIPT_DIR / "FAConfig.ini"


# ==================== 注册状态（委托 FileAgent.exe） ====================

def fileagent_exe() -> Path:
    return default_ini_path().with_name("FileAgent.exe")


def run_fileagent(arguments: list[str]) -> Optional[int]:
    """向 FileAgent.exe 传参并返回其退出码；无法启动时返回 None。"""
    exe = fileagent_exe()
    if not exe.exists():
        return None
    try:
        result = subprocess.run([str(exe), *arguments], cwd=str(exe.parent),
                                capture_output=True, timeout=60)
        return result.returncode
    except (OSError, subprocess.SubprocessError):
        return None


def is_registered() -> bool:
    """查询 FileAgent 是否已安装（--status 退出码 0 = 已安装）。"""
    return run_fileagent(["--status"]) == 0


def register_faconfig() -> bool:
    """调用 FileAgent.exe --install。"""
    return run_fileagent(["--install"]) == 0


def unregister_faconfig() -> bool:
    """调用 FileAgent.exe --uninstall。"""
    return run_fileagent(["--uninstall"]) == 0


def ansi_encoding() -> str:
    """返回 FAConfig.ini 应使用的编码名。

    - Windows：使用 ANSI 代码页（Python 中名为 "mbcs"），与 FAConfig.exe 保持一致。
    - 其他平台：使用系统首选编码（通常为 UTF-8），便于脚本跨平台运行。
    """
    if sys.platform == "win32":
        return "mbcs"
    return locale.getpreferredencoding(False) or "utf-8"


def quote_arg(value: str) -> str:
    if not value:
        return '""'
    if any(ch in value for ch in ' \t"'):
        return '"' + value.replace('"', '\\"') + '"'
    return value


def backup_file(path: Path) -> Optional[Path]:
    if not path.exists():
        return None
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = path.with_name(f"{path.name}.bak-{stamp}")
    try:
        shutil.copy2(path, backup)
        return backup
    except OSError:
        return None


# ==================== FAConfig.ini ====================

class AliasStore:
    """直接读写 FAConfig.ini，保持与 FAConfig.exe 相同的格式。

    编码约定：
    - FAConfig.exe 使用 Windows ANSI 代码页读取 ini，因此读写均使用 ANSI。
    - 不再回退到 UTF-8：FileAgent 无法识别 UTF-8 保存的路径。
    """

    def __init__(self, path: Path):
        self.path = Path(path)

    def _parser(self) -> configparser.RawConfigParser:
        parser = configparser.RawConfigParser(interpolation=None, delimiters=("=",))
        parser.optionxform = str  # type: ignore[assignment]
        parser.strict = False
        return parser

    def _read_text(self) -> str:
        try:
            raw = self.path.read_bytes()
        except OSError as error:
            raise RuntimeError(f"读取 {self.path} 失败：{error}") from error

        encoding = ansi_encoding()
        if raw.startswith(b"\xef\xbb\xbf"):
            raise RuntimeError(
                f"读取 {self.path} 失败：文件为 UTF-8 编码。"
                f"FileAgent 只能识别 ANSI，请将文件另存为 ANSI 后重试。"
            )
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError as error:
            raise RuntimeError(
                f"读取 {self.path} 失败：文件不是 ANSI({encoding}) 编码。"
                f"FileAgent 只能识别 ANSI，请将文件另存为 ANSI 后重试。"
            ) from error

    def _write_text(self, text: str) -> None:
        encoding = ansi_encoding()
        # 先编码再写文件：write_text 会先清空文件，若编码失败会丢失原有内容
        try:
            data = text.encode(encoding)
        except UnicodeEncodeError as error:
            raise RuntimeError(
                f"写入 {self.path} 失败：内容包含 ANSI({encoding}) 无法表示的字符。"
                f"请移除非 ANSI 字符，或调整 Windows 区域与语言设置。"
            ) from error
        try:
            self.path.write_bytes(data)
        except OSError as error:
            raise RuntimeError(f"写入 {self.path} 失败：{error}") from error

    def ensure_exists(self) -> None:
        if self.path.exists():
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        parser = self._parser()
        parser.add_section("Settings")
        parser.set("Settings", "Enabled", "true")
        parser.add_section("Aliases")
        buffer = io.StringIO()
        parser.write(buffer)
        self._write_text(buffer.getvalue())

    def load(self) -> tuple[list[tuple[str, str]], bool]:
        self.ensure_exists()
        parser = self._parser()
        try:
            parser.read_string(self._read_text(), source=str(self.path))
        except configparser.Error as error:
            raise RuntimeError(f"解析 {self.path} 失败：{error}") from error

        enabled = True
        if parser.has_section("Settings"):
            raw = parser.get("Settings", "Enabled", fallback="true").strip().lower()
            enabled = raw not in ("false", "0", "no", "off")

        aliases: list[tuple[str, str]] = []
        if parser.has_section("Aliases"):
            for key, value in parser.items("Aliases"):
                name = key.strip()
                if not name:
                    continue
                aliases.append((name, value.strip()))
        return aliases, enabled

    def save(self, aliases: list[tuple[str, str]], enabled: bool) -> None:
        self.ensure_exists()
        parser = self._parser()
        try:
            parser.read_string(self._read_text(), source=str(self.path))
        except configparser.Error as error:
            raise RuntimeError(f"解析 {self.path} 失败：{error}") from error

        if not parser.has_section("Settings"):
            parser.add_section("Settings")
        parser.set("Settings", "Enabled", "true" if enabled else "false")

        if parser.has_section("Aliases"):
            parser.remove_section("Aliases")
        parser.add_section("Aliases")
        for name, value in aliases:
            parser.set("Aliases", name, value)

        buffer = io.StringIO()
        parser.write(buffer)
        self._write_text(buffer.getvalue())


class FileAgentIniStore(AliasStore):
    """读写 FileAgent.ini 的 [Settings]，与 FileAgent 的默认值保持一致。"""

    DEFAULTS = {
        "Overwrite": "true",
        "BackupRoot": "",
        "BackupDir": ".\\AutoSaveBackup",
        "OpenCopy": "false",
        "AddTimestamp": "true",
        "Extensions": ".doc,.docx,.ppt,.pptx,.xls,.xlsx,.pdf",
        "EnaPath": "",
    }
    BOOL_KEYS = ("Overwrite", "OpenCopy", "AddTimestamp")

    def ensure_exists(self) -> None:
        if self.path.exists():
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        parser = self._parser()
        parser.add_section("Settings")
        for key, value in self.DEFAULTS.items():
            parser.set("Settings", key, value)
        buffer = io.StringIO()
        parser.write(buffer)
        self._write_text(buffer.getvalue())

    def load(self) -> dict[str, object]:
        self.ensure_exists()
        parser = self._parser()
        try:
            parser.read_string(self._read_text(), source=str(self.path))
        except configparser.Error as error:
            raise RuntimeError(f"解析 {self.path} 失败：{error}") from error

        values: dict[str, object] = dict(self.DEFAULTS)
        if not parser.has_section("Settings"):
            return values

        for key in ("BackupDir", "BackupRoot", "Extensions", "EnaPath"):
            values[key] = parser.get("Settings", key, fallback=str(values[key])).strip()
        for key in self.BOOL_KEYS:
            raw = parser.get("Settings", key, fallback=str(values[key])).strip().lower()
            values[key] = raw not in ("false", "0", "no", "off")
        return values

    def save(self, values: dict[str, object]) -> None:
        self.ensure_exists()
        parser = self._parser()
        try:
            parser.read_string(self._read_text(), source=str(self.path))
        except configparser.Error as error:
            raise RuntimeError(f"解析 {self.path} 失败：{error}") from error

        if not parser.has_section("Settings"):
            parser.add_section("Settings")
        for key in ("BackupDir", "BackupRoot", "Extensions", "EnaPath"):
            parser.set("Settings", key, str(values.get(key, "")))
        for key in self.BOOL_KEYS:
            parser.set("Settings", key, "true" if values.get(key) else "false")

        buffer = io.StringIO()
        parser.write(buffer)
        self._write_text(buffer.getvalue())


# ==================== ClassIsland 辅助 ====================

def locate_classisland_root(exe: Path) -> Optional[Path]:
    """从 ClassIsland.exe 所在位置向上寻找包含 data/Config 的根目录。"""
    current = exe.parent
    for base in [current, *current.parents[:3]]:
        if (base / "data" / "Config").is_dir():
            return base
    return None


def automations_dir_from_exe(exe: Path) -> Optional[Path]:
    root = locate_classisland_root(exe)
    if root is None:
        return None
    return root / "data" / "Config" / "Automations"


def load_subjects(exe: Path) -> list[tuple[str, str]]:
    """读取默认档案中的科目列表，返回 [(科目名, SubjectId)]。"""
    root = locate_classisland_root(exe)
    if root is None:
        return []
    profile = root / "data" / "Profiles" / "Default.json"
    try:
        with profile.open("r", encoding="utf-8-sig") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return []

    subjects = data.get("Subjects", {}) if isinstance(data, dict) else {}
    result: list[tuple[str, str]] = []
    if isinstance(subjects, dict):
        for subject_id, info in subjects.items():
            name = ""
            if isinstance(info, dict):
                name = str(info.get("Name", "")).strip()
            if name:
                result.append((name, str(subject_id)))
    result.sort(key=lambda item: item[0])
    return result


def auto_match_subject(alias: str, path: str, subjects: list[tuple[str, str]]) -> Optional[str]:
    lowered = alias.strip().lower()
    for name, subject_id in subjects:
        if name.lower() == lowered:
            return subject_id
    translated = EN_TO_CN.get(lowered)
    if translated:
        for name, subject_id in subjects:
            if name == translated:
                return subject_id
    # 退而求其次：路径中包含科目名称（例如 D:\\...\\3英语）
    for name, subject_id in subjects:
        if name in path:
            return subject_id
    return None


def build_automation(alias: str, subject_name: str, subject_id: str, fa_config_exe: str) -> dict:
    """当前科目变化时切换配置。"""
    rules = [
        {
            "IsReversed": False,
            "Id": RULE_CURRENT_SUBJECT,
            "Settings": {
                "SubjectId": subject_id,
                "IsActive": False,
            },
            "IsActive": False,
        }
    ]
    return _build_automation(subject_name, rules, alias, fa_config_exe)


def build_prepare_automation(alias: str, subject_name: str, subject_id: str, fa_config_exe: str) -> dict:
    """预备铃响时提前切换到下一节课的配置。"""
    rules = [
        {
            "IsReversed": False,
            "Id": RULE_TIME_STATE,
            "Settings": {
                "State": TIME_STATE_PREPARE,
                "IsActive": False,
            },
            "IsActive": False,
        },
        {
            "IsReversed": False,
            "Id": RULE_NEXT_SUBJECT,
            "Settings": {
                "SubjectId": subject_id,
                "IsActive": False,
            },
            "IsActive": False,
        },
    ]
    return _build_automation(f"{subject_name}（预备）", rules, alias, fa_config_exe)


def _build_automation(display_name: str, rules: list, alias: str, fa_config_exe: str) -> dict:
    command = f'"{fa_config_exe}" --subject {quote_arg(alias)}'
    return {
        "Triggers": [
            {
                "Id": TRIGGER_CURRENT_SUBJECT_CHANGED,
                "Settings": None,
                "IsActive": False,
            }
        ],
        "IsConditionEnabled": True,
        "Ruleset": {
            "Mode": 0,
            "IsReversed": False,
            "Groups": [
                {
                    "Rules": rules,
                    "Mode": 1,
                    "IsReversed": False,
                    "IsEnabled": True,
                    "IsActive": False,
                }
            ],
            "IsActive": False,
        },
        "ActionSet": {
            "IsWorking": False,
            "Guid": str(uuid.uuid4()),
            "Name": display_name,
            "Actions": [
                {
                    "IsCompleted": False,
                    "Id": ACTION_RUN,
                    "Settings": {
                        "RunType": "Command",
                        "Value": command,
                        "Args": "",
                        "IsActive": False,
                    },
                    "IsActive": False,
                }
            ],
            "IsEnabled": True,
            "IsRevertEnabled": True,
            "Status": 0,
            "IsActive": False,
        },
        "IsActive": False,
    }


def is_faconfig_automation(item: dict) -> bool:
    action_set = item.get("ActionSet", {}) if isinstance(item, dict) else {}
    if not isinstance(action_set, dict):
        return False
    actions = action_set.get("Actions", [])
    if not isinstance(actions, list):
        return False
    for action in actions:
        if not isinstance(action, dict):
            continue
        settings = action.get("Settings")
        if not isinstance(settings, dict):
            continue
        value = str(settings.get("Value", ""))
        if "FAConfig.exe" in value and "--subject" in value:
            return True
    return False


def read_automations(path: Path) -> list:
    if not path.exists():
        return []
    try:
        with path.open("r", encoding="utf-8-sig") as f:
            data = json.load(f)
        return data if isinstance(data, list) else []
    except (OSError, ValueError):
        return []


def write_automations(target: Path, items: list) -> Optional[Path]:
    target.parent.mkdir(parents=True, exist_ok=True)
    backup = backup_file(target)
    with target.open("w", encoding="utf-8") as f:
        json.dump(items, f, ensure_ascii=False, indent=2)
    return backup


# ==================== 生成对话框 ====================

class GenerateDialog(tk.Toplevel):
    def __init__(self, app: "App", aliases: list[tuple[str, str]], subjects: list[tuple[str, str]]):
        super().__init__(app)
        self.app = app
        self.aliases = aliases
        self.subjects = subjects
        self.subject_names = [name for name, _ in subjects]
        self.mapping: dict[str, str] = {}
        self.alias_to_path = dict(aliases)
        self._sync_guard = False

        self.title("生成 ClassIsland 自动化")
        # 尺寸跟随字体，不写死像素
        width, height = self._calculate_size()
        self.geometry(f"{width}x{height}")
        self.transient(app)
        self.resizable(True, True)

        container = ttk.Frame(self, padding=10)
        container.pack(fill=tk.BOTH, expand=True)

        hint = ttk.Label(container, text=(
            "为每个别名选择对应的 ClassIsland 科目。生成后，ClassIsland 会调用 "
            "FAConfig.exe --subject <别名> 修改 FileAgent.ini。"
        ), wraplength=width - 40, foreground="#555555")
        hint.pack(anchor=tk.W, pady=(0, 8))

        tree_frame = ttk.Frame(container)
        tree_frame.pack(fill=tk.BOTH, expand=True)

        self.tree = ttk.Treeview(tree_frame, columns=("alias", "path", "subject"),
                                 show="headings", selectmode="browse")
        self.tree.heading("alias", text="别名")
        self.tree.heading("path", text="路径")
        self.tree.heading("subject", text="ClassIsland 科目")
        self.tree.column("alias", width=150, stretch=False)
        self.tree.column("path", width=330, stretch=True)
        self.tree.column("subject", width=160, stretch=False)
        scrollbar = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        edit_frame = ttk.Frame(container)
        edit_frame.pack(fill=tk.X, pady=8)
        ttk.Label(edit_frame, text="当前别名科目:").pack(side=tk.LEFT)
        self.subject_var = tk.StringVar(value=NO_SUBJECT)
        self.subject_combo = ttk.Combobox(edit_frame, textvariable=self.subject_var,
                                          values=[NO_SUBJECT] + self.subject_names,
                                          state="readonly", width=24)
        self.subject_combo.pack(side=tk.LEFT, padx=(6, 6))
        self.subject_combo.bind("<<ComboboxSelected>>", self._on_subject_selected)

        self.keep_others_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(edit_frame, text="保留已有的其他自动化",
                        variable=self.keep_others_var).pack(side=tk.RIGHT)

        option_frame = ttk.Frame(container)
        option_frame.pack(fill=tk.X)
        self.include_prepare_var = tk.BooleanVar(
            value=bool(self.app.settings.get("include_prepare", False)))
        ttk.Checkbutton(option_frame, text="同时生成“预备”自动化（预备铃响时提前切换）",
                        variable=self.include_prepare_var).pack(anchor=tk.W)

        button_frame = ttk.Frame(container)
        button_frame.pack(fill=tk.X, pady=(4, 0))
        self.import_button = ttk.Button(button_frame, text="生成并导入到 ClassIsland",
                                        command=self._generate_and_import)
        self.import_button.pack(side=tk.LEFT)
        ttk.Button(button_frame, text="另存为 JSON...",
                   command=self._export_json).pack(side=tk.LEFT, padx=8)
        ttk.Button(button_frame, text="取消", command=self.destroy).pack(side=tk.RIGHT)

        self._populate()
        if self.tree.get_children():
            first = self.tree.get_children()[0]
            self.tree.selection_set(first)
            self.tree.focus(first)
            self._sync_from_selection()
        self.grab_set()

    def _calculate_size(self) -> tuple[int, int]:
        """根据字体大小计算对话框尺寸，跟随字体与 DPI 缩放。"""
        try:
            font = tkfont.nametofont("TkDefaultFont")
            line_height = font.metrics("linespace") or 16
            char_width = font.measure("0") or 8
        except (tk.TclError, AttributeError):
            return 760, 560
        width = max(680, char_width * 80)
        height = max(480, line_height * 28)
        return width, height

    # ---- 界面同步 ----
    def _populate(self) -> None:
        self.tree.delete(*self.tree.get_children())
        saved_names = self.app.settings.get("subject_map", {})
        for alias, path in self.aliases:
            saved_name = saved_names.get(alias) if isinstance(saved_names, dict) else None
            subject_id: Optional[str] = None
            if saved_name:
                for name, sid in self.subjects:
                    if name == saved_name:
                        subject_id = sid
                        break
            if subject_id is None:
                subject_id = auto_match_subject(alias, path, self.subjects)
            if subject_id:
                self.mapping[alias] = subject_id
            self.tree.insert("", tk.END, iid=alias,
                             values=(alias, path, self._subject_label(subject_id)))

    def _subject_label(self, subject_id: Optional[str]) -> str:
        if not subject_id:
            return NO_SUBJECT
        for name, sid in self.subjects:
            if sid == subject_id:
                return name
        return NO_SUBJECT

    def _selected_alias(self) -> Optional[str]:
        selection = self.tree.selection()
        return selection[0] if selection else None

    def _sync_from_selection(self) -> None:
        alias = self._selected_alias()
        if alias is None:
            return
        self._sync_guard = True
        self.subject_var.set(self._subject_label(self.mapping.get(alias)))
        self._sync_guard = False

    def _on_subject_selected(self, _event=None) -> None:
        if self._sync_guard:
            return
        alias = self._selected_alias()
        if alias is None:
            return
        chosen = self.subject_var.get()
        if chosen == NO_SUBJECT:
            self.mapping.pop(alias, None)
        else:
            for name, sid in self.subjects:
                if name == chosen:
                    self.mapping[alias] = sid
                    break
        # 刷新该行
        path = self.alias_to_path.get(alias, "")
        values = (alias, path, chosen)
        self.tree.item(alias, values=values)

    # ---- 生成 ----
    def _collect_automations(self) -> list:
        fa_exe = self.app.fa_exe_var.get().strip()
        if not fa_exe:
            raise ValueError("请先填写 FAConfig.exe 路径。")
        generated = []
        for alias, _path in self.aliases:
            subject_id = self.mapping.get(alias)
            if not subject_id:
                continue
            subject_name = self._subject_label(subject_id)
            generated.append(build_automation(alias, subject_name, subject_id, fa_exe))
            if self.include_prepare_var.get():
                generated.append(build_prepare_automation(alias, subject_name, subject_id, fa_exe))
        self.app.settings["include_prepare"] = bool(self.include_prepare_var.get())
        return generated

    def _merged_items(self, generated: list) -> list:
        target = self.app.automations_dir_var.get().strip()
        if self.keep_others_var.get() and target:
            existing = read_automations(Path(target) / "FileAgent.json")
            preserved = [item for item in existing if not is_faconfig_automation(item)]
            return preserved + generated
        return generated

    def _generate_and_import(self) -> None:
        target_dir = self.app.automations_dir_var.get().strip()
        if not target_dir:
            messagebox.showwarning("提示", "请先在主窗口选择 ClassIsland.exe，以自动定位 Automations 目录。", parent=self)
            return
        try:
            generated = self._collect_automations()
        except ValueError as error:
            messagebox.showwarning("提示", str(error), parent=self)
            return
        if not generated:
            messagebox.showinfo("提示", "没有任何别名被映射到科目，未生成内容。", parent=self)
            return

        target = Path(target_dir) / "FileAgent.json"
        items = self._merged_items(generated)
        try:
            backup = write_automations(target, items)
        except OSError as error:
            messagebox.showerror("写入失败", f"无法写入：\n{target}\n\n{error}", parent=self)
            return
        self.app.save_subject_mapping(self.mapping)
        self.app.log(f"已生成 {len(generated)} 条自动化并导入: {target}")
        if backup:
            self.app.log(f"原文件已备份: {backup.name}")
        else:
            self.app.log("原文件不存在，已新建 FileAgent.json")
        messagebox.showinfo("完成", f"已导入 {len(generated)} 条自动化到：\n{target}", parent=self)
        self.destroy()

    def _export_json(self) -> None:
        try:
            generated = self._collect_automations()
        except ValueError as error:
            messagebox.showwarning("提示", str(error), parent=self)
            return
        if not generated:
            messagebox.showinfo("提示", "没有任何别名被映射到科目，未生成内容。", parent=self)
            return
        path = filedialog.asksaveasfilename(parent=self, title="导出 ClassIsland 自动化 JSON",
                                           defaultextension=".json",
                                           filetypes=[("JSON", "*.json")])
        if not path:
            return
        items = self._merged_items(generated)
        try:
            with Path(path).open("w", encoding="utf-8") as f:
                json.dump(items, f, ensure_ascii=False, indent=2)
        except OSError as error:
            messagebox.showerror("导出失败", f"无法写入：\n{path}\n\n{error}", parent=self)
            return
        self.app.save_subject_mapping(self.mapping)
        self.app.log(f"已导出 {len(generated)} 条自动化: {path}")
        messagebox.showinfo("完成", f"已导出到：\n{path}", parent=self)


# ==================== 主界面 ====================

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        # 启用 DPI 感知后，按实际 DPI 调整 tk 缩放，避免界面过小
        apply_tk_dpi_scaling(self)

        self.title("FAConfig 编辑器 - 别名与 ClassIsland 自动化")
        # 窗口尺寸跟随字体（行高/字符宽）与 DPI 缩放，不再写死像素值
        win_w, win_h = self._calculate_window_size()
        self.geometry(f"{win_w}x{win_h}")
        self.minsize(win_w * 9 // 10, win_h * 8 // 10)

        self.settings = load_settings()
        self.store = AliasStore(default_ini_path())
        self.fa_store = FileAgentIniStore(default_ini_path().with_name("FileAgent.ini"))
        self.aliases: list[tuple[str, str]] = []
        self.enabled = True
        self._editor: Optional[ttk.Entry] = None
        self._editor_iid: Optional[str] = None
        self._editor_column: Optional[int] = None
        self._pending_new_iid: Optional[str] = None
        self._new_row_seq = 0
        self._displayed_registered = False

        self._build_ui()
        self._derive_fa_exe()
        self._load()

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _calculate_window_size(self) -> tuple[int, int]:
        """根据当前字体计算初始窗口尺寸，跟随字体与 DPI 缩放。

        - 宽度：约 110 个 "0" 字符宽，保证表格中路径不被过度压缩。
        - 高度：约 42 行字体行高，保证别名表格能显示足够行数。
        - 同时给出合理下限，避免小字体时窗口过小。
        """
        try:
            font = tkfont.nametofont("TkDefaultFont")
            line_height = font.metrics("linespace") or 16
            char_width = font.measure("0") or 8
        except (tk.TclError, AttributeError):
            return 1000, 800
        width = max(900, char_width * 110)
        height = max(650, line_height * 42)
        return width, height

    # ---- 构建界面 ----
    def _build_ui(self) -> None:
        try:
            style = ttk.Style(self)
            if "vista" in style.theme_names():
                style.theme_use("vista")
            # Treeview 行高显式跟随字体，避免缩放后行内容显示不全
            try:
                default_font = tkfont.nametofont("TkDefaultFont")
                style.configure("Treeview",
                                rowheight=default_font.metrics("linespace") + 6)
            except (tk.TclError, AttributeError):
                pass
        except tk.TclError:
            pass

        main = ttk.Frame(self, padding=10)
        main.pack(fill=tk.BOTH, expand=True)

        # 通用路径设置（两个标签页共用）
        path_frame = ttk.LabelFrame(main, text="路径设置", padding=(10, 6))
        path_frame.pack(fill=tk.X)

        self.ini_var = tk.StringVar(value=str(self.store.path))
        self.fa_exe_var = tk.StringVar(value="")
        self.ci_exe_var = tk.StringVar(value=self.settings.get("ci_exe", ""))
        self.automations_dir_var = tk.StringVar(value="")
        self.subject_count_var = tk.StringVar(value="科目: 未加载")
        self.reg_var = tk.StringVar(value="")

        self._path_row(path_frame, 0, "FAConfig.ini", self.ini_var, None, browse_text="",
                       readonly=True)

        reg_row = ttk.Frame(path_frame)
        reg_row.grid(row=1, column=0, columnspan=3, sticky=tk.EW, pady=(4, 0))
        ttk.Label(reg_row, text="注册状态:").pack(side=tk.LEFT)
        ttk.Label(reg_row, textvariable=self.reg_var,
                  foreground="#555555").pack(side=tk.LEFT, padx=6)
        self.reg_button = ttk.Button(reg_row, text="注册", command=self._toggle_registration)
        self.reg_button.pack(side=tk.RIGHT)
        ttk.Button(reg_row, text="刷新", command=self._refresh_registration).pack(side=tk.RIGHT, padx=6)
        path_frame.columnconfigure(1, weight=1)
        path_frame.columnconfigure(2, weight=0)

        notebook = ttk.Notebook(main)
        notebook.pack(fill=tk.BOTH, expand=True, pady=(10, 0))

        # ---- 标签页：别名 ----
        alias_tab = ttk.Frame(notebook, padding=(8, 8))
        notebook.add(alias_tab, text="别名")

        button_bar = ttk.Frame(alias_tab)
        button_bar.pack(fill=tk.X, pady=(0, 6))
        ttk.Button(button_bar, text="新增", command=self._on_add).pack(side=tk.LEFT)
        ttk.Button(button_bar, text="删除", command=self._on_delete).pack(side=tk.LEFT, padx=6)
        ttk.Button(button_bar, text="浏览...", command=self._browse_selected_path).pack(side=tk.LEFT)
        ttk.Button(button_bar, text="刷新", command=self._load).pack(side=tk.LEFT, padx=6)
        ttk.Label(button_bar, text="提示：双击表格单元格可直接编辑",
                  foreground="#777777").pack(side=tk.LEFT, padx=12)

        self.enabled_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(button_bar, text="启用命令行参数", variable=self.enabled_var,
                        command=self._on_toggle_enabled).pack(side=tk.LEFT, padx=(16, 0))

        tree_frame = ttk.Frame(alias_tab)
        tree_frame.pack(fill=tk.BOTH, expand=True)
        self.tree = ttk.Treeview(tree_frame, columns=("name", "path"), show="headings", selectmode="browse")
        self.tree.heading("name", text="英文名")
        self.tree.heading("path", text="路径")
        self.tree.column("name", width=180, stretch=False)
        self.tree.column("path", width=650, stretch=True)
        vsb = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        vsb.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.bind("<Double-1>", self._on_tree_double_click)

        # ---- 标签页：ClassIsland ----
        ci_tab = ttk.Frame(notebook, padding=(8, 8))
        notebook.add(ci_tab, text="ClassIsland")

        ci_path = ttk.LabelFrame(ci_tab, text="ClassIsland 路径", padding=(10, 6))
        ci_path.pack(fill=tk.X)
        self._path_row(ci_path, 0, "ClassIsland.exe", self.ci_exe_var, self._browse_ci_exe, browse_text="选择...")
        ttk.Label(ci_path, text="Automations 目录").grid(row=1, column=0, sticky=tk.W, pady=3)
        ttk.Entry(ci_path, textvariable=self.automations_dir_var, state="readonly").grid(
            row=1, column=1, columnspan=2, sticky=tk.EW, padx=6, pady=3)
        ttk.Label(ci_path, textvariable=self.subject_count_var, foreground="#555555").grid(
            row=2, column=1, sticky=tk.W, padx=6)
        ci_path.columnconfigure(1, weight=1)
        ci_path.columnconfigure(2, weight=0)

        ci_buttons = ttk.Frame(ci_tab)
        ci_buttons.pack(fill=tk.X, pady=(8, 6))
        ttk.Button(ci_buttons, text="生成并导入...", command=self._open_generate_dialog).pack(side=tk.LEFT)
        ttk.Button(ci_buttons, text="导入 JSON 文件...", command=self._import_json_file).pack(side=tk.LEFT, padx=8)
        ttk.Button(ci_buttons, text="重新定位", command=self._refresh_classisland).pack(side=tk.LEFT)
        ttk.Button(ci_buttons, text="强制关闭 ClassIsland", command=self._close_classisland).pack(side=tk.RIGHT)
        ttk.Button(ci_buttons, text="打开 ClassIsland", command=self._open_classisland).pack(side=tk.RIGHT, padx=8)

        self.log_text = tk.Text(ci_tab, height=8, state=tk.DISABLED, wrap=tk.WORD,
                                background="#fafafa", relief=tk.FLAT)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # ---- 标签页：FileAgent.ini ----
        fa_tab = ttk.Frame(notebook, padding=(8, 8))
        notebook.add(fa_tab, text="FileAgent.ini")

        fa_frame = ttk.LabelFrame(
            fa_tab, text="FileAgent.ini [Settings]（保存后 FileAgent 下次启动时生效）", padding=(10, 6))
        fa_frame.pack(fill=tk.X)

        self.fa_backup_dir_var = tk.StringVar(value="")
        self.fa_backup_root_var = tk.StringVar(value="")
        self.fa_extensions_var = tk.StringVar(value="")
        self.fa_ena_path_var = tk.StringVar(value="")
        self.fa_overwrite_var = tk.BooleanVar(value=True)
        self.fa_open_copy_var = tk.BooleanVar(value=False)
        self.fa_add_timestamp_var = tk.BooleanVar(value=True)

        ttk.Label(fa_frame, text="备份目录").grid(row=0, column=0, sticky=tk.W, pady=3)
        ttk.Entry(fa_frame, textvariable=self.fa_backup_dir_var).grid(
            row=0, column=1, sticky=tk.EW, padx=6, pady=3)
        ttk.Button(fa_frame, text="浏览...", command=self._browse_fileagent_backup_dir).grid(
            row=0, column=2, pady=3)

        ttk.Label(fa_frame, text="跳过根目录").grid(row=1, column=0, sticky=tk.W, pady=3)
        ttk.Entry(fa_frame, textvariable=self.fa_backup_root_var).grid(
            row=1, column=1, columnspan=2, sticky=tk.EW, padx=6, pady=3)

        ttk.Label(fa_frame, text="扩展名").grid(row=2, column=0, sticky=tk.W, pady=3)
        ttk.Entry(fa_frame, textvariable=self.fa_extensions_var).grid(
            row=2, column=1, columnspan=2, sticky=tk.EW, padx=6, pady=3)

        ttk.Label(fa_frame, text="ENA 路径").grid(row=3, column=0, sticky=tk.W, pady=3)
        ttk.Entry(fa_frame, textvariable=self.fa_ena_path_var).grid(
            row=3, column=1, sticky=tk.EW, padx=6, pady=3)
        ttk.Button(fa_frame, text="浏览...", command=self._browse_fileagent_ena_path).grid(
            row=3, column=2, pady=3)

        fa_checks = ttk.Frame(fa_frame)
        fa_checks.grid(row=4, column=0, columnspan=3, sticky=tk.W, pady=(6, 0))
        ttk.Checkbutton(fa_checks, text="覆盖已有备份", variable=self.fa_overwrite_var).pack(side=tk.LEFT)
        ttk.Checkbutton(fa_checks, text="打开副本", variable=self.fa_open_copy_var).pack(side=tk.LEFT, padx=12)
        ttk.Checkbutton(fa_checks, text="文件名添加时间戳",
                        variable=self.fa_add_timestamp_var).pack(side=tk.LEFT)
        fa_frame.columnconfigure(1, weight=1)
        fa_frame.columnconfigure(2, weight=0)

        fa_buttons = ttk.Frame(fa_tab)
        fa_buttons.pack(fill=tk.X, pady=(8, 6))
        ttk.Button(fa_buttons, text="保存", command=self._save_fileagent_settings).pack(side=tk.LEFT)
        ttk.Button(fa_buttons, text="重新加载", command=self._load_fileagent_settings).pack(side=tk.LEFT, padx=8)

        ttk.Label(
            fa_tab,
            text="说明：\n"
                 "· 备份目录支持相对路径（相对于 FileAgent.exe），例如 .\\AutoSaveBackup。\n"
                 "· 跳过根目录可填多个路径，用分号或英文逗号分隔；留空表示不跳过任何目录。\n"
                 "· 扩展名用英文逗号分隔，可带或不带前导点。\n"
                 "· ENA 路径：指定一个 exe，备份成功且文件有更新时以 -toast 弹出通知；留空则不通知。",
            foreground="#666666", justify=tk.LEFT).pack(anchor=tk.W, pady=(4, 0))

        # 状态栏
        status_frame = ttk.Frame(self, padding=(10, 4))
        status_frame.pack(fill=tk.X, side=tk.BOTTOM)
        self.status_var = tk.StringVar(value="")
        ttk.Label(status_frame, textvariable=self.status_var).pack(side=tk.LEFT)
        self._refresh_registration()
        self._load_fileagent_settings()
        self._refresh_classisland()

    def _path_row(self, parent: ttk.LabelFrame, row: int, label: str,
                  variable: tk.StringVar, command, browse_text: str,
                  readonly: bool = False) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky=tk.W, pady=3)
        entry = ttk.Entry(parent, textvariable=variable)
        if readonly:
            entry.configure(state="readonly")
        entry.grid(row=row, column=1, sticky=tk.EW, padx=6, pady=3)
        if command is not None:
            ttk.Button(parent, text=browse_text, command=command).grid(row=row, column=2, pady=3)

    def _derive_fa_exe(self) -> None:
        """FAConfig.exe 与 FAConfig.ini 同目录，无需手动选择。"""
        self.fa_exe_var.set(str(Path(self.store.path).with_name("FAConfig.exe")))

    # ---- FileAgent.ini ----
    def _load_fileagent_settings(self) -> None:
        try:
            values = self.fa_store.load()
        except RuntimeError as error:
            messagebox.showerror("错误", str(error))
            self.log(str(error))
            return
        self.fa_backup_dir_var.set(str(values["BackupDir"]))
        self.fa_backup_root_var.set(str(values["BackupRoot"]))
        self.fa_extensions_var.set(str(values["Extensions"]))
        self.fa_ena_path_var.set(str(values["EnaPath"]))
        self.fa_overwrite_var.set(bool(values["Overwrite"]))
        self.fa_open_copy_var.set(bool(values["OpenCopy"]))
        self.fa_add_timestamp_var.set(bool(values["AddTimestamp"]))
        self.log(f"已读取 FileAgent.ini：{self.fa_store.path}")

    def _save_fileagent_settings(self) -> None:
        values = {
            "BackupDir": self.fa_backup_dir_var.get().strip(),
            "BackupRoot": self.fa_backup_root_var.get().strip(),
            "Extensions": self.fa_extensions_var.get().strip(),
            "EnaPath": self.fa_ena_path_var.get().strip(),
            "Overwrite": self.fa_overwrite_var.get(),
            "OpenCopy": self.fa_open_copy_var.get(),
            "AddTimestamp": self.fa_add_timestamp_var.get(),
        }
        try:
            self.fa_store.save(values)
        except RuntimeError as error:
            messagebox.showerror("保存失败", str(error))
            self.log(str(error))
            return
        self.log(f"已保存 FileAgent.ini：BackupDir={values['BackupDir'] or '(默认)'}；"
                 f"EnaPath={values['EnaPath'] or '(不通知)'}；"
                 f"Overwrite={values['Overwrite']}；OpenCopy={values['OpenCopy']}；"
                 f"AddTimestamp={values['AddTimestamp']}")

    def _browse_fileagent_backup_dir(self) -> None:
        current = self.fa_backup_dir_var.get().strip()
        initial = Path(current) if current and Path(current).is_dir() else Path(self.store.path).parent
        path = filedialog.askdirectory(title="选择备份存储目录", initialdir=str(initial))
        if path:
            self.fa_backup_dir_var.set(path)

    def _browse_fileagent_ena_path(self) -> None:
        current = self.fa_ena_path_var.get().strip()
        initial_dir = str(Path(current).parent) if current else str(Path(self.store.path).parent)
        path = filedialog.askopenfilename(
            title="选择备份成功后弹出通知的程序",
            initialdir=initial_dir,
            filetypes=[("可执行文件", "*.exe")])
        if path:
            self.fa_ena_path_var.set(path)

    # ---- 注册状态 ----
    def _refresh_registration(self) -> None:
        exe = fileagent_exe()
        if not exe.exists():
            self.reg_var.set(f"未找到 {exe}")
            self.reg_button.configure(text="注册", state=tk.DISABLED)
            self._displayed_registered = False
            return
        registered = is_registered()
        self._displayed_registered = registered
        self.reg_var.set("已注册（FileAgent 已安装）" if registered else "未注册（FileAgent 未安装）")
        self.reg_button.configure(text="卸载注册" if registered else "注册", state=tk.NORMAL)

    def _toggle_registration(self) -> None:
        exe = fileagent_exe()
        if not exe.exists():
            messagebox.showwarning("提示", f"未找到 FileAgent.exe：\n{exe}\n请将其放到 FAConfig.ini 所在目录。")
            return
        actual = is_registered()
        # 多个实例同时使用时，界面显示的状态可能已过期，
        # 此时只刷新状态，避免误执行相反的操作（例如本想注册却触发了卸载）。
        if actual != self._displayed_registered:
            self._refresh_registration()
            messagebox.showinfo("提示", "注册状态已被其他程序修改，已刷新为最新状态，请确认后重新操作。")
            return
        registered = actual
        ok = unregister_faconfig() if registered else register_faconfig()
        if not ok:
            messagebox.showerror("错误", "卸载注册失败。" if registered else "注册失败。")
            return
        self.log("已卸载注册。" if registered else "已注册 FAConfig。")
        self._refresh_registration()

    # ---- 加载 / 保存 ----
    def _load(self) -> None:
        self._close_cell_editor(False)
        try:
            self.aliases, self.enabled = self.store.load()
        except RuntimeError as error:
            messagebox.showerror("读取失败", str(error))
            return
        self.enabled_var.set(self.enabled)
        self._refresh_tree()
        self._update_status()

    def _refresh_tree(self, select_name: Optional[str] = None) -> None:
        self.tree.delete(*self.tree.get_children())
        for name, value in self.aliases:
            self.tree.insert("", tk.END, iid=name, values=(name, value))
        if select_name:
            if self.tree.exists(select_name):
                self.tree.selection_set(select_name)
                self.tree.focus(select_name)
                self.tree.see(select_name)

    def _persist(self) -> bool:
        try:
            self.store.save(self.aliases, bool(self.enabled_var.get()))
            self.enabled = bool(self.enabled_var.get())
            self._update_status()
            return True
        except (RuntimeError, OSError) as error:
            messagebox.showerror("保存失败", str(error))
            return False

    def _update_status(self) -> None:
        state = "已启用" if self.enabled_var.get() else "已禁用（仅 --enable 生效）"
        self.status_var.set(f"别名数量: {len(self.aliases)}    |    命令行: {state}")

    # ---- 别名操作 ----
    def current_aliases(self) -> list[tuple[str, str]]:
        result = []
        for iid in self.tree.get_children():
            values = self.tree.item(iid, "values")
            if not values:
                continue
            name = str(values[0]).strip()
            if not name:  # 新增但尚未填写完整的空行不写入配置
                continue
            result.append((name, str(values[1]) if len(values) > 1 else ""))
        return result

    def _on_tree_double_click(self, event) -> str:
        if self.tree.identify_region(event.x, event.y) != "cell":
            return ""
        iid = self.tree.identify_row(event.y)
        column = self.tree.identify_column(event.x)
        if not iid or not column:
            return ""
        try:
            column_index = int(column.lstrip("#")) - 1
        except ValueError:
            return ""
        if column_index not in (0, 1):
            return ""
        self.tree.selection_set(iid)
        self._open_cell_editor(iid, column_index)
        return "break"

    def _open_cell_editor(self, iid: str, column_index: int) -> None:
        self._close_cell_editor(False)
        self.tree.update_idletasks()
        bbox = self.tree.bbox(iid, f"#{column_index + 1}")
        if not bbox:
            return
        x, y, width, height = bbox
        values = self.tree.item(iid, "values")
        current = str(values[column_index]) if values and len(values) > column_index else ""
        editor = ttk.Entry(self.tree)
        editor.place(x=x, y=y, width=width, height=height)
        editor.insert(0, current)
        editor.select_range(0, tk.END)
        editor.focus_set()
        editor.bind("<Return>", lambda _event: self._commit_cell_editor())
        editor.bind("<Escape>", lambda _event: self._close_cell_editor(False))
        editor.bind("<FocusOut>", lambda _event: self._commit_cell_editor())
        self._editor = editor
        self._editor_iid = iid
        self._editor_column = column_index

    def _close_cell_editor(self, commit: bool = False) -> None:
        editor = self._editor
        if editor is None:
            return
        iid = self._editor_iid
        column_index = self._editor_column
        value = editor.get()
        self._editor = None
        self._editor_iid = None
        self._editor_column = None
        try:
            editor.destroy()
        except tk.TclError:
            pass
        if commit and iid and column_index is not None:
            self._apply_cell_value(iid, column_index, value)
        elif iid == self._pending_new_iid:
            self._remove_pending_row(iid)

    def _commit_cell_editor(self) -> None:
        if self._editor is None:
            return
        self._close_cell_editor(True)

    def _remove_pending_row(self, iid: Optional[str]) -> None:
        if iid != self._pending_new_iid:
            return
        if self.tree.exists(iid):
            self.tree.delete(iid)
        self._pending_new_iid = None

    def _apply_cell_value(self, iid: str, column_index: int, raw_value: str) -> None:
        value = raw_value.strip()
        values = self.tree.item(iid, "values")
        old = list(values) if values else ["", ""]
        is_pending = iid == self._pending_new_iid

        if column_index == 0:
            if not value:
                # 英文名为空：直接取消本次编辑
                self._remove_pending_row(iid)
                return
            if "=" in value or "\n" in value:
                messagebox.showwarning("提示", "英文名不能包含等号或换行。")
                return
            if any(other.lower() == value.lower()
                   for other in self.tree.get_children() if other != iid):
                messagebox.showwarning("提示", "其他别名已使用该英文名。")
                return
            new_name = value
            if iid != new_name:
                index = self.tree.index(iid)
                self.tree.delete(iid)
                self.tree.insert("", index, iid=new_name, values=(new_name, old[1]))
            if is_pending:
                # 新行填写完别名后，自动继续编辑路径
                self._pending_new_iid = new_name
                self.tree.selection_set(new_name)
                self.tree.see(new_name)
                self._open_cell_editor(new_name, 1)
                return
        else:
            # 路径允许留空，稍后可双击或使用“浏览...”填写
            new_name = old[0]
            self.tree.item(iid, values=(new_name, value))

        self.aliases = self.current_aliases()
        if not self._persist():
            self._load()
            return
        if is_pending:
            self._pending_new_iid = None
        self._refresh_tree(select_name=new_name)

    def _browse_selected_path(self) -> None:
        self._close_cell_editor(True)
        selection = self.tree.selection()
        if not selection:
            messagebox.showwarning("提示", "请先在表格中选择要设置路径的别名。")
            return
        target = selection[0]
        values = self.tree.item(target, "values")
        name = str(values[0]).strip() if values else ""
        if not name:
            messagebox.showwarning("提示", "请先填写英文名，再浏览选择路径。")
            return

        directory = filedialog.askdirectory(title="选择别名对应的路径")
        if not directory:
            return

        self.tree.item(target, values=(name, directory))
        self.aliases = self.current_aliases()
        if not self._persist():
            self._load()
            return
        if target == self._pending_new_iid:
            self._pending_new_iid = None
        self._refresh_tree(select_name=name)

    def _on_add(self) -> None:
        # 已有未编辑完的空行时，回到该行继续编辑
        if self._pending_new_iid and self.tree.exists(self._pending_new_iid):
            iid = self._pending_new_iid
            self.tree.selection_set(iid)
            self.tree.see(iid)
            self._open_cell_editor(iid, 0)
            return
        self._new_row_seq += 1
        iid = f"@new{self._new_row_seq}"
        self.tree.insert("", tk.END, iid=iid, values=("", ""))
        self.tree.selection_set(iid)
        self.tree.see(iid)
        self._pending_new_iid = iid
        self._open_cell_editor(iid, 0)

    def _on_delete(self) -> None:
        selection = self.tree.selection()
        if not selection:
            messagebox.showwarning("提示", "请先在表格中选择要删除的别名。")
            return
        target = selection[0]
        if target == self._pending_new_iid:
            self._remove_pending_row(target)
            return
        if not messagebox.askyesno("确认", f"确定删除别名“{target}”吗？"):
            return
        self.aliases = [(name, value) for name, value in self.current_aliases() if name != target]
        if not self._persist():
            return
        self._refresh_tree()

    def _on_toggle_enabled(self) -> None:
        self.aliases = self.current_aliases()
        if not self._persist():
            self.enabled_var.set(self.enabled)

    # ---- 路径浏览 ----
    def _browse_ci_exe(self) -> None:
        path = filedialog.askopenfilename(title="选择 ClassIsland.exe",
                                          filetypes=[("ClassIsland.exe", "ClassIsland.exe"), ("可执行文件", "*.exe")])
        if not path:
            return
        self.ci_exe_var.set(path)
        self._refresh_classisland()

    def _refresh_classisland(self) -> None:
        exe_text = self.ci_exe_var.get().strip()
        if not exe_text:
            self.automations_dir_var.set("")
            self.subject_count_var.set("科目: 未加载（请选择 ClassIsland.exe）")
            return
        exe = Path(exe_text)
        target = automations_dir_from_exe(exe)
        if target is None:
            self.automations_dir_var.set("")
            self.subject_count_var.set("科目: 未找到 data/Config，请确认 ClassIsland 目录完整")
            return
        self.automations_dir_var.set(str(target))
        subjects = load_subjects(exe)
        self.subject_count_var.set(f"科目: {len(subjects)} 个" if subjects else "科目: 未读取到（请确认 data/Profiles/Default.json 存在）")

    # ---- ClassIsland ----
    def _open_generate_dialog(self) -> None:
        self.aliases = self.current_aliases()
        if not self.aliases:
            messagebox.showinfo("提示", "别名表为空，请先添加别名。")
            return
        exe_text = self.ci_exe_var.get().strip()
        if not exe_text:
            messagebox.showwarning("提示", "请先选择 ClassIsland.exe。")
            return
        if not self.automations_dir_var.get().strip():
            self._refresh_classisland()
        subjects = load_subjects(Path(exe_text))
        if not subjects:
            if not messagebox.askyesno("提示", "未读取到科目列表，仍要继续吗？\n（没有科目将无法生成自动化）"):
                return
        GenerateDialog(self, self.aliases, subjects)

    def _open_classisland(self) -> None:
        exe_text = self.ci_exe_var.get().strip()
        if not exe_text:
            messagebox.showwarning("提示", "请先选择 ClassIsland.exe。")
            return
        exe = Path(exe_text)
        if not exe.exists():
            messagebox.showwarning("提示", f"找不到 ClassIsland.exe：\n{exe}")
            return
        try:
            subprocess.Popen([str(exe)], cwd=str(exe.parent), close_fds=True)
        except OSError as error:
            messagebox.showerror("启动失败", f"无法启动 ClassIsland：{error}")
            return
        self.log(f"已启动 ClassIsland: {exe}")

    def _close_classisland(self) -> None:
        # taskkill 需要等待目标进程退出，放到后台线程执行，避免阻塞界面
        self.log("正在强制关闭 ClassIsland...")
        threading.Thread(target=self._close_classisland_worker, daemon=True).start()

    def _close_classisland_worker(self) -> None:
        try:
            result = subprocess.run(
                ["taskkill", "/F", "/IM", "ClassIsland.Desktop.exe"],
                capture_output=True, text=True, timeout=10,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            message = f"强制关闭 ClassIsland 失败：{error}"
        else:
            if result.returncode == 0:
                message = "已强制结束进程: ClassIsland.Desktop.exe"
            else:
                message = "未发现正在运行的 ClassIsland.Desktop.exe 进程"
        # 窗口可能在后台线程执行期间被关闭，此时 after 会抛异常
        try:
            self.after(0, lambda: self.log(message))
        except (tk.TclError, RuntimeError):
            pass

    def _import_json_file(self) -> None:
        target_dir = self.automations_dir_var.get().strip()
        if not target_dir:
            self._refresh_classisland()
            target_dir = self.automations_dir_var.get().strip()
        if not target_dir:
            messagebox.showwarning("提示", "请先选择 ClassIsland.exe，以定位 Automations 目录。")
            return
        path = filedialog.askopenfilename(title="选择要导入的自动化 JSON",
                                          filetypes=[("JSON", "*.json"), ("所有文件", "*.*")])
        if not path:
            return
        source = Path(path)
        try:
            with source.open("r", encoding="utf-8-sig") as f:
                items = json.load(f)
        except (OSError, ValueError) as error:
            messagebox.showerror("导入失败", f"无法解析 JSON：{error}")
            return
        if not isinstance(items, list):
            messagebox.showerror("导入失败", "JSON 顶层必须是自动化数组。")
            return
        target = Path(target_dir) / "FileAgent.json"
        if not messagebox.askyesno("确认", f"将覆盖：\n{target}\n\n原文件会自动备份，是否继续？"):
            return
        try:
            backup = write_automations(target, items)
        except OSError as error:
            messagebox.showerror("导入失败", f"无法写入：\n{target}\n\n{error}")
            return
        self.log(f"已导入 {len(items)} 条自动化: {target}")
        if backup:
            self.log(f"原文件已备份: {backup.name}")
        messagebox.showinfo("完成", f"已导入到：\n{target}")

    # ---- 日志与设置 ----
    def log(self, message: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.insert(tk.END, f"[{stamp}] {message}\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def save_subject_mapping(self, mapping: dict[str, str]) -> None:
        subject_names = {}
        subjects = load_subjects(Path(self.ci_exe_var.get().strip())) if self.ci_exe_var.get().strip() else []
        for alias, subject_id in mapping.items():
            for name, sid in subjects:
                if sid == subject_id:
                    subject_names[alias] = name
                    break
        self.settings["subject_map"] = subject_names
        self.settings["ci_exe"] = self.ci_exe_var.get().strip()
        # 立即落盘，避免程序异常退出时丢失映射
        save_settings(self.settings)

    def _on_close(self) -> None:
        self._close_cell_editor(False)
        self.settings["ci_exe"] = self.ci_exe_var.get().strip()
        save_settings(self.settings)
        self.destroy()


def main() -> int:
    # 必须在创建任何窗口（包括 Tk 根窗口）之前启用 DPI 感知
    set_dpi_awareness()
    try:
        app = App()
    except Exception as error:  # 兜底提示，避免静默退出
        try:
            messagebox.showerror("启动失败", f"程序启动失败：{error}")
        except Exception:
            print(f"启动失败：{error}", file=sys.stderr)
        return 1
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())