# PixelEngine 重构详细计划（AI 可执行指令版）

> 本文档是一份
>
> **可直接执行的代码重构指令**
>
> 。拿到本文件的 AI 或开发者，按 
>
> **P0 → P1 → P2 → P3**
>
>  顺序执行即可。每步完成后必须满足「可编译 + 行为等价」，再进入下一步。
> 配套文档：
>
> `PRESENTATION_AND_GAMEPLAY_PARITY_PLAN.md`
>
> （玩法 / 表现对齐基准，重构不得违反其中的任何约束）。



***

## 0. 铁律（必须遵守，违反即失败）



1. **行为完全等价**：重构只改变代码组织，**不改变任何游戏表现**（像素、数值、音效、存档内容、菜单行为）。

2. **每步可编译**：任何一步结束时，项目必须通过 CMake 构建，无新增 warning。

3. **不要一次做完**：按步实施、按步验证、按步提交。

4. **随机数序列不变**：`std::mt19937 gen`（Engine 成员）的**调用顺序与次数**不得改变。陷阱生成、迷宫生成、刷怪、掉落都依赖它，任何顺序变化都会导致地图 / 掉落序列变化，属于行为破坏。

5. **不顺手改别的**：重构过程中不修 bug、不加功能、不改常量数值。发现 bug 单独记录，不在此任务内处理。

6. **文件命名与目录**：新文件放在项目既有目录结构内（`World/`、`Graphics/`、`Core/`），命名风格与现有文件一致（如 `Player.hpp`/`Player.cpp`）。

7. **修改 CMakeLists.txt**：新增 / 删除源文件后必须同步更新 `PixelEngine/CMakeLists.txt` 的源文件列表。



***

## 1. 项目现状基线（AI 先读这些文件再动手）



```
PixelEngine/
├── main.cpp                  # 入口，include 了 MazeGenerator.hpp（可能未直接使用）
├── Engine.hpp / Engine.cpp   # 上帝类：主循环 + 玩家 + 战斗 + 怪物 + 刷怪 + UI 状态（cpp 约 1090 行）
├── CMakeLists.txt            # 构建配置（需同步源文件列表）
├── Core/
│   ├── Types.hpp             # 枚举 + 纯数据 struct（Particle/Bullet/Trap/ScoreRecord…）
│   └── PathUtils.hpp         # namespace 工具（重构的"正确示范"）
├── World/
│   ├── WorldMap.hpp/.cpp     # 地图数据 + 陷阱/障碍/出生点生成
│   ├── PhysicsSystem.hpp/.cpp# 碰撞检测（⚠ 错误耦合：依赖 ParticleSystem）
│   ├── Monster.hpp/.cpp      # 怪物数据 + A* 寻路（数据+行为一体，合理）
│   └── Weapon.hpp/.cpp       # 武器数据 + 工厂（合理）
├── Audio/AudioManager.hpp/.cpp
├── Input/InputManager.hpp/.cpp
├── Graphics/
│   ├── RenderSystem.hpp/.cpp
│   ├── ParticleSystem.hpp/.cpp
│   └── UIRenderer.hpp/.cpp
├── Storage/SaveManager.hpp/.cpp
└── MazeGenerator.hpp         # 伪类：无状态、无成员、单一静态函数、300 行全在头文件
```

### 1.1 Engine 成员分组（迁移源清单，P1/P2 会用到）



