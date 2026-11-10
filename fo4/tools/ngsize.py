import sys; sys.path.insert(0,'.')
from chain import chunks
from decode import load
AL='/mnt/user-data/uploads/mods/Address Library - All In One/F4SE/Plugins/'
ae=load(AL+'version-1-11-240-0.bin'); ng=load('ng/F4SE/Plugins/version-1-10-984-0.bin'); og=load(AL+'version-1-10-163-0.bin')
AEP='/mnt/user-data/uploads/Fallout 4/Fallout4.exe'; NGP='/mnt/user-data/uploads/Fallout4.exe'; OGP='/mnt/user-data/uploads/G:--Fallout 4/Fallout4.exe'
fns={'SkinAllGeometry':(2209478,2209478,1131949),'GetFaceGeomPath':(2207453,2207453,1135980),'MainRun':(4484191,2718225,1125396),'OnIdle':(2228917,)*2+(633524,),
'HeapAllocate':(2267872,2267872,652767),'HeapFree':(2267874,2267874,1582181),'NiNodeCtor':(2269948,2269948,20633),'RSCtor':(2269830,2269830,1198116),'RSDtor':(2269832,2269832,1516202),'RSRead':(2269839,2269839,424286),
'Apply':(2194381,)*2+(224320,),'Attach':(2194388,)*2+(1575810,),'Lookup':(2190940,)*2+(542603,),'CalcLOS':(2230201,)*2+(1324305,),'GetCollFilter':(2229991,)*2+(1474995,),
'AddHeadPart':(2209545,)*2+(913780,),'SetBoneName':(2209387,)*2+(1278503,),'PrepareHeadPart':(2209534,)*2+(840416,),'BTFind':(2270080,)*2+(1435518,),'BTGetOrCreate':(2270071,)*2+(865291,),
'GetFlatBoneRef':(2270469,)*2+(73083,),'AttachToParent':(2194378,)*2+(1370428,),'RemovePart':(2194342,)*2+(575576,),'FTPSwitch':(2233725,)*2+(930195,),'ShowLooksMenu':(2223366,)*2+(411372,),
'Reset3D':(2229913,)*2+(302888,),'AltHPL':(2207501,)*2+(437572,),'AltHPLSize':(2207502,)*2+(517331,),'ConsolePrint':(2248591,)*2+(799546,),'FixHeadParts':(2200988,)*2+(789414,)}
def sz(p,db,i):
    c=chunks(p,db[i]); return sum(e-b for b,e in c), len(c)
for n,(a,g,o) in fns.items():
    A=sz(AEP,ae,a); N=sz(NGP,ng,g); O=sz(OGP,og,o)
    print(f"{n:16s} AE{A} NG{N} OG{O} {'same' if A==N else 'DIFF'}")
