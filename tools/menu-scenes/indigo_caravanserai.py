"""A potters' caravanserai with a blue-tiled belvedere and working textile gallery."""
import math
from mathutils import Vector
from courtyards import TAU, arch, box, diamond, leaf_spray, mesh, prism, rod, vessel, wind


def layout():
    for x,y,w,d,h,key in [(-8.5,7.4,4.4,3.8,4.35,'rose'),(-3.7,9.05,4.3,3.8,3.45,'stucco'),
                          (4.32,7.7,5.0,2.45,3.73,'ivory'),(9.0,7.2,4.22,3.2,4.8,'stucco')]:
        box('Courtyard wing',x,y,h/2,w,d,h,key,.075)
        box('Drip coping',x,y,h-.06,w+.19,d+.22,.16,'ivory',.025)
        box('Roof parapet',x,y,h+.12,w+.09,d+.12,.22,key,.025)
    # The offset belvedere rises above two plaster terraces.
    x,y=-4.72,7.81
    box('Wind tower',x,y,2.69,2.00,2.1,5.42,'rose',.055)
    for z,w,h,key in ((3.47,2.18,.22,'ivory'),(4.87,2.13,.16,'indigo'),(5.38,2.24,.19,'ivory')):
        box('Belvedere cornice',x,y,z,w,w,h,key,.021)
    vessel('Turquoise pavilion cap',x,y,5.47,[(0,1.45),(.14,1.38),(.73,.46),(.86,.20)],
           'tile',8)
    vessel('Lotus finial',x,y,6.30,[(0,.15),(.14,.2),(.29,.06),(.56,.014)],'brass',12)
    box('Loggia interior shadow',4.31,6.41,1.39,4.64,.15,2.7,'deep')
    for i in range(4):
        x=1.98+i*1.55
        vessel('Arcade column',x,5.65,0,[(0,.25),(.22,.25),(.27,.17),(.42,.15),
                (1.68,.13),(1.75,.19),(1.88,.20)],'ivory',12)
        box('Capital abacus',x,5.65,1.92,.49,.5,.17,'ivory',.025)
    for i in range(3):
        x=2.755+i*1.55
        # A pierced, scalloped horseshoe cut from the spandrel, with a real shadow cavity behind.
        outline=[]
        for j in range(33):
            a=math.pi*j/32
            r=.78-.076*math.sin(7*a)**2
            outline.append((x+r*math.cos(a),1.93+r*math.sin(a)))
        prism('Scalloped spandrel',outline+[(x-.79,3.02),(x+.79,3.02)],5.36,5.96,'ivory',.016)
    box('Gallery timber deck',4.305,5.9,3.04,5.24,1.52,.20,'bark',.025)
    for x in (1.8,6.8):
        box('Gallery end pier',x,5.71,1.56,.32,.92,3.10,'rose',.035)
    for j in range(12):
        x=-17+j*3.21
        h=4.0+.85*math.sin(j*1.83)+.8*math.cos(j*.95)
        box('Far roofline',x,14+j%3,h/2,3.22,3.4,h,'haze',.03)
        if j%3==0:
            vessel('Distant cupola',x,14+j%3,h,[(0,.85),(.29,.81),(.62,.58),(.83,.18)],'haze',16)


def fret_window(x,y,z,w,h):
    box('Window recess',x,y,z,w,.09,h,'deep',.025)
    for side in (-1,1):
        box('Window jamb',x+side*(w/2+.04),y-.06,z,.11,.16,h+.19,'ivory',.02)
    box('Window sill',x,y-.11,z-h/2,w+.29,.32,.12,'ivory',.02)
    for i in range(5):
        px=x+(i-2)*w*.16
        rod('Turned screen spindle',(px,y-.11,z-h*.43),(px,y-.11,z+h*.43),.026,'bark',8)
        for k in (-1,0,1):
            vessel('Screen spindle bead',px,y-.11,z+k*h*.23,
                   [(-.056,.028),(0,.061),(.056,.028)],'brass',8)
    for k in (-1,1):
        box('Screen cross rail',x,y-.13,z+k*h*.29,w,.074,.056,'bark',.008)


