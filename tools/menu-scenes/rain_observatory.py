"""A roofless rain calendar above a sunken jungle court."""
import math
import random
from mathutils import Vector
from courtyards import TAU, box, diamond, leaf_spray, mesh, prism, ring, rod, vessel

DIAL_X, DIAL_Z = 3.65, 3.24


def layout():
    # Broken approaches terminate at different heights on either side of the court.
    for i in range(8):
        h=(i+1)*.205
        box('Processional stair',-.55-i*.45,5.35,h/2,.453,2.5,h,'edge',.035)
    for x,y,w,h in ((-7.05,6.3,5.9,2.05),(8.9,7.65,5.3,2.6),(-10.8,8.2,2.2,4.65)):
        box('Rubble core',x,y,h/2,w,2.7,h,'basalt',.045)
    for side in (-1,1):
        x=DIAL_X+side*1.49
        box('Calendar pier',x,6.7,1.23,.84,1.35,2.42,'rock',.09)
        box('Pier shoe',x,6.7,.23,1.14,1.63,.46,'edge',.06)
    box('Suspended lintel',DIAL_X,6.68,2.28,3.9,1.44,.39,'basalt',.06)
    ring('Calendar outer hoop',DIAL_X,6.04,DIAL_Z,1.93,.53,'edge',56)
    ring('Calendar jade inlay',DIAL_X,5.94,DIAL_Z,1.47,.12,'jade',56)
    for i in range(4):
        box('Water stair',DIAL_X,4.27+.29*i,.075*(i+1),3.9,.32,.15*(i+1),'basalt',.022)
    for x,y,h,w in ((-2.8,13.1,4.9,5.7),(-7.9,15.5,5.7,5.8),(8.4,14.8,4.3,5.1)):
        for i in range(5):
            box('Distant terrace',x,y,h*(i+.5)/5,w-i*.70,4.8-i*.62,h/5,'canopy',.055)
        box('Distant roof comb',x,y,h+.432,1.1,1.2,.87,'basalt',.04)


def masonry(x,y,w,h):
    rows=round(h/.48)
    for row in range(rows):
        left=x-w/2
        for i in range(math.ceil(w/.86)+1):
            width=min((.45 if row%2 and i==0 else .86), x+w/2-left)
            if width<.04: break
            k=(i*7+row*13)%11
            box('Hand-cut ashlar',left+width/2,y-.035*(k%3),h*(row+.5)/rows,
                width-.018,.29,h/rows-.024,('edge','basalt','lichen')[k%3] if k<3 else 'rock',.038)
            left+=width


def seal():
    x,z=DIAL_X,DIAL_Z
    for i in range(28):
        a=TAU*i/28
        def pt(radius,ang):
            return (x+radius*math.cos(ang),z+radius*math.sin(ang))
        outline=[pt(radius,a+inset+(TAU/28-2*inset)*j/3)
                 for radius,inset,steps in ((1.87,.01,range(4)),(1.52,.018,range(3,-1,-1)))
                 for j in steps]
        prism('Inscribed calendar segment',outline,5.93,6.04,'lichen' if i%6==1 else 'rock',.016)
        ang=a+math.pi/28
        px,pz=pt(1.68,ang)
        diamond('Incised calendar marker',px,5.90,pz,.11 if i%3 else .16,.16,'gold' if i%7==0 else 'edge')
    # Four bridge pins suspend an original open-work rain medallion inside the ring.
    for i in range(4):
        a=TAU*(i+.5)/4
        def anchor(r):
            return (x+r*math.cos(a),6.03,z+r*math.sin(a))
        rod('Obsidian suspension pin',anchor(.57),anchor(1.35),.08,'basalt',8)
        rod('Gold pin collar',anchor(1.12),anchor(1.21),.112,'gold',10)
    ring('Inner seal rim',x,5.90,z,.65,.13,'gold',36)
    prism('Rain lozenge',[(x-.42,z-.06),(x,z+.69),(x+.42,z-.06),(x,z-.49)],5.85,6.12,'jade',.065)
    diamond('Rain lozenge glint',x,5.824,z+.10,.22,.43,'edge')
    for side in (-1,1):
        cx=x+side*1.49
        for i in range(5):
            box('Pier incised band',cx,5.997,.57+i*.28,.67,.075,.18,'basalt',.015)
            diamond('Pier relief',cx,5.94,.57+i*.28,.34,.14,'jade' if i%2 else 'edge')


