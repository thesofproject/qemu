#!/usr/bin/env python3
"""
parse-qemu-trace.py - Parse a QEMU instruction trace log and annotate
entry/jump instructions with ELF symbol names.

Usage:
    python3 parse-qemu-trace.py [--log FILE] [--elf FILE] [--output FILE]
                                [--only-entry] [--only-jump] [--unique]

Requirements:
    pip install pyelftools
    (or use a Python that already has it, e.g. the SOF venv)
    /path/to/venv/bin/python parse-qemu-trace.py ...

To generate the QEMU log, run QEMU with:
    -d in_asm 2>/tmp/qemu.log

The log is expected to contain QEMU -d in_asm style blocks like:

    IN:
    0xa1048000:  j       0xa104852c
    ...
    0xa10486a0:  entry  a1, 32

Xtensa "entry" is the function prologue (call target).
"j", "jx", "call4/8/12", "callx4/8/12", "ret*" are jump/call/return insns.

For each such instruction the script looks up the PC address and (where
applicable) the target address in the ELF symbol table and prints the
nearest enclosing function name.
"""

import re
import sys
import argparse
import shutil
import subprocess
from pathlib import Path
from collections import defaultdict

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
    HAS_ELFTOOLS = True
except ImportError:
    HAS_ELFTOOLS = False


# ---------------------------------------------------------------------------
# ELF symbol loading
# ---------------------------------------------------------------------------

def load_symbols(elf_path: str) -> list[tuple[int, int, str]]:
    """
    Return a sorted list of (start_addr, end_addr, name) for every
        code-relevant symbol found in the ELF file.

        We include:
            - STT_FUNC: normal function symbols
            - STT_NOTYPE: many Xtensa vector/trampoline labels are emitted as NOTYPE
    """
    if not HAS_ELFTOOLS:
        print("WARNING: pyelftools not installed – symbol lookup disabled.", file=sys.stderr)
        print("         Install with: pip install pyelftools", file=sys.stderr)
        return []

    symbols = []
    try:
        with open(elf_path, "rb") as fh:
            elf = ELFFile(fh)
            for section in elf.iter_sections():
                if not isinstance(section, SymbolTableSection):
                    continue
                for sym in section.iter_symbols():
                    if sym.entry.st_info.type in ("STT_FUNC", "STT_NOTYPE"):
                        addr = sym.entry.st_value
                        size = sym.entry.st_size
                        name = sym.name
                        if addr and name:
                            symbols.append((addr, addr + max(size, 1), name))
    except FileNotFoundError:
        print(f"ERROR: ELF file not found: {elf_path}", file=sys.stderr)
    except Exception as exc:
        print(f"ERROR reading ELF: {exc}", file=sys.stderr)

    symbols.sort(key=lambda x: x[0])
    return symbols


