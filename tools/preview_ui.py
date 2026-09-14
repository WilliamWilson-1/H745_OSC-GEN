"""Compile and exercise the actual C renderer, then export 800x480 previews.

Requires a host GCC/Clang compiler and Pillow. No STM32 device is accessed.
Run: python tools/preview_ui.py [--cc C:/path/to/gcc.exe]
"""
import argparse
import os
import shutil
import subprocess
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='gcc')
parser.add_argument('--sanitize', action='store_true', help='Enable host AddressSanitizer')
args = parser.parse_args()
compiler = Path(shutil.which(args.cc) or args.cc).resolve()
environment = os.environ.copy()
environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
target = subprocess.check_output([str(compiler), '-dumpmachine'], env=environment, text=True)
math_lib = [] if 'msvc' in target else ['-lm']
out = root / 'build/ui_preview'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'ui_preview.exe'
subprocess.run([
    str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    *(['-fsanitize=address', '-g'] if args.sanitize else []),
    '-DUI_HOST_PREVIEW', '-D__MAIN_H', '-D_CRT_SECURE_NO_WARNINGS',
    '-include', str(root / 'tools/ui_preview/host_hal.h'),
    '-I' + str(root / 'CM7/Core/Inc'),
    str(root / 'CM7/Core/Src/graph.c'), str(root / 'tools/ui_preview/preview.c'),
    *math_lib, '-o', str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], cwd=out, check=True, env=environment)
display_test = out / 'display_test.exe'
subprocess.run([
    str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    *(['-fsanitize=address', '-g'] if args.sanitize else []),
    '-D__MAIN_H', '-D_CRT_SECURE_NO_WARNINGS',
    '-include', str(root / 'tools/ui_preview/display_host_hal.h'),
    '-I' + str(root / 'CM7/Core/Inc'),
    str(root / 'CM7/Core/Src/display.c'), str(root / 'tools/ui_preview/display_test.c'),
    '-o', str(display_test),
], check=True, env=environment)
subprocess.run([str(display_test)], cwd=out, check=True, env=environment)
instrument_test = out / 'instrument_test.exe'
subprocess.run([
    str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    *(['-fsanitize=address', '-g'] if args.sanitize else []),
    '-D__MAIN_H', '-D_CRT_SECURE_NO_WARNINGS',
    '-include', str(root / 'tools/ui_preview/instrument_host_hal.h'),
    '-I' + str(root / 'CM7/Core/Inc'),
    str(root / 'tools/ui_preview/instrument_test.c'),
    *math_lib, '-o', str(instrument_test),
], check=True, env=environment)
subprocess.run([str(instrument_test)], cwd=out, check=True, env=environment)
for path in sorted(out.glob('*.ppm')):
    with Image.open(path) as img:
        assert img.size == (800, 480)
        img.save(path.with_suffix('.png'))
for prefix in ('menu_motion', 'output_motion'):
    frames = []
    for path in sorted(out.glob(prefix + '_*.png')):
        with Image.open(path) as img:
            frames.append(img.copy())
    frames[0].save(out / (prefix + '.gif'), save_all=True, append_images=frames[1:],
                   duration=[33] * (len(frames)-1) + [1000], loop=0)
print(f'Previews: {out}')
