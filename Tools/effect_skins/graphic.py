import numpy as np, cv2
from PIL import Image, ImageFilter
src=np.array(Image.open('photos/graphic.png').convert('RGB'))
bg=src.copy()
lum=src.astype(int).sum(2)/3
slots=[(159,188),(233,262),(307,336),(381,410),(455,484),(529,558),(603,632),(676,705),(750,779),(823,852)]
caps=[]
for k,(s,e) in enumerate(slots):
    # the cap: bright (or red) pixels inside the slot rows between x 120 and 200
    y0,y1,x0,x1=s-1,e+2,118,200
    reg=src[y0:y1,x0:x1].astype(int)
    l=reg.sum(2)/3; red=(reg[:,:,0]-reg[:,:,2])
    m=((l>45)|(red>40)).astype(np.uint8)
    # keep the biggest blob
    n,lab,st,_=cv2.connectedComponentsWithStats(m)
    big=1+np.argmax(st[1:,cv2.CC_STAT_AREA]); m=(lab==big).astype(np.uint8)
    m=cv2.dilate(m,np.ones((3,3),np.uint8))
    ys,xs=np.where(m); bx0,bx1,by0,by1=xs.min(),xs.max()+1,ys.min(),ys.max()+1
    a=cv2.GaussianBlur(m.astype(np.float32),(3,3),0.8)
    # a soft shadow under the cap (it sits in the groove)
    sh=cv2.GaussianBlur(cv2.dilate(m,np.ones((5,7),np.uint8)).astype(np.float32),(9,9),3)*0.55
    alpha=np.maximum(a,sh)
    rgb=reg.astype(np.float32)
    rgb=rgb*(a[...,None])+np.array([4,4,5])*(1-a[...,None])
    spr=np.dstack([rgb,alpha*255]).clip(0,255).astype(np.uint8)
    pad=4
    cx0,cx1,cy0,cy1=max(0,bx0-pad),min(x1-x0,bx1+pad),max(0,by0-pad),min(y1-y0,by1+pad)
    spr=spr[cy0:cy1,cx0:cx1]
    Image.fromarray(spr,'RGBA').save(f'out/graphic_cap{k}.png')
    caps.append((x0+cx0,y0+cy0,cx1-cx0,cy1-cy0, x0+(bx0+bx1)/2))
    # the empty slot: each column of the cap's span from the same rows 80 px to the left
    rng=np.random.default_rng(k)
    for y in range(s,e+1):
        side=np.concatenate([src[y,70:96],src[y,206:236]]).astype(float)
        med=np.median(side,axis=0)
        for x in range(112,206):
            bg[y,x]=np.clip(med+rng.normal(0,0.6,3),0,255)
print(caps)
# remove the maker's marks (logo and model number)
mask=np.zeros(lum.shape,np.uint8)
for (x0,y0,x1,y1) in [(10,8,72,72),(182,6,276,56)]:
    sub=src[y0:y1,x0:x1].astype(int)
    mm=((sub[:,:,2]-sub[:,:,0])>25)|(sub.sum(2)/3>70)
    mask[y0:y1,x0:x1]=mm
mask=cv2.dilate(mask,np.ones((5,5),np.uint8))
bg=cv2.inpaint(bg,mask,6,cv2.INPAINT_TELEA)
# a little of the panel's grain back over the filled parts
noise=np.random.default_rng(1).normal(0,1.2,bg.shape)
bg=np.where(mask[...,None]>0,(bg+noise).clip(0,255),bg).astype(np.uint8)
Image.fromarray(bg).save('out/graphic_panel.png')
# the IN lamp lit
cx,cy,r=204,900,14
crop=src[cy-r:cy+r,cx-r:cx+r].astype(np.float32)
yy,xx=np.mgrid[-r:r,-r:r]; d=np.sqrt(xx**2+yy**2)
lens=(d<9.5).astype(np.float32)
lit=crop.copy()
lit[...,0]=np.clip(crop[...,0]*2.6+60,0,255); lit[...,1]=np.clip(crop[...,1]*1.6+18,0,255); lit[...,2]=np.clip(crop[...,2]*1.3+10,0,255)
glow=np.clip(1-d/r,0,1)**1.6
out=np.zeros((2*r,2*r,4),np.float32)
out[...,:3]=lit*lens[...,None]+np.array([255,40,20])*(1-lens[...,None])
out[...,3]=np.maximum(lens,glow*0.55)*255
Image.fromarray(out.clip(0,255).astype(np.uint8),'RGBA').save('out/graphic_lamp.png')
