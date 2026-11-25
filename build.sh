#!/usr/bin/env bash
# Build (and optionally flash) the Phosphor firmware for the Waveshare ESP32-S3-Touch-LCD-3.49.
#
#   ./build.sh                     compile only; outputs in build/
#   ./build.sh /dev/cu.usbmodemXXX upload over USB (serial port path)
#   ./build.sh phosphor-1a2b.local upload over Wi-Fi (OTA); needs OTA_TOKEN in firmware/secrets.h
#
# The firmware reports the version in /status, the log and the installer: env PHOSPHOR_VERSION (CI sets the tag name),
# else `git describe --tags --always` (v1.0.2, or v1.0.2-3-g1a2b3c4 between releases), else "dev".
#
# Needs arduino-cli with the esp32:esp32 core installed (arduino-cli core install esp32:esp32).
# Libraries U8g2, WiFiManager and SensorLib are installed from the Library Manager; ESP32-audioI2S is vendored in libraries/.
# Besides the app image, build/phosphor-full.bin is written: bootloader + partitions + boot_app0 + app merged into one
# file that is flashed at offset 0x0 (for web installers or `esptool write_flash 0x0 build/phosphor-full.bin`).
# The flash layout is firmware/partitions.csv (it overrides the scheme in FQBN): 4.5 MB app slots and an "assets"
# partition at 0x910000 for build/assets.bin, the asset pack tools/pack_assets.py makes from assets/ (scene pictures,
# later fonts). A USB upload writes both; an OTA upload sends the app, then the pack to POST /assets.
set -euo pipefail
cd "$(cd "$(dirname "$0")" && pwd)"

TARGET="${1:-}"
SKETCH="firmware"
BUILD="build"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc"
APP_MAX=4718592        # app0/app1 in firmware/partitions.csv (0x480000)
ASSETS_OFFSET=0x910000 # the assets partition
# The radio stations are MP3 and AAC; the other decoders of the vendored ESP32-audioI2S stay out of the image.
DEFINES="-DBOARD_TOUCH349 -DAUDIO_NO_FLAC -DAUDIO_NO_OPUS -DAUDIO_NO_VORBIS"
U8G2_VERSION="2.36.18"
SENSORLIB_VERSION="0.3.1"
WIFIMANAGER_VERSION="2.0.17"

VERSION="${PHOSPHOR_VERSION:-$(git describe --tags --always 2>/dev/null || echo dev)}"

# The screen texts against the language tables (Lang*.cpp): a text without a translation, a broken table or a mistyped
# T()/TCTX() stops the build here, with the file and line.
python3 tools/check_i18n.py

command -v arduino-cli >/dev/null || { echo "arduino-cli not found: https://arduino.github.io/arduino-cli/"; exit 1; }

arduino-cli lib install "U8g2@${U8G2_VERSION}" "WiFiManager@${WIFIMANAGER_VERSION}" "SensorLib@${SENSORLIB_VERSION}"

arduino-cli compile --fqbn "$FQBN" \
  --libraries libraries \
  --build-property "compiler.c.extra_flags=${DEFINES}" \
  --build-property "compiler.cpp.extra_flags=${DEFINES} -DFW_VERSION=\"${VERSION}\"" \
  --build-property "upload.maximum_size=${APP_MAX}" \
  --build-path "$BUILD" "$SKETCH"

# The esp32 core already writes the merged image next to the app binary.
if [[ -f "$BUILD/$SKETCH.ino.merged.bin" ]]; then
  cp "$BUILD/$SKETCH.ino.merged.bin" "$BUILD/phosphor-full.bin"
else
  # Fallback: merge by hand. ESP32-S3 offsets: bootloader 0x0, partition table 0x8000, boot_app0 0xe000, app 0x10000.
  PLATFORM="$(arduino-cli compile --fqbn "$FQBN" --show-properties "$SKETCH" | sed -n 's/^runtime.platform.path=//p')"
  esptool --chip esp32s3 merge_bin -o "$BUILD/phosphor-full.bin" --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0 "$BUILD/$SKETCH.ino.bootloader.bin" \
    0x8000 "$BUILD/$SKETCH.ino.partitions.bin" \
    0xe000 "$PLATFORM/tools/partitions/boot_app0.bin" \
    0x10000 "$BUILD/$SKETCH.ino.bin"
fi
python3 tools/pack_assets.py assets "$BUILD/assets.bin" "$VERSION"
echo "Version: $VERSION"
echo "Built: $BUILD/$SKETCH.ino.bin (app), $BUILD/phosphor-full.bin (full flash image, write at 0x0),"
echo "       $BUILD/assets.bin (asset pack, write at $ASSETS_OFFSET)"

[[ -z "$TARGET" ]] && exit 0

if [[ "$TARGET" == /dev/* ]]; then
  arduino-cli upload --fqbn "$FQBN" --port "$TARGET" --input-dir "$BUILD" "$SKETCH"
  ESPTOOL="$(command -v esptool || command -v esptool.py || true)"
  [[ -z "$ESPTOOL" ]] && { echo "esptool not found: the asset pack was not written (pip install esptool)"; exit 1; }
  "$ESPTOOL" --chip esp32s3 --port "$TARGET" write_flash "$ASSETS_OFFSET" "$BUILD/assets.bin"
else
  TOKEN=""
  [[ -f "$SKETCH/secrets.h" ]] && TOKEN="$(sed -n 's/^#define OTA_TOKEN "\(.*\)"/\1/p' "$SKETCH/secrets.h")"
  if [[ -z "$TOKEN" ]]; then
    echo "OTA needs OTA_TOKEN in $SKETCH/secrets.h (copy secrets.h.example); it must also be flashed on the device."
    exit 1
  fi
  echo "OTA upload to $TARGET ..."
  curl -4 -fsS --max-time 120 -H "X-Token: $TOKEN" -F "firmware=@$BUILD/$SKETCH.ino.bin" "http://$TARGET/update"
  echo
  # then the asset pack, once the new firmware is up (a device still on the old partition table has no room for it)
  for _ in $(seq 1 30); do sleep 2; curl -4 -fsS --max-time 3 "http://$TARGET/status" >/dev/null 2>&1 && break; done
  curl -4 -fsS --max-time 120 -H "X-Token: $TOKEN" -F "assets=@$BUILD/assets.bin" "http://$TARGET/assets" ||
    echo "Asset pack not sent (old partition table? flash once over USB); scenes fall back to the plain drawing."
  echo
fi
echo "Uploaded to $TARGET"
