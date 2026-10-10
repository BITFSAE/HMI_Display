#!/usr/bin/env python3
"""Run actual firmware application/LVGL code with mocked hardware under ASan/UBSan."""
import argparse
from contextlib import nullcontext
from pathlib import Path
import re
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(list(map(str, args)), check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})


def write_changed(path, data):
    if not path.exists() or path.read_text() != data:
        path.write_text(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compare-ref', help='Git ref whose UI pixels should remain unchanged')
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
        builds = [('current', (ROOT / 'stm32_port/Core/lv_conf.h').read_text(), ROOT / 'stm32_port/Core/Src/dashboard_ui.c')]
        if args.compare_ref:
            config = subprocess.check_output(['git', 'show', args.compare_ref + ':stm32_port/Core/lv_conf.h'], cwd=ROOT, text=True)
            ui = subprocess.check_output(['git', 'show', args.compare_ref + ':stm32_port/Core/Src/dashboard_ui.c'], cwd=ROOT, text=True)
            ui = ui.replace('"../lvgl/lvgl.h"', '"' + str(ROOT / 'stm32_port/Core/lvgl/lvgl.h') + '"')
            write_changed(work / 'baseline_ui.c', ui)
            builds.append(('baseline', config, work / 'baseline_ui.c'))
        for label, config, ui in builds:
            conf = work / (label + '_conf.h')
            # Features match the firmware. Host pointers are 64 bits; double the
            # pool for headless rendering. Target margin is measured on board.
            config = config.replace('#endif /*LV_CONF_H*/', '\n#undef LV_MEM_SIZE\n#define LV_MEM_SIZE (120 * 1024U)\n#undef LV_MEM_ADR\n#define LV_MEM_ADR 0\n#undef LV_ASSERT_HANDLER_INCLUDE\n#define LV_ASSERT_HANDLER_INCLUDE "firmware_diag.h"\n#undef LV_ASSERT_HANDLER\n#define LV_ASSERT_HANDLER Firmware_Diag_LvglAssert();\n#endif /*LV_CONF_H*/')
            write_changed(conf, config)
            directory = work / label
            directory.mkdir(exist_ok=True)
            cmake = work / ('cmake-' + label)
            cmake.mkdir(exist_ok=True)
            sources = [ROOT / 'tools/tests/firmware_regression.c', ROOT / 'stm32_port/Core/Src/dashboard_mode_font.c', *lvgl_sources]
            content = 'cmake_minimum_required(VERSION 3.16)\nproject(hmi_regression C)\n'
            content += 'add_executable(regression\n' + '\n'.join('"' + str(p) + '"' for p in sources) + '\n)\n'
            content += 'target_include_directories(regression PRIVATE "' + str(ROOT / 'tools/tests/stubs') + '" "' + str(work) + '" "' + str(ROOT / 'stm32_port/Core/Inc') + '")\n'
            content += 'target_compile_definitions(regression PRIVATE LV_CONF_PATH="' + str(conf) + '" DASHBOARD_UI_SOURCE="' + str(ui) + '")\n'
            if label == 'baseline' and 'static void dashboard_set_label_text' not in ui.read_text():
                content += 'target_compile_definitions(regression PRIVATE dashboard_set_label_text=lv_label_set_text)\n'
            content += 'target_compile_options(regression PRIVATE -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)\n'
            content += 'target_link_options(regression PRIVATE -fsanitize=address,undefined)\ntarget_link_libraries(regression PRIVATE m)\n'
            write_changed(cmake / 'CMakeLists.txt', content)
            run('cmake', '-S', cmake, '-B', directory / 'build', '-G', 'Ninja', '-DCMAKE_C_COMPILER=/usr/bin/cc')
            run('cmake', '--build', directory / 'build', '-j', '8')
            binary = directory / 'build/regression'
            if label == 'current':
                run(binary)
            if args.compare_ref:
                run(binary, directory)
        if args.compare_ref:
            for mode in range(4):
                assert (work / f'current/mode-{mode}.rgb565').read_bytes() == (work / f'baseline/mode-{mode}.rgb565').read_bytes(), f'Mode {mode}: rendered pixels changed'
            print('All four 800x480 modes match the baseline pixel for pixel')


if __name__ == '__main__':
    main()