def tree(x,y,h,seed):
    rng=random.Random(seed)
    top=Vector((x+.38,y,h))
    base=Vector((x,y,0))
    bend=Vector((x-.22,y+.05,h*.55))
    rod('Ceiba lower trunk',base,bend,.47,'bark',12,.28)
    rod('Ceiba upper trunk',bend,top,.30,'bark',11,.14)
    for i in range(6):
        a=i*TAU/6+.23
        p=(x+1.08*math.cos(a),y+.95*math.sin(a),.04)
        rod('Buttress root',p,(x,y,1.43),.10,'bark',7,.29)
    for i in range(11):
        a=TAU*i/11+.17*rng.random()
        start=base.lerp(top,.50+.37*rng.random())
        end=top+Vector((math.cos(a)*(1.6+.75*rng.random()),math.sin(a)*1.6,.8*rng.random()-.38))
        elbow=start.lerp(end,.54)+Vector((0,0,.55))
        rod('Forked bough',start,elbow,.16,'bark',8,.1)
        rod('Canopy twig',elbow,end,.105,'bark',8,.04)
        # Each spray has individual lanceolate leaves on a forked stem.
        verts,faces=[end],[]
        for j in range(23):
            az=rng.random()*TAU
            r=rng.random()**.5*1.28
            p=end+Vector((r*math.cos(az),r*math.sin(az),(.5-r*.23)*rng.random()))
            # Lift the inner leaf pair clear of the outer spray on the east ceiba.
            if seed==23 and i==1 and j==21:
                p.z+=.09
            f=Vector((math.cos(az),math.sin(az),-.21))*(.38+.25*rng.random())
            s=Vector((-math.sin(az),math.cos(az),0))*.185
            s.z=.10*math.sin(j*2.4+i*.9)
            k=len(verts)
            verts.extend([p-f,p-s,p+Vector((0,0,.085)),p+s,p+f])
            faces.extend([(k,k+1,k+2),(k,k+2,k+3),(k+1,k+4,k+2),(k+2,k+4,k+3)])
            # A tapered petiole connects each leaf blade to its own branch tip.
            verts.append(p-f*.95+s*.046)
            faces.append((0,k,k+5))
        mesh('Ceiba leaf cluster',verts,faces,('canopy','fern','leaf')[i%3])


def hanging_root(x,y,z,seed):
    for j in range(6):
        t=j/6
        def point(v):
            return (x+.17*math.sin(6*v+seed),y-.28*math.sin(math.pi*v),z-v*(1.1+.16*(seed%5)))
        rod('Climbing vine',point(t),point(t+1/6),.028,'bark',6)
        if j%2==0:
            p=point(t)
            diamond('Heart leaf',p[0]+.06,p[1]-.02-.009*(seed%3),p[2]-.08,.19,.26,'leaf')


