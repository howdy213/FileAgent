# 安装与卸载

## 安装

以管理员权限（或当前用户）运行：

```cmd
FileAgent.exe install
```

程序将：

1. 读取配置（`FileAgent.ini`），若不存在则创建默认配置。
2. 为 `Extensions` 列表中每个扩展名修改当前用户的文件关联，指向 `FileAgent.exe`。
3. 在注册表 `HKCU\Software\FileAgent` 中标记已安装，并存储原始命令。

## 卸载

```cmd
FileAgent.exe uninstall
```

程序将：

1. 恢复所有被修改的扩展名的原始打开命令（若备份了原始命令）。
2. 删除注册表中的安装标记和存储的 ProgID。

## 无命令参数

直接运行 `FileAgent.exe "文件路径"`（或双击已关联的文件）会执行备份并打开文件。
