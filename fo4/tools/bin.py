import pefile,struct,bisect,re
from capstone import *
class Bin:
    def __init__(s,path):
        s.pe=pefile.PE(path,fast_load=True)
        s.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
        s.data=open(path,'rb').read()
        s.funcs=sorted(set((e.struct.BeginAddress,e.struct.EndAddress) for e in s.pe.DIRECTORY_ENTRY_EXCEPTION))
        s.starts=[f[0] for f in s.funcs]
        s.md=Cs(CS_ARCH_X86,CS_MODE_64); s.md.detail=False
        s.base=s.pe.OPTIONAL_HEADER.ImageBase
    def read(s,rva,n):
        o=s.pe.get_offset_from_rva(rva); return s.data[o:o+n]
    def func_at(s,rva):
        i=bisect.bisect_right(s.starts,rva)-1; return s.funcs[i]
    def func_end(s,start):
        # follow chained pdata chunks is complex; use first chunk
        i=bisect.bisect_left(s.starts,start); return s.funcs[i][1]
    def insns(s,start,end=None):
        end=end or s.func_end(start)
        return list(s.md.disasm(s.read(start,end-start),s.base+start))
    def cstr(s,rva,maxn=200):
        try: b=s.read(rva,maxn)
        except Exception: return None
        z=b.find(b'\0')
        if z<3: return None
        t=b[:z]
        if all(32<=c<127 for c in t): return t.decode()
        return None
    def refs(s,start,end=None):
        """strings referenced and call targets"""
        strs=[];calls=[]
        for i in s.insns(start,end):
            m=re.search(r'\[rip ([+-]) 0x([0-9a-f]+)\]',i.op_str)
            if m:
                tgt=i.address+i.size+(int(m.group(2),16)*(1 if m.group(1)=='+' else -1))-s.base
                st=s.cstr(tgt)
                if st: strs.append(st)
            if i.mnemonic in('call','jmp') and i.op_str.startswith('0x'):
                calls.append(int(i.op_str,16)-s.base)
        return strs,calls

import numpy as np
def _text(b):
    t=[x for x in b.pe.sections if x.Name.startswith(b'.text')][0]
    return t, b.data[t.PointerToRawData:t.PointerToRawData+t.SizeOfRawData]
def xrefs(b,target):
    """code offsets (rva of disp32) whose rip-relative disp32 points at target rva"""
    t,txt=_text(b)
    if not hasattr(b,'_d32'):
        arr=np.frombuffer(txt,dtype=np.uint8)
        n=len(arr)-4
        d=(arr[:n].astype(np.int64)|(arr[1:n+1].astype(np.int64)<<8)|(arr[2:n+2].astype(np.int64)<<16)|(arr[3:n+3].astype(np.int64)<<24))
        d=np.where(d>=2**31,d-2**32,d)
        b._d32=d+np.arange(n)+t.VirtualAddress+4
    return [int(i)+t.VirtualAddress for i in np.nonzero(b._d32==target)[0]]
def find_str(b,s):
    o=b.data.find(s.encode()+b'\0')
    res=[]
    while o!=-1:
        try: res.append(b.pe.get_rva_from_offset(o))
        except Exception: pass
        o=b.data.find(s.encode()+b'\0',o+1)
    return res
def masked(b,start,n=64):
    """bytes of first n bytes with rel32/rip disp masked -> regex"""
    import re
    out=b''
    for i in b.md.disasm(b.read(start,n+16),b.base+start):
        if i.address-b.base-start>=n: break
        raw=bytes(i.bytes)
        mask=[False]*len(raw)
        if (i.mnemonic in('call','jmp') or i.mnemonic.startswith('j')) and i.op_str.startswith('0x') and len(raw)>=5:
            for k in range(len(raw)-4,len(raw)): mask[k]=True
        if 'rip' in i.op_str:
            # disp32 location: find it
            import struct
            for k in range(len(raw)-3):
                d=struct.unpack_from('<i',raw,k)[0]
                if i.address+i.size+d - b.base >0 and abs(d)>0x1000:
                    for q in range(k,k+4): mask[q]=True
                    break
        out+=b''.join(b'.' if m else re.escape(bytes([c])) for c,m in zip(raw,mask))
    return out
def search(b,rx):
    import re
    t,txt=_text(b)
    return [m.start()+t.VirtualAddress for m in re.finditer(rx,txt,re.S)]

def callers(b,target):
    t,txt=_text(b)
    arr=np.frombuffer(txt,dtype=np.uint8)
    idx=np.nonzero(arr[:-5]==0xE8)[0]
    d=(arr[idx+1].astype(np.int64)|(arr[idx+2].astype(np.int64)<<8)|(arr[idx+3].astype(np.int64)<<16)|(arr[idx+4].astype(np.int64)<<24))
    d=np.where(d>=2**31,d-2**32,d)
    tg=d+idx+5+t.VirtualAddress
    return [int(i)+t.VirtualAddress for i in idx[tg==target]]
