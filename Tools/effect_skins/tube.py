import numpy as np, cv2, json, math
from PIL import Image
src=np.array(Image.open('photos/tube.png').convert('RGB'))
bg=src.copy()
A=src.astype(int); lum=A.sum(2)/3
H,W=lum.shape
yy,xx=np.mgrid[0:H,0:W]
meta={}
erase=np.zeros((H,W),np.uint8)
fill=np.zeros((H,W),np.uint8)
def square(img,cx,cy,R):
    return img[cy-R:cy+R,cx-R:cx+R]
def save(name,rgb,alpha):
    Image.fromarray(np.dstack([rgb,(alpha*255).clip(0,255).astype(np.uint8)]),'RGBA').save('out/'+name+'.png')
# 1. the big knobs: only the white pointer line moves (the skirt and fluted cap stay as photographed)
expect={'lowBoost':128,'lowAtten':-2,'highBoost':34,'highAtten':88,'bandwidth':0}
for name,(cx,cy) in {'lowBoost':(658,170),'lowAtten':(952,170),'highBoost':(1358,174),'highAtten':(1653,175),'bandwidth':(1157,485)}.items():
    R=100
    r=np.hypot(xx-cx,yy-cy)
    cand=((lum>175)&(r>66)&(r<94)).astype(np.uint8)
    n,lab,st,cen=cv2.connectedComponentsWithStats(cand)
    best=None
    for i in range(1,n):
        if st[i][4]<5: continue
        ang=math.degrees(math.atan2(cen[i][0]-cx,-(cen[i][1]-cy)))
        d=abs((ang-expect[name]+180)%360-180)
        if best is None or d<best[0]: best=(d,i,ang)
    _,i,ang=best
    m=(lab==i).astype(np.uint8)
    # grow into the line's soft edges
    grow=cv2.dilate(m,np.ones((9,9),np.uint8))&((lum>60)&(r<99)).astype(np.uint8)
    m=m|grow
    erase|=cv2.dilate(m,np.ones((5,5),np.uint8))
    a=cv2.GaussianBlur(m.astype(np.float32),(3,3),0.7)
    save('tube_ptr_'+name,square(src,cx,cy,R),square(a,cx,cy,R))
    meta[name]=dict(cx=cx,cy=cy,R=R,angle=round(ang,1))
    print(name,round(ang,1),int(m.sum()))
# 2. the bar knobs and the power switch: the whole knob turns
def knobmask(cx,cy,R,lim,seedr=6):
    sub=A[cy-R:cy+R,cx-R:cx+R]; l=sub.sum(2)/3
    gy,gx=np.mgrid[-R:R,-R:R]; rr=np.hypot(gy,gx)
    up=(-gy)/np.maximum(rr,1)
    lim=np.where(up>-0.55,lim,R-2)
    panel=(((sub[:,:,2]-sub[:,:,0])>14)&(l<130))|(rr>lim)
    k=(~panel).astype(np.uint8)
    k=cv2.morphologyEx(k,cv2.MORPH_OPEN,np.ones((3,3),np.uint8))
    n,lab,st,_=cv2.connectedComponentsWithStats(k)
    c=lab[R-seedr:R+seedr,R-seedr:R+seedr].flatten(); c=np.bincount(c[c>0]).argmax()
    m=(lab==c).astype(np.uint8)
    # fill holes
    ff=m.copy(); h,w=ff.shape; msk=np.zeros((h+2,w+2),np.uint8); cv2.floodFill(ff,msk,(0,0),1); m=m|(1-ff)
    return m
