# 布局约束 AI 生图实施状态

更新日期为 2026-11-11。依据用户提供的方案 2.5 执行。首轮曾完成导入，但视觉效果已被用户否决。用户最新决定采用原四宫格右下角的候选 4 原图，并自行在 Tiled 中调整碰撞。已按该决定完成图片导入，源项目、release、预览资源一致。后续碰撞调整由用户进行，原计划对齐验收已按用户新要求改为后续手工调整。最新进度见文末。

## 环境配置已完成

- [x] 所有新增软件、Python 环境、模型、依赖、临时文件和下载缓存位于 D:/PixelEngine-AI。安装器生成的 Python 启动器也已移入该目录。
- [x] 独立 Python 3.12.16、OpenCV 5.0.0、NumPy 2.5.4 已安装并验证导入。
- [x] PyTorch 2.14.1+cu130、torchvision 0.29.1+cu130 已安装，RTX 5060 Laptop GPU 上的 CUDA 矩阵运算通过。
- [x] ComfyUI 0.45.0 已启动，本地地址 http://127.0.0.1:8188，全部 86 个包通过兼容性检查。启动脚本为 D:/PixelEngine-AI/start-comfyui.ps1。
- [x] 官方 SDXL base 1.0 与 ControlNet-canny SDXL fp16 均下载完成，SHA-256 匹配官方元数据，且实际推理成功。
- [x] Tiled 已存在于 D:/Tiled/tiled.exe。

模型校验记录位于 D:/PixelEngine-AI/downloads/sdxl-verified.json 和 controlnet-verified.json。环境变量脚本为 D:/PixelEngine-AI/environment.ps1，安装版本清单为 D:/PixelEngine-AI/installed-packages.txt。

## 步骤 0 已完成

- [x] 初始工作区干净，分支为 feat/menu-persistent-weapon-poses。
- [x] 确认原工作区 dust_2 为 1600×1600 世界，未满足要求。按计划从本地 main 重建修正版，仅修改顶层 width、height、tilewidth、tileheight 四值，验证其余内容与 main 完全相同。
- [x] 修正版已写入项目 assets/cs2d/maps/dust_2.tmj。世界 880×880，网格 440×440，tile 2×2，背景声明 880×880，Collisions 137，spawn_t 8，spawn_ct 7。
- [x] 用户明确要求跳过旧空气墙位置记录。
- [x] 旧地图和旧图片备份于 D:/PixelEngine-AI/backups/dust_2_before_880.tmj 与 dust_2_hd_ai_v1_backup.png。

## 步骤 1 已完成

- [x] D:/PixelEngine-AI/work/dust_2/dust2_layout_mask.png 与 dust2_layout_canny.png 均为 1600×1600。
- [x] 墙占比 0.653673046875，Canny 边缘像素 22564，数值和目视检查通过。
- [x] ancient_ruins、mirage、cs_office 的掩膜和 Canny 图也已生成，分别位于 D:/PixelEngine-AI/work 下同名目录。均取 main 原始世界与碰撞，碰撞数分别为 149、259、200。ancient_ruins 与 mirage 的参考基线修正了背景 jpg 后缀。其余地图的游戏资源未改动。

## 步骤 2 已完成，采用第 7 节的分区生成路线

- [x] 先生成 4 张 ControlNet-canny 变体，1024×1024，25 步、CFG 6.5、dpmpp_2m/karras、强度 0.65，seed 为 2026111101–2026111104，随后 Lanczos 放大至 1600×1600。
- [x] 4 张的可信墙/地面 RGB 均值距离依次为 14.47、23.37、34.12、20.65。选择第 3 张首轮合成，梯度比为 5.35，但目视检查仍发现跨界，未接受该结果。
- [x] 依据第 7 节，继续分别生成墙体和地面纹理，各 2 个变体。选用 wall_2_00001_.png 与 floor_2_00001_.png，Lanczos 放大后按原布局分区。最终分区 RGB 距离为 72.8496。

此路线的成图为灰色石砖墙区与沙地走廊。风格较简洁，原整图变体未作为最终背景。所有工作流 API JSON 与生成记录位于 D:/PixelEngine-AI/work/dust_2，原始图位于 D:/PixelEngine-AI/ComfyUI/output。

## 步骤 3 已完成

