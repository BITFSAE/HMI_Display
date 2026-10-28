#!/usr/bin/env python3
"""Run actual firmware application/LVGL code with mocked hardware under ASan/UBSan.

--png DIR        also render the dashboard (5 drive modes, alert, READY, no data) and the splash animation
                 (6 boot frames + 1 replay frame) to PNG
--compare-ref R  require the rendered frames to match git ref R pixel for pixel
                 (R must already contain dashboard_view.c)
"""
import argparse
from contextlib import nullcontext
from pathlib import Path
import io
import re
import os
import struct
import subprocess
import tarfile
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
FRAMES = 8
SPLASH_FRAMES = 7


def run(*args):
    subprocess.run(list(map(str, args)), check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})


def write_changed(path, data):
    if not path.exists() or path.read_text() != data:
        path.write_text(data)


def rgb565_to_png(source, dest, width=800, height=480):
    data = source.read_bytes()
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for (pixel,) in struct.iter_unpack('<H', data[y * width * 2:(y + 1) * width * 2]):
            r, g, b = pixel >> 11, (pixel >> 5) & 0x3F, pixel & 0x1F
            rows += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    dest.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--compare-ref', help='Git ref whose UI pixels should remain unchanged')
    parser.add_argument('--png', type=Path, help='Write rendered dashboard frames as PNG into this directory')
    parser.add_argument('--build-dir', type=Path, help='Reuse a local build directory')
    args = parser.parse_args()
    if args.build_dir:
        args.build_dir.mkdir(parents=True, exist_ok=True)
    context = nullcontext(str(args.build_dir.resolve())) if args.build_dir else tempfile.TemporaryDirectory(prefix='hmi-regression-')
    with context as name:
        work = Path(name)
        source = (ROOT / 'stm32_port/Core/Src/can.c').read_text()
        # This compiles the exact user-owned CAN implementation, excluding the
        # generated peripheral init code that is covered by the ARM build.
        regions = re.findall(r'/\* USER CODE BEGIN (\w+) \*/(.*?)/\* USER CODE END \1 \*/', source, re.S)
        write_changed(work / 'can_app.inc', '#include "can.h"\n' + '\n'.join(body for tag, body in regions if tag in ['Includes', '0', '1']))
        project = ROOT / 'stm32_port/.cproject'
        exclusions = re.search(r'excluding="([^"]*)"', project.read_text())[1].split('|')
        lvgl_sources = sorted(p for p in (ROOT / 'stm32_port/Core/lvgl/src').rglob('*.c')
                              if not any(p.relative_to(ROOT / 'stm32_port/Core').as_posix() == excluded or
                                         p.relative_to(ROOT / 'stm32_port/Core').as_posix().startswith(excluded + '/')
                                         for excluded in exclusions))
        builds = [('current', ROOT / 'stm32_port/Core')]
        if args.compare_ref:
            # The ref's UI sources and headers; drivers/LVGL come from the work tree.
            tree = work / 'baseline_tree'
            archive = subprocess.check_output(['git', 'archive', args.compare_ref, 'stm32_port/Core/Src',
                                               'stm32_port/Core/Inc', 'stm32_port/Core/lv_conf.h'], cwd=ROOT)
            with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
                tar.extractall(tree, filter='data')
            core = tree / 'stm32_port/Core'
            (core / 'lvgl').unlink(missing_ok=True)
            (core / 'lvgl').symlink_to(ROOT / 'stm32_port/Core/lvgl')
            builds.append(('baseline', core))
        for label, core in builds:
            conf = work / (label + '_conf.h')
            # Features match the firmware. Host pointers are 64 bits; double the
            # pool for headless rendering. Target margin is measured on board.
            config = (core / 'lv_conf.h').read_text()
            config = config.replace('#endif /*LV_CONF_H*/', '\n#undef LV_MEM_SIZE\n#define LV_MEM_SIZE (120 * 1024U)\n#undef LV_MEM_ADR\n#define LV_MEM_ADR 0\n#undef LV_ASSERT_HANDLER_INCLUDE\n#define LV_ASSERT_HANDLER_INCLUDE "firmware_diag.h"\n#undef LV_ASSERT_HANDLER\n#define LV_ASSERT_HANDLER Firmware_Diag_LvglAssert();\n#endif /*LV_CONF_H*/')
            write_changed(conf, config)
            directory = work / label
            directory.mkdir(exist_ok=True)
            cmake = work / ('cmake-' + label)
            cmake.mkdir(exist_ok=True)
            # 开屏动画模块在对比基线里不存在：基线只编译仪表本身。
            splash = [core / 'Src/splash_view.c', core / 'Src/dash_splash_data.c'] if (core / 'Src/splash_view.c').exists() else []
            sources = [ROOT / 'tools/tests/firmware_regression.c', core / 'Src/dashboard_view.c', core / 'Src/dash_logo.c',
                       *splash, *sorted((core / 'Src/fonts').glob('*.c')), *lvgl_sources]
            content = 'cmake_minimum_required(VERSION 3.16)\nproject(hmi_regression C)\n'
            content += 'add_executable(regression\n' + '\n'.join('"' + str(p) + '"' for p in sources) + '\n)\n'
            content += 'target_include_directories(regression PRIVATE "' + str(ROOT / 'tools/tests/stubs') + '" "' + str(work) + '" "' + str(core / 'Inc') + '" "' + str(ROOT / 'stm32_port/Core/Inc') + '")\n'
            content += 'target_compile_definitions(regression PRIVATE LV_CONF_PATH="' + str(conf) + '" DASHBOARD_UI_SOURCE="' + str(core / 'Src/dashboard_ui.c') + '"' + (' HAS_SPLASH=1' if splash else '') + ')\n'
            content += 'target_compile_options(regression PRIVATE -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)\n'
            content += 'target_link_options(regression PRIVATE -fsanitize=address,undefined)\ntarget_link_libraries(regression PRIVATE m)\n'
            write_changed(cmake / 'CMakeLists.txt', content)
            run('cmake', '-S', cmake, '-B', directory / 'build', '-G', 'Ninja', '-DCMAKE_C_COMPILER=/usr/bin/cc')
            run('cmake', '--build', directory / 'build', '-j', '8')
            binary = directory / 'build/regression'
            if label == 'current':
                run(binary)
            if args.compare_ref or (args.png and label == 'current'):
                run(binary, directory)
        if args.png:
            args.png.mkdir(parents=True, exist_ok=True)
            for frame in range(FRAMES):
                rgb565_to_png(work / f'current/mode-{frame}.rgb565', args.png / f'dashboard-{frame}.png')
            for frame in range(SPLASH_FRAMES):
                rgb565_to_png(work / f'current/splash-{frame}.rgb565', args.png / f'splash-{frame}.png')
            print(f'Rendered {FRAMES} dashboard + {SPLASH_FRAMES} splash frames to {args.png}')
        if args.compare_ref:
            for frame in range(FRAMES):
                assert (work / f'current/mode-{frame}.rgb565').read_bytes() == (work / f'baseline/mode-{frame}.rgb565').read_bytes(), f'Frame {frame}: rendered pixels changed'
            print(f'All {FRAMES} 800x480 frames match the baseline pixel for pixel')


if __name__ == '__main__':
    main()
