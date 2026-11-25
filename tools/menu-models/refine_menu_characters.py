"""Repeatable mesh refinement for main-menu BOT (T, CT) and Chicken characters.

Applies quad restoration, crease-preserved 1-level subdivision on curved hulls,
chamfer bevels on hard armor edges, Weighted Normal (FACE_AREA, keep_sharp),
and 4-bone skin weight clamping on CharacterLab_Reconstruction.blend without
changing actor origin, overall height, skeleton, NLA animations, or materials.
"""
import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix


def clean_and_bevel_mesh(
    ob,
    *,
    do_quads=True,
    crease_angle_deg=35.0,
    crease_val=0.85,
    subdiv_levels=0,
    bevel_width=0.008,
    bevel_segments=2,
    bevel_angle_deg=35.0,
    weighted_normal=True,
    remove_ngons=False,
    apply_scale_safely=False,
    limit_4_weights=False,
    fix_nonmanifold=False,
    remove_unused_materials=False,
):
    if ob.data.users > 1:
        ob.data = ob.data.copy()
    me = ob.data

    if apply_scale_safely:
        sx, sy, sz = ob.scale
        if abs(sx - 1.0) > 1e-4 or abs(sy - 1.0) > 1e-4 or abs(sz - 1.0) > 1e-4:
            me.transform(Matrix.Diagonal((sx, sy, sz, 1.0)))
            for ch in ob.children:
                ch.location.x *= sx
                ch.location.y *= sy
                ch.location.z *= sz
            ob.scale = (1.0, 1.0, 1.0)

    bm = bmesh.new()
    bm.from_mesh(me)
    if fix_nonmanifold:
        nm_edges = [e for e in bm.edges if len(e.link_faces) > 2]
        if nm_edges:
            bmesh.ops.split_edges(bm, edges=nm_edges)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    if do_quads:
        bmesh.ops.join_triangles(
            bm,
            faces=bm.faces[:],
            angle_face_threshold=math.radians(40),
            angle_shape_threshold=math.radians(40),
            cmp_seam=True,
            cmp_sharp=True,
            cmp_uvs=True,
            cmp_vcols=True,
            cmp_materials=True,
        )
    if remove_ngons:
        ngons = [f for f in bm.faces if len(f.verts) > 4]
        if ngons:
            bmesh.ops.triangulate(bm, faces=ngons)
    for f in bm.faces:
        f.smooth = True
    bm.to_mesh(me)
    bm.free()
    me.update()

    if subdiv_levels > 0:
        bm = bmesh.new()
        bm.from_mesh(me)
        bm.edges.ensure_lookup_table()
        sharp_idx = [
            e.index
            for e in bm.edges
            if len(e.link_faces) == 2 and e.calc_face_angle(0.0) > math.radians(crease_angle_deg)
        ]
        bm.free()
        if sharp_idx:
            cr = me.attributes.get("crease_edge") or me.attributes.new("crease_edge", "FLOAT", "EDGE")
            for idx in sharp_idx:
                cr.data[idx].value = crease_val

    arm_mods = [m for m in ob.modifiers if m.type == "ARMATURE"]
    added = []

    if subdiv_levels > 0:
        m = ob.modifiers.new("RefSubdiv", "SUBSURF")
        m.levels = subdiv_levels
        m.render_levels = subdiv_levels
        m.use_limit_surface = True
        added.append(m.name)

    if bevel_width > 0.0:
        m = ob.modifiers.new("RefBevel", "BEVEL")
        m.width = bevel_width
        m.segments = bevel_segments
        m.limit_method = "ANGLE"
        m.angle_limit = math.radians(bevel_angle_deg)
        m.harden_normals = True
        m.miter_outer = "MITER_ARC"
        m.use_clamp_overlap = True
        added.append(m.name)

    if weighted_normal:
        m = ob.modifiers.new("RefWN", "WEIGHTED_NORMAL")
        m.mode = "FACE_AREA"
        m.keep_sharp = True
        m.weight = 50
        added.append(m.name)

    if arm_mods:
        with bpy.context.temp_override(object=ob, active_object=ob):
            for name in added:
                while ob.modifiers.find(name) > ob.modifiers.find(arm_mods[0].name):
                    bpy.ops.object.modifier_move_up(modifier=name)

    with bpy.context.temp_override(object=ob, active_object=ob):
        for name in added:
            bpy.ops.object.modifier_apply(modifier=name)

    if "crease_edge" in me.attributes:
        me.attributes.remove(me.attributes["crease_edge"])

    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=bm.edges[:])
    if remove_ngons:
        ngons = [f for f in bm.faces if len(f.verts) > 4]
        if ngons:
            bmesh.ops.triangulate(bm, faces=ngons)
    bm.to_mesh(me)
    bm.free()

    if limit_4_weights and ob.vertex_groups:
        for v in me.vertices:
            if len(v.groups) > 4:
                pairs = sorted([(g.group, g.weight) for g in v.groups], key=lambda x: x[1], reverse=True)
                keep = pairs[:4]
                for g, _ in pairs[4:]:
                    ob.vertex_groups[g].remove([v.index])
                total = sum(w for _, w in keep)
                if total > 1e-6:
                    for g, w in keep:
                        ob.vertex_groups[g].add([v.index], w / total, "REPLACE")

    if remove_unused_materials and me.materials:
        used_mats = {p.material_index for p in me.polygons}
        for idx in range(len(me.materials) - 1, -1, -1):
            if idx not in used_mats:
                with bpy.context.temp_override(object=ob, active_object=ob):
                    ob.active_material_index = idx
                    bpy.ops.object.material_slot_remove()

    me.update()


