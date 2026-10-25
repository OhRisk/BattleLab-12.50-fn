"""Read-only local diagnosis of a specified process/RVA; no writes or network."""
import ctypes as c, sys, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pid, base, rva, size = (int(x, 0) for x in sys.argv[1:5])
k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype=c.c_void_p
k.OpenProcess.argtypes=[c.c_ulong,c.c_int,c.c_ulong]
k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes=[c.c_void_p]
h=k.OpenProcess(0x410,False,pid)
if not h: raise OSError(c.get_last_error())
def read(addr,n):
    buf=c.create_string_buffer(n);got=c.c_size_t()
    k.ReadProcessMemory(h,addr,buf,n,c.byref(got))
    return buf.raw[:got.value]
md=Cs(CS_ARCH_X86,CS_MODE_64)
for i in md.disasm(read(base+rva,size),rva):
    print(f'{i.address:x} {i.mnemonic} {i.op_str}')
    b=i.bytes
    if len(b)==7 and b[0] in (0x48,0x4c) and b[1]==0x8d and b[2]&0xc7==5:
        target=base+i.address+7+struct.unpack_from('<i',b,3)[0]
        text=read(target,240)
        if len(text)>2 and text[1]==0:
            s=text.decode('utf-16le','ignore').split('\x00')[0]
            if all(ch.isprintable() for ch in s) and len(s)>4:print(' STRING',repr(s))
k.CloseHandle(h)
