# PixelEngine 双人 AI 开发与合并契约 v1

状态：待实现的接口设计，不能当作当前源码已存在的 API。本文取代 PIX-6～17 中模糊的“双方自行约定接口”表述；现有功能目标仍有效。Linear 对应契约 Issue 是跨 AI 交接入口。

> 仓库内副本说明：本文由功能方（孙铭轩 / Windows AI）在接口基础 PR 中纳入，内容与 Linear 附件 `AI-MERGE-CONTRACT-v1.md` 一致，仅补充了“实现进度”一节，用于记录本轮实际落地情况。契约正文不改动。

## 0. 基线与事实

- 审计远端 main：`d4a10555f83a6259a5dff0cd5a3ee97f31443dab`。旧本地副本 `868f49b` 不能直接作为集成基线。开工时 fetch，记录实际 base SHA；若 main 前进，先比较本文接口涉及的 diff 并更新契约版本。
- 当前已存在：`GameUI::DrawBuyMenu(const BuyMenuCtx&, const std::function<void(int)>&)`；`CS2DGame::TryBuy(WeaponId, AudioManager*) -> bool`、`TryBuyArmor(int, AudioManager*) -> bool`；`Engine::RenderBuyMenu()` 将二者连接但丢弃 bool 返回值。
- 当前没有下面定义的 Presentation 命名空间、详细购买结果和 ScopeView。`WeaponDef::scoped` 仅为配置标记，不能视为开镜功能已完成。
- 新 main 已有 `CS2DRenderer::DrawThrownGrenade(int,float,float,float)`，投掷物不再共用 DrawBullet。保留新 main 的烟雾、火区、投掷物与 Mac 兼容代码。
- `Team` 的唯一来源为 `Core/Types.hpp`，`None=0,T=1,CT=2`；`Actions::chooseSide(int)` 仍是 `0=T,1=CT`。这两套编号不可强转混用。
- 现有 `Menu3DModel` 是无纹理低模 glTF/GLB 加载器；不能要求只交付贴图就能显示材质。

## 1. 责任与文件锁定

shijin（Mac AI，简称 UI 方）唯一负责：`Graphics/GameUI.cpp` 的布局/表现、`Graphics/ImGuiUI.*`、新增 `Graphics/TeamVisualStyle.hpp`、`Graphics/WeaponIconCache.*`、`CS2DRenderer.cpp` 的 DrawCharacter/DrawBulletVisuals/DrawScopeOverlay 表现和新增相应绘制实现。UI 素材放 `assets/ui/`。不改购买判定、射击/碰撞、模型加载行为。

孙铭轩（Windows AI，简称功能方）是共享边界唯一集成人：新增 `Core/PresentationContract.hpp`；`GameUI.hpp` 的跨层字段/函数签名；`Engine.cpp/.hpp` 的输入门控、Ctx 填充和回调接线；`InputManager.*`；`CS2DGame.hpp`、`CS2DGame*.cpp`；`CS2DRenderer.hpp` 和 UpdateCamera 的倍率逻辑；`Menu3D.*`、`Menu3DModel.*` 与 `assets/models/`。负责 CMake SOURCES 清单接入 UI 新文件。

例外：接口基础 PR 中，功能方可最小修改 GameUI.cpp 的 DrawBuyMenu 回调签名和调用（不重排 UI），以及在 Renderer.cpp 加可链接的空绘制实现；随后这些函数表现归 UI 方。CS2DRenderer.cpp 的 UpdateCamera 归功能方，其他表现函数归 UI 方。双方变更同一 cpp 必须按函数提交，不整体格式化；发生冲突由功能方整合，禁止选 ours/theirs 覆盖整文件。

UI 需要 Engine/Menu3D 改动时，在 Issue 给出字段/函数及补丁，由功能方提交；不能两个 AI 同时修改这些共享文件。接口冻结后改名/改语义要先修订本契约并列出全部调用点迁移，不在各自 PR 中静默更改。