def refine_all():
    for rig_name in ("T_RIG", "CT_RIG", "CHICKEN_RIG"):
        bpy.data.objects[rig_name].data.pose_position = "REST"
    bpy.context.view_layer.update()

    # T
    clean_and_bevel_mesh(
        bpy.data.objects["Character_Enemy_Head"],
        do_quads=True, crease_angle_deg=35.0, crease_val=0.88,
        subdiv_levels=1, bevel_width=0.005, bevel_segments=2, bevel_angle_deg=32.0,
        weighted_normal=True, remove_ngons=True,
    )
    clean_and_bevel_mesh(
        bpy.data.objects["Character_Enemy"],
        do_quads=True, crease_angle_deg=35.0, crease_val=0.90,
        subdiv_levels=1, bevel_width=0.005, bevel_segments=2, bevel_angle_deg=35.0,
        weighted_normal=True, remove_ngons=True, limit_4_weights=True, remove_unused_materials=True,
    )
    for sname in ("SleeveRoot.T_RIG.L", "SleeveRoot.T_RIG.R"):
        clean_and_bevel_mesh(bpy.data.objects[sname], do_quads=False, subdiv_levels=1, bevel_width=0.0)
    clean_and_bevel_mesh(bpy.data.objects["T/Hands"], do_quads=False, bevel_width=0.0, remove_ngons=True)
    for aname, bw in (("T/accessory_back", 0.004), ("T/accessory_headwear", 0.004), ("T/accessory_eyewear", 0.003)):
        clean_and_bevel_mesh(bpy.data.objects[aname], do_quads=False, bevel_width=bw, bevel_segments=2, bevel_angle_deg=35.0)

    # CT
    clean_and_bevel_mesh(
        bpy.data.objects["Head"],
        do_quads=True, crease_angle_deg=32.0, crease_val=0.88,
        subdiv_levels=1, bevel_width=0.005, bevel_segments=2, bevel_angle_deg=32.0,
        weighted_normal=True, remove_ngons=True, apply_scale_safely=True,
    )
    clean_and_bevel_mesh(
        bpy.data.objects["Body"],
        do_quads=True, crease_angle_deg=32.0, crease_val=0.90,
        subdiv_levels=1, bevel_width=0.005, bevel_segments=2, bevel_angle_deg=35.0,
        weighted_normal=True, remove_ngons=True, limit_4_weights=True,
    )
    for sp in ("ShoulderPad.L", "ShoulderPad.R"):
        clean_and_bevel_mesh(
            bpy.data.objects[sp],
            do_quads=False, crease_angle_deg=32.0, crease_val=0.88,
            subdiv_levels=1, bevel_width=0.004, bevel_segments=2, bevel_angle_deg=32.0,
            weighted_normal=True, remove_ngons=True, apply_scale_safely=True,
        )
    for sname in ("SleeveRoot.CT_RIG.L", "SleeveRoot.CT_RIG.R"):
        clean_and_bevel_mesh(bpy.data.objects[sname], do_quads=False, subdiv_levels=1, bevel_width=0.0)
    clean_and_bevel_mesh(bpy.data.objects["CT/Hands"], do_quads=False, bevel_width=0.0, remove_ngons=True)
    for aname, bw in (("CT/accessory_back", 0.004), ("CT/accessory_headwear", 0.004), ("CT/accessory_eyewear", 0.003)):
        clean_and_bevel_mesh(bpy.data.objects[aname], do_quads=False, bevel_width=bw, bevel_segments=2, bevel_angle_deg=35.0)

    # CHICKEN
    clean_and_bevel_mesh(
        bpy.data.objects["ChickenMesh"],
        do_quads=True, crease_angle_deg=35.0, crease_val=0.75,
        subdiv_levels=1, bevel_width=0.003, bevel_segments=2, bevel_angle_deg=35.0,
        weighted_normal=True, remove_ngons=True, limit_4_weights=True, fix_nonmanifold=True,
    )
    for aname, bw in (("CHICKEN/accessory_eyewear", 0.003), ("CHICKEN/accessory_headwear", 0.004)):
        clean_and_bevel_mesh(bpy.data.objects[aname], do_quads=False, bevel_width=bw, bevel_segments=2, bevel_angle_deg=35.0)

    for rig_name in ("T_RIG", "CT_RIG", "CHICKEN_RIG"):
        bpy.data.objects[rig_name].data.pose_position = "POSE"
    bpy.context.view_layer.update()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--save", type=Path, required=True)
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    args = parser.parse_args(argv)
    bpy.ops.wm.open_mainfile(filepath=str(args.source.resolve()))
    refine_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(args.save.resolve()))
    print("Refinement saved to", args.save)


if __name__ == "__main__":
    main()
