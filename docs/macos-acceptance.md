# macOS 验收清单（PIX-11）

本文件是 macOS 侧按 PIX-11 验收的逐项清单与执行脚本骨架。
**当前状态：未在 macOS 上执行**（本轮所有验证都在 Windows 完成），因此 PIX-11 的验收结论必须由
在 Mac 上实际跑过的人填写；未跑过的项一律写「未验证」，不得以「应该没问题」代替。

## 0. 记录模板（每次验收复制一份）

| 项 | 值 |
| --- | --- |
| 集成 SHA | 合并后的 `main`（本轮为 PR #20，head 见 PR 页面） |
| 契约版本 | v1.3（`docs/AI-MERGE-CONTRACT-v1.md`） |
| macOS 版本 / 架构 | 待填（`sw_vers`、`uname -m`） |
| 工具链 | `cmake --version`、`clang++ --version` |
| 构建命令 | `cmake -S PixelEngine -B build-macos -DCMAKE_BUILD_TYPE=Release && cmake --build build-macos --parallel` |
| 结果 | 通过 / 失败（失败项单独建 Issue 并指定负责人） |

## 1. 构建与共享头

- [ ] 干净目录配置 + 编译通过（无 error；warning 记录条数）
- [ ] `Core/PresentationContract.hpp` 在新旧编译器下都能单独编译（不依赖 SDL 之外的平台头）
- [ ] `Graphics/TeamVisualStyle.hpp`、`Graphics/WeaponIconCache.*` 被 CMake SOURCES 收录
- [ ] 资源相对路径：exe 同级 `assets/` 可被 `PathUtils::ResolveAppPath` 解析
- [ ] 文件名大小写一致（`chicken.glb`、`manifest.md`、`cs2d/sprites/*.png`）

## 2. 玩法接口（不依赖 UI 也能验）

- [ ] 购买：`EvaluatePurchase` 无副作用；`ExecutePurchase` 成功只扣一次款
- [ ] 购买失败码可复现：InvalidItem / PlayerDead / WindowClosed / OutsideBuyZone / TeamRestricted /
      InsufficientFunds / InventoryFull / ItemLimitReached / AlreadyEquipped
- [ ] AWP 对满甲 112 伤害（一枪死）；AK 对满甲 28 + 4 护甲
- [ ] 开镜：右键边沿只切一次；三档 关→1.5x→2x→关；0.12s 过渡
- [ ] 开镜重置路径：切枪 / 丢枪 / 换弹 / 阵亡 / 新回合 / 暂停 / 设置 / 失焦 / 选边 / 买枪 / 离场
- [ ] 开枪自动关镜；开启「狙击枪延迟关镜」后按住左键保持、松手关
- [ ] 枪弹视觉段：0.08s 寿命、512 段上限、重复 Render 无副作用
- [ ] `WeaponIconCache::Shutdown` 在 `SDL_DestroyRenderer` 之前被调用（退出无崩溃）

自动化：`cmake -S PixelEngine -B build-macos -DBUILD_GAMEPLAY_TESTS=ON && cmake --build build-macos --target gameplay_tests`
→ 期望 `132 通过, 0 失败`（若 Mac 上因 SDL 视频初始化跳过某组用例，记录跳过原因）。

## 3. 界面与视觉

- [ ] 主菜单：3D 背景 + 三只小鸡正常加载、walk 动画无滑步
- [ ] 选边：T 橙/菱形、CT 蓝/圆，卡片描述在非爆破模式不出现「下包/拆包」
- [ ] 购买菜单：卡片显示图标/名称/类别/价格；已装备/已带满有角标；禁用项悬停有原因
- [ ] HUD：顶部比分 T/CT 分色、回合阶段、倒计时 ≤10s 警示色
- [ ] 开镜：镜筒 + 四象限大十字，镜外被雾遮盖，无第二层准星残留
- [ ] 弹道：连发可见曳光，撞墙处被裁剪，命中处有火花
- [ ] 设置页：迷雾透明度 / 延迟关镜 / 开镜灵敏度 / 准星粗细 / 移动扩散 均即时生效并持久化

## 4. 分辨率与焦点

- [ ] 1280×720、1920×1080、16:10（如 2560×1600）三种视口无截断、中文可读
- [ ] 窗口 ↔ 全屏切换后 UI 缩放正确，无重叠/负坐标
- [ ] 失焦：开镜归零、切回窗口后按键不粘连
- [ ] Retina：镜筒/准星用逻辑坐标绘制，不出现半像素模糊或错位

## 5. 已知差异（不算失败）

- Mac 的 LLM 控制器是 stub/规则回退，不能与 Windows 的真实 LLM 教练等价比较。
- Mac 若用软件渲染后端，帧率低于 Windows 的 d3d11 属预期，记录实测值即可。

## 6. 交接

把本文件填完的副本与截图目录一并交给 PIX-15（Windows 验收）对照；
两端都通过后才能把 PIX-11/PIX-15 标为完成。
