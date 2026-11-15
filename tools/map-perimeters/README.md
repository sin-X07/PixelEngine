# 地图外围制作源

四套外围环境由当前高清地图与 `bake.py` 确定性烘焙产生。地表从各地图的低对比度地面区域取样，保持地图材质和色调。旧 `materials` 与 `*_void.png` 保留作为历史制作源。

古代遗迹使用灰褐碎石、苔绿细节和残缺石基。荒漠迷城使用灰米色地表、枯草及陶瓦屋顶片段，陶瓦纹理复用原地图。沙漠Ⅱ采用原地图沙粒纹理拼接，生成素材参与低频变化，并在原图外侧添加低透明度漂移沙尘。

## 重新制作

安装 `requirements.txt` 中的 Python 依赖，然后在仓库根运行以下命令。可用 `-Python` 指定虚拟环境的解释器。

```powershell
./tools/map-perimeters/bake.ps1 -Python ./build-map-perimeters/venv/Scripts/python.exe
```

脚本先在 `build-map-perimeters/baked` 烘焙全部八份纹理，检查通过后复制到 `assets/cs2d/perimeters`。已存在的同名外围纹理会被覆盖。发布目录也需要复制更新后的六份资源，或重新链接 EngineApp 触发现有构建资源复制步骤。

## 遮罩与坐标

四张高清地图均为 1 像素对应 1 世界单位。外围四侧各扩展 256 世界单位。地图原图逐像素保留，图外沿边缘向外 64 世界单位平滑过渡到循环地表，靠近边缘的 8 单位内逐步引入轻微模糊。装饰限制在过渡带以外。加载器按原图像素与世界尺寸换算。

`*_ground.png` 为 256 像素循环地表，持续覆盖外围场景之外的镜头。`*_scene.png` 包含地图和近景外围，边缘回到循环地表的相同采样相位。

## 验证

开启现有 `BUILD_GAMEPLAY_TESTS` 构建选项后，可构建 `map_perimeter_review`，运行参数为仓库根目录和证据输出目录。测试使用生产地图解析器、碰撞世界和 SDL 渲染器，检查四张地图、六种画幅、八处边缘、开镜倍率、推进镜头、回退绘制和资源清理。

本次验收记录位于 `build-evidence/map-perimeters/ART_REVIEW.md`。