## 2. 共享类型（新增，完整签名）

文件 `PixelEngine/Core/PresentationContract.hpp`，仅包含 Types.hpp、../CS2D/Weapons/WeaponDef.hpp、`<cstdint>`。不能包含 GameUI、CS2DGame、ImGui 或 OpenGL。所有结构按值传递/快照使用，不暴露业务对象可写指针。

```cpp
namespace Presentation {
enum class BuyKind { Weapon, Armor };
struct BuyRequest {
    BuyKind kind = BuyKind::Weapon;
    WeaponId weapon = WeaponId::None; // Weapon 时有效
    int armorTier = 0;               // Armor 时只允许 1/2
};
enum class BuyCode {
    Ok, InvalidItem, PlayerDead, WindowClosed, OutsideBuyZone,
    TeamRestricted, InsufficientFunds, InventoryFull, ItemLimitReached,
    AlreadyEquipped
};
struct BuyResult {
    BuyRequest request{};
    BuyCode code = BuyCode::InvalidItem;
    int moneyAfter = 0;
    int quantityAfter = 0; // 武器持有数/同类投掷物数；护甲为已拥有目标档位的 0/1
};
struct ScopeView {
    WeaponId weapon = WeaponId::None;
    bool supported = false;
    bool active = false;                 // level > 0（目标状态，不等同于过渡是否完成）
    int  level = 0;                      // v1.1 新增：0=关 / 1=1.5x / 2=2x
    float transition = 0.0f;             // 0..1，镜框/遮罩淡入淡出进度（不是倍率进度）
    float zoomMultiplier = 1.0f;         // 当前实际倍率（含过渡）
    float targetZoomMultiplier = 1.0f;   // v1.1 新增：当前档位目标倍率（1.0/1.5/2.0）
};
enum class ScopeResetReason {
    InputBlocked, FocusLost, WeaponChanged, ReloadStarted,
    PlayerDied, RoundRestart, MatchExit
};
// v1.1 新增：档位表与开镜几何参数
inline constexpr int   kScopeLevelCount = 3;
inline constexpr float kScopeZoomLevels[kScopeLevelCount] = { 1.0f, 1.5f, 2.0f };
inline constexpr float kScopeTransitionSeconds = 0.12f;   // 每次换档的过渡时长
inline constexpr float kScopeCircleRadiusFrac = 0.42f;    // 镜筒半径 = 短边 × 该系数
inline constexpr float kScopeCameraShiftMax = 0.5f;       // 相机向准星偏移上限
// v1.1 新增：圆形可见区的唯一几何定义（渲染与测试共用，纯数学）
inline float ComputeScopeVisibility(float centerX, float centerY, float radius,
                                    float targetX, float targetY,
                                    float softWidth = 0.06f);
struct BulletVisualSegment {
    std::uint64_t id = 0;             // 对局内唯一视觉段编号
    WeaponId weapon = WeaponId::None;
    float fromX=0, fromY=0, toX=0, toY=0; // 世界坐标；不是屏幕像素
    float remainingSeconds=0.0f;
    float durationSeconds=0.08f;
    bool impact=false;                // 实际碰撞终止；寿命/射程到期不算命中
};
} // namespace Presentation
```

## 3. 购买：请求、判定、提交、显示

功能方新增 CS2DGame 公有函数：

```cpp
Presentation::BuyCode EvaluatePurchase(const Presentation::BuyRequest& request) const;
Presentation::BuyResult ExecutePurchase(const Presentation::BuyRequest& request,
                                       AudioManager* audio = nullptr);
```

EvaluatePurchase 无扣款/装备/音效副作用。固定失败优先序：非法 kind/WeaponId/tier 或非售卖项 → InvalidItem；死亡 → PlayerDead；购买窗口关闭 → WindowClosed；区域不合法 → OutsideBuyZone；阵营不符 → TeamRestricted；目标已装备/满甲 → AlreadyEquipped；投掷物总数已满 → InventoryFull；同类数量满 → ItemLimitReached；金额不足 → InsufficientFunds；其余 Ok。购买时窗判定不能直接用包含区域/存活的 IsBuyPhase 代替，否则死斗失败原因会错；时窗复用原游戏模式及 round.Elapsed 规则，区域/存活分别判断。

