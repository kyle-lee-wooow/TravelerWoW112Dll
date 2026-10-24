# 2026-10-24 状态扩展与 LoS 估算

已编写并编译为 Release/x86。仍需重启游戏、注入新 DLL 后实测。
管道名称、healstate 请求以及现有 cast 调用方式不变。

## 1. 视线估算

`player`、`raid[]`、`pets[]` 始终包含 `los_estimated`：

- `true`：从自己位置到目标位置的场景射线没有检测到阻挡。
- `false`：检测到阻挡。
- `null`：对象/坐标不可用、客户端签名不匹配或调用异常，不能判断。

使用本地客户端 `0x6AA160` 碰撞函数，在游戏主线程调用；两端 Z 加 2，
碰撞掩码 `0x100111`。读取当前快照位置，不使用 1 秒光环缓存。
异常后本进程停用射线调用，后续返回 null，重启游戏才重试。
根字段 `los_method` 标明算法。自身坐标有效时自身视线为 true。

这是几何估算，不是客户端完整的施法可用性判定，更不是服务端确认。
身高、坡道、栏杆、门、运输工具、特殊模型都可能造成误判。
DLL 不因此拒绝 cast；Python 可优先选择 true，false 可尝试换位，
null 必须视为未知，不能当成 false。实际施法失败仍以 last_cast 为准。

## 2. 控制状态

`player`、`raid[]`、`pets[]` 新增：

| 字段 | 含义 |
|---|---|
| in_combat | 战斗标志（根级原字段保留） |
| silenced | 沉默 |
| stunned | 昏迷 |
| fleeing | 恐惧/逃跑标志 |
| confused | 迷惑标志 |
| movement_disabled | 禁止移动标志 |
| crowd_controlled | stunned / fleeing / confused 任一成立 |

读取失败返回 null。crowd_controlled 不是所有控制效果的完整集合；
movement_disabled 不必然表示不能施法。业务判断仍由 Python 完成。

## 3. 移动和拾取反馈

`player` 新增：

- `map_id`：当前地图编号。
- `movement_flags`：原始移动标志，和按位置变化采样的 moving 含义不同。
- `ctm_action`：客户端 CTM 动作编号。
- `ctm_active`：动作不为 0、12（空闲）、13（停止）；不代表人物一定产生位移。
- `ctm_destination`：仅动作 4（移动）时返回 `{x,y,z}`，其他为 null，避免暴露陈旧目的地。
- `current_loot_guid`：当前拾取对象 GUID 字符串，`"0"` 表示没有，读取失败为 null。

拾取 GUID 只能证明当前对象关联，不能证明物品已进入背包。

## 4. Boss 反馈

已有 boss 对象新增 `npc_id`、`target_guid`、`cast_spell_id`、`channel_spell_id`。
无当前目标或无读条时为 0（GUID 用字符串）；读取失败为 null。
这些字段不改变原有 Boss 选取策略，不自动把精英纳入 Boss。
目标与读条只能用于辅助预判，不保证下一次伤害对象。

## 5. 技能冷却接口

请求 `cooldown <spell_id>`，调用客户端原生 GetSpellCooldown。

```powershell
python heal_pipe_client.py 12345 "cooldown 2061"
```

```json
{"spell_id":2061,"spell":"快速治疗","duration_ms":0,"remaining_ms":0,"enabled":true,"ready":true}
```

示例 ID 必须按实际技能选择。接口验证法术数据存在，但不保证角色学会了该技能。
ready 仅表示客户端冷却结束且 enabled，不包括蓝量、距离、视线、目标和站姿。
客户端返回可能包含公共冷却，不能假设完全独立于 GCD。

```python
from heal_pipe_client import healstate, cooldown
state = healstate(12345)
for unit in state['raid']:
    print(unit['name'], unit['los_estimated'], unit['silenced'])
print(cooldown(12345, 2061))
```

## 6. 实测方法

运行 `python test_state_extensions.py PID --seconds 30`，只读取，不施法不移动，
在当前目录保存 JSONL。期间进行以下操作：

1. 与队友无遮挡站立，再让队友走到实心墙后，比较 los_estimated 与实际施法结果。
2. 测试门两侧、坡道和不同高度，检查估算误差。
3. 跑路/手动停下/跟随/停跟随，比较 movement_flags 和 CTM 字段。
4. 受到沉默、昏迷、恐惧时比较状态；不在附近的成员应返回未知。
5. 打开尸体拾取窗口确认 current_loot_guid，关闭后应清零。
6. Boss 战中核对 NPC ID、目标和施法 ID；使用 cooldown 查询技能前后变化。

## 尚未实现

导航网格绕障寻路需要匹配当前地图的 mmap/mmtile 数据，未随本次加入。
也没有增加新的底层事件 Hook；沿用现有主线程调度和 Lua 事件观察器。
治疗加成 plus_healing 的未验证状态不变。
