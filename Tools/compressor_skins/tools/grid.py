# grid.py photo x0 y0 x1 y1 scale out: a crop with a labelled grid (every 25 px, labels every 50)
import sys
from PIL import Image, ImageDraw
p,x0,y0,x1,y1,sc,out=sys.argv[1],*map(int,sys.argv[2:6]),float(sys.argv[6]),sys.argv[7]
im=Image.open(p).convert('RGB').crop((x0,y0,x1,y1))
im=im.resize((int(im.width*sc),int(im.height*sc)),Image.LANCZOS)
d=ImageDraw.Draw(im)
for x in range((x0//25+1)*25,x1,25):
    X=(x-x0)*sc; d.line((X,0,X,im.height),fill=(255,0,255) if x%50==0 else (0,255,255),width=1)
    if x%50==0: d.text((X+2,2),str(x),fill=(255,255,0))
for y in range((y0//25+1)*25,y1,25):
    Y=(y-y0)*sc; d.line((0,Y,im.width,Y),fill=(255,0,255) if y%50==0 else (0,255,255),width=1)
    if y%50==0: d.text((2,Y+2),str(y),fill=(255,255,0))
im.save(out)