主/副武器持有同 id 返回 AlreadyEquipped，不再允许反复扣款补满同枪弹药；不同武器仍按原规则替换。护甲 tier1 是满甲即已拥有；tier2 是满甲且有头盔才已拥有。重复投掷物遵循现有闪光2个/其他1个和总数4个规则。这是为 UI 与业务一致而明确的行为修正，必须有回归用例。

ExecutePurchase 必须在点击时重新 Evaluate；Ok 才执行一次原装备/扣款及 EQUIP 音效，返回实际执行后的 moneyAfter/quantityAfter；失败不改金额、装备、音效。保留旧 TryBuy/TryBuyArmor 签名，改为 ExecutePurchase(...).code==Ok 的兼容包装，不能两者相互调用递归或重复扣款。非法 enum 值先校验后索引 WeaponDB。

功能方修改 GameUI.hpp：保留 BuyItem 已有字段以减少迁移，新增以下字段；旧 weaponId 的负数护甲编码只作兼容，不再作为新回调入口：

```cpp
// GameUI::BuyItem 新增
Presentation::BuyRequest request{};
Presentation::BuyCode availability = Presentation::BuyCode::InvalidItem;
std::string iconPath;       // WeaponDB.iconPath 的复制；护甲可为空
std::string categoryLabel;
int magazine = 0;
bool automatic = false;
bool scopeSupported = false;
// GameUI::BuyMenuCtx 新增
Team playerTeam = Team::None;
// 替换旧 DrawBuyMenu 声明；不保留 void 回调重载，防止 lambda 歧义
void DrawBuyMenu(const BuyMenuCtx& ctx,
    const std::function<Presentation::BuyResult(const Presentation::BuyRequest&)>& onBuy);
```

Engine::RenderBuyMenu 唯一负责从 WeaponDB/GetPlayer/EvaluatePurchase 填充快照。ctx.canBuy/affordable/owned 仅兼容；新 UI 禁用和原因以 availability 为准；分类从 WeaponDef.cls 映射，图标复制 iconPath，不复制假数值。回调：`[this](const Presentation::BuyRequest& r) { return cs2dGame->ExecutePurchase(r, audioManager.get()); }`。一帧最多提交一次购买请求；执行后余额显示取 result.moneyAfter，其余条目下帧重建，不能继续用旧 ctx 发第二笔。UI 自己保存选中项和短提示，不写 CS2DGame。disabled 项允许悬停读取原因，不能触发回调；执行期失败也必须显示对应 BuyCode 的中文文案。

UI 方新增 WeaponIconCache（hpp/cpp），函数固定：

```cpp
bool Initialize(SDL_Renderer* renderer); // 非 owning；换 renderer 前先 Shutdown
SDL_Texture* Get(const std::string& relativePath); // cache owning，调用者借用，不 Destroy
void Shutdown(); // Destroy 全部缓存纹理；必须早于 SDL_DestroyRenderer
```

类名 `WeaponIconCache`，Get 内用 PathUtils::ResolveAppPath 再 IMG_LoadTexture，失败缓存空值防止每帧刷日志；保持长宽比，失败用文字/轮廓。功能方在 Engine 初始化/退出接入；GameUI::SetWeaponIconCache(WeaponIconCache*)（新增）借用指针；UI 不以 iconPath 每帧加载图片。契约基础 PR 可先提供 cache 的空返回 stub，再由 UI Issue 实现。

## 4. 开镜：唯一状态与每帧时序

