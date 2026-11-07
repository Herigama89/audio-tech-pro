import sys; sys.path.insert(0,'tools')
from face import Face
import vu
f=Face('vvca','photos/vintagevca.png',crop=(0,0,340,595))
f.erase([(92,6,240,32)],threshold=50,dilate=5,radius=6)          # maker's and model name
f.needle('needle',tip=(104.3,84.3),base=(147,138.6),pivot=(166,163),rtip=100,width=2.5,threshold=60,window=(80,57,262,139),light=True)
K={'threshold':(118,296),'makeup':(228,299),'attack':(119,406),'release':(229,405),'ratio':(121,514),'scHpf':(227,515)}
C={'threshold':(119.5,296),'makeup':(229,298.5),'attack':(119.5,406),'release':(228,405),'ratio':(120.5,514),'scHpf':(226.5,514)}
for k,(x,y) in K.items():
    from face import pointer_angle
    a=pointer_angle('photos/vintagevca.png',x,y,1,20,'light',hint=0,spread=40,threshold=70)
    cx,cy=C[k]
    f.knob_whole(k,cx,cy,24.5,info={'angle':round(a,1)})
f.region('kIn',(268,247,330,272),darken_bg=0.6)
f.relabel((92,70,112,90),'0',10,colour=(225,225,225),erase=False,centre=(101,80),font='sans')
f.draw_gr((80,58,260,138),(166,163),74,-38.1,41.4,size=1.05)
f.write()
f.preview('prev_vvca.png',{'threshold':60,'ratio':-90,'needle':40})
