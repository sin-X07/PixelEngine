# ComfyUI 地图局部重绘

> **推荐优先使用 `PixelEngine-map-crop-stitch`（`map-crop-stitch.json`）**：基于 ComfyUI-Inpaint-CropAndStitch，自动按蒙版裁切 → 放大到 1024² 生成 → 缩回贴回。蒙版外像素逐位不变（不经过 VAE），多轮修补不会累积变糊。轮廓图由第二个参数相同的 Crop 节点同步裁切。用 `check_stitch_diff.py before.png after.png --out diff.png` 验证改动范围。工作流由 `gen_map_crop_stitch.py` 生成。

用正确的墙线约束 SDXL，只重绘你圈出的区域。工作流预载了当前 `dust_2` 底图的副本和从当前 Tiled `Collisions` 层导出的轮廓，默认提示词是补墙。

## 细修小墙缝和纹理接缝

在工作流列表打开 `PixelEngine-map-local-detail`，也可以导入同目录的 `map-local-detail.json`。它把底图、蒙版和轮廓裁到同一块，放大局部以后以原图纹理为起点重绘，最后按原坐标贴回。贴回时只在原蒙版里面柔化接缝。所有节点使用现有 ComfyUI 和模型。

1. 载入同尺寸的底图和目标 Canny 图，在 **原图与修复蒙版** 只圈一处要修的区域。
2. 在左侧填写 **裁切左上角 X / Y**，单位是原图像素，左上角为 `(0, 0)`。边长默认 `256`。可以用修复区域中心坐标各减 `128` 算出 X、Y，再把裁切框移进图内。给蒙版四周留一些未改的墙和地面作参照。图内的 **本图细修位置与操作** 也列出了最近三处蒙版的位置。
3. 用正向提示词写清笔下区域需要保留的墙宽、砖缝和路面，用 Canny 图约束形状。先用默认降噪 `0.62`。原纹理已经接近目标时可以试 `0.45–0.55`。墙体形状需要明显改变时，确认轮廓和蒙版后再试 `0.68–0.75`。
4. 运行后看 **检查选中的小块**。白色应只覆盖本轮待修处。整图 PNG 保存在 `D:\PixelEngine-AI\ComfyUI\output\map-local-detail\`。要把多处改动累加到一张图，下一轮从上一轮保存的整图开始，再圈下一处。

如果某处仍反复生成过大的石块，可以先用同图相邻区域的一小段墙面或路面作底，再用细修工作流以约 `0.4` 的降噪处理接口。

## 打开并运行

1. 打开 [本机 ComfyUI](http://127.0.0.1:8188/)，在左侧 **Workflows / 工作流** 双击 `PixelEngine-map-local-repair`，再收起侧栏。也可以把同目录的 `map-local-repair.json` 拖进画布。
2. 在 **原图与修复蒙版** 选正在修的地图 PNG。在 **正确布局 · Canny 轮廓** 选这张底图对应的黑底白线图。两张图要是同一地图、同一裁切范围、同一尺寸。当前 `dust_2` 可以直接用预载的组合。
3. 右键 **原图与修复蒙版** → **Open in Mask Editor / 打开遮罩编辑器**。用蒙版笔以不透明度 `1.0` 涂满错误区域，笔刷边缘可稍软。每次处理一个缺陷，保留周围大部分正常墙面或道路作为参照。点 **Save / 保存** 返回画布。工作流使用 PNG 的透明度承载蒙版，带透明背景的底图请先导出一份 RGB 副本。
4. 按下面的两种情况选择提示词，点 **Run / 运行** 或按 `Ctrl+Enter`。在 **检查蒙版** 中，白色表示要修的位置。在 **检查轮廓与原图是否对齐** 中，白线应沿着附近结构正确的墙边。若整体偏移，检查输入图的版本和裁切范围。
5. 在 **保存修复后的整张地图** 查看输出。PNG 位于 `D:\PixelEngine-AI\ComfyUI\output\map-local-repair\`，尺寸与原底图一致。继续修下一处时，在原图节点载入刚保存的 PNG，再画新的蒙版。核对游戏里的通行时，把 PNG 与 Tiled 的 `Collisions` 图层叠起来看。

## 墙壁画成道路

涂满误画成路面的墙段，并盖到两端的接缝。正常道路尽量留在蒙版外。使用工作流默认的两段提示词，必要时把 `stone masonry wall` 换成相邻墙面的材质。

**正向提示词**

```text
Pure orthographic top-down game map, continuous solid stone masonry wall cap, straight aligned edges, same width, masonry, color and lighting as the adjacent wall segments, seamless joins at both ends, clearly readable overhead wall silhouette, match the surrounding map texture.
```

**反向提示词**

```text
opening through the wall, roadside gap, path across the wall, broken wall ends, stairs, props, characters, labels, text, watermark, isometric view, perspective, dramatic shadows.
```

## 用墙封住道路

在准备封路的位置，沿道路的整个宽度涂出一道有厚度的横向蒙版，两端稍微盖到路边原有墙体的接缝。把新墙前后的正常路面留在蒙版外。Canny 轮廓图里也要有横跨整条路的墙轮廓。如果现有轮廓仍显示这里可通行，先用包含新墙的目标布局重新导出轮廓，再加载新版。把下列两段分别贴进正向和反向节点。

**正向提示词**

```text
Pure orthographic top-down game map, solid continuous stone masonry wall cap spanning the full width of the road from one side wall to the other, sealed joins at both ends, straight parallel edges and consistent wall thickness, matching the nearby masonry texture, color and lighting.
```

**反向提示词**

```text
open passage, gap through the wall, doorway, archway, gate, walkable path across the wall, broken or disconnected wall ends, rubble, stairs, props, characters, lettering, watermark, isometric view, perspective.
```

## 调整结果

| 结果 | 调整办法 |
| --- | --- |
| 补墙后还有缺口 | 检查蒙版是否横跨目标墙体，确认轮廓也画出了完整的墙。`denoise` 默认 `0.88`，可以试 `0.95`。ControlNet `strength` 默认 `0.85`，轮廓对齐时可以试 `0.95–1.05`。 |
| 结构正确，材质接缝明显 | 先换种子再跑一张。也可以把 `denoise` 调到 `0.75–0.82`，让蒙版边缘略微盖住接缝。 |
| 大图上的细墙变糊 | 在 **重绘分辨率** 节点把 `megapixels` 从 `0.8` 提到 `1.0`。遇到显存不足时可试 `0.6`，最终 PNG 尺寸仍按原图保存。 |

## 从 Tiled 导出轮廓

碰撞布局改过后，从项目根目录运行下面的 PowerShell 命令。把 `.tmj` 和 PNG 路径换成当前正在修的地图，输出名用新版本号。脚本读取覆盖全图的底图和顶层 `Collisions` 中的矩形与多边形，生成同尺寸的黑底白线 PNG。

```powershell
& 'D:\PixelEngine-AI\venv\Scripts\python.exe' .\tools\export_comfy_map_guide.py `
  '.\assets\cs2d\maps\dust_2.tmj' `
  '.\assets\cs2d\tilesets\dust_2.png' `
  'D:\PixelEngine-AI\ComfyUI\input\dust_2_layout_v2.png'
```

生成后，在 **正确布局 · Canny 轮廓** 节点上传或选择新文件。像素内容来自圈选后的生成结果，游戏中能否通行由 `.tmj` 的碰撞层决定。

[ComfyUI 官方蒙版编辑器说明](https://docs.comfy.org/interface/maskeditor)
