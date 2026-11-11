#!/usr/bin/env bash
# Kompilacja na Linuksie (arduino-cli, rdzeń esp32 3.3.x) – te same ustawienia płytki co upload.ps1.
# Użycie: ./build.sh [02_Monitor_App|01_Test_v_0_01]   (domyślnie 02_Monitor_App)
# Wynik: <szkic>/build/<szkic>.ino.bin (OTA) oraz .merged.bin (całość do wgrania przez USB od adresu 0x0).
set -euo pipefail
cd "$(dirname "$0")"
SK="${1:-02_Monitor_App}"
CLI="${ARDUINO_CLI:-$HOME/.local/bin/arduino-cli}"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
"$CLI" compile --fqbn "$FQBN" --warnings default --output-dir "$SK/build" "$SK"
ls -l "$SK/build/"*.bin
