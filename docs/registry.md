# 注册表操作细节

- 安装时，优先读取 `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\<ext>\UserChoice` 中的 `Progid`，若存在则修改该 ProgID 的 `shell\open\command`。
- 若无 UserChoice，则依次查找 `HKCU\Classes\<ext>`、`HKCR\<ext>`、以及 `OpenWithProgids` 中的 ProgID。
- 修改前会将当前命令备份到 `OriginalCommand` 值中，卸载时恢复。
- 若没有 ProgID，则直接修改 `HKCU\Classes\<ext>\shell\open\command`。
- 所有修改仅限于 `HKEY_CURRENT_USER`，无需管理员权限（除非程序需要写入自身目录等）。
- 安装标记保存在 `HKCU\Software\FileAgent`，`FAConfig.exe --status` 通过 `FileAgent.exe` 查询该标记。
