#!/usr/bin/env python3
"""Fix case-sensitive ePaper include paths in Seeed_GFX2 v1.0.0 on Linux."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("library", type=Path, help="Seeed_GFX2 library directory")
args = parser.parse_args()
for relative, includes in (
    ("src/runtime/ProductCatalog.cpp", (
        "../board/boards/XIAO_ePaper_Boards.h",
        "../board/boards/reTerminal_ePaper_Boards.h")),
    ("src/board/boards/XIAO_EPaper_Boards.h", (
        "../configs/XIAO_ePaper_Board_Configs.h",)),
    ("src/board/boards/reTerminal_EPaper_Boards.h", (
        "../configs/reTerminal_ePaper_Board_Configs.h",)),
):
    source = args.library / relative
    text = source.read_text()
    for old in includes:
        new = old.replace("_ePaper_", "_EPaper_")
        if not (source.parent / new).is_file():
            raise SystemExit(f"Expected header does not exist: {new}")
        if old not in text and new not in text:
            raise SystemExit(f"Unrecognized library source: {source}")
        text = text.replace(f'"{old}"', f'"{new}"')
    source.write_text(text)
    print(f"Include path capitalization verified: {source}")
