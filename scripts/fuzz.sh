#!/bin/bash
# Build and run fuzz targets for playback plugin parsers.
#
# Usage:
#   ./scripts/fuzz.sh                  # Build all fuzz targets
#   ./scripts/fuzz.sh aon              # Build and run art_of_noise fuzzer
#   ./scripts/fuzz.sh aon --build-only # Build without running
#   ./scripts/fuzz.sh --clean          # Clean fuzz build directory
#
# Fuzzer output:
#   Crashes are saved to fuzz/crashes/<target>/
#   Corpus grows in fuzz/corpus/<target>/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build_fuzz"

# Map short names to target names and corpus dirs
declare -A FUZZ_TARGETS=(
    [aon]="fuzz_aon:art_of_noise"
)

usage() {
    echo "Usage: $0 [target] [options]"
    echo ""
    echo "Targets:"
    echo "  aon    Art of Noise (AON4/AON8) parser"
    echo ""
    echo "Options:"
    echo "  --build-only   Build without running"
    echo "  --clean        Clean build directory"
    echo "  --jobs N       Parallel fuzzer jobs (default: 1)"
    echo "  --max-len N    Max input size in bytes (default: 1048576)"
    echo "  --timeout N    Per-input timeout in seconds (default: 5)"
    echo ""
    echo "Examples:"
    echo "  $0                    # Build all fuzz targets"
    echo "  $0 aon                # Fuzz art_of_noise"
    echo "  $0 aon --jobs 4       # Fuzz with 4 parallel workers"
}

do_build() {
    echo "==> Configuring fuzz build..."
    cmake -B "$BUILD_DIR" -S "$ROOT_DIR" -G Ninja \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DENABLE_FUZZ=ON \
        -DCMAKE_BUILD_TYPE=Debug

    echo "==> Building fuzz targets..."
    cmake --build "$BUILD_DIR" --target "${1:-all}"
}

do_run() {
    local target="$1"
    local info="${FUZZ_TARGETS[$target]}"
    local binary="${info%%:*}"
    local corpus_name="${info##*:}"

    local corpus_dir="$ROOT_DIR/fuzz/corpus/$corpus_name"
    local crash_dir="$ROOT_DIR/fuzz/crashes/$corpus_name"
    local exe="$BUILD_DIR/fuzz/$binary"

    if [ ! -f "$exe" ]; then
        echo "Error: $exe not found. Run build first."
        exit 1
    fi

    mkdir -p "$corpus_dir" "$crash_dir"

    echo "==> Running $binary"
    echo "    Corpus:  $corpus_dir"
    echo "    Crashes: $crash_dir"
    echo "    Max len: $MAX_LEN bytes"
    echo "    Timeout: ${TIMEOUT}s per input"
    echo ""

    if [ "$JOBS" -gt 1 ]; then
        echo "    Workers: $JOBS (parallel mode)"
        "$exe" "$corpus_dir" \
            -artifact_prefix="$crash_dir/" \
            -max_len="$MAX_LEN" \
            -timeout="$TIMEOUT" \
            -jobs="$JOBS" \
            -workers="$JOBS"
    else
        "$exe" "$corpus_dir" \
            -artifact_prefix="$crash_dir/" \
            -max_len="$MAX_LEN" \
            -timeout="$TIMEOUT"
    fi
}

# Parse arguments
TARGET=""
BUILD_ONLY=0
CLEAN=0
JOBS=1
MAX_LEN=1048576
TIMEOUT=5

while [ $# -gt 0 ]; do
    case "$1" in
        --build-only) BUILD_ONLY=1 ;;
        --clean)      CLEAN=1 ;;
        --jobs)       shift; JOBS="$1" ;;
        --max-len)    shift; MAX_LEN="$1" ;;
        --timeout)    shift; TIMEOUT="$1" ;;
        --help|-h)    usage; exit 0 ;;
        *)
            if [ -z "$TARGET" ] && [ -n "${FUZZ_TARGETS[$1]+x}" ]; then
                TARGET="$1"
            else
                echo "Unknown argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
    shift
done

cd "$ROOT_DIR"

if [ "$CLEAN" -eq 1 ]; then
    echo "==> Cleaning $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    exit 0
fi

if [ -z "$TARGET" ]; then
    # Build all targets
    do_build "all"
    echo ""
    echo "All fuzz targets built. Run with a target name to start fuzzing:"
    echo "  $0 aon"
else
    # Build specific target
    info="${FUZZ_TARGETS[$TARGET]}"
    binary="${info%%:*}"
    do_build "$binary"

    if [ "$BUILD_ONLY" -eq 0 ]; then
        do_run "$TARGET"
    fi
fi
