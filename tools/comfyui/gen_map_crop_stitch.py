"""生成 PixelEngine Crop&Stitch 工作流（UI 格式 + API 格式）。"""
import json, sys
from pathlib import Path

IMG = sys.argv[1] if len(sys.argv) > 1 else "ancient_ruins.png"
CANNY = sys.argv[2] if len(sys.argv) > 2 else "ancient_ruins_other_maps_canny.png"
OUT_UI = Path(sys.argv[3]) if len(sys.argv) > 3 else Path("map-crop-stitch.json")
OUT_API = Path(sys.argv[4]) if len(sys.argv) > 4 else None

CROP_W = ["bilinear", "bicubic", False, "ensure minimum resolution", 1024, 1024, 16384, 16384,
          True, 10, False, 20, 0.1, False, 1.0, 1.0, 1.0, 1.0, 3.2, True, 1024, 1024, "32",
          "cpu (compatible)"]
CROP_KEYS = ["downscale_algorithm", "upscale_algorithm", "preresize", "preresize_mode",
             "preresize_min_width", "preresize_min_height", "preresize_max_width", "preresize_max_height",
             "mask_fill_holes", "mask_expand_pixels", "mask_invert", "mask_blend_pixels", "mask_hipass_filter",
             "extend_for_outpainting", "extend_up_factor", "extend_down_factor", "extend_left_factor",
             "extend_right_factor", "context_from_mask_extend_factor", "output_resize_to_target_size",
             "output_target_width", "output_target_height", "output_padding", "device_mode"]

POS = ("pure orthographic top-down game map, overhead view, solid continuous dark weathered stone wall cap, "
       "crisp pale beige highlight trim along the top edges matching adjacent walls, straight parallel wall edges, "
       "consistent narrow wall thickness, wall spanning the full width of the passage with seamless joins at both ends, "
       "deep dark contact shadow along one side, surrounded by pale sandy dirt ground or mossy green grass matching "
       "the adjacent floor, identical art style, color and lighting, sharp details")
NEG = ("gap in the wall, opening, doorway, broken wall ends, missing highlight trim, thick oversized wall, stairs, "
       "props, crates, trees, characters, text, watermark, isometric view, perspective, 3d render, blurry, smudged, "
       "saturated neon green, flat color patch")

NOTE = """### Crop & Stitch 局部修补（自动对齐 Canny 尺寸）

**已内置自动尺寸对齐**：`GetImageSize` + `ImageScale (nearest-exact)` 会自动把「② Canny 布局轮廓」缩放到与「① 地图原图」完全一致的宽高（例如 `1024×957` → `1952×1824`），再送入第二个裁切节点。

**每轮流程**
1. 「① 地图原图」选最新整图 → 右键 *Open in Mask Editor*，不透明度 1.0 涂一处要改的墙/地面 → Save。
2. 「② Canny 布局」选对应的黑底白线图（尺寸不同会自动对齐）。
3. 改提示词与 KSampler 的 denoise（见下表）→ Ctrl+Enter。
4. 先看「裁切区 + 轮廓叠加」：白线必须压在想要的墙边上。
5. 满意后在 `output/map-crop-stitch/` 取整图；下一轮把它载入「① 地图原图」继续。

| 任务 | denoise | ControlNet |
|---|---|---|
| 补墙/封路 | 0.90–1.00 | 0.80 / end 0.75 |
| 删墙改地面 | 0.95–1.00 | 0.60–0.70 / end 0.60 |
| 接缝/纹理细修 | 0.35–0.50 | 0.80 |

**两个 Inpaint Crop 节点的参数必须完全一致**（默认 `expand=10`、`blend=20`、`context=3.2`、目标 `1024×1024`）。未涂蒙版的像素不经过 VAE，多轮修补不会让整图变糊。"""

nodes, links = [], []
_lid = [0]


def node(nid, ntype, pos, size, widgets=None, title=None, inputs=(), outputs=(), color=None):
    in_list = []
    for item in inputs:
        nm, tp, opt = item[:3]
        w_name = item[3] if len(item) > 3 else None
        entry = {"name": nm, "type": tp, "link": None}
        if opt:
            entry["shape"] = 7
        if w_name:
            entry["widget"] = {"name": w_name}
        in_list.append(entry)
    n = {"id": nid, "type": ntype, "pos": pos, "size": size, "flags": {}, "order": nid, "mode": 0,
         "inputs": in_list,
         "outputs": [{"name": nm, "type": tp, "links": [], "slot_index": i} for i, (nm, tp) in enumerate(outputs)],
         "properties": {"Node name for S&R": ntype}, "widgets_values": widgets or []}
    if title:
        n["title"] = title
    if color:
        n["color"], n["bgcolor"] = color
    nodes.append(n)
    return n


