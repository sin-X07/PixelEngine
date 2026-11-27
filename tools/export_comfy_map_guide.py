"""Export the wall outlines of a PixelEngine map for ComfyUI Canny ControlNet."""

import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


def export_guide(map_path, image_path, output_path):
    data = json.loads(map_path.read_text(encoding="utf-8-sig"))
    with Image.open(image_path) as image:
        width, height = image.size

    layer = next((layer for layer in data["layers"] if layer["name"] == "Collisions"), None)
    if layer is None or layer["type"] != "objectgroup" or not layer["objects"]:
        raise ValueError("Collisions must be a nonempty Tiled object layer.")

    scale_x = width / (data["width"] * data["tilewidth"])
    scale_y = height / (data["height"] * data["tileheight"])
    offset_x = layer.get("offsetx", 0)
    offset_y = layer.get("offsety", 0)
    layout = np.zeros((height, width), dtype=np.uint8)
    for wall in layer["objects"]:
        if wall.get("rotation", 0) or any(
            key in wall for key in ("ellipse", "point", "polyline", "gid", "text")
        ):
            raise ValueError(f"Collision object {wall['id']} must be an unrotated rectangle or polygon.")

        x = wall["x"] + offset_x
        y = wall["y"] + offset_y
        if "polygon" in wall:
            points = np.array([
                (round((x + point["x"]) * scale_x), round((y + point["y"]) * scale_y))
                for point in wall["polygon"]
            ], dtype=np.int32)
            cv2.fillPoly(layout, [points], 255)
            continue

        left = max(0, round(x * scale_x))
        top = max(0, round(y * scale_y))
        right = min(width, round((x + wall["width"]) * scale_x))
        bottom = min(height, round((y + wall["height"]) * scale_y))
        if left < right and top < bottom:
            layout[top:bottom, left:right] = 255

    # Extract edges after combining the shapes so overlapping walls share one outline.
    edges = Image.fromarray(cv2.Canny(layout, 50, 150))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("xb") as output:
        edges.save(output, format="PNG")
    print(f"{width} x {height} | {output_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=Path, help="Tiled .tmj map with the desired collisions")
    parser.add_argument("image", type=Path, help="Base map image covering the full Tiled world")
    parser.add_argument("output", type=Path, help="Output PNG path for the Canny guide")
    args = parser.parse_args()
    try:
        export_guide(args.map, args.image, args.output)
    except (FileExistsError, ValueError) as error:
        parser.exit(1, f"{error}\n")