| 分组           | 成员（Engine.hpp 现状）                                                                                                                                                                                                                                                                                          | 归属决策                      |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------- |
| 玩家实体         | `playerX, playerY, playerSize, vx, vy`、`ACCEL/FRICTION/MAX_SPEED`、`playerHealth, PLAYER_MAX_HEALTH, invulnTimer, noHitTimer, hpRegenTimer`、`diedByTrap, diedByMonster`                                                                                                                                     | **P1 → Player**           |
| 战斗系统         | `weaponCooldown, attackAnimActive, attackAnimTimer, meleeHitRegistered, aimAngle, ATTACK_DURATION, MELEE_ARC_HALF`、`meleeWeapon, hasPistol, hasRifle, pistolWeapon, rifleWeapon, ActiveSlot, activeSlot`、`noWeaponHintTimer, noWeaponHintText`                                                             | **P2 → CombatSystem**     |
| 怪物 / 掉落 / 刷怪 | `monsters, bullets, weaponDrops`、`pressure, recentKills, recentKillTimer, spawnTimer, weaponSpawnTimer, rangedUnlocked`、`MELEE_MONSTER_CAP … BULLET_SPEED` 等常量、`PickRandomWeaponType, PickRandomMonsterType, SpawnMonster, SpawnWeaponDrop, SpawnWeaponDropAt, UpdateMonsters, UpdateBullets, KillMonster` | **P3 → EnemyManager（可选）** |
| 引擎协调         | `window, renderer, isRunning, isFullscreen`、`GameState/GameMode` 状态机、`gameTimer, score`、`rd/gen`、子系统 `unique_ptr` 们                                                                                                                                                                                        | **留在 Engine**             |
| UI / 音量      | `draggingVolumeSlider, gameOverStartTime, settingsReturnState, exitToDesktop`、`HandleMenuClick, UpdateVolumeFromMouse`                                                                                                                                                                                     | **留在 Engine**（与 UI 渲染耦合）  |

### 1.2 Engine.cpp 方法定位（行号基准，迁移时按名查找）



```
生命周期:   构造(6) 析构(21) Initialize(25) Run(74) Clean(95) ToggleFullscreen(116)
辅助:       GetActiveWeapon(129/135) CurrentMonsterCap(141)
            ComputeTotalWalkPenalty(145) ComputeTotalSprintPenalty(150)
            NormalizeAngle(155) DirFromAngle(162)
战斗:       UpdateAimAngle(171) TryAttack(182) FireWeapon(211) ApplyMeleeHit(230)
            TrySwitchWeapon(251) HandlePickupOrDrop(265) AutoPickupWeapons(285)
            PickUpMeleeDrop(323) DropActiveWeapon(344) DamagePlayer(366)
怪物/掉落:  PickRandomWeaponType(393) PickRandomMonsterType(401)
            SpawnWeaponDropAt(413) SpawnWeaponDrop(417) SpawnMonster(439)
            UpdateMonsters(457) UpdateBullets(509) KillMonster(566)
模式/UI:    StartGame(592) UpdateVolumeFromMouse(661) HandleMenuClick(668)
            HandleInput(728) Update(819) Render(1021)
```



***

## 2. 整体验证方式（每步都要跑）

### 2.1 构建



```
cd "D:\BJTU\Cpp\Outstanding Projects\PixelEngine"
cmake -S PixelEngine -B build
cmake --build build --config Release
```

若项目已有 build 目录，用现有配置即可；目标：**0 error、0 新增 warning**。

### 2.2 手动回归清单（每步后人工跑一遍）



* [ ] 主菜单 → 模式选择 → 限时挑战：正常开局，地图 / 陷阱 / 障碍生成与重构前一致（随机种子固定时逐帧比对更佳）

* [ ] 移动：WASD 移动手感（加速度 / 摩擦 / 最大速度）与重构前完全一致

* [ ] 战斗：近战挥砍（含砍碎木箱 / 矮墙）、手枪 / 步枪射击、切换武器、拾取 / 丢弃武器，行为一致

* [ ] 陷阱：踩地刺 / 老鼠夹死亡流程、结算页显示一致

* [ ] 怪物：近战怪 / 远程怪生成频率、寻路、击杀、掉落一致

* [ ] 迷宫模式：`MazeGenerator` 生成的迷宫结构与重构前一致

* [ ] 存档：历史战绩读写正常

* [ ] 音频 / 音量 / 静音 / 全屏 / 暂停菜单正常

> 回归时若发现任何不一致：
>
> **回滚该步（git revert 或撤销改动），不要在原步上打补丁继续**
>
> 。定位到差异后单独解决。



***

## 3. P0：`MazeGenerator` 伪类 → namespace

**目标**：消除无状态伪类。`MazeGenerator` 无成员变量、无多态需求，只有一个静态函数，应按 `PathUtils` 的模式改为命名空间 + 自由函数，并把 300 行实现挪进 `.cpp`。

