"""Author the perimeter of each courtyard with the existing kit and material palette."""
import math
from salt_market import box, mesh, prism, rod
from courtyards import leaf_spray, vessel


def rain_court():
    x,y=-7.19,.85
    box('Spillway foot',x,y,.105,1.30,1.75,.257,'basalt',.045)
    for side in (-1,1):
        box('Spillway jamb',x+side*.43,y,.39,.37,1.39,.34,'rock',.038)
    prism('Fractured spillway hood',[(x-.66,.536),(x+.66,.536),(x+.62,.74),
          (x+.28,.85),(x-.51,.91),(x-.65,.83)],y-.81,y+.62,'lichen',.035)
    box('Spillway shadow',x,y+.34,.385,.55,.36,.35,'deep')
    box('Wet stone tongue',x,y-.65,.232,.48,.52,.034,'water',.008)
    for i in range(3):
        box('Hood rain cut',x-.26+i*.19,y-.81,.646,.052,.022,.152,'jade',.004)
    # A stepped rain mask faces the court. Its basalt throat is deliberately open.
    for side in (-1,1):
        cx=x+.39*side
        prism('Spillway carved cheek',[(cx-.19,.29),(cx+.19,.29),(cx+.18,.65),
              (cx+.06,.79),(cx-.15,.69)],y-.79,y-.60,'edge',.018)
        prism('Obsidian eye socket',[(cx-.145,.59),(cx+.135,.60),(cx+.08,.677),
              (cx-.103,.681)],y-.807,y-.776,'basalt',.006)
        prism('Jade eye',[(cx-.061,.619),(cx+.051,.616),(cx+.035,.654),
              (cx-.042,.657)],y-.816,y-.801,'jade',.003)
    prism('Spillway brow',[(x-.30,.70),(x-.12,.66),(x,.50),(x+.12,.66),(x+.30,.70),
          (x+.22,.81),(x-.21,.84)],y-.885,y-.746,'rock',.021)
    for side in (-1,1):
        cx=x+.24*side
        prism('Stone fang',[(cx-.060,.56),(cx,.365),(cx+.062,.56)],y-.75,y-.56,'edge',.009)
    for cx,cy,w,d,h in ((7.59,1.39,1.16,1.23,.91),(9.72,1.58,1.33,1.2,1.3),(10.01,2.69,.72,1.10,1.63)):
        box('Watercourse masonry',cx,cy,(h-.03)/2,w,d,h+.03,'rock',.045)
        box('Lichen drip cap',cx,cy,h+.069,w+.14,d+.10,.154,'lichen',.034)
        for j in range(3):
            leaf_spray('Watercourse fern',(cx+.20,cy,h+.14),1.4+j*1.8,.40,.068,.29,'fern')
    for j in range(3):
        cx=7.24+.32*j
        prism('Canal relief panel',[(cx-.13,.35),(cx+.13,.35),(cx+.13,.72),(cx-.13,.72)],
              .747,.79,'basalt',.013)
        prism('Water level glyph',[(cx-.082,.55),(cx,.664),(cx+.082,.55),(cx,.435)],
              .735,.756,'jade',.008)
    box('Projecting water spout',7.58,.70,.72,.46,.58,.18,'edge',.027)
    for i,cx in enumerate((-3,.25)):
        h=4.38+i*.30
        box('Upstream pier',cx,17.65,(h-.03)/2,.88,1.08,h+.03,'canopy',.045)
        box('Upstream cap',cx,17.65,h+.12,1.19,1.33,.26,'basalt',.04)
    prism('Broken crossing',[(-3.52,4.54),(-1.13,4.54),(-.94,4.76),(-1.31,5.01),(-3.52,5.01)],17.11,18.22,'rock',.038)