输入方新增 `InputManager::IsMouseRightClicked() const -> bool` 和 `HasWindowFocus() const -> bool`。右键 clicked 是 SDL_BUTTON_RIGHT 的 down 边沿，只活一帧；ResetPerFrameActions 清零；失焦清全部 held 状态和边沿。窗口焦点由 SDL_WINDOWEVENT_FOCUS_GAINED/LOST 更新。只由功能方实现，不用 UI 自行 SDL_GetMouseState。

CS2DGame 新增：

```cpp
void UpdateScopeInput(bool rightClicked, bool gameplayInputAllowed);
Presentation::ScopeView GetScopeView() const; // 返回值快照，无消费副作用
void ResetScope(Presentation::ScopeResetReason reason); // 立即归零，幂等
```

按右键循环三档：**关 → 1.5x → 2x → 关**（`level = (level + 1) % kScopeLevelCount`）；supported 来自当前有效武器的 WeaponDef.scoped。输入允许且活着、非换弹才可切换。只改变相机与表现，不改伤害/散布/移速/射速。每次换档在 CS2DGame::Update 用 dt 在 0.12 秒内线性过渡倍率；`transition` 只在开/关时变化（1.5x ↔ 2x 换档不重播镜框淡入），`zoomMultiplier` 是当前实际倍率，`targetZoomMultiplier` 是本档目标倍率。`ResetScope` 立即 `level=0/active=false/transition=0/zoomMultiplier=1`，幂等。supported 依照武器变化更新。

**v1.1 可见性**：开镜时可见区由「扇形 + 半径」改为**以准星为圆心的圆**（取代扇形，不是求交），圆外一律不可见并由视野雾遮盖（雾不透明度从 160 提到 242）。几何定义只有一份：

```cpp
// Presentation::ComputeScopeVisibility(cx, cy, radius, tx, ty, softWidth=0.06f)
// 圆内 1、圆外 0、边缘 softWidth 内 smoothstep；墙体遮挡（HasLineOfSight）由渲染层叠加
void CS2DRenderer::SetScopeMask(bool active, float centerX, float centerY, float radiusWorld);
```

`ComputeVisibility` 在 mask 激活时先走圆形规则（含 LOS），否则走原扇形规则——敌人剔除、`IsVisibleToPlayer`、`DrawVisionFog` 共用同一函数，因此雾与可见性不会漂移。圆形可见区半径 `世界半径 = kScopeCircleRadiusFrac * min(viewW,viewH) / zoom`。

**v1.1 相机**：开镜时相机向准星方向偏移，保证准星下的世界点不随倍率漂移：`shift = f(zoom)`（1.5x→0.35、2x→0.5，其余按倍率线性），相机中心 = 玩家 + (准星世界点 − 玩家) × shift。由于 `UpdateCursor` 用 `GetZoom()` 反解世界坐标，该偏移只是把鼠标灵敏度按 `1/(1-shift)` 放大，不产生累积漂移。

Engine 原有 CS2DGame::Update 签名保留。HandleInput 采集到右键边沿后存 Engine 成员，Engine::Update 在调用 game.Update 之前调用一次 UpdateScopeInput 并消费边沿；不在 HandleInput 和 Update 各切换一次。`gameplayInputAllowed = PLAYING && !buyMenuOpen_ && !sideChoicePending_ && !sidePopupOpen_ && !IsTransitionActive() && HasWindowFocus() && !ImGui::GetIO().WantCaptureMouse`。当 Update 不执行（暂停/设置/退出）时也要在状态切换路径 ResetScope；InputBlocked/FocusLost 等重置不得依赖 game.Update 得到 dt。

功能方负责在 SwitchWeaponSlot/DropCurrentWeapon/成功购买替换当前武器、换弹起始、死亡/重生、RoundRestart、Reset/LoadMap/离开比赛处重置；每次 Update 防御检查当前 WeaponId 与 ScopeView.weapon 不同就 Reset。消耗性切槽不能漏掉投掷物自动切回。

Renderer 接口由功能方声明/接线：

