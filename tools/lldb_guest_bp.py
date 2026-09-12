"""Breakpoint sulle funzioni PPU liftate del port vivo, senza ricompilare.

Uso (il gioco gia' in esecuzione, pid noto):
  lldb --batch -o "command script import tools/lldb_guest_bp.py" \\
       -o "script lldb_guest_bp.run(lldb.debugger, <pid>, <secondi>, 'func_0049BF48,func_0051C468')"

Per ogni chiamata stampa r3-r5, f1 e la catena dei chiamanti (nomi host =
func_XXXX). I simboli sono C++ (mangled) ma lldb li trova per nome base;
`ctx` e' in x0, gpr[n] a x0+8n, fpr[n] a x0+256+8n; la memoria guest e'
`vm_base + ea`, big-endian. Al termine uccide il processo (Kill): il
detach in batch mode resta appeso.
"""
import lldb, time, struct

VMBASE = [0]

def rd(proc, addr, n=8):
    e = lldb.SBError()
    return proc.ReadUnsignedFromMemory(addr, n, e)

def guest32(proc, ea):
    """u32 big-endian all'EA guest."""
    v = rd(proc, VMBASE[0] + ea, 4)
    return struct.unpack('>I', struct.pack('<I', v))[0]

def cb(frame, bp_loc, d):
    x0 = frame.FindRegister("x0").GetValueAsUnsigned()
    proc = frame.GetThread().GetProcess()
    th = frame.GetThread()
    f1 = struct.unpack('<d', struct.pack('<Q', rd(proc, x0 + 32*8 + 8)))[0]
    chain = ' '.join((th.GetFrameAtIndex(i).GetFunctionName() or '?')[:13] for i in range(1, 7))
    print("[bp] %s r3=%08X r4=%08X r5=%08X f1=%.4f <- %s" % (
        (frame.GetFunctionName() or '?')[:13],
        rd(proc, x0 + 3*8) & 0xffffffff, rd(proc, x0 + 4*8) & 0xffffffff,
        rd(proc, x0 + 5*8) & 0xffffffff, f1, chain), flush=True)
    return False

def run(debugger, pid, secs, funcs):
    err = lldb.SBError()
    target = debugger.CreateTarget("")
    proc = target.AttachToProcessWithID(debugger.GetListener(), int(pid), err)
    if not err.Success():
        print("attach failed:", err); return
    sym = target.FindSymbols("vm_base")
    if sym.GetSize():
        a = sym.GetContextAtIndex(0).GetSymbol().GetStartAddress().GetLoadAddress(target)
        VMBASE[0] = rd(proc, a)
    for fn in funcs.split(','):
        bp = target.BreakpointCreateByName(fn.strip())
        bp.SetScriptCallbackFunction("lldb_guest_bp.cb")
        print("bp", fn, bp.GetNumLocations(), flush=True)
    proc.Continue()
    time.sleep(float(secs))
    proc.Kill()
