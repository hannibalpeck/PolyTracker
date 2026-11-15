import struct
from l4 import parse, render
T="""00 00 00 00 00|00 00 5F 00 00|00 07 00 07 00|14 7F 14 7F 14|24 2A 7F 2A 12|23 13 08 64 62|36 49 56 20 50|00 08 07 03 00|00 1C 22 41 00|00 41 22 1C 00|2A 1C 7F 1C 2A|08 08 3E 08 08|00 80 70 30 00|08 08 08 08 08|00 00 60 60 00|20 10 08 04 02|3E 51 49 45 3E|00 42 7F 40 00|72 49 49 49 46|21 41 49 4D 33|18 14 12 7F 10|27 45 45 45 39|3C 4A 49 49 31|41 21 11 09 07|36 49 49 49 36|46 49 49 29 1E|00 00 14 00 00|00 40 34 00 00|00 08 14 22 41|14 14 14 14 14|00 41 22 14 08|02 01 59 09 06|3E 41 5D 59 4E|7C 12 11 12 7C|7F 49 49 49 36|3E 41 41 41 22|7F 41 41 41 3E|7F 49 49 49 41|7F 09 09 09 01|3E 41 41 51 73|7F 08 08 08 7F|00 41 7F 41 00|20 40 41 3F 01|7F 08 14 22 41|7F 40 40 40 40|7F 02 1C 02 7F|7F 04 08 10 7F|3E 41 41 41 3E|7F 09 09 09 06|3E 41 51 21 5E|7F 09 19 29 46|26 49 49 49 32|03 01 7F 01 03|3F 40 40 40 3F|1F 20 40 20 1F|3F 40 38 40 3F|63 14 08 14 63|03 04 78 04 03|61 59 49 4D 43|00 7F 41 41 41|02 04 08 10 20|00 41 41 41 7F|04 02 01 02 04|40 40 40 40 40|00 03 07 08 00|20 54 54 78 40|7F 28 44 44 38|38 44 44 44 28|38 44 44 28 7F|38 54 54 54 18|00 08 7E 09 02|18 A4 A4 9C 78|7F 08 04 04 78|00 44 7D 40 00|20 40 40 3D 00|7F 10 28 44 00|00 41 7F 40 00|7C 04 78 04 78|7C 08 04 04 78|38 44 44 44 38|FC 18 24 24 18|18 24 24 18 FC|7C 08 04 04 08|48 54 54 54 24|04 04 3F 44 24|3C 40 40 20 7C|1C 20 40 20 1C|3C 40 30 40 3C|44 28 10 28 44|4C 90 90 90 7C|44 64 54 4C 44|00 08 36 41 00|00 00 77 00 00|00 41 36 08 00|02 01 02 04 02"""
cols={32+i:[int(x,16) for x in g.split()] for i,g in enumerate(T.split('|'))}
assert len(cols)==95 and all(len(v)==5 for v in cols.values())
# slot: advance, glyph px width, cell w, cell h, scaleY, offY, bold
SLOTS=[(9,8,10,19,2,1,False),(10,8,10,22,2,4,False),(9,7,10,19,2,1,True),(11,9,12,22,2,4,True),(7,5,8,16,1,5,False)]
def glyph(ch,GW,cw,ch_h,sy,oy,bold):
    c=cols[ch]; rows=[[0]*cw for _ in range(ch_h)]
    gw=GW-1 if bold else GW
    for y in range(ch_h):
        sr=(y-oy)//sy if y>=oy else -1
        if not 0<=sr<8: continue
        for x in range(gw):
            sc=min(4,int((x+0.5)*5/gw))
            if (c[sc]>>sr)&1:
                rows[y][x]=15
                if bold and x+1<cw: rows[y][x+1]=15
    return rows
def build_font(slot,addr):
    A,GW,cw,h,sy,oy,bold=slot; stride=cw//2
    hdr=bytearray(148)
    for ch in cols: hdr[ch]=A
    struct.pack_into('<IIIII',hdr,128,2,stride,cw,h,addr+148)
    body=bytearray()
    for ch in sorted(cols):
        g=glyph(ch,GW,cw,h,sy,oy,bold)
        for r in g:
            for i in range(0,cw,2): body.append((r[i]<<4)|r[i+1])
    return bytes(hdr)+bytes(body)
fonts=[];addr=0
for s in SLOTS:
    addr=(addr+3)//4*4
    f=build_font(s,addr); fonts.append((addr,f)); addr+=len(f)
hdr=b'PTFN'+struct.pack('<HH',1,5)+bytes(8)
off=len(hdr)+12*5; ent=b''; data=b''
for i,(a,f) in enumerate(fonts):
    ent+=struct.pack('<IIHH',off+len(data),len(f),i+1,2); data+=f
open('PixelAmiga.ptfont','wb').write(hdr+ent+data)
print(len(hdr+ent+data),[len(f) for a,f in fonts],[a for a,f in fonts])
# preview
from PIL import Image
imgs=[]
for i,(a,f) in enumerate(fonts):
    render(parse(f),'C-3 01 C30  Hello World! 0123 jgpqy','pv%d.png'%i); imgs.append(Image.open('pv%d.png'%i))
W=max(i.width for i in imgs); H=sum(i.height for i in imgs)
sheet=Image.new('L',(W,H)); y=0
for i in imgs: sheet.paste(i,(0,y)); y+=i.height
sheet.save('preview.png')
