import struct,sys
def load(p):
    b=open(p,'rb').read(); n=struct.unpack_from('<Q',b,0)[0]
    a=struct.unpack_from('<%dQ'%(2*n),b,8)
    return {a[i]:a[i+1] for i in range(0,2*n,2)}
