import sys; sys.path.insert(0,'tools')
from face import Face
import vu
import numpy as np
f=Face('svca','photos/studiovca.jpg',crop=(112,558,1942,884))
# the maker's marks: the big number and the name
f.brushed((185,560,408,795),keep=[(230,585,286,640),(234,798,292,856),(318,790,408,872)],ref_rect=(122,560,228,795),feather=6)
# knobs (bar knobs: they turn whole)
K={'gain1':(647,674,40),'gain2':(646,825,44),'ratio1':(740,605,31),'ratio2':(742,761,34),'thresh1':(838,683,30),'thresh2':(839,833,35),
   'attack1':(953,615,31),'attack2':(955,769,30),'release1':(1051,675,31),'release2':(1052,825,34),'limThr1':(1749,685,30),'limThr2':(1744,836,36)}
# (the knobs alone turn: their silver caps and bars, not the red round them)
notRed=lambda A,l,s: ~((A[:,:,0]-np.maximum(A[:,:,1],A[:,:,2])>45)&(A[:,:,0]>90)) & (l>55)
for k,(x,y,r) in K.items(): f.knob_cap(k,x,y,r+6,notRed)
# lamps (illuminated keys and indicators, lit in the photo)
L={'in1':(479,667,13),'key1':(538,669,13),'in2':(483,820,14),'key2':(539,820,14),'auto1':(1051,612,14),'auto2':(1051,764,14),
   'gainChange':(1159,621,13),'stereo':(1523,820,14),'limIn2':(1619,821,14),'limIn1':(1623,669,13),'limit2':(1679,776,13),'limit1':(1683,623,14),
   'power':(366,826,26)}
for k,(x,y,r) in L.items(): f.lamp(k,x,y,r,glow=1.45,dark=0.35)
f.boost('power',gain=1.5,glow=(255,50,30),amount=0.55)
# the VU meters' needles (pivots under the dials)
f.needle('needle1',tip=(1396,644),base=(1418,689),pivot=(1430,722),rtip=89,width=2.5,threshold=60,window=(1372,622,1478,689))
f.needle('needle2',tip=(1205,738),base=(1225,773),pivot=(1243,805),rtip=77,width=2.5,threshold=60,window=(1168,705,1290,775))
for (y0,y1) in [(600,642),(748,788)]:
    f.erase([(606,y0,684,y1)],threshold=70,dilate=5,radius=6,brushed=True)
    f.relabel((606,y0,684,y1),'GAIN',12,colour=(246,240,240),font='sansbold',erase=False,centre=(645,(y0+y1)/2))
# the meters' scales drawn clean (the photo's were crossed by their needles)
for (pv,circle,below,r,ang,sz,vu_at) in [((1430,722),(1429,671,59),688,80,(-28.2,15.4,26.6),0.7,(1428,669)),((1243,805),(1242,758,54),776,69,(-30.5,15.0,31.9),0.6,(1241,762))]:
    cx,cy,rr=circle
    f.draw_vu((cx-rr-2,cy-rr-2,cx+rr+2,below+1),pv,r,ang,ink=(28,24,20),red=(200,40,24),labels='plain',size=sz,face_mask=circle,
              mask_below=below,numbers=[-20,-10,-7,-5,-3,0,3],red_width=5,percent=True,
              extra=(('VU',vu_at[0],vu_at[1],13,False),))
f.write()
f.preview('prev_svca.png',{k:40 for k in K}|{'needle1':40,'needle2':10})
