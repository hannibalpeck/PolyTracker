import re,struct
def stock(path):
    s=open(path).read()
    out=[]
    for v,c in re.findall(r'\{\s*(\d+),\s*(\d+)\s*\}',s): out+=[int(v)]*int(c)
    return bytes(out)
def parse(b):
    w=list(b[:128]); stride=struct.unpack('<I',b[132:136])[0]; cw=struct.unpack('<I',b[136:140])[0]; h=struct.unpack('<I',b[140:144])[0]
    glyphs={}; off=148
    for ch in range(128):
        if w[ch]:
            g=[]
            for y in range(h):
                row=b[off+y*stride:off+(y+1)*stride]
                px=[]
                for byte in row: px+= [byte>>4, byte&15]
                g.append(px)
            glyphs[ch]=g; off+=stride*h
    return w,cw,h,glyphs
def render(font,text,path):
    from PIL import Image
    w,cw,h,g=font
    W=sum(w[ord(c)] for c in text)+2
    im=Image.new('L',(W,h+2),0); x=1
    for c in text:
        gl=g[ord(c)]
        for y in range(h):
            for xx in range(min(cw,w[ord(c)]+2)):
                v=gl[y][xx]
                if x+xx<W and v: im.putpixel((x+xx,y+1),max(im.getpixel((x+xx,y+1)),v*17))
        x+=w[ord(c)]
    im=im.resize((im.width*4,im.height*4),Image.NEAREST); im.save(path)
if __name__=='__main__':
    f=parse(stock('/home/claude/polyend/tracker-ce/core/libs/fonts/RobotoMono-Regular_14_L4.cpp'))
    render(f,'C-3 01 C30 Hello','stock14.png')