def link(src, sslot, dst, dslot):
    _lid[0] += 1
    lid = _lid[0]
    tp = src["outputs"][sslot]["type"]
    src["outputs"][sslot]["links"].append(lid)
    dst["inputs"][dslot]["link"] = lid
    links.append([lid, src["id"], sslot, dst["id"], dslot, tp])


I, M = ("IMAGE", "MASK")
crop_in = [("image", I, False), ("mask", M, True), ("optional_context_mask", M, True)]
crop_out = [("stitcher", "STITCHER"), ("cropped_image", I), ("cropped_mask", M)]

n_img = node(1, "LoadImage", [-80, 0], [420, 520], [IMG, "image"], "① 地图原图 + 修复蒙版",
             outputs=[("IMAGE", I), ("MASK", M)])
n_can = node(2, "LoadImage", [-80, 560], [420, 420], [CANNY, "image"], "② Canny 布局轮廓（自动对齐原图尺寸）",
             outputs=[("IMAGE", I), ("MASK", M)])
n_gsz = node(21, "GetImageSize", [380, 560], [220, 90], [], "读取原图尺寸",
             inputs=[("image", I, False)], outputs=[("width", "INT"), ("height", "INT"), ("batch_size", "INT")])
n_csc = node(22, "ImageScale", [640, 560], [260, 140], ["nearest-exact", 1952, 1824, "disabled"],
             "同步 Canny 尺寸到原图",
             inputs=[("image", I, False), ("width", "INT", False, "width"), ("height", "INT", False, "height")],
             outputs=[("IMAGE", I)])
n_ck = node(3, "CheckpointLoaderSimple", [-80, 1020], [420, 100], ["sd_xl_base_1.0.safetensors"], "SDXL 模型",
            outputs=[("MODEL", "MODEL"), ("CLIP", "CLIP"), ("VAE", "VAE")])
n_dd = node(4, "DifferentialDiffusion", [460, 1060], [300, 60], [1.0], "Differential Diffusion（软边过渡）",
            inputs=[("model", "MODEL", False)], outputs=[("MODEL", "MODEL")])
n_pos = node(5, "CLIPTextEncode", [460, 720], [420, 160], [POS], "正向 · 描述蒙版里要出现的东西",
             inputs=[("clip", "CLIP", False)], outputs=[("CONDITIONING", "CONDITIONING")], color=("#232", "#353"))
n_neg = node(6, "CLIPTextEncode", [460, 900], [420, 140], [NEG], "反向",
             inputs=[("clip", "CLIP", False)], outputs=[("CONDITIONING", "CONDITIONING")], color=("#322", "#533"))
n_c1 = node(7, "InpaintCropImproved", [460, 0], [380, 530], list(CROP_W), "✂️ 裁切原图（主）",
            inputs=crop_in, outputs=crop_out, color=("#223", "#335"))
n_c2 = node(8, "InpaintCropImproved", [940, 0], [380, 530], list(CROP_W), "✂️ 裁切轮廓（参数须与主节点一致）",
            inputs=crop_in, outputs=crop_out, color=("#223", "#335"))
n_cnl = node(9, "ControlNetLoader", [940, 1060], [380, 60], ["controlnet-canny-sdxl-1.0-fp16.safetensors"],
             "Canny ControlNet", outputs=[("CONTROL_NET", "CONTROL_NET")])
n_cna = node(10, "ControlNetApplyAdvanced", [940, 660], [320, 190], [0.8, 0.0, 0.75], "按轮廓约束墙形",
             inputs=[("positive", "CONDITIONING", False), ("negative", "CONDITIONING", False),
                     ("control_net", "CONTROL_NET", False), ("image", I, False), ("vae", "VAE", True)],
             outputs=[("positive", "CONDITIONING"), ("negative", "CONDITIONING")])
n_imc = node(11, "InpaintModelConditioning", [1360, 660], [280, 140], [True], "编码裁切区 + 噪声蒙版",
             inputs=[("positive", "CONDITIONING", False), ("negative", "CONDITIONING", False),
                     ("vae", "VAE", False), ("pixels", I, False), ("mask", M, False)],
             outputs=[("positive", "CONDITIONING"), ("negative", "CONDITIONING"), ("latent", "LATENT")])
n_ks = node(12, "KSampler", [1680, 660], [320, 470], [20260127, "randomize", 30, 5.5, "dpmpp_2m", "karras", 0.95],
            "KSampler（denoise 按任务调）",
            inputs=[("model", "MODEL", False), ("positive", "CONDITIONING", False),
                    ("negative", "CONDITIONING", False), ("latent_image", "LATENT", False)],
            outputs=[("LATENT", "LATENT")])
n_vd = node(13, "VAEDecode", [2040, 660], [220, 50], [], "解码裁切区",
            inputs=[("samples", "LATENT", False), ("vae", "VAE", False)], outputs=[("IMAGE", I)])
