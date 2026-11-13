# 配置文件

`FileAgent.ini` 位于程序同目录下，首次运行自动创建。以下是一个完整的配置示例：

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

; 备份成功后弹出浮窗通知的程序（可执行文件，留空则不通知；示例为 ENAnalyzer）
EnaPath = D:\Apps\ENAnalyzer\ENAnalyzer.exe
```

## 配置项说明

| 键名           | 类型       | 说明                                                         |
| -------------- | ---------- | ------------------------------------------------------------ |
| `BackupRoot`   | 路径列表   | 分号或逗号分隔的目录路径。若文件位于其中任意目录（含子目录）下，则跳过备份直接打开原件。可用于排除云同步、临时目录等。 |
| `BackupDir`    | 路径       | 备份文件存放目录。支持相对路径（相对于程序目录）或绝对路径。 |
| `OpenCopy`     | bool       | `true` 表示打开备份副本，`false` 则打开原文件（但备份仍会创建）。 |
| `AddTimestamp` | bool       | 备份文件名后追加 `_YYYYMMDD_HHMMSS` 时间戳，防止重名覆盖。   |
| `Overwrite`    | bool       | 当目标备份文件已存在时是否覆盖：`true` 覆盖，`false` 跳过备份（保留已有备份），此时即使 `OpenCopy=true` 也会回退打开原文件。 |
| `Extensions`   | 字符串列表 | 需要监控的扩展名，以逗号分隔。缺省为常见 Office 和 PDF 扩展名。 |
| `EnaPath`      | 路径       | 备份成功且源文件确有更新时，以 `-toast "已备份到: <最后一级文件夹>\<备份文件名>"` 启动该 exe 弹出浮窗通知。仅接受 `.exe`，相对路径相对于程序目录；留空则不通知。 |

## 通过界面修改

- `FAConfig.exe` 无参数启动后，可在界面下方的“FileAgent.ini 设置”区域直接编辑上述配置，详见 [FAConfig](faconfig.md)。
- `tools/FAConfig.pyw` 的“FileAgent.ini”标签页提供同样的编辑能力，详见 [Python 配置编辑器](faconfig-py.md)。
- 两种方式写入后，FileAgent 在下次启动时读取新配置。

> 编码约定：FileAgent 以 Windows ANSI 代码页读取 ini，请勿保存为 UTF-8，否则中文路径会失效。
