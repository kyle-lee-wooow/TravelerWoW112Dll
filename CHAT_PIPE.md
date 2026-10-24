# 游戏聊天读取

聊天监听使用客户端 Lua 5.0 的 CHAT_MSG 事件，与游戏聊天窗口共享消息来源。
不扫描屏幕，不自动回复，不在 healstate 中塞入聊天内容，也不默认写聊天日志到文件。

## 启动

使用 Release/x86 编译出的新 DLL，重启游戏并重新注入。
主线程定时任务会在进入游戏后约一秒内安装监听；显式请求 `chat start`
或 `chat poll 0` 也会初始化。只能收到安装监听之后客户端实际接收到的消息。

监听类型：SAY、YELL、WHISPER、WHISPER_INFORM、PARTY、RAID、RAID_LEADER、
RAID_WARNING、GUILD、OFFICER、CHANNEL、SYSTEM、EMOTE、TEXT_EMOTE。
没有权限接收的聊天不会凭空出现。战斗日志未包含。

## 管道请求

继续使用 `\\.\pipe\TravelerRouteControl_PID_<PID>`。

```text
chat start
chat poll 0
chat poll 101
```

start 成功返回 `ok`；poll 返回 JSON：

```json
{
  "messages": [
    {"seq":101,"type":"WHISPER","sender":"逛街牧师","text":"给我加血","channel":"","channel_id":0,"time":1234.5}
  ],
  "next_seq":101,
  "latest_seq":101,
  "oldest_seq":90,
  "dropped":0,
  "reset":false,
  "session":"1629"
}
```

- `seq`：该客户端 Lua 监听器的递增序号，下一次用返回的 next_seq。
- `time`：客户端 GetTime() 秒数，不是 Unix 时间。
- `channel` / `channel_id`：旧版事件 arg9（无则 arg4）/ arg8，非频道消息可能为空/0；具体频道参数需客户端实测。
- `session`：当前角色 GUID 字符串。角色变化时清空旧消息，序号保持递增。
- `dropped`：游标与仍保留的最早消息之间的缺口，可能来自队列覆盖或进出世界清理。
- `reset`：请求游标大于监听器当前序号，通常表示客户端重启，按游标 0 重新返回现存消息。

队列最多 200 条。每次最多返回 20 条且消息 JSON 总长度不超过约 48KB。
`next_seq < latest_seq` 时可立即继续翻页，读空后建议每 500ms 查询一次。
每条内容最多 2048 UTF-8 字节、发送者/频道最多 256 字节；截断不拆分 UTF-8 字符。
保留游戏原始颜色和物品链接标记。进入世界会清空队列，不能据此恢复历史聊天。
读取不删除消息，各调用方维护自己的游标；不要同时向同一 PID 并发发管道命令。

## Python

直接看聊天：

```powershell
python watch_chat.py 12345
```

单次查看：

```powershell
python heal_pipe_client.py 12345 "chat poll 0"
```

接口：

```python
from heal_pipe_client import chat_poll
page = chat_poll(12345, 0)
for message in page['messages']:
    print(message['type'], message['sender'], message['text'])
cursor = page['next_seq']
```

## 验证

开发检查：`python -m unittest test_chat_observer`（需要 lupa）。
这些检查验证 Lua 队列、中文/JSON 转义、截断、覆盖和分页，不等于游戏内验证。
游戏中分别发送中文说话、密语、小队消息和带物品链接的频道消息，
运行 watch_chat.py 与聊天窗口逐条对比。切换角色后确认不再返回旧角色消息。