def rug(x,y,z,w,h,index,animated=False):
    # Folded, woven kilim, including its borders and medallions in the same animated mesh.
    uv_points,patches,inks=[],[],[]
    def co(u,v):
        return (x+u*w,y-(.030+.016*(1-v))*math.cos(2*math.pi*(u+.5)*3+v*.42),z+(v-1)*h)
    def patch(coords,key):
        start=len(uv_points)
        uv_points.extend(Vector(uv) for uv in coords)
        patches.append(tuple(range(start,len(uv_points))))
        inks.append(key)
    for j in range(12):
        a=-.5+j/12; b=a+1/12
        patch([(a,0),(b,0),(b,1),(a,1)],'cloth' if index%2 else 'indigo')
        for low,high in ((.08,.12),(.88,.92)):
            patch([(a,low),(b,low),(b,high),(a,high)],'ivory')
        patch([(a,.015),(a+.025,-.063),(a+.039,.015)],'ivory')
    for u in (-.41,.41):
        patch([(u-.012,.12),(u+.012,.12),(u+.012,.88),(u-.012,.88)],'gold')
    for v,s in ((.29,.14),(.50,.21),(.71,.14)):
        for scale,key in ((1,'gold'),(.67,'tile'),(.27,'ivory')):
            patch([(-s*scale,v),(0,v+.12*scale),(s*scale,v),(0,v-.12*scale)],key)

    # Split the cloth at woven color boundaries so the design bends with the fabric.
    from mathutils.geometry import delaunay_2d_cdt
    points,_,triangles,_,_,_=delaunay_2d_cdt(uv_points,[],patches,1,1e-7)
    verts=[co(p.x,p.y) for p in points]
    from mathutils.geometry import intersect_point_tri_2d
    outlines=[[uv_points[i] for i in patch] for patch in patches]
    faces,materials=[],[]
    for triangle in triangles:
        center=sum((points[i] for i in triangle),Vector((0,0)))/len(triangle)
        for outline,ink in reversed(list(zip(outlines,inks))):
            if any(intersect_point_tri_2d(center,outline[0],a,b) for a,b in zip(outline[1:],outline[2:])):
                faces.append(triangle)
                materials.append(ink)
                break
    from courtyards import mat
    obj=mesh('Wind | kilim' if animated else 'Hanging kilim',verts,faces,materials[0])
    keys=list(dict.fromkeys(materials))
    for key in keys[1:]:
        obj.data.materials.append(mat(key))
    for face,key in zip(obj.data.polygons,materials):
        face.material_index=keys.index(key)
    import bmesh
    cloth=bmesh.new()
    cloth.from_mesh(obj.data)
    bmesh.ops.remove_doubles(cloth,verts=list(cloth.verts),dist=.00002)
    bmesh.ops.dissolve_degenerate(cloth,edges=list(cloth.edges),dist=.00001)
    cloth.to_mesh(obj.data)
    cloth.free()
    if animated: wind(obj,Vector((x,y,z)),x)


def star(x,y,z,r):
    points=[]
    for i in range(16):
        a=TAU*i/16
        radius=r if i%2==0 else r*.66
        points.append((x+radius*math.cos(a),z+radius*math.sin(a)))
    prism('Eight-point ceramic rosette',points,y,y+.035,'tile',.005)
    diamond('Tile ochre centre',x,y-.024,z,r*.6,r*.6,'gold')


def palm(x,y,h,phase):
    for i in range(12):
        t=i/12
        def p(q): return (x+.41*q*q,y+.13*q,h*q)
        rod('Date palm ringed bole',p(t),p(t+1/12),.23-.09*t,'bark',10,.19-.09*t)
    origin=(x+.41,y+.13,h)
    for i in range(8):
        a=TAU*i/8+phase
        verts,faces=[],[]
        along=Vector((math.cos(a),math.sin(a),0))
        side=Vector((-along.y,along.x,0))
        def rib(t): return Vector(origin)+along*2.05*t+Vector((0,0,.61*math.sin(math.pi*t)-.66*t*t))
        for j in range(12):
            t=j/12; start=rib(t); end=rib(t+1/12)
            k=len(verts)
            verts.extend([start-side*.028,start+side*.028,end+side*.019,end-side*.019])
            faces.append((k,k+1,k+2,k+3))
            for sign in (-1,1):
                mid=(start+end)/2+side*sign*(.14+.31*math.sin(math.pi*t))
                k=len(verts)
                verts.extend([start,end,mid+along*.18,mid+along*.08-Vector((0,0,.12))])
                faces.extend([(k,k+1,k+2),(k,k+2,k+3)])
        obj=mesh('Wind | fan palm',verts,faces,'leaf')
        wind(obj,Vector(origin),a)


