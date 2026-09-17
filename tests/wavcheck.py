import struct, sys, numpy as np
b = open(sys.argv[1],'rb').read()
assert b[:4]==b'RIFF' and b[8:12]==b'WAVE'
pos=12; fmt=None; data=None
while pos+8<=len(b):
    cid,size=b[pos:pos+4],struct.unpack('<I',b[pos+4:pos+8])[0]
    if cid==b'fmt ': fmt=b[pos+8:pos+8+size]
    if cid==b'data': data=b[pos+8:pos+8+size] if size else b[pos+8:]; break
    pos+=8+size+(size&1)
tag,ch,sr,_,align,bits=struct.unpack('<HHIIHH',fmt[:16])
n=len(data)//align
print('tag=0x%04x channels=%d bits=%d rate=%d frames=%d seconds=%.2f'%(tag,ch,bits,sr,n,n/sr))
if bits==16: d=np.frombuffer(data[:n*align],dtype='<i2').reshape(-1,ch).astype(float)
elif bits==32: d=np.frombuffer(data[:n*align],dtype='<i4').reshape(-1,ch).astype(float)/65536
else:
    raw=np.frombuffer(data[:n*align],dtype=np.uint8).reshape(-1,ch,3).astype(np.int32)
    v=raw[:,:,0]|(raw[:,:,1]<<8)|(raw[:,:,2]<<16); v=np.where(v>=1<<23,v-(1<<24),v); d=v.astype(float)/256
act=np.where(np.abs(d).max(axis=1)>200)[0]
if len(act)==0: print('silent'); sys.exit(1)
print('signal from %.3fs to %.3fs (%.3fs)'%(act[0]/sr,act[-1]/sr,(act[-1]-act[0])/sr))
seg=d[act[0]+sr//10:act[0]+sr//10+sr]
f=np.fft.rfftfreq(len(seg),1/sr)
for c in range(ch):
    sp=np.abs(np.fft.rfft(seg[:,c]*np.hanning(len(seg))))
    print('ch%d: dominant %.1f Hz, peak %.0f'%(c,f[sp.argmax()],np.abs(seg[:,c]).max()))
