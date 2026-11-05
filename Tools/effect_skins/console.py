import numpy as np, cv2, json, math
from PIL import Image, ImageDraw, ImageFont
src=np.array(Image.open('photos/console.png').convert('RGB'))
bg=src.copy()
A=src.astype(int); lum=A.sum(2)/3
H,W=lum.shape; yy,xx=np.mgrid[0:H,0:W]
meta={}; erase=np.zeros((H,W),np.uint8)
def sq(img,cx,cy,R): return img[cy-R:cy+R,cx-R:cx+R]
def save(name,rgb,alpha):
    Image.fromarray(np.dstack([rgb,(np.clip(alpha,0,1)*255).astype(np.uint8)]),'RGBA').save('out/'+name+'.png')
def disc(R,r,f=1.2):
    d=np.hypot(*np.mgrid[-R:R,-R:R])+0.5
    return np.clip((r-d)/f+0.5,0,1)
# pointer lines only (the knobs and rings stay as photographed, lit as they were)
P={'highGain':(878,113,'white',24,42),'midGain':(1034,116,'white',24,41),'lowGain':(1189,116,'white',24,41),
   'midFreq':(1034,116,'dark',42,58),'lowFreq':(1189,116,'dark',42,58)}
for name,(cx,cy,kind,r0,r1) in P.items():
    r=np.hypot(xx-cx,yy-cy); ang=np.degrees(np.arctan2(xx-cx,-(yy-cy)))%360
    near=(abs(ang-180)<25)&(r>r0)&(r<r1)
    cand=((lum>185) if kind=='white' else (lum<70))&near
    m=cand.astype(np.uint8)
    n,lab,st,cen=cv2.connectedComponentsWithStats(m)
    i=1+np.argmax(st[1:,4]); m=(lab==i).astype(np.uint8)
    ccx,ccy=cen[i]; a=math.degrees(math.atan2(ccx-cx,-(ccy-cy)))%360
    grow=cv2.dilate(m,np.ones((3,3),np.uint8))&(((lum>120) if kind=='white' else (lum<120))&near).astype(np.uint8)
    m=m|grow
    erase|=cv2.dilate(m,np.ones((3,3),np.uint8))
    R=r1+2
    save('console_ptr_'+name,sq(src,cx,cy,R),sq(cv2.GaussianBlur(m.astype(np.float32),(3,3),0.6),cx,cy,R))
    meta[name]=dict(cx=cx,cy=cy,R=R,angle=round(a,1)); print(name,round(a,1),int(m.sum()))
# whole knobs that turn (the red and blue ones have a fin; the level knob is knurled)
for name,(cx,cy,r) in {'drive':(725,113,57.5),'hpf':(1342,114,58.5),'output':(1741,116,25.5)}.items():
    R=int(r)+3
    save('console_knob_'+name,sq(src,cx,cy,R),disc(R,r))
    meta[name]=dict(cx=cx,cy=cy,R=R,angle=180.0)
bg=cv2.inpaint(bg,erase,4,cv2.INPAINT_TELEA)
# LEDs: all of them unlit in the panel, the lit ones as sprites over it
off=sq(src,1657,45,10).copy()
offa=disc(10,9.6,1.8)
def put_off(cx,cy):
    global bg
    s=sq(bg,cx,cy,10).astype(np.float32)
    bg[cy-10:cy+10,cx-10:cx+10]=(s*(1-offa[...,None])+off*offa[...,None]).astype(np.uint8)
for (cx,cy) in [(1626,44),(1813,44),(1631,111),(1655,179),(1681,179),(1708,179),(1735,178),(1761,178)]:
    put_off(cx,cy)
def lit(name,cx,cy):
    R=13; s=sq(src,cx,cy,R).astype(np.float32)
    d=np.hypot(*np.mgrid[-R:R,-R:R])
    core=np.clip((8.5-d)/1.5+0.5,0,1); glow=np.clip(1-d/R,0,1)**2*0.6
    col=s[R,R]
    rgb=s*core[...,None]+col*(1-core[...,None])
    save('console_led_'+name,rgb.clip(0,255).astype(np.uint8),np.maximum(core,glow))
