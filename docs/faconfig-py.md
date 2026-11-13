# Python 配置编辑器

`tools/FAConfig.pyw` 是图形化配置工具，可编辑 `FAConfig.ini`、`FileAgent.ini`，并对接 ClassIsland 生成课表自动化。所有自身数据都保存在脚本所在目录：

- `FAConfig.ini`：自动读取/创建脚本目录下的文件，`FAConfig.exe` 与 `FileAgent.exe` 也按同目录推导。
- `FileAgent.ini`：在“FileAgent.ini”标签页中读取/保存。
- `settings.json`：保存已选择的 `ClassIsland.exe` 与科目映射等设置。

## 界面结构

界面分为三个标签页：

- **别名**：新增、删除、双击编辑别名；路径可留空或通过“浏览...”选择文件夹；勾选“启用命令行参数”直接写入 `FAConfig.ini` 的 `Enabled`。
- **ClassIsland**：选择 `ClassIsland.exe` 后自动定位 `data\Config\Automations`，并从 `data\Profiles\Default.json` 读取科目；“生成并导入...”为每个别名生成自动化并写入 `Automations\FileAgent.json`（存在时自动备份），也可导入 JSON、“打开 ClassIsland”“强制关闭 ClassIsland”。
- **FileAgent.ini**：直接编辑 `BackupDir`、`BackupRoot`、`Extensions`、`EnaPath` 与 `Overwrite`、`OpenCopy`、`AddTimestamp`，保存后 FileAgent 下次启动生效。`EnaPath` 可通过“浏览...”选择 exe，备份成功且文件确有更新时会以 `-toast` 弹出浮窗通知。

顶部“路径设置”显示 `FAConfig.ini` 位置与 FileAgent 注册状态，可手动刷新或注册/卸载注册（委托同目录 `FileAgent.exe`，退出码 0 表示已注册）。

## ClassIsland 自动化

- 自动定位规则：从 `ClassIsland.exe` 所在目录向上查找包含 `data/Config` 的根目录，自动化输出到 `data\Config\Automations\FileAgent.json`。
- 每条自动化的动作为执行 `FAConfig.exe --subject <别名>`，实现课表切换科目时同步切换备份目录。
- 可选择同时生成“预备”自动化。
- 导入前会备份已有的 `FileAgent.json`（带时间戳）。
- “强制关闭 ClassIsland”在后台线程执行，不会阻塞界面。

> 注意：脚本与 FAConfig 均以 Windows ANSI 编码读写 ini，不使用 UTF-8；FileAgent 无法识别 UTF-8 保存的路径。
