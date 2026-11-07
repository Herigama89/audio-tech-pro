import sys; sys.path.insert(0,'tools')
from face import Face
f=Face('vfet','photos/vintagefet.png',crop=(0,2,1701,320),scale=1.0)
# the maker's marks
f.erase([(1222,6,1336,70)],threshold=45,brushed=True)            # logo above the meter
f.erase([(1115,240,1440,298)],threshold=45,brushed=True,dilate=5)          # model and maker under it
f.erase([(1248,156,1300,182)],threshold=40)                       # logo on the meter face
# the needle (pivot below the face: the scale's centre), drawn behind the window's edges
f.needle('needle',tip=(1302,91),base=(1283,183),pivot=(1276,228),rtip=139,width=3,threshold=50,window=(1151,79,1399,187))
# the big knobs: their pointer lines on the glass skirts; the small ones: their dot
f.knob_pointer('input',242,163,40,72,'light',hint=5,spread=18,fillturn=22)
f.knob_pointer('output',583,163,40,72,'light',hint=5,spread=18,fillturn=22)
f.knob_pointer('attack',847,91,8,19,'dark',hint=0,spread=30,threshold=45,fillmode='inpaint',grow=7)
f.knob_pointer('release',847,228,8,19,'dark',hint=0,spread=30,threshold=45,fillmode='inpaint',grow=7)
f.pressed('r20',(1053,53,1079,104),k=2.6)
f.pressed('r12',(1053,106,1079,157),k=2.6)
f.pressed('r8',(1053,159,1079,210),k=2.6)
f.pressed('r4',(1053,212,1079,264),k=2.6)
f.pressed('mGR',(1472,53,1498,104),k=2.6)
f.pressed('m8',(1472,106,1498,157),k=2.6)
f.pressed('m4',(1472,159,1498,210),k=2.6)
f.pressed('mOff',(1472,212,1498,264),k=2.6)
f.write()
f.preview('prev_vfet.png',{'input':-90,'output':60,'attack':120,'release':-120,'needle':-30})
