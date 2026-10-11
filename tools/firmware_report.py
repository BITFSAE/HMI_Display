#!/usr/bin/env python3
"""Report loadable firmware memory, including the fixed-address LVGL pool."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def output(prefix, tool, *args):
    return subprocess.check_output([prefix + tool, *map(str, args)], text=True)


def report(elf, prefix, stack_dir):
    # Program area only: flash sector 7 (the last 128 KiB) stores the odometer.
    regions = {"FLASH": (0x08000000, 384 * 1024),
               "SRAM": (0x20000000, 128 * 1024),
               "CCM": (0x10000000, 64 * 1024)}
    usage = dict.fromkeys(regions, 0)
    flash_end = regions["FLASH"][0]
    sections = []
    lines = output(prefix, "objdump", "-h", elf).splitlines()
    for index, line in enumerate(lines[:-1]):
        match = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
        if not match or "ALLOC" not in lines[index + 1]:
            continue
        name, size, vma, lma = match.groups()
        size, vma, lma = int(size, 16), int(vma, 16), int(lma, 16)
        sections.append({"name": name, "bytes": size, "vma": vma, "lma": lma})
        for region, (start, capacity) in regions.items():
            if start <= vma < start + capacity:
                usage[region] += size
        start, capacity = regions["FLASH"]
        if "LOAD" in lines[index + 1] and start <= lma < start + capacity:
            flash_end = max(flash_end, lma + size)
    # Use the highest load address so linker alignment gaps are counted too.
    usage["FLASH"] = flash_end - regions["FLASH"][0]
    config = Path(__file__).resolve().parents[1] / "stm32_port/Core/lv_conf.h"
    memory_match = re.search(r"#define\s+LV_MEM_SIZE\s+\((\d+)\s*\*\s*1024U\)", config.read_text())
    if memory_match is None:
        raise ValueError("Cannot determine fixed-address LVGL pool size")
    pool = int(memory_match[1]) * 1024
    usage["CCM"] += pool
    result = {"elf": str(elf), "memory": {}, "sections": sections, "largest_frames": []}
    for region, (_, capacity) in regions.items():
        used = usage[region]
        result["memory"][region] = {"used_bytes": used, "capacity_bytes": capacity,
                                     "free_bytes": capacity - used}
        if used > capacity:
            raise ValueError(f"{region} exceeds hardware capacity: {used} > {capacity}")
        print(f"{region:5}: {used:6,d} / {capacity:6,d} bytes ({used / capacity:5.1%}), free {capacity - used:,d}")
    print("SRAM includes linker heap/MSP reservation; RTOS stacks are inside .bss or its heap.")
    print("CCM includes the 60 KiB LVGL pool at LV_MEM_ADR, which ELF size omits.")
    if stack_dir:
        frames = []
        for path in Path(stack_dir).rglob("*.su"):
            for line in path.read_text().splitlines():
                fields = line.split("\t")
                if len(fields) == 3 and fields[1].isdigit():
                    frames.append({"function": fields[0], "bytes": int(fields[1]), "kind": fields[2]})
        result["largest_frames"] = sorted(frames, key=lambda frame: frame["bytes"], reverse=True)[:8]
        print("Largest individual stack frames (not full call-chain bounds):")
        for frame in result["largest_frames"]:
            print(f"  {frame['bytes']:4} bytes  {frame['function']}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--tool-prefix", default="arm-none-eabi-")
    parser.add_argument("--stack-dir", type=Path)
    args = parser.parse_args()
    result = report(args.elf, args.tool_prefix, args.stack_dir)
    args.elf.with_suffix(".memory.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
