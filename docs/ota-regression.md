# OTA authorization regression

The `/update` and `/assets` routes must authorize each request using that
request's `X-Token`. An earlier authorized upload must not authorize a later
request without a token or file.

Previously, both routes reused `ota_authorized`, which was updated only by
`UPLOAD_FILE_START`. A subsequent POST without a file skipped that callback
and inherited the previous flag. After a failed or interrupted authorized
upload, it could reach an error or restart path instead of returning 401.

The fix checks the current request's headers in both callbacks and final
handlers, requires a completed file in the current request, and aborts an
interrupted firmware update so a later upload can start again.

## Hardware regression

`tools/test_ota.py` uses the actual HTTP server on an ESP32. It requires only
Python's standard library. It deliberately reinstalls the supplied application
image and reboots the device once. Use a known-good image matching the device's
board and partition layout, with the same version already running on it.
Build and install the checkout you want to test first, using `./build.sh`.
The device must have a nonempty `OTA_TOKEN` configured in `firmware/secrets.h`.
The test verifies the image against an explicitly supplied SHA-256 before
connecting and checks that the same firmware version returns after reboot.

The script checks both routes without writing an asset pack. Its interrupted
upload and successful retry exercise `/update`; it does not interrupt an asset
pack write, because that can leave the asset partition partially overwritten.

```bash
read -r -s -p "OTA token: " PHOSPHOR_OTA_TOKEN
export PHOSPHOR_OTA_TOKEN
sha256sum build/firmware.ino.bin
# Supply the expected hash of your known-good image below.
python3 tools/test_ota.py http://192.168.1.14 \
  --firmware build/firmware.ino.bin --firmware-sha256 EXPECTED_SHA256 \
  --output ota-results.json
unset PHOSPHOR_OTA_TOKEN
```

The token is never printed or included in the JSON result. The script waits
for at least 30 seconds of uptime before testing for unexpected restarts.
`--token-env` can
select another environment variable. The firmware is an application image,
not the merged image intended for flash address zero.

Expected results:

- Missing/wrong tokens, with and without multipart files: 401, no restart.
- A correct token without a file: 400, no restart.
- An authorized invalid firmware file: 500; subsequent unauthorized requests
  still return 401 on both routes.
- An authorized firmware upload whose connection closes before the body is
  complete: no restart; subsequent unauthorized requests still return 401.
- A subsequent complete, authorized firmware upload: 200 and a reboot.
- After reboot, unauthorized requests still return 401 and `/api/settings`
  remains byte-for-byte unchanged.

The script exits nonzero on the first failed check. These are device tests,
not an unattended GitHub Actions job: they require a reachable board, its
token and an explicitly supplied firmware image. The normal CI firmware build
continues to check that the route code compiles.
