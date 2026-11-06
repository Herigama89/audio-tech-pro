# The channel strip's face: the photo, cropped (no ears), at twice its size; the knobs' pointer lines cut out
# to turn, every LED unlit in the panel (lit ones drawn over it), the power button lit and unlit.
import numpy as np, cv2, json, math
from PIL import Image, ImageFilter
src_im=Image.open('photos/strip.png').convert('RGB').crop((96,3,1600,159))
big=src_im.resize((src_im.width*2,src_im.height*2),Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=2,percent=60,threshold=2))
src=np.array(big); A=src.astype(int); H,W,_=A.shape
yy,xx=np.mgrid[0:H,0:W]
bg=src.copy(); erase=np.zeros((H,W),np.uint8)
def save(name,rgb,alpha):
    Image.fromarray(np.dstack([rgb.clip(0,255).astype(np.uint8),(np.clip(alpha,0,1)*255).astype(np.uint8)]),'RGBA').save('out/'+name+'.png')
knobs={'mic':(97.6,212.7,31),'trim':(227,86,34),'deEss':(355,212,32),'ratio':(505,211,33),'thresh':(604,84,33),'release':(700,212,33),
'range':(879.7,214.1,31.6),'gate':(975.4,87.4,31.4),'gateRel':(1071.9,214.5,31.6),'hpf':(1250,211,33),'lpf':(1360,85,33),'lfFreq':(1483.1,216.5,34),
'lf':(1584.8,88.1,34),'lmfFreq':(1697.1,212.2,30.3),'lmf':(1793.8,85.3,30.2),'lmfQ':(1890.3,212.3,30.3),'hmfQ':(2051,211,32),'hmf':(2147.9,87,31.4),
'hmfFreq':(2245,213.8,31.1),'hf':(2356.2,85.7,30.4),'hfFreq':(2456.9,212.7,30.3),'out':(2707,84,33)}
meta={'knobs':{},'leds':{}}
rng=np.random.default_rng(2)
for name,(cx,cy,r) in knobs.items():
    d=np.hypot(xx-cx,yy-cy)
    inner=d<r*0.8
    med=np.median(A[inner],axis=0)
    diff=np.abs(A-med).sum(2)
    band=(np.abs(xx-cx)<7)&(yy<cy+4)&(d<r+1.5)
    m=(band&(diff>90)).astype(np.uint8)
    n,lab,st,cen=cv2.connectedComponentsWithStats(m)
    i=1+np.argmax(st[1:,4]); m=(lab==i).astype(np.uint8)
    # its soft edges too
    m=m|(cv2.dilate(m,np.ones((3,3),np.uint8))&(band&(diff>45)).astype(np.uint8))
    R=int(r)+3; icx,icy=int(round(cx)),int(round(cy))
    a=cv2.GaussianBlur(m.astype(np.float32),(3,3),0.6)
    save('strip_ptr_'+name,src[icy-R:icy+R,icx-R:icx+R].astype(np.float32),a[icy-R:icy+R,icx-R:icx+R])
    # the cap without its pointer: the cap is round, so its own surface a little round the dial fills in
    strip=((np.abs(xx-cx)<6.5)&(yy>cy-r-6)&(yy<cy+4)&(d<r+4.5)).astype(np.uint8)
    em=(cv2.dilate(m,np.ones((3,3),np.uint8))|strip)&(d<r+4.5).astype(np.uint8)
    rot=cv2.getRotationMatrix2D((float(cx),float(cy)),70,1.0)
    turned=cv2.warpAffine(src,rot,(W,H),flags=cv2.INTER_CUBIC)
    half=cv2.warpAffine(src,cv2.getRotationMatrix2D((float(cx),float(cy)),90,1.0),(W,H),flags=cv2.INTER_CUBIC)
    near=np.clip((r*0.55-d)/4,0,1)[...,None]
    turned=(turned*(1-near)+half*near)
    soft=cv2.GaussianBlur(em.astype(np.float32),(3,3),0.8)[...,None]
    bg=(bg.astype(np.float32)*(1-soft)+turned.astype(np.float32)*soft).clip(0,255).astype(np.uint8)
    # what's left at the very centre (no turn moves it away): filled from round it
    left=((np.abs(bg.astype(int)-med).sum(2)>55)&(d<9)&(em>0)).astype(np.uint8)
    if left.any():
        bg=cv2.inpaint(bg,cv2.dilate(left,np.ones((3,3),np.uint8)),3,cv2.INPAINT_TELEA)
    meta['knobs'][name]=dict(cx=icx,cy=icy,R=R)
