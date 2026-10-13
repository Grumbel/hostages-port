#!/bin/sh
# Run the scripted scenarios headless and collect their screenshots.
#   tests/run_scenarios.sh [build/hostages] [outdir]
# Scenarios are in tests/scenarios/<release>/, run with the release's ADF
# (hostage: "Hostage (Infogrames).adf", hostages: "Hostages (1988)
# (Infogrames)[cr].adf", both in the repository root). A line
# "# args: ..." in a scenario adds command line options.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
bin=$(realpath "${1:-$root/build/hostages}")
out=${2:-$root/build/scenarios}
for dir in "$here"/scenarios/*/; do
  release=$(basename "$dir")
  case "$release" in
    hostage) adf="$root/Hostage (Infogrames).adf" ;;
    hostages) adf="$root/Hostages (1988)(Infogrames)[cr].adf" ;;
    *) continue ;;
  esac
  if [ ! -f "$adf" ]; then echo "$release: $adf not found, skipped"; continue; fi
  for s in "$dir"*.txt; do
    name=$release/$(basename "$s" .txt)
    mkdir -p "$out/$name"
    last=$(grep -v '^#' "$s" | awk '{print $1}' | sort -n | tail -1)
    args=$(sed -n 's/^# args: //p' "$s")
    (cd "$out/$name" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
       "$bin" --fast --save "$out/$name/save" $args --script "$s" --frames $((last + 10)) "$adf" \
       2>&1 | grep -v '^\[' || true)
    echo "$name: $(ls "$out/$name"/*.ppm 2>/dev/null | wc -l) screenshots in $out/$name"
  done
done
