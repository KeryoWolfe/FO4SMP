import sys,struct; sys.path.insert(0,'.')
import pefile
from decode import load
from bin import Bin, callers
AL='/mnt/user-data/uploads/mods/Address Library - All In One/F4SE/Plugins/'
def chunks(path, root):
    pe=pefile.PE(path,fast_load=True)
    d=pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    raw=pe.get_data(d.VirtualAddress,d.Size)
    def rd(rva,n): return pe.get_data(rva,n)
    res=[]
    for k in range(0,len(raw),12):
        b,e,u=struct.unpack_from('<III',raw,k)
        rb=b; uu=u; depth=0
        while True:
            if uu&1: # indirect
                break
            hdr=rd(uu,4); flags=hdr[0]>>3; cnt=hdr[2]
            if flags&4:
                off=uu+4+((cnt+1)&~1)*2
                rb,_,uu=struct.unpack('<III',rd(off,12)); depth+=1
                if depth>32: break
            else: break
        if rb==root: res.append((b,e))
    return res
def sites(binobj,path,root,target):
    out=[]
    for b,e in chunks(path,root):
        code=binobj.read(b,e-b)
        for i in range(len(code)-4):
            if code[i]==0xE8:
                d=struct.unpack_from('<i',code,i+1)[0]
                if b+i+5+d==target: out.append(b+i)
    return out
if __name__=='__main__':
    og=load(AL+'version-1-10-163-0.bin'); ae=load(AL+'version-1-11-240-0.bin')
    for nm,p,db,ids in (('OG','/mnt/user-data/uploads/G:--Fallout 4/Fallout4.exe',og,(224320,1575810,1125396,633524)),('AE','/mnt/user-data/uploads/Fallout 4/Fallout4.exe',ae,(2194381,2194388,4484191,2228917))):
        B=Bin(p)
        print(nm,'apply chunks',[(hex(a),hex(b)) for a,b in chunks(p,db[ids[0]])])
        print(nm,'attach sites',[hex(x) for x in sites(B,p,db[ids[0]],db[ids[1]])],'all callers',[hex(x) for x in callers(B,db[ids[1]])])
        print(nm,'onidle sites',[hex(x-db[ids[2]]) for x in sites(B,p,db[ids[2]],db[ids[3]])])
    ng=load('ng/F4SE/Plugins/version-1-10-984-0.bin')
    print('NG apply chunks',[(hex(a),hex(b)) for a,b in chunks('/mnt/user-data/uploads/Fallout4.exe',ng[2194381])], 'run chunks',[(hex(a),hex(b)) for a,b in chunks('/mnt/user-data/uploads/Fallout4.exe',ng[2718225])])
