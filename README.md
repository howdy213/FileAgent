# FileAgent

一个 Windows 实用工具，在用户双击打开特定类型文件时自动创建备份副本，并可选打开备份副本而非原文件。通过注册表接管文件关联，支持安装/卸载，配置文件灵活可调。附带 `FAConfig.exe`，用于通过别名快速切换配置，便于外部程序或课表工具调用。

---

## 功能特性

- **自动备份**：当打开指定扩展名的文件时，先将文件复制到备份目录（默认 `程序目录\AutoSaveBackup`）。
- **智能跳过**：支持配置**多个**跳过备份的根目录（例如云同步目录、系统目录等），也自动跳过备份存储目录本身，防止递归备份。
- **时间戳备份**：备份文件名可附带时间戳（`_YYYYMMDD_HHMMSS`），避免覆盖（默认开启）。
- **灵活覆盖策略**：通过 `Overwrite` 配置项控制当备份文件已存在时是否覆盖（默认覆盖），可设定为 `false` 以跳过已存在的备份，保留历史版本。
- **打开副本模式**：设置 `OpenCopy = true` 后，双击文件会直接打开备份副本，原文件保持未修改。
- **关联接管**：通过修改当前用户注册表（`HKCU`）接管文件打开命令，不影响原始程序，卸载时可完全恢复。
- **配置灵活**：通过 INI 文件控制备份路径、跳过目录、扩展名列表等，无需重新编译。
- **别名切换**：`FAConfig.exe --subject <别名>` 可将别名解析为路径并写入 `FileAgent.ini`，便于外部程序调用。
- **日志记录**：程序目录下生成 `FileAgent.log`，便于排查问题。

## 安装与卸载

### 安装

以管理员权限（或当前用户）运行：

```cmd
FileAgent.exe install
```

程序将：

1. 读取配置（`FileAgent.ini`），若不存在则创建默认配置。
2. 为 `Extensions` 列表中每个扩展名修改当前用户的文件关联，指向 `FileAgent.exe`。
3. 在注册表 `HKCU\Software\FileAgent` 中标记已安装，并存储原始命令。

### 卸载

```cmd
FileAgent.exe uninstall
```

程序将：

1. 恢复所有被修改的扩展名的原始打开命令（若备份了原始命令）。
2. 删除注册表中的安装标记和存储的 ProgID。

### 无命令参数

直接运行 `FileAgent.exe "文件路径"`（或双击已关联的文件）会执行备份并打开文件。

## 配置文件

位于程序同目录下，首次运行自动创建。以下是一个完整的配置示例：

```ini
[Settings]
; 跳过备份的根目录，多个路径用分号(;)或逗号(,)分隔，留空则不跳过
BackupRoot = C:\;D:\Data;E:\CloudDrive

; 备份存储目录（相对或绝对路径）
; 相对路径请以“.\”开头，否则将会在打开的文档目录建立文件夹
BackupDir = .\AutoSaveBackup

; 是否打开备份副本（true/false）
OpenCopy = false

; 备份文件名是否添加时间戳（true/false）
AddTimestamp = true

; 当备份文件已存在时是否覆盖（true=覆盖，false=跳过，此时若 OpenCopy=true 则回退打开原文件）
Overwrite = true

; 要处理的扩展名列表，逗号分隔（可带点或不带点）
Extensions = .doc,.docx,.ppt,.pptx,.xls,.xlsx,.pdf
```

### 配置项说明

| 键名           | 类型       | 说明                                                         |
| -------------- | ---------- | ------------------------------------------------------------ |
| `BackupRoot`   | 路径列表   | 分号或逗号分隔的目录路径。若文件位于其中任意目录（含子目录）下，则跳过备份直接打开原件。可用于排除云同步、临时目录等。 |
| `BackupDir`    | 路径       | 备份文件存放目录。支持相对路径（相对于程序目录）或绝对路径。 |
| `OpenCopy`     | bool       | `true` 表示打开备份副本，`false` 则打开原文件（但备份仍会创建）。 |
| `AddTimestamp` | bool       | 备份文件名后追加 `_YYYYMMDD_HHMMSS` 时间戳，防止重名覆盖。   |
| `Overwrite`    | bool       | 当目标备份文件已存在时是否覆盖：`true` 覆盖，`false` 跳过备份（保留已有备份），此时即使 `OpenCopy=true` 也会回退打开原文件。 |
| `Extensions`   | 字符串列表 | 需要监控的扩展名，以逗号分隔。缺省为常见 Office 和 PDF 扩展名。 |

