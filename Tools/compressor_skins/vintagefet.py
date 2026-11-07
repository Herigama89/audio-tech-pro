import sys; sys.path.insert(0,'tools')
from face import Face
import vu
f=Face('vfet','photos/vintagefet.png',crop=(0,2,1701,320),scale=1.0)
# the maker's marks
f.erase([(1222,6,1336,70)],threshold=45,brushed=True)            # logo above the meter
f.erase([(1115,240,1440,298)],threshold=45,brushed=True,dilate=5)          # model and maker under it
f.erase([(1248,156,1300,182)],threshold=40)                       # logo on the meter face
# the needle (pivot below the face: the scale's centre), drawn behind the window's edges
f.needle('needle',tip=(1302,91),base=(1283,183),pivot=(1276,228),rtip=139,width=3,threshold=50,window=(1151,79,1399,187))
# the big knobs: their pointer lines on the glass skirts; the small ones: their dot
f.knob_pointer('input',241.5,160.5,40,72,'light',hint=5,spread=18,fillmode='inpaint',grow=5)
f.knob_pointer('output',578.5,160,40,72,'light',hint=5,spread=18,fillmode='inpaint',grow=5)
from face import pointer_angle
for k,y in [('attack',91),('release',228)]:
    a=pointer_angle('photos/vintagefet.png',847,y,8,19,'dark',hint=0,spread=30,threshold=45)
    f.knob_cap(k,847,y,24,lambda A,l,s: l>150,info={'angle':round(a,1)})
f.pressed('r20',(1053,53,1079,104),k=2.6)
f.pressed('r12',(1053,106,1079,157),k=2.6)
f.pressed('r8',(1053,159,1079,210),k=2.6)
f.pressed('r4',(1053,212,1079,264),k=2.6)
f.pressed('mGR',(1472,53,1498,104),k=2.6)
f.pressed('m8',(1472,106,1498,157),k=2.6)
f.pressed('m4',(1472,159,1498,210),k=2.6)
f.pressed('mOff',(1472,212,1498,264),k=2.6)
f.erase([(1296,87,1315,104)],smooth=True,sigma=((10,4),(5,2)),dilate=0,feather=1.5)
f.relabel((1296,87,1315,104),'0',15,colour=(140,122,100),erase=False,centre=(1306.5,96.5))
f.erase([(1283,126,1311,145)],smooth=True,sigma=((10,4),(5,2)),dilate=0,feather=1.5)
f.relabel((1283,126,1311,145),'100',16,colour=(140,122,100),erase=False,centre=(1296.5,135.5))
f.draw_vu((1156,83,1394,177),(1276,228),118,(-41.3,11.6,42.5),ink=(110,96,82),red=(238,58,58),size=1.1,
          face_mask='rect',red_width=9,vu_left=(1176,97,13),vu_right=(1376,97,13,True))
f.write()
f.preview('prev_vfet.png',{'input':-90,'output':60,'attack':120,'release':-120,'needle':-30})