def details():
    from courtyard_edges import rain_court
    rain_court()
    masonry(-7.05,4.90,5.9,2.05)
    masonry(8.9,6.25,5.3,2.6)
    masonry(-10.8,6.80,2.2,4.65)
    seal()
    for x,y,h in ((-7.9,5.55,2.05),(-5.55,5.55,2.05),(8.05,6.9,2.6),(10.55,6.9,2.6)):
        for i,(w,hh) in enumerate(((1.12,.22),(.86,.28),(.68,.73),(.83,.16),(.98,.23))):
            z=h+sum((.22,.28,.73,.16,.23)[:i])
            box('Stela course',x,y,z+hh/2,w,w,hh,'edge' if i%2==0 else 'rock',.04)
        for i in range(3):
            diamond('Stela relief',x,y-.37,h+.67+.19*i,.34,.17,'jade')
    for i in range(12):
        x=-9.62+i*.47
        box('Terrace frieze',x,4.70,1.79,.43,.08,.26,'basalt',.016)
        diamond('Terrace split lozenge',x,4.64,1.79,.35,.21,'edge')
    for i in range(6):
        hanging_root(-9.1+i*.68,4.75,2.18+i%2*.23,2+i)
    for i in range(5):
        hanging_root(7.35+i*.51,6.12,2.73,i+10)
    for x,y,h,seed in ((-9.4,7.9,5.7,11),(-5.3,10.9,5.3,43),(8.4,9.8,6.1,21),
                        (12.3,12,6.3,23),(-14.4,11,6.2,13)):
        tree(x,y,h,seed)
    for g,(x,y,z) in enumerate(((-6.0,4.12,.38),(6.35,4.85,.42),(-3.75,6.15,1.67))):
        for i in range(6):
            leaf_spray('Wind | bromeliad', (x,y,z),TAU*i/6+g*.42,1+.13*(i%3),.20,.20,
                       'leaf' if i%2==0 else 'fern',True)
        for i in range(3):
            leaf_spray('Bromeliad heart',(x,y,z+.08),TAU*i/3,.50,.10,-.7,'clay')
    # Recessed water trough with a stone lip at the foot of the calendar.
    box('Rain collector water',DIAL_X,4.96,.573,2.9,.64,.08,'water',.035)
    for x in (DIAL_X-1.52,DIAL_X+1.52):
        box('Trough end',x,4.96,.62,.15,.92,.22,'edge',.025)
    for y in (4.56,5.36):
        box('Trough rim',DIAL_X,y,.624,2.89,.13,.22,'edge',.025)
    for i in range(7):
        x=-7.3+.41*(i%4); y=3.90+.46*(i//4)
        vessel('Archaeological pot',x,y,0,[(0,.13),(.14,.24),(.34,.22),(.42,.14),(.46,.16)],
               'clay' if i%2 else 'basalt',12)
    understory()
    rng=random.Random(28)
    for i in range(38):
        x=rng.choice((-1,1))*rng.uniform(5.45,10.2)
        y=rng.uniform(3.08,5.15)
        w=rng.uniform(.13,.51)
        if i==32: y+=.63
        prism('Spalled rubble',[(x-w,-.024),(x-.55*w,.37*w),(x+.28*w,.54*w),(x+w,.04)],
              y,y+w*.84,'lichen' if i%4==0 else 'rock',.019)



def understory():
    rng=random.Random(934)
    # Pennate ferns with paired, diminishing leaflets contrast with the broad epiphytes.
    for x,y,z,radius in ((-8.6,3.6,.08,1.0),(-6.9,4.65,2.01,.62),
                         (6.72,5.8,.1,.88),(9.3,6.55,2.6,.70),(-3.2,7.05,1.4,.65)):
        for i in range(8):
            a=i*TAU/8+rng.uniform(-.20,.20)
            f=Vector((math.cos(a),math.sin(a),0)); side=Vector((-f.y,f.x,0))
            verts,faces=[],[]
            for j in range(11):
                t=j/11; width=radius*.29*math.sin(math.pi*(t+.035))**.68
                mid=Vector((x,y,z))+radius*(f*t+Vector((0,0,.24+.009*i+.64*math.sin(math.pi*t*.83))))
                for sign in (-1,1):
                    k=len(verts)
                    verts.extend([mid-.055*f,mid+.080*f,mid+sign*side*width+.105*f,
                                  mid+sign*side*width*.58+.046*f+Vector((0,0,.035))])
                    faces.extend([(k,k+1,k+3),(k+1,k+2,k+3),(k+2,k,k+3)])
            # Thread the leaflets on a continuous rachis down to the plant base.
            first=len(verts)
            for j in range(-1,11):
                if j<0:
                    mid=Vector((x,y,z-.11))
                else:
                    mid=verts[j*8]+.055*f
                verts.extend([mid-side*.012,mid+side*.012])
                if j>=0:
                    k=first+2*(j+1)
                    faces.append((k-2,k-1,k+1,k))
            mesh('Fern with paired leaflets',verts,faces,'leaf' if i%3==0 else 'fern')
    # A low relief records the rise of the water, three carved steps around a turquoise eye.
    for side in (-1,1):
        x=DIAL_X+side*1.49
        points=[(x-.31,.60),(x-.31,.93),(x-.16,.93),(x-.16,1.18),
                (x+.02,1.18),(x+.02,1.41),(x+.29,1.41)]
        for (ax,az),(bx,bz) in zip(points,points[1:]):
            rod('Rain-gauge carved meander',(ax,5.89+.007*(az==bz),az),(bx,5.89+.007*(az==bz),bz),.041,'edge',4)
    for k in range(14):
        a=TAU*(k+.5)/14
        x=DIAL_X+1.38*math.cos(a); z=DIAL_Z+1.38*math.sin(a)
        rod('Dial inward gnomon',(x,5.97,z),(x-.13*math.cos(a),5.97,z-.13*math.sin(a)),.046,'jade',5,.012)
    # Rear canopy tiers obscure the junctions between the distant terrace silhouettes.
    for i in range(25):
        x=-15+i*1.28
        y=13.7+.55*math.sin(i*2)
        z=3.82+.70*math.sin(i*1.2)
        crown=Vector((x,y,z+.14))
        rod('Rear canopy stem',(x-.18,y,-.03),crown+Vector((0,0,.012 if i==7 else 0)),.14,'bark',7,.06)
        verts=[crown]; faces=[]
        for j in range(17):
            a=rng.random()*TAU; r=rng.random()**.5*1.70
            p=Vector((x+r*math.cos(a),y+r*math.sin(a),z+rng.random()*.67))
            f=Vector((math.cos(a),math.sin(a),-.12))*.65
            side=Vector((-f.y,f.x,.36*math.sin(j*2.4+i*.9)))*.48
            k=len(verts)
            verts.extend([p-f,p+side,p+Vector((0,0,.10)),p-side,p+f])
            faces.extend([(k,k+1,k+2),(k,k+2,k+3),(k+1,k+4,k+2),(k+2,k+4,k+3)])
            verts.append(p-f*.94+side*.08)
            faces.append((0,k,k+5))
        mesh('Distant canopy scallop',verts,faces,'canopy')

    # The near field has an asymmetrical broken stair-tower and hanging strangler roots.
    for i in range(5):
        w=2.26-i*.19
        x=-8.95+i*.065; y=3.00+i*.07
        box('Broken stair tower',x,y,.51+i*.82,w,1.77-i*.12,.86,'rock',.075)
        box('Stepped dripstone',x,y-.01,.91+i*.82,w+.19,1.88-i*.12,.16,'edge',.035)
    for i in range(3):
        diamond('Obsidian inset',-8.82,2.23+.073*i,2.14+i*.54,.67,.47,'basalt')
        diamond('Jade drop inlay',-8.82,2.198+.073*i,2.14+i*.54,.35,.25,'jade')
    for i in range(5):
        hanging_root(-9.54+i*.20,2.23,3.85-.13*i,19+i)
    # Leaning stones and a carved spout bring the far right wall forward.
    for i in range(5):
        x=8.30+i*.40
        box('Aqueduct coping',x,3.89,.93,.42,.86,.22,'lichen',.024)
        box('Aqueduct sill',x,4.02,.44,.44,.66,.86,'rock',.04)
    for x,y,z in ((-9.1,3.08,4.24),(9.23,4.0,1.02)):
        for i in range(7):
            leaf_spray('Lip fern', (x,y,z),TAU*i/7,.78,.11,.47,'fern' if i%2 else 'leaf')
