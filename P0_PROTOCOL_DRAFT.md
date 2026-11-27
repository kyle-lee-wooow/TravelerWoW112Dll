# P0 管道协议草案（待 Python 侧确认）

实施顺序：敌方枚举 → 通用 Lua 查询 → 复核现有 cooldown。
本文件只定义候选协议，不表示敌方枚举或通用 Lua 查询已经交付。

## 敌方枚举

建议独立命令 `enemies`，不扩大每次 healstate 的负载。
主线程生成快照，缓存 100ms；客户端可按 50–100ms 请求，但返回
sample_seq/sample_time_ms 判断是否为同一帧。时间为 DLL 单调时钟，不能和墙钟直接相减。

```json
{"schema_version":1,"session":"player-guid","sample_seq":1,"sample_time_ms":1234,"available":true,"truncated":false,"enemies":[{"guid":"123","name":"怪物","hp":100,"max_hp":200,"x":1.0,"y":2.0,"z":3.0,"distance_raw":10.0,"distance":9.7,"target_guid":"456","targeting_player":true,"reaction":2,"hostile":true,"tapped":true,"tapped_by_player":false}]}
```

- GUID 均为十进制字符串，字段读取失败用 null；0 GUID 表示已确认无目标。
- distance_raw 为中心点三维距离；distance 使用现有目标距离拟合公式。
- hostile 仅在反应接口已核实且成功读取时给布尔值；未知不能当友方。
- tapped 和 tapped_by_player 分开，不能把“有人归属”误当“归属玩家”。
- 仅可见、已解析的生物单位；不承诺全地图，不包含玩家和游戏物件。
- 上限建议 200 个、响应 128KiB；截断必须标明，Python 不得当作完整清单。
  实现前先核对管道客户端接收上限；必要时采用分页，不能静默截断 JSON。
- 切角色或地图后清空缓存；不可用时 available=false、enemies=[]，不回放旧怪表。
- 团员 x/y/z 追加到 healstate 团员项，未知坐标为 null。
- 客户端属性偏移沿用已核实实现；UnitReaction、tap 字段需特征码和实机验证后启用。

## 通用 Lua 查询

建议 `luaquery <单行表达式>`，主线程求值并立即读取唯一临时全局，完成后清理。
响应采用 JSON：`{"ok":true,"type":"string","value":"..."}`；失败返回
`{"ok":false,"error":"..."}`。支持 nil、boolean、number、string；表由调用者
显式序列化。字符串必须正确 JSON 转义，限制长度并明确报错。现有 lua 命令不变。
优先复用已有 Lua 5.0 栈读取，不直接套用未验证的原版 GetText 地址。

## 已有能力与验收

boss.hp/max_hp、boss.cast_spell_id、target 和 cooldown 已存在。
boss=null 应先排查交战筛选，不重复添加字段。cooldown 就绪口径沿用现有
实现，不能未经验证改成 started==0 即就绪。
28785/18173 仍需现场采样，文档或编译通过不能替代 Boss 战验证。
当前 target.distance 已使用拟合，不再是未修正的中心点距离。
