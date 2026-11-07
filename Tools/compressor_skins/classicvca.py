import sys; sys.path.insert(0,'tools')
from face import Face
f=Face('cvca','photos/classicvca.png',crop=(0,6,1702,172))
f.erase([(30,50,140,125)],threshold=50,dilate=5,radius=7)          # maker's name
f.erase([(1628,55,1702,125)],threshold=50,dilate=5,radius=7)       # model name
# the meter's needle (white on black)
f.needle('needle',tip=(180,50),base=(211,92),pivot=(226,112),rtip=77,width=2.5,threshold=60,window=(152,22,302,95),light=True)
K={'threshold':(401,88),'attack':(542,89),'ratio':(679,89),'release':(818,89),'makeup':(955,89),'scHpf':(1095,91),'vcaDrive':(1234,88)}
for k,(x,y) in K.items():
    f.knob_pointer(k,x,y,0.5,26.5,'dark',hint=-18,spread=40,threshold=70,fillmode='inpaint',grow=7)
f.region('kComp',(1330,62,1386,118),darken_bg=0.42)
f.region('kExt',(1454,63,1510,119),darken_bg=0.42)
f.lamp('power',1599,85,30,glow=1.15,dark=0.45)
f.lamp('driveLed',1191,131,5,glow=2.4,dark=0.3)
f.write()
f.preview('prev_cvca.png',{'threshold':60,'needle':30})
