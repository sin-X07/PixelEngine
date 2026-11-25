"""Portable courtyard surface details for the menu's tiled sRGB base-color pipeline."""
import bpy
import numpy as np


def paving(theme, rgb, x, y, grain, cloud):
    """Screen-print a worn survey seal, salt chevrons or ceramic star into the paving."""
    cx, cy = {'ancient_ruins': (2.4, 2.4), 'dust_2': (2.1, 3.7), 'mirage': (-2.95, 2.1)}[theme]
    dx, dy = x-cx, y-cy
    radius, angle = np.hypot(dx, dy), np.arctan2(dy, dx)
    if theme == 'ancient_ruins':
        mark = (abs(radius-.64)<.033) | ((radius>.49)&(radius<.61)&(abs(np.sin(8*angle))<.16))
        mark |= (abs(abs(dx)+abs(dy)*.64-.29)<.027)
        ink = [.30,.53,.43]
    elif theme == 'mirage':
        edge = .49+.13*np.cos(8*angle)
        mark = (abs(radius-edge)<.040) | (abs(radius-.73)<.034) | (radius<.12)
        ink = [.16,.37,.49]
    else:
        mark = (abs(dx)<.6)&((abs(dy+.58*abs(dx))<.075)|(abs(dy+.58*abs(dx)-.36)<.075))
        ink = [.88,.74,.45]
    chips = np.clip((grain+.42*cloud-.25)*2.5,0,1)
    opacity = (.79*mark*chips)[:,:,None]
    dust = np.clip((cloud-.35)*.41+.065*grain,0,.25)[:,:,None]
    dry = rgb*(1-dust) + np.array([.78,.68,.53])*dust
    ground = dry*(1-opacity) + np.array(ink)*opacity
    # Only the existing paving carries marks inside the moving actors' envelope.
    if theme == 'dust_2':
        track = abs(abs(x+1.75-.13*(y+4))-.62)
        mask = np.exp(-(track/.073)**2) * np.clip((2-y)*.43,0,1)
        tint = [.44,.35,.25]
    elif theme == 'ancient_ruins':
        channel = abs(abs(x+.12*y)-2.16-.17*np.sin(y*1.31))
        mask = np.exp(-(channel/.067)**2) * np.clip((-y-1.8)*.53,0,1)
        tint = [.32,.42,.36]
    else:
        axis = abs(x-.14*y)-2.78
        border = (abs(abs(axis)-.18)<.034)
        diamonds = abs(axis)*3.3+abs((y*1.55)%1-.5)<.45
        mask = (border|diamonds)*np.clip((-y-1.5)*.66,0,1)
        tint = [.22,.38,.50]
    faded = (.59*mask*chips)[:,:,None]
    return ground*(1-faded) + np.array(tint)*faded


def materials(scene, directory):
    """Wire packed linear roughness/metal data beside the existing base-color UVs."""
    used = {m for obj in scene.objects if obj.type=='MESH' for m in obj.data.materials if m}
    for material in sorted(used,key=lambda m:m.name):
        key=material.name.split(' | ')[-1]
        tree=material.node_tree
        shader=next(n for n in tree.nodes if n.type=='BSDF_PRINCIPLED')
        metal=key in ('brass','iron','gold')
        rough={'water':.17,'glass':.25,'tile':.31,'jade':.36,'gate':.55,'rust':.88,
               'cloth':.94,'linen':.94}.get(key,.41 if metal else .84)
        shader.inputs['Metallic'].default_value=.91 if metal else 0
        shader.inputs['Roughness'].default_value=rough
        base=shader.inputs['Base Color']
        if not base.is_linked:
            continue
        source=base.links[0].from_node.image
        source.colorspace_settings.name='sRGB'
        w,h=source.size
        pixels=np.empty((h,w,4),np.float32)
        source.pixels.foreach_get(pixels.ravel())
        # Sample the local albedo variation. Exclude broad baked shadows from roughness.
        luma=pixels[:,:,:3].mean(axis=2)
        smooth=sum(np.roll(luma,d,axis) for axis in (0,1) for d in (-4,4))/4
        rm=pixels[::2,::2].copy()
        rm[:,:,0]=1
        rm[:,:,1]=np.clip(rough+.37*(luma-smooth)[::2,::2],.05,.99)
        rm[:,:,2]=0
        rm[:,:,3]=1
        image=bpy.data.images.new(material.name+' RM',width=rm.shape[1],height=rm.shape[0],alpha=False,is_data=True)
        image.pixels.foreach_set(rm.ravel())
        image.file_format='PNG'
        image.filepath_raw=str(directory/(key+'-rm.png'))
        image.save()
        image.pack()
        tex=tree.nodes.new('ShaderNodeTexImage')
        tex.image=image
        split=tree.nodes.new('ShaderNodeSeparateColor')
        tree.links.new(tex.outputs['Color'],split.inputs[0])
        tree.links.new(split.outputs['Green'],shader.inputs['Roughness'])
        tree.links.new(split.outputs['Blue'],shader.inputs['Metallic'])


def repair_uvs(scene):
    for obj in scene.objects:
        if obj.type!='MESH':
            continue
        data=obj.data
        existing=data.uv_layers.active
        uv=existing or data.uv_layers.new(name='Surface')
        for face in data.polygons:
            indices=list(face.loop_indices)
            if existing:
                pts=[uv.data[i].uv.copy() for i in indices]
                if abs(sum(p.x*pts[(j+1)%len(pts)].y-p.y*pts[(j+1)%len(pts)].x for j,p in enumerate(pts)))>1e-10:
                    continue
            axis=max(range(3),key=lambda i:abs(face.normal[i]))
            for i in indices:
                co=data.vertices[data.loops[i].vertex_index].co
                uv.data[i].uv=(co[(axis+1)%3],co[(axis+2)%3])

