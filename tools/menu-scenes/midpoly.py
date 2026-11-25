"""Evaluate the menu's mid-poly bevels before world-projected UVs and shadow atlases."""
import math
import bpy
import bmesh
from mathutils import Vector


def finish(scene):
    objects=[obj for obj in scene.objects if obj.type=='MESH']
    bpy.context.view_layer.update()
    angle=math.radians(30)
    for obj in objects:
        assert all(abs(s-1)<1e-5 for s in obj.scale),obj.name
        data=obj.data
        size=sorted(obj.dimensions)
        hint=obj.get('edge_width')
        if hint is None and scene.get('menu_environment') in ('ancient_ruins','mirage') and any(word in obj.name.lower() for word in
                ('leaf','fern','canopy scallop','bromeliad','frond','palm fan')):
            data.shade_smooth()
            data.set_sharp_from_angle(angle=angle)
        if hint is None or size[0]<1e-6:
            # Textile strips, leaf cards and applied lettering retain their open boundaries.
            continue
        width=min(size[0]*.12, max(.0004,min(.04,size[1]*.012)))
        if hint>0:
            width=min(size[0]*.12,max(width,min(hint,.04)))
        data.shade_smooth()

        bev=obj.modifiers.new('Mid-poly edge highlight','BEVEL')
        bev.width=width
        bev.segments=2
        bev.limit_method='ANGLE'
        bev.angle_limit=angle
        bev.face_strength_mode='FSTR_ALL'
        bev.harden_normals=True
        bev.miter_outer='MITER_ARC'
        bev.use_clamp_overlap=True
    apply_meshes(objects)
    for obj in objects:
        if obj.get('edge_width') is None: continue
        data=obj.data
        bm=bmesh.new()
        bm.from_mesh(data)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=.000004)
        # Retopologize each planar cap separately so a triangulation cannot
        # cross material boundaries, bevel bands or existing feature loops.
        for face in [f for f in bm.faces if len(f.verts)>4]:
            tris=bmesh.ops.triangulate(bm,faces=[face],ngon_method='BEAUTY')['faces']
            if len(tris)>1:
                bmesh.ops.join_triangles(bm,faces=tris,angle_face_threshold=.01,
                                         angle_shape_threshold=math.pi)
        strength=bm.faces.layers.int.get('__mod_weightednormals_faceweight')
        for edge in bm.edges:
            # Smooth the entire machined fillet. Preserve 30-degree splits between
            # untouched faces, including sharp boundaries on non-rounded geometry.
            fs=edge.link_faces
            edge.smooth=(len(fs)==2 and (strength and min(f[strength] for f in fs)<16384
                                         or edge.calc_face_angle(0)<=angle))
        bm.to_mesh(data)
        bm.free()
        normal=data.attributes.get('custom_normal')
        if normal: data.attributes.remove(normal)
        for face in data.polygons: face.use_smooth=True
        wn=obj.modifiers.new('Area weighted normals','WEIGHTED_NORMAL')
        wn.mode='FACE_AREA'
        wn.use_face_influence=True
        wn.keep_sharp=True
        wn.weight=50
    apply_meshes(objects)
    for obj in objects:
        if 'lathe_axis' not in obj: continue
        axis=Vector(obj['lathe_axis'])
        origin=Vector(obj['lathe_origin'])
        data=obj.data
        normals=[]
        radial=[]
        for vertex in data.vertices:
            v=vertex.co-origin
            radial.append((v-axis*v.dot(axis)).normalized())
        for loop,corner in zip(data.loops,data.corner_normals):
            n=corner.vector
            z=n.dot(axis)
            r=radial[loop.vertex_index]
            transverse=(n-axis*z).length
            normals.append(axis*z+r*(transverse if n.dot(r)>=0 else -transverse))
        data.normals_split_custom_set(normals)


def apply_meshes(objects):
    deps=bpy.context.evaluated_depsgraph_get()
    # Snapshot all evaluated meshes before invalidating the depsgraph with replacements.
    evaluated=[(o,bpy.data.meshes.new_from_object(o.evaluated_get(deps),depsgraph=deps,
               preserve_all_data_layers=True)) for o in objects if o.modifiers]
    for obj,data in evaluated:
        old=obj.data
        name=old.name
        obj.modifiers.clear()
        obj.data=data
        if old.users==0:
            bpy.data.meshes.remove(old)
            data.name=name