### 3.1 改动步骤



1. 新建 `PixelEngine/MazeGenerator.cpp`，把 `GenerateRandomMazeFile` 的完整实现从 `.hpp` 移入，改为自由函数：



```
// MazeGenerator.cpp
#include "MazeGenerator.hpp"
#include <algorithm> …（按需保留原 include）
namespace MazeGenerator {
void GenerateRandomMazeFile(const std::string& filename, std::mt19937& gen) { …原实现原样搬入… }
}
```



1. 改写 `MazeGenerator.hpp`：



```
#pragma once
#include <random>
#include <string>

namespace MazeGenerator {
// 生成一个随机编织迷宫并写入文件（P0 从 class 改造为 namespace）
void GenerateRandomMazeFile(const std::string& filename, std::mt19937& gen);
}
```



1. **调用方零改动**：`Engine.cpp` 的 3 处 `MazeGenerator::GenerateRandomMazeFile(...)`（61、652、765 行）与 `main.cpp` 的 include 均**不需要改**——namespace 与静态类的调用语法完全一致。`Engine.hpp:13` 的 include 保留。

2. 更新 `CMakeLists.txt`：`MazeGenerator.hpp` 保留在头文件列表；**新增** `MazeGenerator.cpp` 到源文件列表。

### 3.2 验证



* 构建通过；

* 迷宫模式生成的地图与重构前**逐字符一致**（随机种子相同时）；

* `MazeGenerator.hpp` 不再包含任何类定义。



***

## 4. P1：抽出 `Player` 类（最高优先级）

**目标**：把玩家实体（位置、速度、血量、受伤、无敌帧、回血）从 Engine 中剥离。Engine 不再直接持有玩家物理状态。

### 4.1 新建 `World/Player.hpp`（接口草案，可微调但行为必须等价）



```
#pragma once

// 玩家实体：位置、速度、生命、受伤/无敌/回血逻辑
class Player {
public:
    // 常量（从 Engine.hpp 迁移，数值不变）
    static constexpr int   MAX_HEALTH   = 10;
    static constexpr float ACCEL        = 500.0f;
    static constexpr float FRICTION     = 8.0f;
    static constexpr float MAX_SPEED    = 140.0f;

    // 状态（从 Engine.hpp 迁移，默认值不变）
    float x = 50.0f;
    float y = 50.0f;
    float size = 12.0f;
    float vx = 0.0f;
    float vy = 0.0f;

    int   health = MAX_HEALTH;
    float invulnTimer = 0.0f;
    float noHitTimer = 0.0f;
    float hpRegenTimer = 0.0f;

    // 死亡原因标记（供结算页使用，从 Engine 迁移）
    bool diedByTrap = false;
    bool diedByMonster = false;

public:
    void Reset();                                  // 重置到出生状态（StartGame 时调用）
    void TakeDamage(int amount);                   // 由 Engine::DamagePlayer 迁移：处理无敌帧与扣血
    void UpdateTimers(float dt);                   // 无敌帧倒计时、无伤计时、回血计时（原 Engine::Update 内逻辑）
    bool IsInvulnerable() const { return invulnTimer > 0.0f; }
};
```

> **说明**
>
> ：
>
> `TakeDamage`
>
>  必须原样搬入 
>
> `Engine::DamagePlayer`
>
>  的既有逻辑（含无敌帧检查、
>
> `diedByTrap/diedByMonster`
>
>  之外的通知、扣血、死亡判定）。玩家
>
> **移动物理**
>
> （加速度 / 摩擦 / 碰撞）在 P1 中
>
> **暂不搬**
>
> （留在 Engine::Update，见 4.3），避免一步改动过大；如需一并迁移，则新建 
>
> `void Move(float dt, bool up, bool down, bool left, bool right, const WorldMap&, const PhysicsSystem&)`
>
>  并把 
>
> `Engine::Update`
>
>  中玩家位移 / 碰撞部分原样搬入，二选一，推荐后者。

### 4.2 从 Engine 迁移的成员（精确清单）