- [x] 严格使用计划原文硬遮罩代码进行最终合成，腐蚀核 9×9，羽化 sigma 1，阴影外扩 0。
- [x] 最终候选图为 D:/PixelEngine-AI/work/dust_2/dust_2_composite.png，1600×1600。
- [x] 碰撞叠加图为同目录 dust_2_overlay_check.png，137 个矩形。
- [x] 固定随机种子抽查 5 处墙角，放大检查通过。坐标记录为 corner_coordinates.json，汇总图为 five_corner_check.png。

## 步骤 4 已完成

- [x] 最终分区距离 72.8496，通过 >15 阈值。
- [x] 边界梯度 157.5，非边界梯度 60.1，比值 2.62，通过 >2 阈值。详细记录为 composite_validation.json。
- [x] 在 D:/PixelEngine-AI/game-preview 建立独立预览资源，加载新图，实际游戏截图确认显示正常。预览使用已有 build-release 的 EngineApp.exe，版本显示 0.3.0-release (3d2e2c4)。
- [x] 使用现有 build-dev 的 dev_replay_runner 完整运行 5v5 仿真，退出码 0，14 回合，T 8:6 CT，53631 tick，下包 10 次、拆包 6 次，ai_conflicts 为 0。记录位于 D:/PixelEngine-AI/work/dust_2/replay/summary.csv。
- [x] 用户完成预览走测并明确反馈“没有问题，导入吧！”，人工走测验收通过。

完整仿真、游戏显示检查与用户人工走测均已完成。

## 步骤 5 已完成

- [x] 旧图片备份完成，导入前已确认旧项目图片 SHA-256 与备份一致。
- [x] 独立预览目录已加载新图，并完成上述 5v5 仿真。
- [x] 用户授权后已正式导入 assets/cs2d/tilesets/dust_2.png，并同步 build-release 的地图与图片资源。release 原资源另备份于 D:/PixelEngine-AI/backups/build-release。
- [x] 最终校验通过。源项目、release 与候选图片 SHA-256 一致，尺寸均为 1600×1600。地图世界仍为 880×880、tile 2×2，图片导入期间地图文件未变化，地图全部数据与 main 加四个网格值修正一致。
- [x] 最终验收记录已保存至 D:/PixelEngine-AI/work/dust_2/final_acceptance.json。

Linear 同步仍由原计划指定的 Muse 负责，本次未发送外部消息或关闭 issue。

源项目 dust_2.tmj 已恢复修正版基线，dust_2.png 已更新为验收通过的分区合成图。原计划文档写入返回 Access denied，因此采用本状态文件同步完成情况。

## 第二轮实施进行中

依据修正版计划开展第二轮。首轮分区纹理成品已被用户否决，之前的完成结论仅记录首轮历史，第二轮验收尚未完成。

- [x] 阅读修正版计划并保存到 D:/PixelEngine-AI/work/dust_2/round2/plan.md。
- [x] 复核步骤 0/1，游戏地图与 main 加四个网格值修正完全一致，世界 880×880，原掩膜占比和边缘数复验通过，ComfyUI 服务可用。
- [x] 复用 4 张原候选图，整理至 round2/candidates。当前已导入的首轮分区图另行留档。
- [ ] 步骤 3 批量合成 4 张图及叠加图，进行中。
- [ ] 步骤 4 逐张目视，记录 A/B/C 缺陷与排序。
- [ ] 步骤 4b 最多三轮定点修复及复验。
- [ ] 最终视觉验收、走测及步骤 5 导入。

- [x] 第二轮步骤 3 已完成，四张候选图均由修正版原文脚本生成合成图和叠加图，输出在 round2/composites。
- [x] 步骤 4 已逐张检查并建立 round2/defects.md 与 defects_initial.json。四张均有明显或严重问题，当前无可验收图。暂以树篱覆盖较多的候选 1 作为修复对象，排序 1、2、3、4，排序来自目视判断。
- [ ] 步骤 4b 进行中，先试局部 inpaint，保留原 canny，去噪 0.5，逐处修复后复验。存在区域性 C 类错画，必须复验，不能因数值通过定稿。

### 定点修复第 1 轮已执行，复验未通过

对候选 1 的 7 个重点缺陷逐处进行 ComfyUI inpaint，原 canny 裁切沿用，去噪 0.5。遮罩以外像素贴回时保持原样，之后再次运行原硬遮罩合成。生成历史与工作流在 round2/repair_1。

