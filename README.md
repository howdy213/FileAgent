# FileAgent

一个 Windows 实用工具，在用户双击打开特定类型文件时自动创建备份副本，并可选打开备份副本而非原文件。通过注册表接管文件关联，支持安装/卸载，配置文件灵活可调。附带 `FAConfig.exe`，用于通过别名快速切换配置，便于外部程序或课表工具调用。

## 功能特性

- **自动备份**：当打开指定扩展名的文件时，先将文件复制到备份目录（默认 `程序目录\AutoSaveBackup`）。
- **智能跳过**：支持配置**多个**跳过备份的根目录（例如云同步目录、系统目录等），也自动跳过备份存储目录本身，防止递归备份。
- **时间戳备份**：备份文件名可附带时间戳（`_YYYYMMDD_HHMMSS`），避免覆盖（默认开启）。
- **灵活覆盖策略**：通过 `Overwrite` 配置项控制当备份文件已存在时是否覆盖（默认覆盖），可设定为 `false` 以跳过已存在的备份，保留历史版本。
- **打开副本模式**：设置 `OpenCopy = true` 后，双击文件会直接打开备份副本，原文件保持未修改。
- **关联接管**：通过修改当前用户注册表（`HKCU`）接管文件打开命令，不影响原始程序，卸载时可完全恢复。
- **配置灵活**：通过 INI 文件控制备份路径、跳过目录、扩展名列表等，无需重新编译。
- **别名切换**：`FAConfig.exe --subject <别名>` 可将别名解析为路径并写入 `FileAgent.ini`，便于外部程序调用。
- **可视化配置**：`FAConfig.exe` 与 `tools/FAConfig.pyw` 均可直接编辑别名与 `FileAgent.ini` 设置。
- **日志记录**：程序目录下生成 `FileAgent.log`，便于排查问题。
- **备份通知**：可通过 `EnaPath` 指定外部 exe（如 [ENAnalyzer](https://github.com/howdy213/ENAnalyzer)），备份成功且文件确有更新时弹出浮窗提示备份位置。

## 快速开始

```cmd
:: 安装文件关联
FileAgent.exe install

:: 通过别名切换备份目录
FAConfig.exe --subject Chinese
```

## 文档

| 文档 | 内容 |
| ---- | ---- |
| [安装与卸载](docs/usage.md) | `install` / `uninstall` 的行为与无参数运行说明 |
| [配置文件](docs/configuration.md) | `FileAgent.ini` 示例与各配置项含义 |
| [FAConfig](docs/faconfig.md) | 原生配置界面、命令行参数、启用/禁用、注册管理 |
| [Python 配置编辑器](docs/faconfig-py.md) | `tools/FAConfig.pyw` 的三个标签页与 ClassIsland 自动化 |
| [注册表操作细节](docs/registry.md) | 文件关联的接管与还原逻辑 |
| [日志](docs/logging.md) | `FileAgent.log` 记录内容 |

## 注意事项

1. **安装后立即生效**：安装完成会触发系统关联刷新，无需重启 Explorer。
2. **备份目录权限**：确保程序有写入 `BackupDir` 的权限。
3. **多路径支持**：`BackupRoot` 支持多个目录，推荐使用分号分隔（路径本身不含分号），也可使用逗号。
4. **卸载不删除备份文件**：卸载仅恢复关联，已备份的文件需要手动清理。
5. **多用户环境**：由于操作的是 `HKCU`，每个用户需独立安装，互不影响。
6. **编码**：`FileAgent.ini`、`FAConfig.ini` 必须以 ANSI 编码保存，UTF-8 会导致中文路径无法识别。

## 许可证

本项目采用 **GNU General Public License v3.0**。详见 [LICENSE](LICENSE)。
