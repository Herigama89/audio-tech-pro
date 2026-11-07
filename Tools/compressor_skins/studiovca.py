import sys; sys.path.insert(0,'tools')
from face import Face
f=Face('svca','photos/studiovca.jpg',crop=(112,558,1942,884))
# the maker's marks: the big number and the name
f.brushed((185,560,408,795),keep=[(230,585,286,640),(234,798,292,856),(318,790,408,872)],ref_rect=(122,560,228,795),feather=6)
# knobs (bar knobs: they turn whole)
K={'gain1':(647,674,40),'gain2':(646,825,44),'ratio1':(740,605,31),'ratio2':(742,761,34),'thresh1':(838,683,30),'thresh2':(839,833,35),
   'attack1':(953,615,31),'attack2':(955,769,30),'release1':(1051,675,31),'release2':(1052,825,34),'limThr1':(1749,685,30),'limThr2':(1744,836,36)}
for k,(x,y,r) in K.items(): f.knob_whole(k,x,y,r+3)
# lamps (illuminated keys and indicators, lit in the photo)
L={'in1':(479,667,13),'key1':(538,669,13),'in2':(483,820,14),'key2':(539,820,14),'auto1':(1051,612,14),'auto2':(1051,764,14),
   'gainChange':(1159,621,13),'stereo':(1523,820,14),'limIn2':(1619,821,14),'limIn1':(1623,669,13),'limit2':(1679,776,13),'limit1':(1683,623,14),
   'power':(366,826,26)}
for k,(x,y,r) in L.items(): f.lamp(k,x,y,r,glow=1.45,dark=0.35)
# the VU meters' needles (pivots under the dials)
f.needle('needle1',tip=(1398,647),base=(1418,688),pivot=(1437,727),rtip=89,width=2.5,threshold=60,window=(1372,622,1478,689))
f.needle('needle2',tip=(1205,738),base=(1225,773),pivot=(1243,805),rtip=77,width=2.5,threshold=60,window=(1168,705,1290,775))
f.write()
f.preview('prev_svca.png',{k:40 for k in K}|{'needle1':40,'needle2':10})