```cpp
void UpdateCamera(float playerCenterX,float playerCenterY,
                  int viewportW,int viewportH,float zoomMultiplier=1.0f);
void SetScopeMask(bool active,float centerX,float centerY,float radiusWorld); // v1.1 新增
void DrawScopeOverlay(const Presentation::ScopeView& scope,                   // v1.1 签名变更
                      float centerScreenX,float centerScreenY,float radiusPx,
                      int viewW,int viewH);
```

CS2DGame::Render 先按倍率算相机偏移再调 UpdateCamera；`GetZoom()` 返回实际倍率，`UpdateCursor` 不二次乘；`visionRadius_` 仍按 base fitZoom 计算（开镜不改变敌人知情范围，圆形可见区单独生效）。预览相机 SetPreviewCamera 不受开镜影响。Render 在 UpdateCamera 之后、绘制角色之前调用 `SetScopeMask`，在世界雾之后、闪光覆盖之前调用 `DrawScopeOverlay`，圆心传「准星世界点投影到屏幕」的坐标（避免相机被地图边界钳制时镜筒与可见区错位）。UI 方只需按 `centerScreenX/Y + radiusPx` 画镜筒与刻度，不做命中/相机计算、不重复画遮罩。功能方新增 GameUI::InGameCtx::scope（ScopeView），Engine 填充，UI 用于提示档位（level / targetZoomMultiplier）。

## 5. CT/T：沿用 Team，不建立另一份阵营状态

功能方在 GameUI::InGameCtx、BuyMenuCtx 新增 `Team playerTeam=Team::None`，Engine 从 `GetPlayer().team` 填充；SideSelectCtx 新增 `bool bombMode=false`，由 Engine 的实际 MatchMode 填充。ScoreRowView 的 int team 保留既有0/1/2约定。

SpriteDrawParams 新增 `Team team=Team::None; bool friendly=false; bool localPlayer=false;`，CS2DGame::Render 的 fillParams 填充，friendly 按当前观察者阵营计算。现有敌人剔除/观战可见性检查必须先执行，之后才 DrawCharacter；标识复用已经获准绘制的 sprite，不新增全敌人遍历。死亡/None 不错误标成 CT。

UI 方 `Graphics/TeamVisualStyle.hpp` 定义 `TeamVisualStyle GetTeamVisualStyle(Team team)`（inline，无动态状态），返回字段 `SDL_Color accent; const char* label; enum class BadgeShape {Circle,Diamond,Neutral}; BadgeShape badge;`。CT 蓝/圆、T 橙/菱形、None 灰/中性；不按“敌人恒红”覆盖阵营映射。ImGui 与 SDL 同源配色映射，函数不更改 SDL 状态。战斗角色视觉只改 DrawCharacter 和 assets/ui 标记，不改 spriteKey/碰撞大小或角色业务 team。

## 6. 枪弹可见性：数据由功能方出，绘制由 UI 方做

CS2DGame 新增：

```cpp
const std::vector<Presentation::BulletVisualSegment>& GetBulletVisuals() const;
```

功能方在 UpdateBullets 的实际运动前后生成世界坐标段；不根据 vx 从弹头无限反推。每次模拟段赋唯一递增 id，duration/remaining 初值0.08s。实际 wall/player/bot 碰撞使 impact=true；射程到期仅普通段。先随 dt 衰减旧段再加入新段；上限512段，满时丢最旧；Reset/新回合清空，不在 Render 消费/计时。失活弹擦除前保留最后一段，避免高速弹当帧消失就完全不显示。两次 Render 不应改变 GetBulletVisuals 内容。

当前碰撞是终点检测，并不提供精确接触面。不能声称 toX/toY 为精确命中点；渲染层按地图阻挡瓦片裁剪线段，碰撞火花可选且只能在确认的阻挡边界/已有角色命中点显示，未获可靠点时不画火花。此任务不改物理碰撞算法或伤害判定。

