# 战术表（每张地图一份）

新增地图时，**先加一张表**，再考虑改代码。教练（规则/LLM）会按这里的区域名与分兵模板行动。

## 文件

`assets/config/tactics/<mapId>.ini`，`mapId` 与 `assets/cs2d/maps/<mapId>.tmj` 同名。

## 格式

```ini
[areas]
# 代码里的关键词 → 地图 TMJ 的实际区域名（区域来自 TMJ 的 group site 子图层）
A_Site = A_Site
Long   = Long      # 行尾注释会被忽略

[slots.T.Default]
# 槽位序号 = 角色|区域别名[|noTrade][|rotate]
# 角色：Entry / Support / Lurk / Anchor / Awper
0 = Entry|Long
1 = Support|A_Site
2 = Lurk|Mid|rotate
3 = Anchor|Tunnel
4 = Support|B_Site
```

槽位组名格式：`[slots.<队伍>.<意图>]`，队伍 = `T` / `CT`，意图 =
`Default` / `RushA` / `RushB` / `SplitA` / `SplitB` / `StackA` / `StackB` /
`RetakeA` / `RetakeB` / `Save` / `Fake`。

## 规则

1. **只覆盖"通用分兵模板"**：回合开局的默认分兵、速推、常规守点。
   事件整形出来的计划（下包后回防、保枪、减员收缩）仍由代码决定，避免表和事件逻辑打架。
2. **缺省即回退**：没有这张表、或表里没有某个意图，教练用代码里的默认逻辑，不会崩。
3. **加载即校验**：启动时会检查 `[areas]` 里每个区域名是否在这张图上真实存在，
   不存在会打印 `[Tactics] 区域不存在: ...`，对应槽位会退化为"无目标"。
4. 改表会影响确定性哈希（行为变了），改完请按 `tools/dev_replay/WORKFLOW.md` 跑 60 局对比。

## 当前地图

| 地图 | 说明 |
| --- | --- |
| dust_2 | Long / Short / Tunnel 都是真实图层 |
| mirage | 没有 Long/Short/Tunnel，表里暂时回退到包点；可改成 Cat / Palace / B_Apts 等真实区域 |
| ancient_ruins | 旧代码的子串匹配会把 Long/Short 落到 B 侧；表里暂时保持同样取值，纠正属调参项 |
