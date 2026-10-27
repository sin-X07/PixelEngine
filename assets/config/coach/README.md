# 主教练战术库目录

LLM 教练的 system prompt = 引擎固定契约 + 本目录内容。按地图拆文件，便于单图迭代，
同时让同一局内的 system 前缀保持字节稳定（`cache_prompt` 前缀缓存仍有效）。

| 文件 | 内容 |
| --- | --- |
| `_common.txt` | 所有地图共用的总规则：优先级、经济/save、C4/下包、转点、回防、slots 规范 |
| `<playbookId>.txt` | 单图专属：区域别名、T/CT × 经济模板、局势剧本、few-shot |
| `SOURCES.md` | 公开攻略来源与改写原则 |

## 加载规则

- 引擎先读 `_common.txt`，再追加 `assets/config/coach/<playbookId>.txt`。
- `playbookId` 默认等于地图 `id`；`MapCatalog` 条目的 `playbook` 字段可覆盖
  （例如两张图共用一份战术，或临时切到 `dust_2_test`）。
- 某张图没有专属文件时，只加载 `_common.txt`，并在日志打印
  `[Coach] 地图 <id> 无专属战术库，仅使用通用规则`。
- 每个 `<playbookId>` 只加载一次并缓存；一局内 system prompt 不再变化。

## 单图文件格式

```text
# 注释行以 # 开头，加载时剔除

[areas]
A_Site Long Mid B_Site T_Spawn CT_Spawn
# ↑ 必须是该图 tmj 里 site 组子图层的精确区域名，空格或逗号分隔

【地图区域别名】
A侧: ...
B侧: ...
中路: ...

────────────────
T 满钱 / 强起
────────────────
default | target="" | tempo=normal | ttl=18~25
  ...
```

`[areas]` 是机器可读清单，加载时会和 `world->GetAreas()` 比对：
未知区域、大小写不一致都会打 `[Coach] 警告`。few-shot / 模板里
`"area":"X"` 的引用也会一起校验。该清单只用于校验，不会原样送进模型
（送模型的是 prompt 里的「可选区域」）。

当前三张图的 system prompt 约 3.3k~3.6k tokens；llama-server 以
`-c 8192 --parallel 2` 启动，每槽约 4096 tokens。新增内容时优先精简
few-shot 和重复描述，别把单图文件堆到超过上下文。

## 新增地图

1. 把 `assets/cs2d/maps/<id>.tmj` 放进仓库。
2. 在 `PixelEngine/CS2D/MapCatalog.hpp` 的 `kMapCatalog` 里登记；`playbook`
   留空即默认使用 `id`，需要复用/覆盖时再填。
3. 新建 `assets/config/coach/<playbookId>.txt`，至少写 `[areas]` 清单。
4. 启动后看日志：
   - `[Coach] 战术库就绪(<id>): ... bytes`
   - `[Coach] 战术库区域校验(<id>): 清单 N 个, slots 示例 M 个, 未知 0`

没有专属文件的图也能正常开局，只是只有通用规则。
