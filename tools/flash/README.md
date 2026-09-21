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
| `-Yes` | `--yes` | Assume yes for every confirmation prompt |

Run with `-?` / `--help` for the full list.

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
