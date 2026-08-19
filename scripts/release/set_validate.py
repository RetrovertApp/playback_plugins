#!/usr/bin/env python3
"""Set-level validation of a gathered release set, for one target.

Extracts every rostered artifact of the target into a single generation
directory — the collision check — and then loads all of them simultaneously
in one process through the set-load host, which is what a catalog activation
actually does. Per-artifact correctness is already attested by the harness;
only whole-set properties are judged here.
"""

import argparse
import io
import subprocess
import sys
import tarfile
import tomllib
from pathlib import Path


def fail(msg):
    print(f"set-validate: error: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def info(msg):
    print(f"set-validate: {msg}", flush=True)


def zstd_decompress(data):
    try:
        import zstandard

        return zstandard.ZstdDecompressor().decompress(data, max_output_size=1 << 31)
    except ImportError:
        out = subprocess.run(["zstd", "-d", "-c"], input=data, capture_output=True, check=True)
        return out.stdout


def safe_members(tar, archive_name):
    for member in tar.getmembers():
        name = member.name
        if name.startswith("/") or ".." in Path(name).parts or "\\" in name or ":" in name:
            fail(f"{archive_name}: unsafe path '{name}'")
        if not (member.isreg() or member.isdir()):
            fail(f"{archive_name}: non-regular entry '{name}'")
        yield member


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--channel", required=True)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--generation", required=True, type=Path)
    parser.add_argument("--host", type=Path, help="set-load host binary; skip load when omitted")
    args = parser.parse_args()

    roster = tomllib.loads((Path("channels") / args.channel / "roster.toml").read_text())
    plugins = sorted(roster["plugins"])

    lib_suffix = ".dll" if args.target == "windows-x86_64" else ".so"
    generation = args.generation
    if generation.exists():
        fail(f"generation dir {generation} already exists")
    generation.mkdir(parents=True)

    seen = {}
    for name in plugins:
        archive = args.artifacts / f"{name}-{args.target}.tar.zst"
        if not archive.is_file():
            fail(f"complete-release: {archive.name} is missing from the gathered set")
        raw = zstd_decompress(archive.read_bytes())
        with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
            for member in safe_members(tar, archive.name):
                if member.isreg() and member.name in seen:
                    fail(f"collision: '{member.name}' in both {seen[member.name]} and {archive.name}")
                seen[member.name] = archive.name
            try:
                tar.extractall(generation, filter="data")
            except TypeError:
                # Refuse to extract without the data filter rather than
                # silently downgrading the safety gate.
                fail("this python lacks tarfile extraction filters (needs 3.11.4+)")
        module = generation / f"{name}_playback{lib_suffix}"
        if not module.is_file():
            fail(f"{archive.name}: extracted no {module.name}")
        info(f"{name}: extracted into the generation")

    info(f"collision check passed: {len(seen)} entries, no overlaps")

    if args.host:
        modules = [str((generation / f"{name}_playback{lib_suffix}").resolve()) for name in plugins]
        result = subprocess.run([str(args.host)] + modules)
        if result.returncode != 0:
            fail("whole-set load failed")
        info(f"whole-set load passed: {len(modules)} plugins loaded simultaneously")


if __name__ == "__main__":
    main()
