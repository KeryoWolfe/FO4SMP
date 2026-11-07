import struct
def q(b,r): return struct.unpack('<Q',b.read(r,8))[0]-b.base
def vtables(b,mangled):
    rd=[x for x in b.pe.sections if x.Name.startswith(b'.rdata')][0]
    dsec=[x for x in b.pe.sections if x.Name.startswith(b'.data')][0]
    o=b.data.find(mangled.encode()+b'\0'); td=b.pe.get_rva_from_offset(o)-16
    blob=b.data[rd.PointerToRawData:rd.PointerToRawData+rd.SizeOfRawData]
    out=[]
    o=blob.find(struct.pack('<I',td))
    while o!=-1:
        c=o-12
        if struct.unpack_from('<I',blob,c)[0]==1:
            off=struct.unpack_from('<I',blob,c+4)[0]
            col=rd.VirtualAddress+c
            o2=blob.find(struct.pack('<Q',col+b.base))
            while o2!=-1:
                out.append((off,rd.VirtualAddress+o2+8)); o2=blob.find(struct.pack('<Q',col+b.base),o2+1)
        o=blob.find(struct.pack('<I',td),o+1)
    return td,out
