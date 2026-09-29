from __future__ import annotations

import argparse
import re
from pathlib import Path


def export_model(
    source: Path,
    header: Path,
    implementation: Path,
    symbol: str,
    section: str | None,
) -> None:
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", symbol):
        raise ValueError(f"invalid C++ symbol: {symbol}")
    data = source.read_bytes()
    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        f"extern const std::uint8_t {symbol}[];",
        f"extern const std::size_t {symbol}_len;",
        "",
    ]
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text("\n".join(lines), encoding="ascii")

    array_declaration = f"alignas(16) const std::uint8_t {symbol}[]"
    if section:
        if not re.fullmatch(r"[A-Za-z0-9_.$]+", section):
            raise ValueError(f"invalid linker section: {section}")
        array_declaration += f' __attribute__((section("{section}"), used))'
    implementation_lines = [
        f'#include "{header.name}"',
        "",
        f"{array_declaration} = {{",
    ]
    for offset in range(0, len(data), 12):
        row = data[offset : offset + 12]
        implementation_lines.append("    " + ", ".join(f"0x{value:02x}" for value in row) + ",")
    implementation_lines.extend(
        [
            "};",
            f"const std::size_t {symbol}_len = sizeof({symbol});",
            "",
        ]
    )
    implementation.parent.mkdir(parents=True, exist_ok=True)
    implementation.write_text("\n".join(implementation_lines), encoding="ascii")
    print(f"saved: {header}")
    print(f"saved: {implementation}")
    print(f"bytes: {len(data)}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Export a TFLite model as C++ byte-array sources.")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--header", type=Path, default=Path("app/src/ai/model/fish_bbox_model_data.hpp"))
    parser.add_argument(
        "--source", type=Path, default=Path("app/src/ai/model/fish_bbox_model_data.cc")
    )
    parser.add_argument("--symbol", default="g_fish_bbox_model_data")
    parser.add_argument("--section", help="Place the model array in this linker section.")
    args = parser.parse_args()
    export_model(args.input, args.header, args.source, args.symbol, args.section)


if __name__ == "__main__":
    main()