## FAConfig

`FAConfig.exe` 与 `FileAgent.exe` 放在同一目录，使用同目录的 `FAConfig.ini` 保存别名，并在需要时修改同目录的 `FileAgent.ini`。

### 无参数启动

直接运行 `FAConfig.exe` 会打开配置界面：

- 表格展示“英文名 → 路径”，支持新增、删除、双击单元格直接编辑。
- “浏览...”按钮可选择文件夹填入路径；路径允许留空，稍后填写。
- “启用命令行参数”复选框控制命令行调用是否生效。

### 命令行调用

```cmd
FAConfig.exe --subject Chinese
```

- `--subject` 会先查找 `FAConfig.ini` 中的别名（不区分大小写），将别名对应的路径写入 `FileAgent.ini` 的 `BackupDir`；若未匹配到别名，则把参数原样写入。
- 除 `--subject` 外，还支持 `--BackupRoot`、`--BackupDir`、`--Extensions`、`--Overwrite`、`--OpenCopy`、`--AddTimestamp`（大小写不敏感），也支持 `--键=值` 形式。
- 多个命令可写在同一行，例如：

```cmd
FAConfig.exe --extensions .doc,.docx --opencopy true
```

### 启用 / 禁用

```cmd
FAConfig.exe --disable
FAConfig.exe --enable
```

- `--disable` 会写入 `[Settings] Enabled = false`，此后所有非 `--enable` 命令都不再生效。
- `--enable` 恢复命令行生效；`--disable` 与 `--enable` 本身始终可用。

### FAConfig.ini

```ini
[Settings]
Enabled = true

[Aliases]
Chinese = D:\Material\Chinese
Math = D:\Material\Math
```

## Python 配置编辑器

`tools/fa_config_editor.py` 是 `FAConfig.ini` 的图形化编辑工具，并对接 ClassIsland 生成课表自动化。所有自身数据都保存在脚本所在目录：

- `FAConfig.ini`：自动读取/创建脚本目录下的文件，`FAConfig.exe` 也按同目录推导。
- `settings.json`：保存已选择的 `ClassIsland.exe` 与科目映射等设置。

主要功能：

- 表格内直接新增、删除、双击编辑别名；路径可留空或通过“浏览...”选择文件夹。
- 勾选“启用命令行参数”直接写入 `FAConfig.ini` 的 `Enabled`。
- 选择 `ClassIsland.exe` 后自动定位 `data\Config\Automations`，并从 `data\Profiles\Default.json` 读取科目。
- “生成并导入...”为每个别名生成一条“当前科目变化 → 执行 `FAConfig.exe --subject <别名>`”的自动化，写入 `Automations\FileAgent.json`（存在时自动备份），并可选择保留其他已有自动化。
- 支持导出/导入 JSON，以及“打开 ClassIsland”“强制关闭 ClassIsland”。

## 注册表操作细节

- 安装时，优先读取 `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\<ext>\UserChoice` 中的 `Progid`，若存在则修改该 ProgID 的 `shell\open\command`。
- 若无 UserChoice，则依次查找 `HKCU\Classes\<ext>`、`HKCR\<ext>`、以及 `OpenWithProgids` 中的 ProgID。
- 修改前会将当前命令备份到 `OriginalCommand` 值中，卸载时恢复。
- 若没有 ProgID，则直接修改 `HKCU\Classes\<ext>\shell\open\command`。
- 所有修改仅限于 `HKEY_CURRENT_USER`，无需管理员权限（除非程序需要写入自身目录等）。

## 日志

日志文件 `FileAgent.log` 与程序同目录，记录：

- 启动、配置加载详情。
- 安装/卸载操作步骤。
- 每次文件打开时的备份结果（成功/失败，以及是否因 `Overwrite=false` 而跳过）。
- 错误和警告信息（如注册表访问失败、路径解析异常）。

## 注意事项

1. **安装后立即生效**：安装完成会触发系统关联刷新，无需重启 Explorer。
2. **备份目录权限**：确保程序有写入 `BackupDir` 的权限。
3. **多路径支持**：`BackupRoot` 支持多个目录，推荐使用分号分隔（路径本身不含分号），也可使用逗号。
4. **卸载不删除备份文件**：卸载仅恢复关联，已备份的文件需要手动清理。
5. **多用户环境**：由于操作的是 `HKCU`，每个用户需独立安装，互不影响。

## 许可证

本项目采用 **GNU General Public License v3.0**。详见 [LICENSE](LICENSE)。