def salt_street():
    x,y=-7.48,.73
    box('Weighbridge bed',x,y,.127,1.37,1.83,.30,'stone',.045)
    box('Iron weigh deck',x,y,.295,1.22,1.61,.049,'iron',.012)
    for dx,dy in ((-.24,.36),(.21,.22),(-.10,-.35)):
        vessel('Tied salt sack',x+dx,y+dy,.316,[(0,.15),(.065,.22),(.24,.235),(.34,.145),(.38,.078),(.42,.086)],'linen',12)
        vessel('Twine binding',x+dx,y+dy,.68,[(0,.106),(.021,.102)],'timber',12)
    rod('Scale standard',(x-.49,y+.52,.316),(x-.49,y+.52,1.09),.041,'iron')
    rod('Scale dial rim',(x-.49,y+.54,.99),(x-.49,y+.46,.99),.16,'brass',16)
    rod('Scale dial face',(x-.49,y+.462,.99),(x-.49,y+.449,.99),.131,'chalk',16)
    rod('Scale pointer',(x-.49,y+.448,.99),(x-.56,y+.448,1.062),.010,'rust',5)
    for cx,w in ((7.59,.72),(9.72,1.05)):
        box('Warehouse door pier',cx,1.98,1.25,w,1.22,2.56,'plaster',.04)
    box('Warehouse header',8.72,1.98,2.58,3.03,1.27,.24,'clay',.035)
    box('Warehouse recessed door',8.60,2.49,1.21,1.47,.25,2.49,'gate',.025)
    box('Warehouse awning',8.67,1.11,2.51,3.23,.95,.14,'timber',.023)
    for i in range(7):
        box('Clay eave tile',7.32+i*.445,.691,2.565,.426,.24,.11,'rust',.019)
    for i in range(2):
        vessel('Sealed brine jar',7.42+i*.49,1.13,-.022,[(0,.14),(.12,.23),(.37,.215),(.45,.133),(.50,.14)],'clay' if i else 'chalk',14)
    # Recessed inventory shutters and a hanging guild sign on the street corner.
    box('Warehouse tally board',7.59,1.357,1.37,.57,.065,.99,'timber',.021)
    for j in range(5):
        box('Vent slat',7.59,1.315,1.06+j*.14,.49,.046,.066,'gate',.010)
    for side in (-1,1):
        box('Door bronze edging',8.60+side*.635,2.356,1.17,.055,.047,2.23,'brass',.009)
    box('Door push rail',8.60,2.356,1.12,1.35,.048,.12,'timber',.012)
    rod('Sign crook',(7.42,1.44,2.12),(7.42,.74,2.12),.033,'iron',8)
    for cx in (7.22,7.62):
        rod('Sign lug',(cx,.78,1.84),(cx,.78,2.13),.019,'iron',7)
    rod('Sign yoke',(7.18,.78,2.115),(7.67,.78,2.115),.025,'brass',8)
    prism('Salt guild plaque',[(7.10,1.88),(7.75,1.88),(7.74,1.46),(7.42,1.33),(7.11,1.46)],
          .745,.823,'gate',.023)
    for j,(cx,z,w,h) in enumerate(((7.42,1.64,.135,.32),(7.25,1.61,.105,.19),(7.59,1.61,.105,.19))):
        prism('Crystal enamel',[(cx-w/2,z),(cx,z+h/2),(cx+w/2,z),(cx,z-h/2)],
              .731,.754,'paper' if j==0 else 'ochre',.006)
    for i in range(2):
        cx=7.42+i*.49
        vessel('Jar shoulder band',cx,1.13,.277,[(0,.224),(.033,.223)],'gate',16)
    for i,(cx,cy) in enumerate(((-3.8,18),(.60,19))):
        h=3.78+i*.43
        for dx in (-1.31,1.31):
            for dy in (-.72,.72):
                box('Drying shed post',cx+dx,cy+dy,(h-.04)/2,.16,.19,h+.06,'timber')
        prism('Drying shed roof',[(cx-1.56,h-.06),(cx+1.56,h-.06),(cx+1.56,h+.085),
              (cx-.18,h+.54),(cx-1.56,h+.14)],cy-.92,cy+.92,'ochre',.021)