leds={'phantom':(102,144),'pad':(158,144),'phase':(228,268),'line':(284,268),'deEssIn':(357,144),'de10':(406,81),'de6':(406,112),'de3':(406,144),
'knee':(473,144),'compFast':(529,144),'linRel':(624,268),'gr3':(746,51),'gr6':(776,51),'gr10':(806,51),'gr14':(836,52),'gr20':(865,52),
'gg3':(746,104),'gg6':(776,104),'gg10':(806,104),'gg14':(836,104),'gg20':(865,104),'dynIn':(806,268),'exp':(988,268),'gateFast':(1111,144),
'filtSc':(1199,144),'filtIn':(1255,144),'lpfOn':(1297,144),'hpfOn':(1197,268),'lfBell':(1482,144),'black':(1904,144),'eqIn':(1960,144),'eqSc':(2016,144),
'hfBell':(2435,144),'dynPost':(2537,144),'deEssPost':(2593,144),'ext':(2537,268),'link':(2593,268),'insIn':(2652,268),'insOp':(2708,268),
'o24':(2785,47),'o12':(2785,79),'o0':(2785,111),'om12':(2785,142),'om24':(2785,174),'mtrInp':(2785,268)}
RL=17
gy,gx=np.mgrid[-RL:RL,-RL:RL]; dl=np.hypot(gx,gy)
for name,(x,y) in leds.items():
    # find the LED's own centre (its brightest blob)
    sub=A[y-RL:y+RL,x-RL:x+RL]
    sat=sub.max(2)-sub.min(2)
    w=((sat>90)&(sub.max(2)>170)).astype(float)
    if w.sum()>0:
        oy,ox=[(g*w).sum()/w.sum() for g in np.mgrid[0:2*RL,0:2*RL]]
        x=int(round(x-RL+ox)); y=int(round(y-RL+oy))
    lit=src[y-RL:y+RL,x-RL:x+RL].astype(np.float32)
    ring=lit[(dl>14)&(dl<17)]; panel=np.median(ring,axis=0)
    core=np.clip((10.5-dl)/1.5+0.5,0,1)
    halo=np.clip((16-dl)/3,0,1)
    k=np.where(dl<10.5,0.24,0.08)
    # unlit: the lens dark, a little of its colour left; its glow gone
    off=panel+(lit-panel)*k[...,None]
    off=off*1.0
    blend=halo[...,None]
    patch=bg[y-RL:y+RL,x-RL:x+RL].astype(np.float32)
    bg[y-RL:y+RL,x-RL:x+RL]=(patch*(1-blend)+off*blend).clip(0,255).astype(np.uint8)
    save('strip_led_'+name,lit,np.maximum(core,halo*0.85))
    meta['leds'][name]=[x,y]
# the power button: lit as photographed (a sprite), unlit in the panel
px,py,PR=2916,160,62
pb=src[py-PR:py+PR,px-PR:px+PR].astype(np.float32)
dp=np.hypot(*np.mgrid[-PR:PR,-PR:PR])
lum=pb.mean(2)
glow=np.clip((lum-120)/80,0,1)*(dp<56)
offp=pb*(1-glow[...,None]*0.62)
bg[py-PR:py+PR,px-PR:px+PR]=offp.clip(0,255).astype(np.uint8)
save('strip_power_on',pb,np.clip((58-dp)/2,0,1))
meta['power']=[px,py,PR]
bg=bg[:, :2990]; W=2990
Image.fromarray(bg).save('out/strip_panel.png')
meta['size']=[W,H]
json.dump(meta,open('strip_layout.json','w'),indent=1)
print(json.dumps(meta['leds']))