复验发现遮罩内灰色空块，画面不合格，本轮结果被拒绝。第二轮从未损坏的候选 1 合成图重新开始，采用保留原图潜变量并显式设置局部噪声遮罩的工作流，去噪 0.7。正式图片尚未替换。

### 定点修复第 2 轮已执行，复验仍未通过

去噪 0.7，保留原图潜变量并显式设置噪声遮罩，完成 7 处局部重绘和重新合成。灰色空块问题消失，但中央偏右和左侧长墙内仍画成沙地，C 类明显缺陷未解决，部分贴回区域也出现纹理接缝。记录在 round2/repair_2。

第 3 轮采用计划步骤 4b 的代码仿制图章，仅修缺陷位置，从原候选图同类别区域复制小块纹理、3px 羽化，源区域逐块验证全部属于同类布局。完成后重新合成和目视复验。

### 定点修复第 3 轮已完成，按计划停止并汇报

第三轮采用同类局部仿制图章，共 60 个小块，3px 羽化，并再次执行原硬遮罩合成。源纹理均取自原候选图，逐块验证源矩形全部属于同类布局。输出在 round2/repair_3。

目视复验仍有明显或严重 C 类墙内沙地/铺地缺口，局部地面修补还产生规则接缝。未达到画面同级与缺陷清零的要求。按新计划步骤 4b 的三轮上限停止。详细记录在 D:/PixelEngine-AI/work/dust_2/round2/review.md，剩余重点缺陷在 defects_final.json；清单非穷尽。

- [x] 4 张候选图全部合成、叠加比较和随机墙角检查。
- [x] 3 轮局部修复与逐轮复验、记录。
- [x] 整理原候选与第三轮的并排对比 originals_vs_repair3.png，以及 before_after.png。
- [ ] 视觉验收通过，仍有明显/严重问题。
- [ ] 第二轮步骤 5 正式导入、走测和最终验收，待问题解决。

建议按计划补生成条款，针对共同的 C 类问题补生成 2–4 张候选，再重新比较。按三轮停止条款，该下一步由用户决定。第二轮未改动项目布局和正式图片，原图备份完整。

## 用户授权继续，补生成第 1 批进行中

用户明确选择“补生成 2–4 张并继续修复”。已排入 4 张同风格 ControlNet-canny 候选，编号 5–8，强度 0.6–0.7，继续使用原始布局 canny 与 SDXL。提示词重点强调墙块内部为实体石墙/浓密树篱，减少墙内空院。记录位于 round2/supplemental1。

- [ ] 候选 5–8 生成完成并全部合成、叠加比较。
- [ ] 选择新主攻对象，更新缺陷清单并修复。
- [ ] 最终视觉验收与正式导入。

### 补生成 4 张完成，继续局部修复

- [x] 候选 5–8 全部生成、合成、叠加比较。RGB 距离依次为 42.3、1.8、44.3、30.4，数值仅辅助。
- [x] 目视仍发现共同 C 类问题，候选 6 整体对比不足，候选 8 还出现装饰符号。新图未超过原候选图的画面水准。
- [ ] 继续以原候选 1 修复，扩大 C 类修补位置至对应的真实碰撞矩形，并使用颜色更接近周围铺地的同类地面源，减少沙地边框和规则拼缝。该补充迭代在用户已授权的“继续修复”范围内。

### 补充迭代第 1 轮完成

针对原候选 1 的 6 处墙内缺口和 3 处地面/边界问题，使用原图同类局部纹理修补，输出 supplemental1/repair_1。复验显示中央和左侧大块墙内沙地已改善，地面复制源改用近似灰色铺地后，规则浅色接缝减少。仍有下方中央连续墙块和北侧短墙内部呈地面，进入第 2 轮。

区域颜色统计仅用于定位待复核区域，未将非绿色像素自动判为缺陷。第 2 轮选取目视可见的 9 处墙内缺口，继续局部修补。

### 补充迭代第 2 轮完成

修补了 9 处目视可见的墙内缺口，复制小块 63 个，输出 supplemental1/repair_2。中央大块墙内沙地与下方连续墙块内部已改进；复验继续发现北侧中部墙内铺地、若干小墙块，以及西北、中央、东侧地面内绿篱错画。

第 3 轮仅针对以上新定位的 10 个局部位置修补，继续取自原候选同类纹理，完成后重新合成与复验。需用户最终并排判断画面水准。


### 用户追加授权后的补充三轮已完成，仍待验收

