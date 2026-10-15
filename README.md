# TravelerWoW112Dll

## 统一仓库

此目录统一管理 DLL、注入器、Python 客户端、测试和说明。
打开 `TravelerWoW112Dll.sln`，选择 **Release / x86**，生成解决方案。
DLL 工程在 `TravelerWoW112Dll/`，注入器工程在 `WowDllInjectTest/`。
两个产物均输出到根目录 `Release/`，注入器直接加载旁边的 DLL。
以后修改本目录内的注入器源码；旧独立目录保留作备份，不再作为维护入口。

Git 应提交 `.sln`、`.vcxproj`、源代码、头文件、Python 和 Markdown。
`.gitignore` 已排除 VS 缓存、编译产物、测试日志和运行时路线数据。
当前功能及代码结构见 `CURRENT_STATUS.md`，管道协议见 `HEAL_PIPE.md`。

32 位 WoW 1.12.1 进程内 DLL。现有 GUID、角色名和自动拾取命名管道保持不变，并增加了基于本地玩家对象坐标的路线录制功能。

## 路线控制

DLL 不注册全局热键。注入成功后，`WowDllInjectTest.exe` 会隐藏控制台并显示路线控制窗口。输入路线名称后使用以下按钮：

```text
开始录制  开始录制并使用输入框中的路线名称
停止      停止录制或移动；停止录制时自动保存
保存      保存当前路线
播放路线  加载并播放输入框中的路线
路线列表  显示所有已保存路线
当前状态  显示当前路线、录制和播放状态
退出界面  关闭控制窗口，但不卸载 DLL
```

路线名称只允许 1-64 个英文字母、数字、下划线或连字符。

DLL 每 200 毫秒读取一次本地玩家对象，并在距离上一个路点至少 1 码时记录：

```text
player + 0x09B8 -> X
player + 0x09BC -> Y
player + 0x09C0 -> Z
```

路线保存在 DLL 所在目录的 `routes` 子目录：

```text
routes\<name>.csv
```

CSV 格式：

```csv
elapsed_ms,x,y,z
0,-8913.42,554.18,93.79
1350,-8911.83,556.02,93.75
```

## 构建

Visual Studio 2022：选择 `Release | x86`。输出文件为：

```text
Release\TravelerWoW112Dll.dll
```

## 路线播放

`play <name>` 使用原版 WoW `1.12.1.5875` 的内部函数：

```text
CGPlayer_C::ClickToMove = 0x00611130
Move = 0x04
Stop/None = 0x0D
```

调用签名为：

```cpp
__thiscall(player, clickType, interactGuid*, position*, precision)
```

移动请求通过 WoW 窗口线程 Hook 派发到游戏主线程。播放器在距离当前路点 1.5 码以内时进入下一点，并每秒重发当前目标以处理短暂停顿。调用前会检查 `0x00611130` 是否处于可执行内存。

该地址来自多个公开的原版 1.12.1 偏移项目，并与本项目已验证的 `EnumVisibleObjects` 和 `RightClickUnit` 地址属于同一套客户端偏移。乌龟服客户端仍可能修改函数内容，首次测试应使用短路线，并在控制台输入 `halt` 或 `stop` 停止。