lit('green',1655,179); lit('yellow',1708,179); lit('red',1962,179)
leds={'sig':(1655,179),'m10':(1681,179),'m0':(1708,179),'p5':(1735,178),'p14':(1761,178),'p18':(1788,178),'p24':(1815,178),'eq':(1631,111)}
# the right-hand button pair becomes the Silk pair: RED and BLUE
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf',66)
def relabel(x0,y0,x1,y1,text):
    global bg
    reg=bg[y0:y1,x0:x1].astype(np.float32)
    # the cap's own white, from its edges, row by row
    for y in range(reg.shape[0]):
        edge=np.concatenate([reg[y,:4],reg[y,-4:]])
        reg[y,4:-4]=np.median(edge,axis=0)
    cap=Image.fromarray(reg.clip(0,255).astype(np.uint8))
    w,h=cap.size
    big=Image.new('L',(w*4,h*4),0); dr=ImageDraw.Draw(big)
    pitch=(h*4-24)/len(text)
    for i,ch in enumerate(text):
        bb=dr.textbbox((0,0),ch,font=font)
        tw,th=bb[2]-bb[0],bb[3]-bb[1]
        lay=Image.new('L',(tw+4,th+4),0); ImageDraw.Draw(lay).text((2-bb[0],2-bb[1]),ch,font=font,fill=255)
        lay=lay.resize((max(1,int((tw+4)*0.5)),int((th+4)*1.0)),Image.LANCZOS)
        big.paste(lay,(int(w*2-lay.size[0]/2),int(12+pitch*i+(pitch-lay.size[1])/2)),lay)
    mask=np.array(big.resize((w,h),Image.LANCZOS)).astype(np.float32)/255
    ink=np.array([48,48,50],np.float32)
    mask=np.clip(mask*1.25,0,1)
    reg=reg*(1-mask[...,None])+ink*mask[...,None]
    bg[y0:y1,x0:x1]=reg.clip(0,255).astype(np.uint8)
relabel(1494,47,1523,104,'RED'); relabel(1494,114,1523,173,'BLUE')
# a lit cap of each colour (the buttons glow while on)
def glowcap(name,x0,y0,x1,y1,col):
    reg=bg[y0-3:y1+3,x0-3:x1+3].astype(np.float32)
    l=reg.mean(2,keepdims=True)/255
    tint=np.array(col,np.float32)
    rgb=reg*(tint/255.0)*1.02+tint*0.06
    h,w=reg.shape[:2]
    a=np.zeros((h,w),np.float32); a[3:-3,3:-3]=1
    a=np.maximum(a,cv2.GaussianBlur(a,(0,0),2.5)*0.7)
    save('console_cap_'+name,rgb.clip(0,255).astype(np.uint8),a)
    meta['cap_'+name]=dict(x=x0-3,y=y0-3,w=w,h=h)
glowcap('red',1492,44,1524,107,(255,70,55)); glowcap('blue',1492,111,1524,176,(80,150,255))
X0,Y0,X1,Y1=616,12,2012,206
Image.fromarray(bg[Y0:Y1,X0:X1]).save('out/console_panel.png')
for k,v in meta.items():
    if 'cx' in v: v['cx']-=X0; v['cy']-=Y0
    else: v['x']-=X0; v['y']-=Y0
meta['leds']={k:(x-X0,y-Y0) for k,(x,y) in leds.items()}
meta['buttons']={'phase':(1439-X0,41-Y0,33,64),'eqIn':(1439-X0,109-Y0,33,65),'red':(1492-X0,44-Y0,32,63),'blue':(1492-X0,111-Y0,32,65),'power':(1926-X0,112-Y0,72,34)}
meta['size']=[X1-X0,Y1-Y0]
json.dump(meta,open('console_layout.json','w'),indent=1); print(meta)