- [x] 补生成候选 5–8，全部合成和叠加检查。
- [x] 原候选 1 的补充第 1/2/3 轮定点修复完成，分别 83/63/52 个同类小块，逐轮重新合成与复验。
- [x] 更新完整对比和剩余缺陷记录至 D:/PixelEngine-AI/work/dust_2/round2/supplemental1/review.md 与 review.json。
- [x] 最新并排图为 supplemental1/originals_vs_latest.png，左侧四张原候选，右侧当前修复结果。完整叠加在 supplemental1/repair_3/overlay.png。
- [ ] 视觉验收。东侧与西北地面修补仍有拼接感，中央墙内局部缺口仍需处理，边界误差未完成全图逐处确认。辅助指标不能作为通过依据。
- [ ] 第二轮正式导入和走测，待验收。

本次补充修复达到三轮上限，依计划停止并提交材料。建议下一轮优先处理东侧拼接与中央缺口，由用户决定继续以候选 1 修复或换其他原候选。当前正式图片为首轮已导入的石砖图，第二轮图均为待审成果，未更改地图数据。


## 用户最终选定原候选 4，已完成导入

用户明确指示“采用左侧四宫格右下角的这一版地图吧，我可以之后在 tiled 里更改碰撞区域”。本指示覆盖此前必须先实现完全对齐才导入的要求，采用原候选图作为画面。

- [x] 确认选中原候选 4，即 round2/candidates/cand_4.png，1600×1600，由最初 variant_4 经 Lanczos 放大得到。
- [x] 将所选原图导入 assets/cs2d/tilesets/dust_2.png，同步 build-release 和 D:/PixelEngine-AI/game-preview 的相应图片。
- [x] 源项目、release、预览图片 SHA-256 全部与选定原图一致，尺寸均为 1600×1600。
- [x] 本次图片导入前后地图文件字节一致，世界为 880×880。
- [x] 被替换的图片备份至 D:/PixelEngine-AI/backups/before_candidate4，最初旧图备份继续保留。
- [x] 交付记录为 D:/PixelEngine-AI/work/dust_2/round2/selected_candidate4_import.json。

后续碰撞区域由用户在 Tiled 中调整。目前图像与现有碰撞之间的偏差仍存在，本次不将其记录为“对齐验收通过”。本次范围为 dust_2 所选图片导入，其余地图的进展沿用前述记录。Linear 同步由 Muse 负责。


## 2026-11-12 按用户要求缩小 Tiled 背景

- [x] 依据当前地图的 880×880 世界范围，将选定背景图从实际 1600×1600 等比缩为 880×880，采用 Lanczos4。
- [x] 同步项目、release 和预览图片，并校验尺寸和文件一致性。
- [x] 地图文件导入前后字节一致，保留用户当前所有碰撞及对象编辑。
- [x] 原尺寸图片备份于 D:\PixelEngine-AI\backups\tiled-resize\dust_2_1600x1600_e0d247afdc0d.png。

此步骤修正 Tiled 显示背景和碰撞布局的比例差异，局部碰撞边界继续由用户编辑。当前实际背景尺寸更新为 880×880，先前 1600×1600 的记录为历史状态。

## 2026-11-12 其他三张地图批量制作

用户要求全部制作后统一验收修改。本批范围 ancient_ruins、mirage、office，交付目录 D:/PixelEngine-AI/delivery/other-maps。每图生成四张候选、高清版本、世界尺寸一致的 Tiled 编辑版本和原布局对象。碰撞由用户后续统一调整，正式项目资源待统一验收。

- [x] ancient_ruins 四张候选与高清图、Tiled 编辑图、四个候选地图文件已完成。世界 976×912，碰撞 149。默认地图暂引用候选 1，最终选择由用户统一验收。

- [x] mirage 四张候选与高清图、Tiled 编辑图、四个候选地图文件已完成。世界 1168×912，碰撞 259。默认地图暂引用候选 1，最终选择由用户统一验收。

- [x] office 四张候选与高清图、Tiled 编辑图、四个候选地图文件已完成。世界 640×600，碰撞 200。默认地图暂引用候选 1，最终选择由用户统一验收。


### 其他三张地图制作及交付检查已完成

