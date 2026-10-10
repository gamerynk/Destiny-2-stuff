import ctypes as C
from ctypes import wintypes as W
from pathlib import Path


def read_process_image(pid, game_root):
    import pefile
    k=C.WinDLL('kernel32',use_last_error=True)
    class Module(C.Structure):
        _fields_=[('size',W.DWORD),('moduleID',W.DWORD),('pid',W.DWORD),('usage',W.DWORD),
            ('procUsage',W.DWORD),('base',C.c_void_p),('imageSize',W.DWORD),('module',W.HMODULE),
            ('name',W.WCHAR*256),('path',W.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];k.CreateToolhelp32Snapshot.restype=W.HANDLE
    k.Module32FirstW.argtypes=[W.HANDLE,C.POINTER(Module)];k.Module32NextW.argtypes=[W.HANDLE,C.POINTER(Module)]
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
    k.CloseHandle.argtypes=[W.HANDLE]
    k.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
    k.ReadProcessMemory.restype=W.BOOL
    snap=k.CreateToolhelp32Snapshot(0x18,pid)
    if snap==C.c_void_p(-1).value:raise OSError(C.get_last_error(),'Module snapshot failed')
    row=Module();row.size=C.sizeof(row)
    base=None
    try:
        ok=k.Module32FirstW(snap,C.byref(row))
        while ok:
            if row.name.lower()=='destiny2.exe':
                if Path(row.path).resolve()!=Path(game_root,'destiny2.exe').resolve():
                    raise ValueError('Game process does not match the selected installation')
                base=row.base
                break
            ok=k.Module32NextW(snap,C.byref(row))
    finally:k.CloseHandle(snap)
    if base is None:raise ValueError('Destiny executable not found in process')
    handle=k.OpenProcess(0x410,False,pid)
    if not handle:raise OSError(C.get_last_error(),'Read-only process access failed')
    pe=pefile.PE(str(Path(game_root,'destiny2.exe')),fast_load=True)
    image=bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
    try:
        for section in pe.sections:
            if not section.Characteristics & 0x40000000:continue
            size=section.Misc_VirtualSize
            buffer=C.create_string_buffer(size);read=C.c_size_t()
            if not k.ReadProcessMemory(handle,base+section.VirtualAddress,buffer,size,C.byref(read)):
                raise OSError(C.get_last_error(),'Mapped section is not yet readable')
            if read.value!=size:raise ValueError('Incomplete mapped section')
            image[section.VirtualAddress:section.VirtualAddress+size]=buffer.raw
        return image
    finally:k.CloseHandle(handle)