for name,(cx,cy,R,lim) in {'lowFreq':(810,470,116,88),'highFreq':(1502,472,116,88),'attenFreq':(1942,172,96,78),'tubes':(1970,481,52,46),'eqIn':(527,475,52,48)}.items():
    m=knobmask(cx,cy,R,lim)
    md=cv2.dilate(m,np.ones((3,3),np.uint8))
    a=cv2.GaussianBlur(md.astype(np.float32),(3,3),0.8)
    save('tube_knob_'+name,square(src,cx,cy,R),a)
    big=cv2.dilate(m,np.ones((13,13),np.uint8))
    gy,gx=np.mgrid[-R:R,-R:R]; rr=np.hypot(gy,gx); big=big&((rr<lim+3)|((-gy)/np.maximum(rr,1)<=-0.55))
    sub=lum[cy-R:cy+R,cx-R:cx+R]
    shadow=((sub<47)&(rr<lim+2)).astype(np.uint8)
    fill[cy-R:cy+R,cx-R:cx+R]|=big|shadow|(rr<min(40,lim-6)).astype(np.uint8)
    meta[name]=dict(cx=cx,cy=cy,R=R)
# the bar tops' glare just under their labels
for (x0,y0,x1,y1) in [(1478,386,1528,400),(812,388,862,402)]:
    fill[y0:y1,x0:x1]|=(lum[y0:y1,x0:x1]>62).astype(np.uint8)
# 3. the maker's name and model (keeps PROGRAM EQUALIZER)
tm=np.zeros((H,W),np.uint8)
for (x0,y0,x1,y1) in [(285,236,392,272),(290,308,388,346)]:
    tm[y0:y1,x0:x1]=(lum[y0:y1,x0:x1]>95)
erase|=cv2.dilate(tm,np.ones((7,7),np.uint8))
# the knob areas: the bars' pixels are big, use a wider radius
bg=cv2.inpaint(bg,erase,9,cv2.INPAINT_TELEA)
# the panel under the turning knobs: its own colour, smoothly, from the clear panel around (no smearing of the labels)
pan=(((A[:,:,2]-A[:,:,0])>14)&(lum>40)&(lum<120)&(cv2.dilate(fill,np.ones((9,9),np.uint8))==0)).astype(np.float32)
f=bg.astype(np.float32)
est=None
for sg in (45,18):
    num=cv2.GaussianBlur(f*pan[...,None],(0,0),sg); den=cv2.GaussianBlur(pan,(0,0),sg)[...,None]
    e=num/np.maximum(den,1e-6); wgt=np.clip(den/0.15,0,1)
    est=e if est is None else est*(1-wgt)+e*wgt
fs=np.maximum(fill.astype(np.float32),cv2.GaussianBlur(fill.astype(np.float32),(0,0),3))[...,None]
bg=(f*(1-fs)+est*fs)
bg=(bg+np.random.default_rng(5).normal(0,1.4,bg.shape)*fs).clip(0,255).astype(np.uint8)
erase=erase|fill
# grain over filled panel
noise=np.random.default_rng(3).normal(0,1.6,bg.shape)
bg=np.where(erase[...,None]>0,(bg+noise).clip(0,255),bg).astype(np.uint8)
# 4. the pilot lamp, unlit (glass dark red, no glow)
cx,cy,R=1875,327,46
lamp=square(src,cx,cy,R).astype(np.float32)
r=np.hypot(*np.mgrid[-R:R,-R:R])
jewel=np.clip((33-r)/2.5,0,1)
l2=lamp.mean(2)
dark=np.dstack([l2*0.34+18,l2*0.10+6,l2*0.09+6])
save('tube_lampoff',(lamp*(1-jewel[...,None])+dark*jewel[...,None]).clip(0,255).astype(np.uint8),jewel)
meta['lamp']=dict(cx=cx,cy=cy,R=R)
X0,Y0,X1,Y1=86,12,2238,638
Image.fromarray(bg[Y0:Y1,X0:X1]).save('out/tube_panel.png')
for k in meta: meta[k]['cx']-=X0; meta[k]['cy']-=Y0
meta['size']=[X1-X0,Y1-Y0]
json.dump(meta,open('tube_layout.json','w'),indent=1)
print(meta)