- [x] ancient_ruins、mirage、office 各四张候选，共 12 张，目视检查完成。
- [x] 每张提供高清图、世界尺寸一致的编辑图、独立 Tiled 文件和碰撞叠加图。
- [x] 共 15 份 Tiled 地图通过路径、尺寸、原布局数据和瓦片引用校验。
- [x] 验收说明和 validation.json 已写入 D:/PixelEngine-AI/delivery/other-maps。
- [ ] 用户统一挑选、验收及碰撞修改。
- [ ] 选定图正式导入游戏资源。

默认入口暂用候选 1，具体选择由用户决定。office 保留原瓦片层，尚未加入 MapCatalog。生成的图像与碰撞仍有偏差，依用户新流程留待 Tiled 手工修改。

## 2026-11-12 按用户选定基准各制作两个衍生版本

基准为 ancient_ruins 原候选 3、mirage 原候选 2、office 原候选 3。采用选定原图 img2img + 原布局 ControlNet-canny，A 去噪 0.35，B 去噪 0.50。最终版本待用户对比决定。

- [x] ancient_ruins 基准 3 的 A/B 两版已完成，三图并排为 refinements/comparison.png，顺序原选定版本、A、B。

- [x] mirage 基准 2 的 A/B 两版已完成，三图并排为 refinements/comparison.png，顺序原选定版本、A、B。

- [x] office 基准 3 的 A/B 两版已完成，三图并排为 refinements/comparison.png，顺序原选定版本、A、B。

- [x] 三图六个追加版本已完成，三组原版/A/B 并排图已整理。
- [x] 六个 Tiled 文件的图片、瓦片路径与尺寸、布局对象校验通过，记录在 delivery/other-maps/refinements_validation.json。
- [ ] 用户对比后确定最终版本，随后导入。


## 用户最终选定 A、A、A，三图导入完成

- [x] ancient_ruins、mirage、office 均导入追加的 A 版本，并同步 release 与预览资源，逐文件哈希一致。
- [x] 使用已交付的原世界编辑地图，尺寸分别 976×912、1168×912、640×600；ancient_ruins 和 mirage 从旧高清放大世界恢复到原世界基线。
- [x] office 瓦片图层保留，图片引用已修正为存在的 cs_office_tiles.png。
- [x] 原图片与地图备份至 D:/PixelEngine-AI/backups/other-maps-selected-A，最终记录为 delivery/other-maps/final_A_import.json。
- [ ] 用户在 Tiled 调整局部碰撞。office 尚未加入游戏地图选择目录。


## 2026-11-12 当前四图验收记录

- [x] 资源、图片尺寸、路径、瓦片引用和运行产物同步校验完成。
- [x] 每图最新碰撞叠加与 5 处随机墙角检查完成，重点缺陷已记录。
- [x] 玩法回归测试 267 通过、0 失败。
- [x] 四图各三场完整 5v5 仿真，合计 12 场全部决出胜负。
- [ ] 无空气墙/嵌墙和 ≤2px 世界误差，现有图像与碰撞偏差仍明显，未通过。
- [ ] 全图逐墙及五处墙角实际贴墙走测，未完成；自动比赛不替代该项。
- [ ] office 爆破目标验收，原图 0 炸弹点，三场 0 下包/0 拆包，未通过该标准。

报告位于 D:/PixelEngine-AI/acceptance/2026-11-12/验收报告.md。dust_2 当前 210 碰撞对象，按用户当前编辑成果检查；源布局未修改。总体验收状态为未通过，待碰撞调整后复验。

## 2026-11-12 修复地图选择页与对局背景不一致

原因已定位到 CS2DRenderer::DrawImageBackground。加载 perimeters/*_scene.png 后，旧实现绘制场景图便提前返回，遮住所选背景。选择页直接加载 tilesets 图片，因此显示新图。

- [x] 修复生产渲染顺序，先绘制外围场景，再绘制当前地图背景覆盖游戏内地图区域。
- [x] 在既有 map_perimeter_review 中增加回归检查，替换背景纹理后读取中心像素，断言外围场景不能遮盖当前背景。
- [x] 使用真实生产渲染器检查 ancient_ruins、dust_2、mirage，多分辨率、边缘和转场检查全部通过，退出码 0。
- [x] release 与 dev 的 EngineApp 已重新构建，预览目录 executable 已同步 release 并校验哈希。

截图位于项目 build-evidence/background-render-review，日志位于 D:/PixelEngine-AI/acceptance/2026-11-12/background-render-review.log。此前资源路径和静态图片检查没有覆盖这条实际背景绘制分支，本次新增测试覆盖该回归。