| 删除的 Engine 成员                           | 迁移到                                          |
| --------------------------------------- | -------------------------------------------- |
| `playerX, playerY, playerSize`          | `player.x, player.y, player.size`            |
| `vx, vy`                                | `player.vx, player.vy`                       |
| `ACCEL, FRICTION, MAX_SPEED`            | `Player::ACCEL/FRICTION/MAX_SPEED`           |
| `playerHealth, PLAYER_MAX_HEALTH`       | `player.health, Player::MAX_HEALTH`          |
| `invulnTimer, noHitTimer, hpRegenTimer` | `player.invulnTimer/noHitTimer/hpRegenTimer` |
| `diedByTrap, diedByMonster`             | `player.diedByTrap/diedByMonster`            |
| 方法 `DamagePlayer(int)`                  | `Player::TakeDamage(int)`（含无敌帧逻辑）            |

### 4.3 Engine 改动清单



1. `Engine.hpp`：删除上表全部成员；新增 `#include "World/Player.hpp"`；新增成员 `Player player;`（值成员即可，Player 无资源需管理）。

2. **全文件替换引用**：`Engine.cpp` 中所有 `playerX` → `player.x`，`playerY` → `player.y`，`playerSize` → `player.size`，`playerHealth` → `player.health`，`invulnTimer` → `player.invulnTimer`，`noHitTimer` → `player.noHitTimer`，`hpRegenTimer` → `player.hpRegenTimer`，`diedByTrap` → `player.diedByTrap`，`diedByMonster` → `player.diedByMonster`，`DamagePlayer(...)` 调用 → `player.TakeDamage(...)`。

* 涉及位置：`Engine::Update`（玩家移动 / 受伤 / 无敌 / 回血段，约 819 行起）、`UpdateMonsters`/`UpdateBullets`（怪物命中玩家处，\~457/509 行起）、`CheckTrapCollision` 调用处（\~977 行）、`StartGame`（\~592 行，重置玩家）、`Render`（\~1021 行，传坐标给 RenderSystem）、`HandleInput`（\~728 行）。

1. 若按「4.1 推荐做法」迁移移动物理：把 `Engine::Update` 中玩家位移 / 碰撞整段搬入 `Player::Move`，`Engine::Update` 改为调用 `player.Move(dt, up, down, left, right, *worldMap, *physicsSystem)`（方向来自 `inputManager`）。

2. `StartGame` 中玩家重置逻辑改为调用 `player.Reset()`（出生点坐标由 `LoadLevelFromFile` 输出，赋给 `player.x/player.y`）。

3. `Engine::Update` 中的无敌 / 回血计时逻辑改为调用 `player.UpdateTimers(dt)`。

4. 确认 `Render()` 传给 `RenderSystem::DrawWorld` 的仍是相同数值（`player.x/player.y/player.size`）。

5. 更新 `CMakeLists.txt`：新增 `World/Player.hpp` 与 `World/Player.cpp`。

### 4.4 行为等价注意点



* `DamagePlayer` 原来的**调用顺序与调用次数**必须保持（`UpdateMonsters`、`UpdateBullets`、`CheckTrapCollision` 触发受伤的先后不变），否则无敌帧表现会变。

* `player.health` 的读写点逐一核对（HUD 绘制、GameOver 判定、回血阈值）。

* 随机数 `gen` **不动**，仍在 Engine。

### 4.5 验证

构建通过 + 回归清单全绿；重点：受伤无敌帧闪烁、回血节奏、移动手感与重构前一致。



***

## 5. P2：抽出 `CombatSystem` 类

**目标**：把武器槽、攻击、射击、近战判定、拾取 / 丢弃从 Engine 剥离。**前置：P1 已完成**（CombatSystem 依赖 Player 的坐标 / 朝向 / 位置）。

### 5.1 新建 `World/CombatSystem.hpp`（接口草案）