Renderer 新增 `void DrawBulletVisuals(const std::vector<Presentation::BulletVisualSegment>& segments);`。功能方在 CS2DGame::Render 原子弹循环位置调用一次；UI 方实现按 remaining/duration 淡出、逻辑像素宽度1.5～3、长度上限24逻辑像素，并裁剪真实段及阻挡瓦片。几何绘制无纹理可用；不得在视野雾/闪光之后补画世界弹道。保留 DrawBullet 四参接口作兼容，但主 Render 不再与新接口重复画。保留 DrawThrownGrenade，不让投掷物调用新枪弹接口。UI 不触碰 UpdateBullets 的业务逻辑。

## 7. 小鸡：使用现有 API 和确定的资产边界

功能方 PIX-12 交付路径维持 `assets/models/chickensoft/chicken.glb`，固定基础动画名 `walk`（区分大小写、循环），y 向上，模型单位/朝向/原点在新增 `assets/models/chickensoft/manifest.md` 写明数值，并记录经现有 Draw(scale,yOffset,yaw) 校准后的参数；不得没有核对就假设模型已有正确比例。使用现有支持的无纹理材质色、TRS、LINEAR/STEP；外部纹理/PBR/扩展压缩不作为必需功能。新动画必须列名并用 PlayAnimation 返回值检查，不满足则回退 walk。

PIX-13 调用已存在 API：`bool Menu3DModel::Load(const std::string&, const std::string& preferredAnimation="")`、`bool PlayAnimation(const std::string&)`、`void Update(float dt)`、`void Draw(float scale,float yOffset=0,float yawDegrees=0,float opacity=1) const`；LoadModels 负责加载，UpdateChickens 负责路径，DrawModels 负责绘制。Load 失败用 LastError 记录并走既有降级，不能将未载入实例当已就绪。

GameUI 的菜单只通过现有 MenuCtx.backdropTex/backdropW/backdropH/backdrop3D 读取背景；菜单交互继续通过 Actions.focus3DSelect/close3DFocus 等回调传给 Engine。UI 方 PIX-7 不改 Menu3D 文件或 kObjectCount/ObjectIndex：0=T,1=CT,2..4=鸡；不新增 GameUI->Menu3D 直接依赖。背景贴图归 Menu3D，UI 不释放。模型纹理缓存归各自加载器，不与 WeaponIconCache 混用。

## 8. 合并顺序与任务交付边界

1. 功能方先交“契约基础 PR”：共享头、声明、默认不激活 ScopeView、购买详细结果、Ctx/输入字段、最小接线和可链接绘制/cache stub；改完全部调用点，可独立构建。此时 DrawBuyMenu 可暂时忽略结果做旧样式显示，不得调用两次购买。无开镜/新弹道默认行为时也必须可运行。
2. 基础 PR 合入 main 后双方分别基于该合并 SHA 开分支；没有接口基础不得各造同名不同结构。UI 方先 PIX-6 样式，再 PIX-7/8/9/16 的表现；新字段按契约使用。
3. 功能方 PIX-14 完成状态/相机倍率、PIX-17 所需游戏视觉段 producer；UI 方 PIX-10 实现 DrawScopeOverlay，PIX-17 实现 DrawBulletVisuals。生产者与消费者分别提交，空 stub 只允许在明确未完成阶段存在。
4. 功能方 PIX-12 资产可独立并行；素材完成再 PIX-13 接入。资产不与 UI 排版 PR 混杂。
5. 功能方整合共享 cpp，双方从同一集成 SHA 完成 PIX-11/15 验收后再合并候选 PR。每个 PR 针对最新目标 main 解冲突，重新编译；禁止直接推 main，按仓库 docs/pr-workflow.md 走分支→PR→评审→合并。不得无授权由 AI 直接合并。

这里的顺序是技术依赖，不是 Linear 优先级或截止日期。所有任务保持 No priority。

## 9. 必须通过的验证与交接格式