def load_objdump_labels(elf_path: str) -> list[tuple[int, int, str]]:
    """
    Fallback symbol extraction from disassembly labels.

    This follows the user's requested heuristic: use the nearest label
    above an address as shown by objdump/llvm-objdump disassembly, e.g.
    "a003110c <xtensa_excint1_c>:".
    """
    candidates = [
        ["llvm-objdump", "-d", elf_path],
        ["xtensa-zephyr-elf-objdump", "-d", elf_path],
        ["xtensa-elf-objdump", "-d", elf_path],
        ["objdump", "-d", elf_path],
    ]

    cmd = None
    for c in candidates:
        if shutil.which(c[0]):
            cmd = c
            break
    if cmd is None:
        return []

    try:
        proc = subprocess.run(
            cmd,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except Exception:
        return []

    if proc.returncode != 0 or not proc.stdout:
        return []

    re_label = re.compile(r'^([0-9a-fA-F]+)\s+<([^>]+)>:$')
    labels: list[tuple[int, str]] = []
    for line in proc.stdout.splitlines():
        m = re_label.match(line.strip())
        if not m:
            continue
        addr = int(m.group(1), 16)
        name = m.group(2).strip()
        if name:
            labels.append((addr, name))

    if not labels:
        return []

    labels.sort(key=lambda x: x[0])
    out: list[tuple[int, int, str]] = []
    for i, (start, name) in enumerate(labels):
        end = labels[i + 1][0] if i + 1 < len(labels) else start + 1
        if end <= start:
            end = start + 1
        out.append((start, end, name))
    return out


def find_symbol(symbols: list, addr: int) -> str | None:
    """Binary-search for the symbol that contains *addr*."""
    lo, hi = 0, len(symbols) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        start, end, name = symbols[mid]
        if start <= addr < end:
            return name
        elif start > addr:
            hi = mid - 1
        else:
            # addr >= end; this symbol *starts* before addr
            best = name          # keep as fallback nearest-below
            lo = mid + 1
    return best


# ---------------------------------------------------------------------------
# Log parsing
# ---------------------------------------------------------------------------

# IN: block header (QEMU in_asm trace)
RE_IN = re.compile(r'^IN:\s*$')

# Instruction line:  0xADDR:  mnemonic  operands
RE_INSN = re.compile(r'^\s*(0x[0-9a-fA-F]+):\s+(\S+)\s*(.*)')

# Jump/branch targets encoded literally in the operand field
RE_HEX_TARGET = re.compile(r'0x[0-9a-fA-F]+')

# Xtensa instructions we care about
ENTRY_MNEMONICS = {"entry"}

# Direct calls/jumps with a literal PC target in operands
DIRECT_JUMP_MNEMONICS = {
    "j",
    "call0", "call4", "call8", "call12",
    "bnez", "beqz", "bgez", "bltz",
    "bnez.n", "beqz.n",
    "bne", "beq", "bge", "blt", "bgeu", "bltu",
    "ball", "bany", "bnall", "bnone",
    "bf", "bt",
    "loop", "loopgtz", "loopnez",
}

# Indirect calls/jumps that use a register; target is the *next* IN: block
INDIRECT_JUMP_MNEMONICS = {
    "jx",
    "callx0", "callx4", "callx8", "callx12",
}

RETURN_MNEMONICS = {"ret", "ret.n", "retw", "retw.n"}

JUMP_MNEMONICS = DIRECT_JUMP_MNEMONICS | INDIRECT_JUMP_MNEMONICS | RETURN_MNEMONICS


# ---------------------------------------------------------------------------
# Block-level parser
# ---------------------------------------------------------------------------

# An InsnBlock is a list of (lineno, pc, mnemonic, operands) tuples
InsnBlock = list[tuple[int, int, str, str]]


def parse_blocks(log_path: str) -> list[InsnBlock]:
    """
    Split the log into QEMU IN: basic-block slices.
    Each slice is a list of (lineno, pc, mnemonic, operands).
    """
    blocks: list[InsnBlock] = []
    current: InsnBlock = []
    in_block = False

    with open(log_path, "r", errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            if RE_IN.match(raw):
                if current:
                    blocks.append(current)
                current = []
                in_block = True
                continue
            if not in_block:
                continue
            m = RE_INSN.match(raw)
            if m:
                pc_str = m.group(1)
                mnemonic = m.group(2)
                operands = m.group(3).strip()
                current.append((lineno, int(pc_str, 16), mnemonic, operands))

    if current:
        blocks.append(current)

    return blocks


def parse_log(log_path: str,
              symbols: list,
              only_entry: bool,
              only_jump: bool,
              unique: bool) -> list[dict]:
    """
    Parse the log and return one event dict per interesting instruction.

    For indirect calls (callx*) the target is resolved from the first
    instruction of the immediately following IN: block, exactly as QEMU
    executes it.

    Each dict has:
        line_no, pc, mnemonic, operands,
        pc_sym,               # function containing the instruction
        target,               # resolved target address (int or None)
        target_sym,           # function name at target (str or None)
        kind                  # "ENTRY" | "CALL" | "RET" | "JUMP"
    """
    blocks = parse_blocks(log_path)

    events = []
    seen: set[tuple[int, str]] = set()

    def want(mnem: str) -> bool:
        if only_entry:
            return mnem in ENTRY_MNEMONICS
        if only_jump:
            return mnem in JUMP_MNEMONICS
        return mnem in ENTRY_MNEMONICS or mnem in JUMP_MNEMONICS

    for blk_idx, block in enumerate(blocks):
        # First PC of the NEXT block (used to resolve indirect call targets)
        next_block_first_pc: int | None = None
        if blk_idx + 1 < len(blocks):
            next_block = blocks[blk_idx + 1]
            if next_block:
                next_block_first_pc = next_block[0][1]  # first insn's PC

        for (lineno, pc, mnemonic, operands) in block:
            mnem_lower = mnemonic.lower().rstrip(".")

            if not want(mnem_lower):
                continue

            # Deduplicate (QEMU re-translates hot blocks)
            key = (pc, mnem_lower)
            if unique and key in seen:
                continue
            seen.add(key)

            # ---- resolve caller's function (pc_sym) --------------------
            pc_sym = find_symbol(symbols, pc) if symbols else None

            # ---- resolve target ----------------------------------------
            target: int | None = None
            target_sym: str | None = None

            if mnem_lower in INDIRECT_JUMP_MNEMONICS:
                # Target is a register; use the next IN: block's first PC
                if next_block_first_pc is not None:
                    target = next_block_first_pc
                    if symbols:
                        target_sym = find_symbol(symbols, target)

            elif mnem_lower in DIRECT_JUMP_MNEMONICS:
                # Target may appear as a hex literal in operands
                hex_matches = RE_HEX_TARGET.findall(operands)
                if hex_matches:
                    target = int(hex_matches[-1], 16)
                    if symbols:
                        target_sym = find_symbol(symbols, target)

            elif mnem_lower in ENTRY_MNEMONICS:
                # entry itself IS the function – pc_sym is the answer;
                # no separate "target" needed
                pass

            # ---- classify ----------------------------------------------
            if mnem_lower in ENTRY_MNEMONICS:
                kind = "ENTRY"
            elif mnem_lower in (INDIRECT_JUMP_MNEMONICS | {"call0", "call4", "call8", "call12"}):
                kind = "CALL"
            elif mnem_lower in RETURN_MNEMONICS:
                kind = "RET"
            else:
                kind = "JUMP"

            events.append({
                "line_no":    lineno,
                "pc":         pc,
                "mnemonic":   mnemonic,
                "operands":   operands,
                "pc_sym":     pc_sym,
                "target":     target,
                "target_sym": target_sym,
                "kind":       kind,
            })

    return events


# ---------------------------------------------------------------------------
# Formatting / output
# ---------------------------------------------------------------------------

def format_addr(addr: int | None, sym: str | None) -> str:
    if addr is None:
        return ""
    s = f"0x{addr:08x}"
    if sym:
        s += f" <{sym}>"
    return s


def print_events(events: list[dict], out) -> None:
    for ev in events:
        kind = ev["kind"]
        pc_str = format_addr(ev["pc"], ev["pc_sym"])
        target = ev["target"]
        target_sym = ev["target_sym"]
        caller_fn = ev["pc_sym"] or "(unknown)"

        # For ENTRY, show the function being entered (pc_sym is the answer)
        if kind == "ENTRY":
            print(
                f"[ENTRY] line {ev['line_no']:>7}  {pc_str}  fn={caller_fn}",
                file=out,
            )
            continue

        # For all other kinds, show instruction and resolved target
        target_str = format_addr(target, target_sym)
        target_fn = target_sym or (f"0x{target:08x}" if target is not None else "(unknown)")
        insn_str = f"{ev['mnemonic']:<10} {ev['operands']}".rstrip()

        if target_str:
            line = (f"[{kind:<4}] line {ev['line_no']:>7}  "
                f"{pc_str:<52}  {insn_str}  ->  {target_str}  "
                f"caller={caller_fn} target_fn={target_fn}")
        else:
            line = (f"[{kind:<4}] line {ev['line_no']:>7}  "
                f"{pc_str:<52}  {insn_str}  caller={caller_fn}")
        print(line, file=out)


def print_summary(events: list[dict], out) -> None:
    """Print a frequency table of called/entered functions."""
    call_counts: dict[str, int] = defaultdict(int)
    entry_counts: dict[str, int] = defaultdict(int)

    for ev in events:
        if ev["kind"] == "ENTRY":
            sym = ev["pc_sym"] or f"0x{ev['pc']:08x}"
            entry_counts[sym] += 1
        elif ev["kind"] == "CALL":
            sym = ev["target_sym"] or (f"0x{ev['target']:08x}" if ev["target"] else "(indirect)")
            call_counts[sym] += 1

    if entry_counts:
        print("\n=== Functions entered (entry instruction) ===", file=out)
        for sym, cnt in sorted(entry_counts.items(), key=lambda x: -x[1]):
            print(f"  {cnt:>6}  {sym}", file=out)

    if call_counts:
        print("\n=== Call targets ===", file=out)
        for sym, cnt in sorted(call_counts.items(), key=lambda x: -x[1]):
            print(f"  {cnt:>6}  {sym}", file=out)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Annotate QEMU entry/jump trace with ELF symbols."
    )
    parser.add_argument("--log",  "-l", default="/tmp/qemu.log",
                        help="Path to QEMU log file (default: /tmp/qemu.log)")
    parser.add_argument("--elf",  "-e", default="",
                        help="Path to ELF file for symbol lookup")
    parser.add_argument("--output", "-o", default="",
                        help="Write results to this file (default: stdout)")
    parser.add_argument("--only-entry", action="store_true",
                        help="Only report 'entry' instructions (function prologues)")
    parser.add_argument("--only-jump",  action="store_true",
                        help="Only report jump/call/branch instructions")
    parser.add_argument("--unique", "-u", action="store_true",
                        help="Skip duplicate (PC, mnemonic) pairs (de-dup re-translations)")
    parser.add_argument("--summary", "-s", action="store_true",
                        help="Print a symbol hit-count summary at the end")
    args = parser.parse_args()

    log_path = args.log
    if not Path(log_path).exists():
        # Try common alternate name
        alt = log_path.replace(".jog", ".log")
        if Path(alt).exists():
            log_path = alt
            print(f"Note: using {alt} (original path not found)", file=sys.stderr)
        else:
            print(f"ERROR: log file not found: {log_path}", file=sys.stderr)
            sys.exit(1)

    symbols = []
    if args.elf:
        symbols = load_symbols(args.elf)
        dump_symbols = load_objdump_labels(args.elf)
        if dump_symbols:
            # Add disassembly labels as fallback ranges
            symbols.extend(dump_symbols)
            symbols.sort(key=lambda x: x[0])
            print(
                f"Loaded {len(symbols)} code symbols/labels from {args.elf}",
                file=sys.stderr,
            )
        else:
            print(f"Loaded {len(symbols)} code symbols from {args.elf}", file=sys.stderr)
    else:
        # Auto-detect common ELF names in current directory and parent
        candidates = [
            "zephyr.elf", "firmware.elf", "sof.elf",
            "../zephyr.elf", "build/zephyr/zephyr.elf",
        ]
        for c in candidates:
            p = Path(c)
            if p.exists():
                symbols = load_symbols(str(p))
                print(f"Auto-detected ELF: {p.resolve()}  ({len(symbols)} symbols)", file=sys.stderr)
                break
        if not symbols:
            print("Note: no ELF provided and none auto-detected – addresses will not be resolved.",
                  file=sys.stderr)

    events = parse_log(log_path, symbols,
                       only_entry=args.only_entry,
                       only_jump=args.only_jump,
                       unique=args.unique)

    out = open(args.output, "w") if args.output else sys.stdout
    try:
        print_events(events, out)
        if args.summary:
            print_summary(events, out)
    finally:
        if args.output:
            out.close()

    print(f"\nTotal events: {len(events)}", file=sys.stderr)


if __name__ == "__main__":
    main()
