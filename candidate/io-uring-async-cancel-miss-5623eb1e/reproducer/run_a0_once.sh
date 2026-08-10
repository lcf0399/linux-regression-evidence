#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 a0_miss|a0_hit smoke|point OUTPUT_DIR" >&2
}

[[ $# -eq 3 ]] || { usage; exit 2; }
profile=$1
mode=$2
out_dir=$3
[[ "$profile" == a0_miss || "$profile" == a0_hit ]] || { usage; exit 2; }
[[ "$mode" == smoke || "$mode" == point ]] || { usage; exit 2; }

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
binary="$script_dir/build/io_uring_cancel_round"
mkdir -p -- "$out_dir"
make -C "$script_dir"

result="$out_dir/${profile}-${mode}.tsv"
validation="$out_dir/${profile}-${mode}.validation.json"
environment="$out_dir/${profile}-${mode}.env"
[[ ! -e "$result" && ! -e "$validation" && ! -e "$environment" ]] || {
    echo "Refusing to overwrite an existing output in $out_dir" >&2
    exit 1
}

{
    printf 'timestamp_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'uname=%s\n' "$(uname -a)"
    printf 'cmdline=%s\n' "$(tr -d '\r\n' </proc/cmdline)"
    printf 'boot_id=%s\n' "$(tr -d '\r\n' </proc/sys/kernel/random/boot_id)"
    printf 'binary_sha256=%s\n' "$(sha256sum "$binary" | awk '{print $1}')"
} >"$environment"

"$binary" --profile "$profile" --mode "$mode" --execute --output "$result"
phase=semantic-smoke
[[ "$mode" == point ]] && phase=measured
python3 "$script_dir/validate_cancel_tsv.py" \
    --input "$result" --profile "$profile" --phase "$phase" \
    --output "$validation"

printf 'result=%s\nvalidation=%s\nenvironment=%s\n' \
    "$result" "$validation" "$environment"
