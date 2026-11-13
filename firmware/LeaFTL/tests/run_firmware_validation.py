"""Windows/MSVC host NAND + DMA integration. Does not access a real SSD."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
import time

args = argparse.ArgumentParser()
args.add_argument('--gammas', nargs='+', type=int, default=[0, 1, 4, 16])
opts = args.parse_args()
if any(g not in range(17) for g in opts.gammas):
    args.error('gamma must be 0..16')
project = Path(__file__).resolve().parents[1]
src = project / 'firmware/leaftl/src'
out = Path(tempfile.gettempdir()) / 'LeaFTL-host-validation'
out.mkdir(exist_ok=True)
programs = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))
installed = sorted((programs / 'Microsoft Visual Studio').glob('*/BuildTools/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
cl = shutil.which('cl') or (str(installed[-1]) if installed else None)
if not cl:
    raise SystemExit('Install MSVC Build Tools or run from x64 Native Tools Command Prompt')
msvc = Path(cl).resolve().parents[3]
sdk = programs / 'Windows Kits/10'
versions = sorted(p.name for p in (sdk / 'Include').glob('*') if (p / 'ucrt').is_dir())
if not versions:
    raise SystemExit('Windows SDK UCRT not found')
version = versions[-1]
(out / 'xil_printf.h').write_text('#include <stdio.h>\n#define xil_printf printf\n')
includes = [msvc / 'include', sdk / f'Include/{version}/ucrt', src, project / 'tests/bsp_stub', out]
libs = [msvc / 'lib/x64', sdk / f'Lib/{version}/ucrt/x64', sdk / f'Lib/{version}/um/x64']
names = ['leaftl_crb.c', 'leaftl_segment.c', 'leaftl_index.c', 'leaftl_mapping.c',
         'leaftl_memory.c', 'leaftl_writeback.c', 'leaftl_gc.c', 'leaftl_flash.c',
         'leaftl_cache.c', 'leaftl_metadata.c', 'leaftl_oob.c', 'leaftl_cosmos.c',
         'address_translation.c', 'garbage_collection.c', 'data_buffer.c',
         'request_transform.c', 'request_allocation.c', 'dftl_cmt.c', 'dftl_gtd.c',
         'nvme/nvme_io_cmd.c']
flags = ['/nologo', '/TC', '/std:c11', '/W4', '/WX', '/O2', '/fp:strict', '/Gy',
         '/wd4312', '/wd4244', '/wd4267', '/wd4018', '/wd4100', '/wd4013', '/wd4701']
for gamma in opts.gammas:
    exe = out / f'firmware_gamma{gamma}.exe'
    command = [cl, *flags, f'/DLEAFTL_GAMMA={gamma}',
               *('/I'+str(p) for p in includes), *(str(src / n) for n in names),
               str(project / 'tests/test_leaftl_firmware_path.c'),
               '/Fo:'+str(out)+'\\', '/Fe:'+str(exe), '/link',
               *('/LIBPATH:'+str(p) for p in libs), '/OPT:REF', 'kernel32.lib']
    subprocess.run(command, check=True)
    start = time.perf_counter()
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    (out / f'gamma{gamma}.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    result.check_returncode()
    print(f'Host validation elapsed: {time.perf_counter()-start:.3f}s (not SSD performance)')
print(f'PASS; artifacts: {out}')
