# FAConfig

`FAConfig.exe` 与 `FileAgent.exe` 放在同一目录，使用同目录的 `FAConfig.ini` 保存别名，并在需要时修改同目录的 `FileAgent.ini`。

## 无参数启动

直接运行 `FAConfig.exe` 会打开配置界面：

- 表格展示“英文名 → 路径”，支持新增、删除、双击单元格直接编辑。
- “浏览...”按钮可选择文件夹填入路径；路径允许留空，稍后填写。
- “启用命令行参数”复选框控制命令行调用是否生效。
- 界面下方的“FileAgent.ini 设置”可直接修改 `FileAgent.ini`：
  - 备份目录 `BackupDir`（可通过“浏览...”选择文件夹）；
  - 跳过根目录 `BackupRoot`（多个路径用分号或逗号分隔，留空表示不跳过）；
  - 扩展名 `Extensions`；
  - 通知程序 `EnaPath`（可通过“浏览...”选择 exe，备份成功且文件有更新时以 `-toast` 弹出浮窗通知，留空则不通知）；
  - 覆盖已有备份 `Overwrite`、打开副本 `OpenCopy`、文件名添加时间戳 `AddTimestamp`；
  - 修改后点击“保存”写入 `FileAgent.ini`，点击“重新加载”从文件重新读取；保存结果会显示在状态栏。
- 状态栏依次显示：别名数量、命令行启用状态、FileAgent 注册状态、最近一次 FileAgent.ini 操作结果。
- “注册 / 卸载注册”按钮通过调用同目录的 `FileAgent.exe install/uninstall` 完成，并实时显示当前注册状态。

## 命令行调用

```cmd
FAConfig.exe --subject Chinese
```

- `--subject` 会先查找 `FAConfig.ini` 中的别名（不区分大小写），将别名对应的路径写入 `FileAgent.ini` 的 `BackupDir`；若未匹配到别名，则把参数原样写入。
- 除 `--subject` 外，还支持 `--BackupRoot`、`--BackupDir`、`--Extensions`、`--Overwrite`、`--OpenCopy`、`--AddTimestamp`（大小写不敏感），也支持 `--键=值` 形式。
- 多个命令可写在同一行，例如：

```cmd
FAConfig.exe --extensions .doc,.docx --opencopy true
```

## 启用 / 禁用

```cmd
FAConfig.exe --disable
FAConfig.exe --enable
```

- `--disable` 会写入 `[Settings] Enabled = false`，此后所有非 `--enable` 命令都不再生效。

  > 主要应用场景：当日课表变化较大时，可以暂时 disable 掉，然后临时手动分类，避免课件由于 ClassIsland 课表未手动更新而导致的混乱。

- `--enable` 恢复命令行生效；`--disable` 与 `--enable` 本身始终可用。

## 注册管理

```cmd
FAConfig.exe --register       :: 注册（调用 FileAgent.exe install）
FAConfig.exe --unregister     :: 卸载注册（调用 FileAgent.exe uninstall）
FAConfig.exe --status         :: 查询注册状态，退出码 0=已注册，1=未注册
```

注册管理与启用状态无关，始终可用。注册信息与 `FileAgent.exe` 共用，位于 `HKCU\Software\FileAgent`。

## FAConfig.ini

```ini
[Settings]
Enabled = true

[Aliases]
Chinese = D:\Material\Chinese
Math = D:\Material\Math
```

- `Enabled`：命令行是否生效。
- `[Aliases]`：英文名到路径的映射，路径可留空。
- 文件以 ANSI 编码读写，避免中文路径失效。
