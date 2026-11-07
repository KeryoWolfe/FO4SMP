import pefile,re,sys,math,collections
P='/mnt/user-data/uploads/Fallout 4/Fallout4.exe'
pe=pefile.PE(P,fast_load=True); data=open(P,'rb').read()
t=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
text=data[t.PointerToRawData:t.PointerToRawData+t.SizeOfRawData]
def rva(off): return t.VirtualAddress+off
def find(sig,limit=10):
    rx=b''.join(b'.' if x=='?' else re.escape(bytes([int(x,16)])) for x in sig.split())
    return [rva(m.start()) for m in re.finditer(rx,text,re.S)][:limit]
