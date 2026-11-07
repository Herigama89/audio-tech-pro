import sys; sys.path.insert(0,'tools')
import numpy as np
from face import Face
f=Face('plat','photos/platinum.png',crop=(0,0,1710,320),scale=1.0)
# the maker's marks: name and logo
f.erase([(118,80,242,128)],threshold=40,dilate=5,radius=7)
f.erase([(1478,62,1580,146)],threshold=40,dilate=5,radius=7)
# the meters: bars (lit from the bottom up to the level, drawn by the app), the tracks unlit
track=np.median(f.src[60:115,803:812].reshape(-1,3),axis=0)
lit=(1059,57,1068,156)
for k,x in {'barInL':523,'barInR':565,'bar3':761,'bar4':803,'barGrL':845,'barGrR':887,'bar7':929,'bar8':971,'barOutL':1059,'barOutR':1101}.items():
    f.bar(k,(x,57,x+9,156),lit_rect=lit,track=track)
# the status lamps under the meters: all dark
for r in [(503,193,594,280),(741,193,1001,240)]:
    f.lamp_mask('status_'+str(r[0]),r,sat=50,bright=90,dark=0.18)
# arcs round the black knobs (value from the lowest setting), numbers round the gold ones
f.arc('arcInput',676.7,106.7,37.5,47.5,lit_angle=-118,unlit_angle=124,unlit_from=(1193.7,103,36,48))
f.arc('arcMonitor',1193.7,103,36,48,lit_angle=-118,unlit_angle=124,lit_from=(676.7,106.7,37.5,47.5))
f.arc('arcHp1',1290.6,95.6,23.5,32,lit_angle=-118,unlit_angle=124,lit_from=(676.7,106.7,37.5,47.5),unlit_from=(1193.7,103,36,48))
f.arc('arcHp2',1375,95.6,23.5,32,lit_angle=-118,unlit_angle=124,lit_from=(676.7,106.7,37.5,47.5),unlit_from=(1193.7,103,36,48))
f.ring('ringThreshold',340,106.7,29,43,tint=(255,196,70))
f.ring('ringRatio',441.7,106.7,29,43,tint=(255,196,70))
# keys: their lamp strips
keys={'kRms':(316.7,205),'kSum':(366,205),'kScFilter':(415,205),'kAutoRel':(316.7,235.7),'kListen':(366,235.7),'kLimit':(415,235.7),
      'kOhm':(316.7,267.3),'kConsole':(366,267.3),'kAir':(702.3,235.7),'kDrive':(702.3,267.3),
      'kLP':(1059,203),'kBP':(1108,203),'kHP':(1059,234),'kEQ':(1108,234),'kHS':(1059,267),'kOff':(1192.5,203),'k0dB':(1192.5,234),'k12dB':(1192.5,267)}
for k,(x,y) in keys.items():
    f.lamp_mask(k,(x-20,y+1,x+20,y+12),sat=60,bright=100)
# keys that have no part here: blank
for (x,y) in [(653.3,205),(702.3,205),(653.3,235.7),(1290.6,203),(1375,203)]:
    f.erase_ink((x-19,y-12,x+19,y+3),t=22,dilate=5,darker=False,size=21)   # (the keys stay, blank: their names taken off)
    f.lamp_mask('unused',(x-20,y+1,x+20,y+12),sat=50,bright=90,dark=0.15)    # (and their lamps dark)
    del f.layout['parts']['unused']
f.erase([(976-14,267-14,976+14,267+14),(965,238,988,252)],dilate=1,radius=6)   # channel 8's knob
# relabelled
f.relabel((316,38,455,54),'Threshold                 Ratio',10.5,centre=(390,46))
f.relabel((295,192,338,214),'RMS',8.2,centre=(316.7,201.5),colour=(222,224,232))
f.relabel((345,192,388,214),'Sum',8.2,centre=(366,201.5),colour=(222,224,232))
f.relabel((295,223,338,244),'Auto',8.2,centre=(316.7,232),colour=(222,224,232))
f.relabel((345,223,388,244),'Listen',8.2,centre=(366,232),colour=(222,224,232))
f.relabel((394,223,437,244),'Limit',8.2,centre=(415,232),colour=(222,224,232))
f.relabel((295,255,338,275),'Hold',8.2,centre=(316.7,264),colour=(222,224,232))
f.relabel((445,188,495,202),'Attack',8.5,centre=(470,195.7))
f.relabel((445,238,495,252),'Release',8.5,centre=(470,245))
f.relabel((510,161,545,176),'IN',8,centre=(527.5,168.5),colour=(240,170,70))
f.relabel((553,161,590,176),'IN',8,centre=(569.5,168.5),colour=(240,170,70))
f.relabel((755,161,825,176),'',8)
f.relabel((840,161,905,176),'GR',8,centre=(870,168.5),colour=(240,170,70))
f.relabel((920,161,985,176),'',8)
for (x,t) in [(766,'Knee'),(808,'Freq'),(850,'Q'),(892,'Gain'),(934,'Ceiling')]:
    f.relabel((x-12,238,x+12,252),t,8.5,centre=(x,245))
for (x,y,t) in [(1059,201,'LP'),(1108,201,'BP'),(1059,232,'HP'),(1108,232,'EQ'),(1192.5,201,'Off'),(1192.5,232,'0 dB'),(1192.5,264,'-12 dB')]:
    f.relabel((x-19,y-8,x+19,y+6),t,8.2,centre=(x,y),colour=(222,224,232))
f.erase_ink((1046,259,1073,275),t=25,dilate=3,darker=False,size=15)
f.relabel((1040,258,1078,272),'HS',8.2,centre=(1059,264),colour=(222,224,232),erase=False)
f.relabel((1172,38,1215,52),'Output',10.5,centre=(1193.7,45))
f.relabel((1290,38,1376,52),'',10.5)
f.relabel((1270,38,1312,52),'Make Up',10.5,centre=(1290.6,45),erase=False,colour=(225,228,240))
f.relabel((1355,38,1395,52),'Mix',10.5,centre=(1375,45),erase=False,colour=(225,228,240))
f.relabel((1282,135,1300,150),'',9); f.relabel((1366,135,1384,150),'',9)
f.relabel((152,230,212,246),'Key',9,centre=(181,238))
f.relabel((1150,180,1236,195),'',9)    # (none)
f.lamp('power',1522,234,27,glow=1.25,dark=0.3)
# (the lock key, now HS, had no lamp strip: it gets LP's, under it)
import copy
hs=copy.deepcopy(f.layout['parts']['kLP']); hs['y']+=round(267-203); f.layout['parts']['kHS']=hs
f.write()
f.preview('prev_plat.png')