```
#pragma once
#include "Weapon.hpp"
#include <string>
#include <vector>
#include <random>

class Player;
class WorldMap;
class PhysicsSystem;
class ParticleSystem;
class AudioManager;
class Monster;
struct Bullet;
struct WeaponDrop;

// 战斗系统：武器槽、攻击/射击、近战判定、拾取与丢弃
class CombatSystem {
public:
    // 从 Engine 迁移的常量（数值不变）
    static constexpr float ATTACK_DURATION = 0.18f;
    static constexpr float MELEE_ARC_HALF  = 1.0f;
    static constexpr float PICKUP_RADIUS   = 16.0f;

    // 从 Engine 迁移的状态
    float weaponCooldown = 0.0f;
    bool  attackAnimActive = false;
    float attackAnimTimer = 0.0f;
    bool  meleeHitRegistered = false;
    float aimAngle = 0.0f;

    Weapon meleeWeapon;
    bool hasPistol = false;
    bool hasRifle = false;
    Weapon pistolWeapon;
    Weapon rifleWeapon;
    enum ActiveSlot { SLOT_MELEE, SLOT_PISTOL, SLOT_RIFLE };
    ActiveSlot activeSlot = SLOT_MELEE;

    float noWeaponHintTimer = 0.0f;
    std::string noWeaponHintText;

    // 依赖注入：怪物/子弹/掉落容器在 P3 前仍由 Engine 持有，以引用传入
    CombatSystem(Player& player,
                 std::vector<Monster>& monsters,
                 std::vector<Bullet>& bullets,
                 std::vector<WeaponDrop>& weaponDrops,
                 WorldMap& worldMap,
                 PhysicsSystem& physics,
                 ParticleSystem& particles,
                 AudioManager* audio,
                 std::mt19937& rng);

    void Reset();                          // StartGame 时重置武器槽/冷却（原 Engine 逻辑）
    void Update(float dt);                 // 冷却、攻击动画计时（原 Engine::Update 内战斗段）

    // 从 Engine 迁移的方法（实现原样搬入，签名按需调整）
    void UpdateAimAngle();                 // 需要鼠标坐标：改为从 InputManager 查询或传入参数
    void TryAttack();
    void FireWeapon(const Weapon& weapon);
    void ApplyMeleeHit(const Weapon& weapon);
    void TrySwitchWeapon(ActiveSlot slot);
    void HandlePickupOrDrop();
    void AutoPickupWeapons();
    void PickUpMeleeDrop(const WeaponDrop& drop);
    void DropActiveWeapon();

    const Weapon* GetActiveWeapon() const;
    Weapon* GetActiveWeapon();
    float ComputeTotalWalkPenalty() const;
    float ComputeTotalSprintPenalty() const;
    bool HasActiveNoWeaponHint() const { return noWeaponHintTimer > 0.0f; }
    const std::string& GetNoWeaponHintText() const { return noWeaponHintText; }

private:
    WeaponType PickRandomWeaponType();     // 从 Engine 迁移，使用注入的 rng
    // 引用成员（注入，不拥有）
    Player& player_;
    std::vector<Monster>& monsters_;
    std::vector<Bullet>& bullets_;
    std::vector<WeaponDrop>& weaponDrops_;
    WorldMap& worldMap_;
    PhysicsSystem& physics_;
    ParticleSystem& particles_;
    AudioManager* audio_;
    std::mt19937& rng_;
};
```

### 5.2 从 Engine 迁移的成员与方法（精确清单）



| 删除 / 迁出的 Engine 内容                                                                                                                                                                                                            | 去向                                                                                       |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `weaponCooldown, attackAnimActive, attackAnimTimer, meleeHitRegistered, aimAngle`                                                                                                                                             | `CombatSystem` 同名字段                                                                      |
| `ATTACK_DURATION, MELEE_ARC_HALF`                                                                                                                                                                                             | `CombatSystem` 常量                                                                        |
| `meleeWeapon, hasPistol, hasRifle, pistolWeapon, rifleWeapon, ActiveSlot, activeSlot`                                                                                                                                         | `CombatSystem`                                                                           |
| `noWeaponHintTimer, noWeaponHintText`                                                                                                                                                                                         | `CombatSystem`                                                                           |
| 方法：`UpdateAimAngle, TryAttack, FireWeapon, ApplyMeleeHit, TrySwitchWeapon, HandlePickupOrDrop, AutoPickupWeapons, PickUpMeleeDrop, DropActiveWeapon, GetActiveWeapon(×2), ComputeTotalWalkPenalty, ComputeTotalSprintPenalty` | `CombatSystem`                                                                           |
| `PICKUP_RADIUS` 常量                                                                                                                                                                                                            | `CombatSystem::PICKUP_RADIUS`                                                            |
| `PickRandomWeaponType`                                                                                                                                                                                                        | `CombatSystem`（使用注入 rng，`gen` 调用顺序不变）                                                    |
| `NormalizeAngle, DirFromAngle`                                                                                                                                                                                                | 若仅战斗用 → 移入 CombatSystem；若怪物也用 → 移入 `Core/Types.hpp` 旁的新工具命名空间或保留在 Engine 供两处使用（二选一，保持行为） |
| `CurrentMonsterCap`                                                                                                                                                                                                           | 属刷怪逻辑 → **P3 迁 EnemyManager**，P2 阶段可暂留 Engine（CombatSystem 不依赖它则无需处理）                    |

