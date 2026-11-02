import sys
from PIL import Image, ImageDraw, ImageFilter
N=1024; out=sys.argv[1]
def grad(size,top,bot):
    g=Image.new('RGB',(1,size[1]))
    for y in range(size[1]):
        t=y/(size[1]-1); g.putpixel((0,y),tuple(int(a+(b-a)*t) for a,b in zip(top,bot)))
    return g.resize(size)
img=grad((N,N),(52,152,255),(150,215,255)).convert('RGBA')   # sky
d=ImageDraw.Draw(img)
# back hill (darker) and fairway (lighter), big soft curves
back=Image.new('L',(N,N),0); ImageDraw.Draw(back).ellipse((-500,470,1500,1900),fill=255)
img.paste(grad((N,N),(46,140,74),(30,105,58)),(0,0),back)
fair=Image.new('L',(N,N),0); ImageDraw.Draw(fair).ellipse((-900,600,1300,2100),fill=255)
img.paste(grad((N,N),(110,200,90),(60,160,70)),(0,0),fair)
# putting green on the back hill
d.ellipse((520,520,900,640),fill=(160,230,140)); d.ellipse((540,532,880,628),outline=(120,205,110),width=6)
# hole + pin + flag
d.ellipse((690,570,740,592),fill=(25,60,35))
d.rounded_rectangle((706,200,722,582),radius=8,fill=(250,250,250))
d.polygon([(722,206),(900,262),(722,330)],fill=(255,82,54))
d.polygon([(722,268),(900,262),(722,330)],fill=(225,60,40))
# golf ball with soft shadow
sh=Image.new('L',(N,N),0); ImageDraw.Draw(sh).ellipse((200,830,500,900),fill=130)
img.paste((0,50,20),(0,0),sh.filter(ImageFilter.GaussianBlur(14)))
ball=Image.new('RGBA',(N,N),(0,0,0,0)); bd=ImageDraw.Draw(ball)
cx,cy,r=345,700,175
bd.ellipse((cx-r,cy-r,cx+r,cy+r),fill=(255,255,255))
for i in range(40):  # shading toward bottom-right
    t=i/40; rr=r*(1-t*0.55)
    bd.ellipse((cx-rr+r*0.18*t*2,cy-rr+r*0.18*t*2,cx+rr+r*0.18*t*2,cy+rr+r*0.18*t*2),fill=None)
shade=Image.new('L',(N,N),0); sd=ImageDraw.Draw(shade)
sd.ellipse((cx-r,cy-r,cx+r,cy+r),fill=70); sd.ellipse((cx-r-60,cy-r-60,cx+r-60,cy+r-60),fill=0)
shade=shade.filter(ImageFilter.GaussianBlur(30))
mask=Image.new('L',(N,N),0); ImageDraw.Draw(mask).ellipse((cx-r,cy-r,cx+r,cy+r),fill=255)
from PIL import ImageChops
shade=ImageChops.multiply(shade,mask)
ball.paste((120,140,160,255),(0,0),shade)
for gx in range(-4,5):
    for gy in range(-4,5):
        x=cx+gx*42+(gy%2)*21; y=cy+gy*38
        if (x-cx)**2+(y-cy)**2<(r-30)**2:
            bd.ellipse((x-9,y-9,x+9,y+9),fill=(222,228,236))
img.alpha_composite(ball)
# rounded-square mask
m=Image.new('L',(N,N),0); ImageDraw.Draw(m).rounded_rectangle((0,0,N-1,N-1),radius=225,fill=255)
img.putalpha(m)
img.save(out+'/icon-1024.png')
for s,name in((144,'icon-large.png'),(80,'icon-small.png')): img.resize((s,s),Image.LANCZOS).save(out+'/'+name)