def indigo_court():
    x,y=-7.08,.91
    box('Tea dais',x,y,.164,1.39,1.73,.38,'rose',.05)
    box('Glazed seat edge',x,y,.382,1.49,1.82,.094,'tile',.022)
    box('Limestone seat',x,y,.447,1.48,1.81,.057,'ivory',.016)
    vessel('Brass serving tray',x+.10,y-.29,.473,[(0,.33),(.026,.36),(.048,.36)],'brass',24)
    for i,(dx,dy) in enumerate(((-.21,.35),(.14,-.30))):
        base=.472 if i==0 else .519
        vessel('Tea pot',x+dx,y+dy,base,[(0,.11),(.055,.165),(.21,.19),(.31,.12),
               (.335,.13),(.37,.085),(.398,.034)],'indigo' if i else 'clay',16)
        rod('Pouring spout',(x+dx+.132,y+dy,base+.13),(x+dx+.29,y+dy,base+.29),.061,'brass',9,.035)
        # One continuous swept handle, without caps between the bends.
        verts=[]
        arc, section = 24, 12
        for j in range(arc+1):
            a=math.pi*j/arc
            for k in range(section):
                b=2*math.pi*k/section
                verts.append((x+dx-.13-(.13+.021*math.cos(b))*math.sin(a),
                              y+dy+.021*math.sin(b),base+.185+(.077+.021*math.cos(b))*math.cos(a)))
        faces=[tuple(range(section)),tuple(range(arc*section,(arc+1)*section))]
        for j in range(arc):
            for k in range(section):
                a=j*section+k
                b=j*section+(k+1)%section
                faces.append((a,b,b+section,a+section))
        mesh('Tea handle',verts,faces,'brass',0,True)
        vessel('Teapot rim glaze',x+dx,y+dy,base+.309,[(0,.124),(.024,.133)],'gold',16)
        vessel('Teapot lid knob',x+dx,y+dy,base+.39,[(0,.039),(.034,.050),(.066,.016)],'brass',12)
    for dx in (-.16,.28):
        vessel('Tea glass',x+dx,y-.42,.518,[(0,.042),(.072,.057),(.092,.061)],'ivory',12)
    # The runner drapes over the camera-facing edge in a continuous fold.
    for j in range(15):
        xa=x-.57+j*.064
        key='ivory' if j in (1,3,11,13) else ('gold' if j in (2,12) else 'cloth')
        points=[(xa+dx,cy,cz) for dx in (0,.064)
                for cy,cz in ((y-.51,.478),(y-.90,.478),(y-.917,.452),(y-.92,.20+.025*math.cos(j*.85)))]
        mesh('Woven tea runner',points,[(0,1,5,4),(1,2,6,5),(2,3,7,6)],key)
    box('Lane step',7.82,1.65,.087,1.59,1.29,.218,'ivory',.036)
    box('Lane parapet',7.82,1.91,.62,1.37,.66,.89,'rose',.035)
    box('Turquoise coping',7.82,1.91,1.12,1.51,.78,.13,'tile',.024)
    for i in range(6):
        box('Parapet tile',7.32+i*.195,1.572,.88,.125,.027,.13,'indigo',.007)
    # Turquoise-and-ochre eight-point stars are inset in the parapet tiles.
    for j in range(3):
        cx=7.43+j*.388
        points=[(cx+(.168 if k%2==0 else .090)*math.cos(k*math.pi/8),
                 .575+(.168 if k%2==0 else .090)*math.sin(k*math.pi/8)) for k in range(16)]
        prism('Iznik rosette',points,1.557,1.596,'tile',.006)
        prism('Honey lozenge',[(cx-.064,.575),(cx,.656),(cx+.064,.575),(cx,.494)],
              1.545,1.565,'gold',.004)
    for cx in (7.28,9.65):
        box('Return canopy upright',cx,2.15,1.03,.104,.146,2.13,'bark',.016)
    rod('Return canopy beam',(7.20,2.15,2.07),(9.74,2.15,2.07),.07,'bark')
    for j in range(10):
        x0=7.23+j*.245
        pts=[(x0+dx,2.16-.52*t,2.119-.29*t-.06*math.sin(math.pi*t))
             for t in (0,.33,.67,1) for dx in (0,.245)]
        mesh('Half shade valance',pts,[(k,k+1,k+3,k+2) for k in (0,2,4)],'cloth' if j%3 else 'ivory')
    for i,(cx,cy,w) in enumerate(((-4.05,18.12,2.91),(-.41,19,2.38))):
        h=3.50+i*.66
        box('Hostel roof block',cx,cy,h/2-.012,w,1.86,h,'haze',0)
        box('Hostel parapet',cx,cy-.84,h+.15,w+.12,.24,.35,'rose',0)
    vessel('Terracotta vent',-.65,18.85,4.12,[(0,.19),(.54,.16),(.57,.24),(.67,.22)],'clay',12)