### 5.3 Engine 改动清单



1. `Engine.hpp`：删除上述成员与方法声明；新增 `#include "World/CombatSystem.hpp"`；新增成员 `std::unique_ptr<CombatSystem> combatSystem;`（或值成员 `CombatSystem*`，推荐 unique\_ptr 与现有子系统风格一致）。

2. `Engine::Initialize` 或 `Engine` 构造：创建 `combatSystem`，注入 `player`、`monsters/bullets/weaponDrops`（P3 前仍为 Engine 成员，直接传引用）、`*worldMap`、`*physicsSystem`、`*particleSystem`、`audioManager.get()`、`gen`。

3. `Engine::StartGame`：战斗重置逻辑 → `combatSystem->Reset()`。

4. `Engine::Update`：战斗段（冷却 / 动画 / 攻击 / 拾取）→ `combatSystem->Update(dt)`；把原来 `TryAttack/TrySwitchWeapon/HandlePickupOrDrop` 的触发点改为 `combatSystem->TryAttack()` 等。

5. `Engine::HandleInput`：武器切换 / 攻击按键处理 → 转发给 `combatSystem`。

6. `Engine::Render`：武器状态栏绘制所需的 `GetActiveWeapon()`、`noWeaponHint*` 数据 → 从 `combatSystem` 取；攻击动画参数（`attackAnimActive` 等）→ 从 `combatSystem` 取。

7. 音频调用（攻击音效等）由 CombatSystem 内部通过 `audio_` 触发，**触发时机与次数与原来一致**。

8. 更新 `CMakeLists.txt`：新增 `World/CombatSystem.hpp/.cpp`。

### 5.4 行为等价注意点



* `FireWeapon` 生成的子弹要 push 进 `bullets_`（即 Engine 的 `bullets`），`UpdateBullets` 仍在 Engine（P3 才迁走），确认引用指向同一容器。

* `ApplyMeleeHit` 发射粒子（刀光 / 碎屑）的**时机与数量不变**。

* 攻击判定使用 `player.x/player.y/player.size` 与 `aimAngle`，换算公式原样搬移。

* `UpdateAimAngle` 依赖鼠标坐标：原实现从 `inputManager->GetMouseX/Y()` 取，迁移时通过**构造传入 InputManager 引用**或**改为方法参数传入**，二选一，但坐标换算逻辑不变。

### 5.5 验证

构建通过 + 回归清单全绿；重点：近战 / 射击 / 切换 / 拾取 / 丢弃全流程、武器 HUD、无武器提示。



***

## 6. P3：`EnemyManager` + 断开 PhysicsSystem 粒子耦合（可选，工作量最大）

> 本步可延后。P1+P2 已完成时，架构已明显改善。若执行，
>
> **建议拆成 6.1 与 6.2 两个独立子步骤**
>
> ，各自验证。

### 6.1 抽出 `EnemyManager`

**目标**：把怪物 / 子弹 / 掉落 / 刷怪 / 压力系统从 Engine 剥离。

新建 `World/EnemyManager.hpp`，成员 / 方法迁移：



