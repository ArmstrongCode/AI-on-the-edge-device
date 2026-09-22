# One-shot ESP32-CAM flashing scripts

Clone, build and flash this repository onto an ESP32-CAM in a single command.
The scripts wrap the manual steps from [`code/README.md`](../../code/README.md):
clone the branch, initialise the submodules, build the `esp32cam` PlatformIO
environment, detect the serial port, erase, upload, and optionally prepare the
SD card.

Requires **git** and the **PlatformIO IDE** extension for VS Code. The scripts
find the PlatformIO CLI themselves under `~/.platformio`, so they work from any
terminal — not just the PlatformIO one.

## Windows

```powershell
.\flash-esp32cam.ps1
```

If PowerShell blocks the script, run it for this session only:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash-esp32cam.ps1
```

## macOS / Linux

```bash
chmod +x flash-esp32cam.sh
./flash-esp32cam.sh
```

## Common options

| Windows | macOS/Linux | Meaning |
| --- | --- | --- |
| `-Path DIR` | `--path DIR` | Where to clone to (default `~/aiotedge`) |
| `-Branch NAME` | `--branch NAME` | Branch to build |
| `-Port COM5` | `--port /dev/ttyUSB0` | Serial port; auto-detected when omitted |
| `-UploadSpeed 115200` | `--upload-speed 115200` | Fallback baud for flaky CH340/CP2102 adapters |
| `-SdCard E:\` | `--sd-card /Volumes/NO\ NAME` | Copy `sd-card/` to a mounted card |
| `-Erase` | `--erase` | Erase the whole flash first (wipes stored config) |
| `-BuildOnly` | `--build-only` | Compile only, never touch the device |
| `-Monitor` | `--monitor` | Open the serial log afterwards |
| `-Device http://192.168.1.50` | `--device http://192.168.1.50` | Push the web UI to a running device over Wi-Fi (before flashing) |
| `-DeviceUser user:pass` | `--device-user user:pass` | Basic auth for `-Device`, if the web UI has a password |
| `-WebOnly` | `--web-only` | Only update the checkout and push the web UI; no build, no flash |
| `-Yes` | `--yes` | Assume yes for every confirmation prompt |

Run with `-?` / `--help` for the full list.

## Updating only the web UI

The pages under `sd-card/html/` live on the SD card, not in the firmware, so a
change to them needs no reflash. With the device on Wi-Fi:

```powershell
.\flash-esp32cam.ps1 -WebOnly -Device http://192.168.1.50
```

```bash
./flash-esp32cam.sh --web-only --device http://192.168.1.50
```

This replaces every file under the card's `html/` folder with the checkout's
copy, one file at a time through the device's own file server. Nothing outside
`html/` is touched: `config.ini`, `wlan.ini`, the models and the logs stay as
they are. Two details of the firmware make the per-file dance necessary: its
upload handler refuses to overwrite an existing file, and when both `page.html`
and `page.html.gz` exist it serves the `.gz` one — so each file is deleted under
both names before its new copy is uploaded. Hard-refresh the browser afterwards
(Ctrl+F5), it caches these pages aggressively.

Add `-Device` / `--device` to a normal run and the same push happens after the
build and before the USB flash, while the device is still up.

## Notes

- The **first run is slow**: it downloads the submodules (a few hundred MB) and
  the ESP-IDF toolchain (~1 GB). Later runs reuse both.
- The device is useless without its **SD card**. Either pass `-SdCard` /
  `--sd-card`, or copy the contents of `sd-card/` to a FAT32 card by hand and
  fill in `wlan.ini`.
- When asked for Wi-Fi credentials, the scripts write them into `wlan.ini` on
  the card. A double quote cannot be represented in that file's format, so a
  credential containing one is refused and the file left for you to edit.
- A **bare AI-Thinker module on a USB-TTL adapter** has no auto-reset: tie
  GPIO0 to GND and tap reset before the upload, then remove the jumper and
  reset again. Boards with onboard USB handle this themselves.
- Use a **data** USB cable. Charge-only cables enumerate no serial port, which
  looks exactly like a broken board.