n_st = node(14, "InpaintStitchImproved", [2300, 660], [260, 50], [], "✂️ 贴回原图（只改蒙版+羽化区）",
            inputs=[("stitcher", "STITCHER", False), ("inpainted_image", I, False)], outputs=[("image", I)],
            color=("#223", "#335"))
n_sv = node(15, "SaveImage", [2300, 760], [520, 560], ["map-crop-stitch/map"], "保存整张地图（PNG 无损）",
            inputs=[("images", I, False)])
n_bl = node(16, "ImageBlend", [1360, 0], [260, 100], [0.6, "screen"], "叠加轮廓",
            inputs=[("image1", I, False), ("image2", I, False)], outputs=[("IMAGE", I)])
n_pv1 = node(17, "PreviewImage", [1660, 0], [400, 440], [], "检查：裁切区 + 轮廓叠加（白线应压墙边）",
             inputs=[("images", I, False)])
n_mp = node(18, "MaskPreview", [1360, 160], [260, 300], [], "检查：裁切区蒙版",
            inputs=[("mask", M, False)])
n_pv2 = node(19, "PreviewImage", [2100, 0], [400, 440], [], "检查：生成的裁切区（贴回前）",
             inputs=[("images", I, False)])
node(20, "MarkdownNote", [-560, 0], [440, 760], [NOTE], "使用说明")

link(n_img, 0, n_c1, 0); link(n_img, 1, n_c1, 1)
link(n_img, 0, n_gsz, 0)
link(n_can, 0, n_csc, 0); link(n_gsz, 0, n_csc, 1); link(n_gsz, 1, n_csc, 2)
link(n_csc, 0, n_c2, 0); link(n_img, 1, n_c2, 1)
link(n_ck, 0, n_dd, 0); link(n_ck, 1, n_pos, 0); link(n_ck, 1, n_neg, 0)
link(n_pos, 0, n_cna, 0); link(n_neg, 0, n_cna, 1); link(n_cnl, 0, n_cna, 2)
link(n_c2, 1, n_cna, 3); link(n_ck, 2, n_cna, 4)
link(n_cna, 0, n_imc, 0); link(n_cna, 1, n_imc, 1); link(n_ck, 2, n_imc, 2)
link(n_c1, 1, n_imc, 3); link(n_c1, 2, n_imc, 4)
link(n_dd, 0, n_ks, 0); link(n_imc, 0, n_ks, 1); link(n_imc, 1, n_ks, 2); link(n_imc, 2, n_ks, 3)
link(n_ks, 0, n_vd, 0); link(n_ck, 2, n_vd, 1)
link(n_c1, 0, n_st, 0); link(n_vd, 0, n_st, 1); link(n_st, 0, n_sv, 0)
link(n_c1, 1, n_bl, 0); link(n_c2, 1, n_bl, 1); link(n_bl, 0, n_pv1, 0)
link(n_c1, 2, n_mp, 0); link(n_vd, 0, n_pv2, 0)

ui = {"last_node_id": 22, "last_link_id": _lid[0], "nodes": nodes, "links": links, "groups": [],
      "config": {}, "extra": {"ds": {"scale": 0.55, "offset": [600, 60]}}, "version": 0.4}
OUT_UI.write_text(json.dumps(ui, ensure_ascii=False, indent=2), encoding="utf-8")
print("UI ->", OUT_UI)

if OUT_API:
    wnames = {
        "LoadImage": ["image", None], "CheckpointLoaderSimple": ["ckpt_name"], "DifferentialDiffusion": ["strength"],
        "CLIPTextEncode": ["text"], "InpaintCropImproved": CROP_KEYS, "ControlNetLoader": ["control_net_name"],
        "ControlNetApplyAdvanced": ["strength", "start_percent", "end_percent"],
        "InpaintModelConditioning": ["noise_mask"],
        "KSampler": ["seed", None, "steps", "cfg", "sampler_name", "scheduler", "denoise"],
        "SaveImage": ["filename_prefix"], "ImageBlend": ["blend_factor", "blend_mode"],
        "ImageScale": ["upscale_method", "width", "height", "crop"],
    }
    api = {}
    lmap = {l[0]: l for l in links}
    for n in nodes:
        if n["type"] == "MarkdownNote":
            continue
        inp = {}
        for k, v in zip(wnames.get(n["type"], []), n["widgets_values"]):
            if k:
                inp[k] = v
        for i in n["inputs"]:
            if i["link"] is not None:
                l = lmap[i["link"]]
                inp[i["name"]] = [str(l[1]), l[2]]
        api[str(n["id"])] = {"class_type": n["type"], "inputs": inp}
    OUT_API.write_text(json.dumps(api, ensure_ascii=False, indent=2), encoding="utf-8")
    print("API ->", OUT_API)
