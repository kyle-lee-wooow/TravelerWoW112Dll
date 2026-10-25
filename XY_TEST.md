# XYZ 坐标写入测试

默认构建关闭。测试版用 MSBuild 属性 `ExperimentalPositionWrite=true`，
并指定单独 OutDir（当前产物在 Release/xy-test）。普通 Release 不包含写入代码。
源码尚未提交或推送；这不是单独的正式版功能。

退出游戏后，用 xy-test 中的 EXE 和相邻 DLL 重新注入。
移动工具页勾选“启用 XYZ 写入测试”，选择“仅当前角色”，填写 X、Y、Z，
点击“确认写入 XYZ”，核对数值后确认。每次打开界面默认未勾选。

管道（仅测试构建支持）：

```cmd
python heal_pipe_client.py PID "testpos confirm -1320.79 1150.03 127.646"
```

请求经现有主线程派发，解析有限 float，只向当前角色对象 +0x9B8/+0x9BC/+0x9C0
写入三个 float。兼容旧的 testpos confirm X Y 请求，省略 Z 时只写 X、Y，保持 Z 不变。
不移动到其他地图、不修改服务端。
路线播放或钓鱼运行时拒绝写入。ok 表示本进程内存写入完成，未证明服务器接收。
用 healstate 的 player.x/y/z 检查结果；实际摆位效果待游戏实测。
普通构建收到命令会返回 error: test position write disabled in this build。
