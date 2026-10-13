#!/bin/sh
# Run the scripted scenarios headless and collect their screenshots.
#   tests/run_scenarios.sh [build/hostages] [disk.adf] [outdir]
set -e
here=$(cd "$(dirname "$0")" && pwd)
bin=$(realpath "${1:-$here/../build/hostages}")
adf=$(realpath "${2:-$here/../Hostage (Infogrames).adf}")
out=${3:-$here/../build/scenarios}
for s in "$here"/scenarios/*.txt; do
  name=$(basename "$s" .txt)
  mkdir -p "$out/$name"
  last=$(grep -v '^#' "$s" | awk '{print $1}' | sort -n | tail -1)
  (cd "$out/$name" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
     "$bin" --fast --save "$out/$name/save" --script "$s" --frames $((last + 10)) "$adf" \
     2>&1 | grep -v '^\[' || true)
  echo "$name: $(ls "$out/$name"/*.ppm 2>/dev/null | wc -l) screenshots in $out/$name"
done
