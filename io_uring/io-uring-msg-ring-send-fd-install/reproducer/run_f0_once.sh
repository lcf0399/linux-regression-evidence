#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 smoke|point OUTPUT_DIR" >&2
}

[[ $# -eq 2 ]] || { usage; exit 2; }
mode=$1
out_dir=$2
[[ "$mode" == smoke || "$mode" == point ]] || { usage; exit 2; }

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir=${BUILD_DIR:-"$script_dir/build"}
binary="$build_dir/io_uring_msg_ring_round"
mkdir -p -- "$out_dir"
make -C "$script_dir" BUILD_DIR="$build_dir"

result="$out_dir/f0-$mode.tsv"
validation="$out_dir/f0-$mode.validation.json"
[[ ! -e "$result" && ! -e "$validation" ]] || {
    echo "Refusing to overwrite $result or $validation" >&2
    exit 1
}

"$binary" --profile f0 --mode "$mode" --execute --output "$result"
phase=semantic-smoke
[[ "$mode" == point ]] && phase=measured
python3 "$script_dir/validate_msg_ring_tsv.py" \
    --input "$result" --profile f0 --phase "$phase" --output "$validation"

printf 'result=%s\nvalidation=%s\n' "$result" "$validation"
