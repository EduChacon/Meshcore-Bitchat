#!/usr/bin/env bash
# Copies what one built environment produced to a folder, under the environment's name.
# Usage: collect_firmware.sh <environment> <folder>. BITCHAT_REGION must be the one the build used.
set -euo pipefail
environment="$1"
folder="$2"
out=".pio/build/$environment"
mkdir -p "$folder"
if [ -f "$out/firmware.bin" ]; then
  # ESP32: the application image and the complete one
  pio run -e "$environment" -t mergebin 2>&1 | tail -5
  cp "$out/firmware.bin" "$folder/$environment.bin"
  cp "$out/firmware-merged.bin" "$folder/$environment-merged.bin"
else
  # nRF52: a UF2 for the bootloader drive and the package for the update tool
  python3 bin/uf2conv/uf2conv.py "$out/firmware.hex" -c -o "$folder/$environment.uf2" -f 0xADA52840
  cp "$out/firmware.zip" "$folder/$environment.zip"
fi