| 迁出（Engine）                                                                                                                               | 迁入（EnemyManager）                                                    |
| ---------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| `monsters, bullets, weaponDrops`                                                                                                         | 容器成员                                                                |
| `pressure, recentKills, recentKillTimer`                                                                                                 | 成员                                                                  |
| `spawnTimer, weaponSpawnTimer, rangedUnlocked`                                                                                           | 成员                                                                  |
| `MELEE_MONSTER_CAP … BULLET_SPEED` 全部刷怪常量                                                                                                | 常量                                                                  |
| `SpawnMonster, SpawnWeaponDrop, SpawnWeaponDropAt, UpdateMonsters, UpdateBullets, KillMonster, CurrentMonsterCap, PickRandomMonsterType` | 方法                                                                  |
| 刷怪 / 掉落计时逻辑（原 `Engine::Update` 内）                                                                                                        | `EnemyManager::Update(float dt, const Player&, const WorldMap&, …)` |

依赖注入：`std::mt19937& rng`（保持 gen 调用顺序不变）、`AudioManager*`（击杀音效）、`ParticleSystem&`（击杀 / 爆炸粒子）。此后 `CombatSystem` 的 `monsters_/bullets_/weaponDrops_` 引用改为指向 `EnemyManager` 内的容器（或直接引用 `EnemyManager`）。

### 6.2 断开 PhysicsSystem → ParticleSystem 耦合

**问题**：`PhysicsSystem`（物理层）的 `PerformAttackHitCheck` / `BreakObstaclesInRect` / `BulletPassesAndBreaks` 直接调用 `ParticleSystem`（图形层）发射粒子，跨层依赖。

**改法**：



1. 删除 `PhysicsSystem` 三个方法中的 `ParticleSystem&` 参数；

2. 方法只负责**判定与修改地图数据**，返回破坏结果（如 "是否命中" 或 "本次破坏的障碍矩形列表 / 坐标"）；

3. 粒子发射移到**调用方**（CombatSystem / Engine / EnemyManager 的对应流程），使用返回值决定发射时机与数量，**表现必须与原来一致**（粒子种类、数量、位置）。

4. 同时检查 `PhysicsSystem.hpp` 的 `#include "../Graphics/ParticleSystem.hpp"` 是否可删除（物理层不再依赖 Graphics 层）。

### 6.3 验证

构建通过 + 回归清单全绿；重点：攻击碎箱粒子、子弹穿障粒子、击杀粒子、刷怪节奏、掉落序列（随机种子固定时一致）。



***

## 7. 执行顺序与工作量



| 步骤   | 内容                        | 预估      | 前置                   |
| ---- | ------------------------- | ------- | -------------------- |
| P0   | MazeGenerator → namespace | 0.5 天   | 无                    |
| P1   | 抽 Player                  | 1–2 天   | P0（可与 P0 并行，但建议先 P0） |
| P2   | 抽 CombatSystem            | 2–3 天   | P1                   |
| P3.1 | 抽 EnemyManager            | 2–3 天   | P2                   |
| P3.2 | 断 PhysicsSystem 粒子耦合      | 0.5–1 天 | P3.1 或独立             |

**建议节奏**：P0+P1 一次提交，P2 一次提交，P3 按子步提交。每次提交信息注明「refactor: …（行为等价，已跑回归清单）」。



***

## 8. 风险与回滚



1. **引用悬挂**：CombatSystem/EnemyManager 持有 Engine 内容器的引用（P2 阶段）。**先改构造、后删成员**；删除 Engine 成员前，先用 `grep -rn "memberName"` 确认零引用。

2. **随机序列漂移**：任何重构若改变 `gen` 的调用顺序（如提前 / 延后调用 `PickRandomWeaponType`），地图与掉落会变。**每步构建后，用固定种子跑一遍迷宫模式与限时挑战，与重构前记录比对**。

3. **行为漂移兜底**：若项目尚未使用 git，**执行重构前先&#x20;**`git init`**&#x20;并提交基线**；每个子步骤完成后提交一次，发现问题直接 revert 该步，不在原步上打补丁。

4. **范围蔓延**：重构中发现的 bug、风格问题、常量调整，一律记录到本文档末尾的「发现的问题」区，**不在本次重构中处理**。



***

## 9. 发现的问题（重构过程中遇到非本次范围的 bug 记录于此）

（留空，重构执行者按需追加：`- [ ] 文件/行号：问题描述`）