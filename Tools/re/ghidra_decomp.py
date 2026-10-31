"""Decompiles functions of speed.exe with Ghidra (the project made by analyzeHeadless, D:/Tools/ghidra_projects, program "speed.exe").

usage: python -I Tools/re/ghidra_decomp.py 0x4EDD90 [0x4EE3E0 ...]     a function by an address inside it
       python -I Tools/re/ghidra_decomp.py --name SNDvol                 by name (the decomp's names are imported with --import-names)
       python -I Tools/re/ghidra_decomp.py --range 0x4A0000 0x500000 out.c   every function in a range into a file
       python -I Tools/re/ghidra_decomp.py --import-names                gives the functions the names of Tools/re/names.json and of the decomp's symbols.json, and saves the project

Needs GHIDRA_INSTALL_DIR (D:/Tools/ghidra_12.1.3_PUBLIC) and JAVA_HOME (D:/Tools/jdk-21.0.12.1+1) in the environment: the defaults below are used when they are not.
"""
import json
import os
import re
import sys
from pathlib import Path

os.environ.setdefault('GHIDRA_INSTALL_DIR', r'D:\Tools\ghidra_12.1.3_PUBLIC')
os.environ.setdefault('JAVA_HOME', r'D:\Tools\jdk-21.0.12.1+1')

import pyghidra  # noqa: E402

PROJECT = r'D:\Tools\ghidra_projects'
REPO = Path(__file__).resolve().parent.parent.parent


def import_names(program):
    """Makes a function of every function of the decomp's symbols.json that Ghidra did not find (virtual methods are only called through tables), names them,
    names the data (globals) and gives our own names (Tools/re/names.json) the last word."""
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.app.cmd.label import DemanglerCmd
    from ghidra.program.model.symbol import SourceType
    from ghidra.util.task import ConsoleTaskMonitor
    monitor = ConsoleTaskMonitor()
    space = program.getAddressFactory().getDefaultAddressSpace()
    symbols = json.load(open(REPO / 'Tools/nfsmw-decomp/config/SPEED_EXE_1_3/symbols.json'))
    ours = json.load(open(REPO / 'Tools/re/names.json'))
    functions = {int(f['start'], 16): f['name'] for f in symbols['functions']}
    for a, v in ours.items():
        if a.startswith('0x'):
            functions[int(a, 16)] = v['name']
    fm = program.getFunctionManager()
    st = program.getSymbolTable()
    created = renamed = labelled = 0

    def clean(name):
        return re.sub(r'[^A-Za-z0-9_?@$]', '_', name)[:250]

    for n, (addr, name) in enumerate(sorted(functions.items())):
        a = space.getAddress(addr)
        f = fm.getFunctionAt(a)
        if f is None:
            DisassembleCommand(a, None, True).applyTo(program, monitor)
            if CreateFunctionCmd(clean(name), a, None, SourceType.IMPORTED).applyTo(program, monitor):
                created += 1
            f = fm.getFunctionAt(a)
        elif f.getName() != clean(name):
            try:
                f.setName(clean(name), SourceType.IMPORTED)
                renamed += 1
            except Exception:  # the name is already a symbol at that address
                pass
        if f is not None and name.startswith('?') and name == clean(name):
            try:
                DemanglerCmd(a, name).applyTo(program, monitor)
            except Exception:
                pass
        if n % 5000 == 0:
            print(n, 'of', len(functions), 'created', created, 'renamed', renamed, flush=True)
    for s in symbols['names']:
        addr = int(s['addr'], 16)
        if addr in functions:
            continue
        a = space.getAddress(addr)
        try:
            st.createLabel(a, clean(s['name']), SourceType.IMPORTED)
            labelled += 1
        except Exception:
            pass
    return created, renamed, labelled


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    pyghidra.start()
    with pyghidra.open_program(r'D:\Git\mets_nfsmw_mods\Game Versions\Redux v3 + Our Mods\speed.exe', project_location=PROJECT, project_name='nfsmw', analyze=False) as flat:
        program = flat.getCurrentProgram()
        from ghidra.util.task import ConsoleTaskMonitor
        if args[0] == '--import-names':
            with pyghidra.transaction(program, 'names'):
                print('created, renamed, labelled:', import_names(program), flush=True)
            program.getDomainFile().save(ConsoleTaskMonitor())
            print('saved')
            return
        from ghidra.app.decompiler import DecompInterface
        from ghidra.util.task import ConsoleTaskMonitor
        ifc = DecompInterface()
        ifc.openProgram(program)
        fm = program.getFunctionManager()
        targets = []
        if args[0] == '--range':  # --range START END OUT: every function in [START, END) into the file OUT (for searching without starting Ghidra each time)
            start, end = int(args[1], 16), int(args[2], 16)
            count = 0
            with open(args[3], 'w', encoding='utf-8') as out:
                for f in fm.getFunctions(True):
                    entry = f.getEntryPoint().getOffset()
                    if not start <= entry < end:
                        continue
                    res = ifc.decompileFunction(f, 60, ConsoleTaskMonitor())
                    out.write(f'// ---- {f.getName()} @ {f.getEntryPoint()}\n')
                    out.write((res.getDecompiledFunction().getC() if res.decompileCompleted() else f'// decompile failed: {res.getErrorMessage()}\n') + '\n')
                    count += 1
                    if count % 500 == 0:
                        print(count, 'functions', flush=True)
            print('done', count, 'functions')
            return
        if args[0] == '--name':
            for f in fm.getFunctions(True):
                if args[1] in f.getName():
                    targets.append(f)
        else:
            for a in args:
                addr = program.getAddressFactory().getDefaultAddressSpace().getAddress(int(a, 16))
                f = fm.getFunctionContaining(addr)
                if f is not None:
                    targets.append(f)
                else:
                    print(f'{a}: no function')
        for f in targets[:20]:
            res = ifc.decompileFunction(f, 60, ConsoleTaskMonitor())
            print(f'// ---- {f.getName()} @ {f.getEntryPoint()}')
            print(res.getDecompiledFunction().getC() if res.decompileCompleted() else f'// decompile failed: {res.getErrorMessage()}')


if __name__ == '__main__':
    main()