def details():
    from courtyard_edges import indigo_court
    indigo_court()
    for x,y,z,w,h in ((-4.72,6.72,4.58,.92,1.09),(-8.4,5.42,2.8,1.10,1.31),
                       (-2.55,7.10,2.18,.84,1.11),(8.11,5.54,3.31,.89,1.14),
                       (9.83,5.54,3.31,.89,1.14)):
        fret_window(x,y,z,w,h)
    for j in range(19):
        x=1.83+j*.271
        box('Glazed frieze bedding',x,5.27,2.91,.271,.12,.26,'indigo',.004)
        star(x,5.191,2.91,.114)
    for x in (1.83,3.04,4.26,5.48,6.76):
        box('Balcony newel',x,5.25,3.46,.12,.13,.77,'bark',.018)
        vessel('Newel finial',x,5.25,3.84,[(0,.08),(.06,.1),(.15,.024)],'brass',10)
    for z in (3.20,3.75):
        box('Balcony handrail',4.29,5.25,z,5.025,.10,.12,'bark',.018)
    for j in range(36):
        x=1.84+j*.14
        vessel('Turned balcony baluster',x,5.25,3.23,[(0,.04),(.1,.045),(.18,.067),
               (.28,.026),(.48,.042)],'bark',8)
    for i,x in enumerate((2.43,4.09,5.85)):
        rod('Rug suspension bar',(x-.49,5.025,3.735),(x+.49,5.025,3.735),.033,'brass')
        for side in (-1,1):
            rod('Rug bracket',(x+side*.47,5.24,3.744),(x+side*.47,5.025,3.744),.023,'brass')
        rug(x,5.025,3.73,.91,.98+.13*(i%2),i,i<2)
    for i in range(3):
        x=2.75+i*1.55
        box('Arcade back door',x,6.23,1.12,.87,.10,2.11,'indigo',.038)
        for j in range(4):
            box('Door panel',x+(j-1.5)*.19,6.15,1.10,.16,.06,1.86,'tile',.017)
        for z in (.56,1.66):
            box('Strap hinge',x,6.10,z,.72,.06,.065,'brass',.009)
    # A lighter, blue-striped sail folds between projecting timber outriggers.
    for j in range(16):
        xa=-9.88+j*.22
        verts=[]
        for side in (0,.22):
            for k in range(9):
                t=k/8
                verts.append((xa+side,5.30-1.8*t,2.98-.62*t-.20*math.sin(math.pi*t)))
        mesh('Canvas sail panel',verts,[(k,k+1,k+10,k+9) for k in range(8)],'indigo' if j%4<2 else 'ivory')
        box('Scalloped sail hem',xa+.11,3.5,2.28,.218,.05,.16,'indigo' if j%4<2 else 'ivory',.015)
    for x in (-9.89,-6.39):
        rod('Awning brace',(x,5.4,1.93),(x,3.5,2.38),.052,'bark',8)
    for i in range(9):
        x=-9.56+(i%5)*.59; y=4.48-(i//5)*.58
        h=.64+.17*(i%3)
        vessel('Potter glazed amphora',x,y,0,[(0,.15),(.12*h,.21),(.53*h,.31),
               (.79*h,.24),(.87*h,.13),(h,.14),(h*1.05,.18)],'tile' if i%3==0 else 'clay')
        vessel('Amphora collar',x,y,h*.67,[(0,.295),(.05,.284)],'ivory',20)
        for side in (-1,1):
            for j in range(6):
                def handle(k):
                    a=math.pi*k/6
                    return (x+side*(.19+.17*math.sin(a)),y,.67*h+.16*math.cos(a))
                rod('Amphora loop handle',handle(j),handle(j+1),.036,'clay',6)
    # Octagonal street fountain on the return side, with a scalloped tiled lip.
    fx,fy=7.35,4.11
    vessel('Fountain octagonal plinth',fx,fy,0,[(0,.95),(.15,1.0),(.22,.86),(.45,.87),(.52,.99)],
           'ivory',8)
    vessel('Fountain blue coping',fx,fy,.48,[(0,.98),(.08,1.0),(.11,.91)],'tile',8)
    vessel('Fountain water',fx,fy,.588,[(0,.89),(.014,.89)],'water',8)
    vessel('Fountain central jet',fx,fy,.60,[(0,.21),(.08,.18),(.20,.1),(.29,.23),(.34,.22)],
           'brass',16)
    for i in range(8):
        a=TAU*i/8
        rod('Fountain tile joint',(fx+.19*math.cos(a),fy+.19*math.sin(a),.604),
            (fx+.87*math.cos(a),fy+.87*math.sin(a),.604),.012,'ivory',5)
    for x,y,h in ((-10.12,9.85,5.33),(8.04,9.43,5.94)):
        palm(x,y,h,x)
    street_finish()
    for x in (1.98,6.62):
        rod('Lantern bracket',(x,5.55,2.56),(x,5.04,2.56),.034,'brass')
        rod('Lantern suspension',(x,5.04,2.56),(x,5.04,2.32),.018,'bark')
        vessel('Pierced brass lantern',x,5.04,1.98,[(0,.05),(.07,.13),(.24,.13),(.34,.035)],'gold',8)
        for k in range(6):
            a=TAU*k/6
            rod('Lantern ribs',(x+.131*math.cos(a),5.04+.131*math.sin(a),2.05),
                (x+.131*math.cos(a),5.04+.131*math.sin(a),2.22),.014,'bark',6)



def street_finish():
    # Decorative wind slots run up the tower above a stepped cobalt door.
    for side in (-1,1):
        box('Tower pilaster',-4.72+side*.81,6.70,2.58,.15,.16,5.14,'ivory',.022)
        for i in range(5):
            diamond('Pier glazed inset',-4.72+side*.81,6.60,1.02+i*.69,.13,.22,'tile')
    box('Workshop door recess',-4.72,6.69,1.19,1.26,.17,2.38,'deep',.045)
    for j in range(7):
        x=-5.23+j*.17
        box('Workshop cedar plank',x,6.54,1.09,.155,.10,2.15,'indigo',.018)
    for z in (.36,1.71):
        box('Workshop door hinge',-4.72,6.47,z,1.02,.06,.07,'brass',.008)
    arch('Workshop transom',-4.72,6.63,.57,1.83,'ivory',13)
    for i in range(3):
        box('Workshop worn doorstep',-4.72,6.20+i*.21,.055*(i+1)-.008,1.72,.24,.11*(i+1),'ivory',.025)
    # Lower rosettes double as ceramic guards on the long sunlit facade.
    for j in range(12):
        x=7.13+j*.32
        box('Wainscot cobalt course',x,5.52,.56,.32,.07,.66,'indigo',.008)
        star(x,5.456,.59,.15)
    for x,y,z in ((-8.46,5.30,1.08),(8.98,5.49,2.00)):
        box('Ceramic shop plaque',x,y,z,.71,.09,.56,'ivory',.035)
        star(x,y-.07,z,.23)
    # Slender cables and hanging ceramic chimes give the open skyline a depth cue.
    for j in range(10):
        def p(t):
            return (-3.65+5.62*t,7.24-.36*t,4.41-.93*t-.50*math.sin(math.pi*t))
        rod('Courtyard catenary',p(j/10),p((j+1)/10),.019,'bark',6)
        if j%2==1:
            x,y,z=p(j/10)
            rod('Chime cord',(x,y,z),(x,y,z-.20),.012,'bark',5)
            vessel('Glazed chime',x,y,z-.36,[(0,.084),(.11,.069),(.17,.024)],'tile' if j%4==1 else 'brass',10)

    # A nearer return facade and a ribbed roof lantern give the skyline a diagonal.
    x,y=-10.96,4.61
    box('Street corner return',x,y,2.35,1.95,2.06,4.7,'rose',.06)
    box('Return cornice',x,y,4.68,2.14,2.20,.16,'ivory',.022)
    fret_window(x,y-1.10,3.44,.87,1.19)
    for i in range(6):
        box('Corner toothed corbel',x-.81+i*.33,y-1.11,4.49,.17,.24,.24,'ivory',.027)
    x,y=8.98,7.36
    vessel('Roof lantern drum',x,y,5.0,[(0,.95),(.18,1.01),(.25,.87),(.85,.87),(.96,.98)],'rose',16)
    profile=[(0,1.02),(.15,1.00),(.39,.91),(.65,.73),(.83,.49),(.98,.12)]
    vessel('Indigo ribbed cupola',x,y,5.96,profile,'indigo',24)
    for j in range(12):
        a=TAU*j/12
        for (h,r),(hh,rr) in zip(profile,profile[1:]):
            rod('Cupola copper seam',(x+r*math.cos(a),y+r*math.sin(a),5.96+h),
                (x+rr*math.cos(a),y+rr*math.sin(a),5.96+hh),.022,'gold',6)
    vessel('Cupola finial',x,y,6.92,[(0,.07),(.09,.11),(.18,.05),(.43,.01)],'brass',12)
    for i in range(7):
        x=8.44+(i%4)*.42; y=4.72+(i//4)*.35
        vessel('Oil merchant jug',x,y,-.021,[(0,.1),(.1,.16),(.35,.18),(.44,.09),(.52,.10)],
               'clay' if i%2 else 'ivory',16)