- 两端从干净构建目录 `cmake -S PixelEngine -B build/<platform>`，再 `cmake --build build/<platform> --parallel`；Windows 先采用仓库 README 要求的 MinGW UCRT 及对应生成器，Mac 用本机可用工具链，记录完整命令/版本。Mac LLM 当前使用 stub/规则回退，不能宣称 LLM 与 Windows 等价；本轮接口与 UI 不依赖真实 LLM。
- 基础/购买测试：所有 BuyCode 可复现；Evaluate 无副作用；一次 Execute 成功只扣一次；失败快照不变；两次同枪请求第二次 AlreadyEquipped；护甲/闪光总数边界；非法 WeaponId 无越界。
- 开镜测试：边沿只切一次；按住不抖动；三档循环 关→1.5x→2x→关 且每档 0.12s 过渡；圆形可见区（圆内可见、圆外不可见、软边渐变、与朝向无关）；切枪/丢枪/自动切回/换弹/死亡/重生/暂停/设置/失焦/选边/买枪/新回合/退出均正确归零；鼠标相对位移转换取实际 GetZoom；DrawScopeOverlay 空实现替换后确认真正在运行。
- 阵营测试：None/T/CT、chooseSide 0/1 显式映射；只给可见角色标记；死斗不显示爆破职责。
- 枪弹测试：512上限、0.08s清理、重复Render无副作用、最后一段保留、穿墙视觉裁剪、不同帧率/缩放；DrawThrownGrenade 回归，烟雾火区/闪光遮挡保留。
- 生命周期：退出/重建 renderer 前 cache.Shutdown，UI 借用资源不得重复释放；小鸡模型失败不崩；两端文件路径大小写一致。
- 每条 Issue/PR 交付模板：base SHA / head SHA / 契约版本 / 修改文件和函数 / 新旧调用点 / 测试命令与逐项结果 / 截图或录屏 / 对方待验项。没执行写“未验证”；不能仅以截图或 stub 能链接就关闭功能任务。
- 这次只制定契约和任务，不代表上述接口已实现、已编译或已验证。

## 10. 变更记录

### v1.1（2026-10-31，功能方提出，需求方确认）

开镜从「单级 1.5x + 屏幕中心镜框」改为：

1. **三档循环**：右键 关 → 1.5x → 2x → 关（`level` 字段；`kScopeZoomLevels`；每次换档 0.12s 过渡）。
2. **圆形可见区取代扇形**：开镜时可见区 = 以准星为圆心的圆（软边），圆外不可见并由更实的雾遮盖；
   `Presentation::ComputeScopeVisibility` 是唯一几何定义，`CS2DRenderer::SetScopeMask` 消费。
3. **相机向准星偏移**：1.5x → 0.35、2x → 0.5，保证准星下的世界点不随倍率漂移。
4. **`DrawScopeOverlay` 签名变更**：新增 `centerScreenX/centerScreenY/radiusPx`，圆筒由 CS2DGame
   按准星世界点投影给出；UI 方只画表现，不重复画遮罩。

影响面：`PresentationContract.hpp`（ScopeView 字段 + 档位表 + 几何函数）、`CS2DGame`（档位/过渡/相机偏移/
SetScopeMask 接线）、`CS2DRenderer`（SetScopeMask、ComputeVisibility 圆形分支、DrawScopeOverlay 新签名）、
`Engine`（无改动）、`GameUI::InGameCtx.scope`（多两个字段，UI 可用于显示档位）、回归测试与 PIX-10 说明。

未变更：右键边沿与 `gameplayInputAllowed` 门控、所有归零路径、`GetZoom` 不二次乘、`visionRadius_` 仍按
baseZoom 计算、伤害/散布/移速/射速不受影响。

## 11. 实现进度（功能方追加，非契约正文）

见 `docs/handoff-windows-ai.md`：base/head SHA、逐 Issue 改动文件与函数、构建与测试命令、验证结果、对方待验项。
