#!/usr/bin/env bash
#
# Clone, build and flash AI-on-the-edge-device onto an ESP32-CAM (macOS/Linux).
#
# One-shot helper: fetches the repo at the requested branch, initialises the
# submodules, builds the esp32cam environment with PlatformIO, detects the
# serial port and uploads. Optionally prepares the SD card as well.
#
# Run it from the PlatformIO terminal in VS Code, or from any shell -- the
# PlatformIO CLI is located automatically under ~/.platformio.
#
#   ./flash-esp32cam.sh                      # build, then prompt before flashing
#   ./flash-esp32cam.sh --port /dev/ttyUSB0 --erase --monitor
#   ./flash-esp32cam.sh --sd-card /Volumes/NO\ NAME
#   ./flash-esp32cam.sh --build-only         # compile only, device untouched

set -euo pipefail

# Kept short by default; ESP-IDF builds generate deep paths.
REPO_PATH="$HOME/aiotedge"
BRANCH="claude/wonderful-carson-1v9sea"
REPO_URL="https://github.com/ArmstrongCode/AI-on-the-edge-device.git"
PORT=""
UPLOAD_SPEED=""
SD_CARD=""
DO_ERASE=0
BUILD_ONLY=0
DO_MONITOR=0
ASSUME_YES=0

usage() {
    sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'USAGE'

Options:
  --path DIR          Where to clone to          (default: ~/aiotedge)
  --branch NAME       Branch to build            (default: claude/wonderful-carson-1v9sea)
  --repo URL          Repository URL
  --port DEV          Serial port; auto-detected when omitted
  --upload-speed N    Fallback baud for flaky CH340/CP2102 adapters, e.g. 115200
  --sd-card PATH      Copy sd-card/ contents to this mounted card
  --erase             Erase the whole flash before uploading (wipes stored config)
  --build-only        Compile only, never touch the device
  --monitor           Open the serial log after a successful upload
  --yes               Assume yes for every confirmation prompt
  -h, --help          Show this help
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --path)         REPO_PATH="$2"; shift 2 ;;
        --branch)       BRANCH="$2"; shift 2 ;;
        --repo)         REPO_URL="$2"; shift 2 ;;
        --port)         PORT="$2"; shift 2 ;;
        --upload-speed) UPLOAD_SPEED="$2"; shift 2 ;;
        --sd-card)      SD_CARD="$2"; shift 2 ;;
        --erase)        DO_ERASE=1; shift ;;
        --build-only)   BUILD_ONLY=1; shift ;;
        --monitor)      DO_MONITOR=1; shift ;;
        --yes)          ASSUME_YES=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        *)              echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -t 1 ]; then
    C_STEP=$'\033[36m'; C_NOTE=$'\033[90m'; C_WARN=$'\033[33m'; C_OFF=$'\033[0m'
else
    C_STEP=""; C_NOTE=""; C_WARN=""; C_OFF=""
fi

step() { printf '\n%s==> %s%s\n' "$C_STEP" "$1" "$C_OFF"; }
note() { printf '%s    %s%s\n' "$C_NOTE" "$1" "$C_OFF"; }
warn() { printf '%s    %s%s\n' "$C_WARN" "$1" "$C_OFF"; }
die()  { printf '\nError: %s\n' "$1" >&2; exit 1; }

# $1 = question, $2 = "y" when the default is yes
confirm() {
    [ "$ASSUME_YES" -eq 1 ] && return 0
    local suffix='[y/N]' answer
    [ "${2:-n}" = "y" ] && suffix='[Y/n]'
    read -r -p "    $1 $suffix " answer || answer=""
    if [ -z "$answer" ]; then
        [ "${2:-n}" = "y" ]
        return
    fi
    case "$answer" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

run() {
    note "$*"
    "$@"
}

# --- Locate the tools -------------------------------------------------------

step "Locating git and PlatformIO"

command -v git >/dev/null 2>&1 || die "git was not found on PATH. Install it and try again."

PIO=""
for candidate in pio platformio "$HOME/.platformio/penv/bin/pio" "$HOME/.platformio/penv/bin/platformio"; do
    if command -v "$candidate" >/dev/null 2>&1; then PIO="$(command -v "$candidate")"; break; fi
    if [ -x "$candidate" ]; then PIO="$candidate"; break; fi
done
[ -n "$PIO" ] || die "The PlatformIO CLI was not found.
Open VS Code, wait for the PlatformIO IDE extension to finish its first-run
install, then rerun this script from the PlatformIO terminal."

# Python ships with PlatformIO's virtualenv; used to parse the port listing.
PY=""
for candidate in "$HOME/.platformio/penv/bin/python" python3; do
    if [ -x "$candidate" ] || command -v "$candidate" >/dev/null 2>&1; then PY="$candidate"; break; fi
done

note "git:        $(command -v git)"
note "platformio: $PIO"

# --- Fetch the source -------------------------------------------------------

if [ -d "$REPO_PATH/.git" ]; then
    step "Updating existing checkout at $REPO_PATH"
    run git -C "$REPO_PATH" fetch origin "$BRANCH"
    run git -C "$REPO_PATH" checkout -B "$BRANCH" "origin/$BRANCH"
elif [ -e "$REPO_PATH" ]; then
    die "$REPO_PATH already exists but is not a git checkout. Remove it or pass --path <other-dir>."
else
    step "Cloning $REPO_URL ($BRANCH) into $REPO_PATH"
    run git clone --branch "$BRANCH" "$REPO_URL" "$REPO_PATH"
fi

step "Initialising submodules (esp-tflite-micro, esp32-camera, ...)"
note "First run downloads a few hundred MB; be patient."
run git -C "$REPO_PATH" submodule update --init --recursive

# --- Build ------------------------------------------------------------------

CODE_DIR="$REPO_PATH/code"
[ -f "$CODE_DIR/platformio.ini" ] || die "No platformio.ini under $CODE_DIR -- the checkout looks incomplete."

if [ -n "$UPLOAD_SPEED" ]; then
    note "Overriding upload speed: $UPLOAD_SPEED baud"
    export PLATFORMIO_UPLOAD_SPEED="$UPLOAD_SPEED"
fi

step "Building firmware (esp32cam)"
note "The first build also downloads the ESP-IDF toolchain (~1 GB)."
run "$PIO" run -e esp32cam -d "$CODE_DIR"
note "Artifacts: $CODE_DIR/.pio/build/esp32cam"

# --- SD card ----------------------------------------------------------------

if [ -n "$SD_CARD" ]; then
    step "Preparing SD card at $SD_CARD"
    [ -d "$SD_CARD" ] || die "SD card path $SD_CARD not found."
    warn "This overwrites config/, html/ and wlan.ini on $SD_CARD with the repo versions."
    if confirm "Continue?" n; then
        # Trailing /. copies the directory contents, not the directory itself.
        cp -R "$REPO_PATH/sd-card/." "$SD_CARD/"
        note "Copied."

        INI="$SD_CARD/wlan.ini"
        if [ -f "$INI" ] && grep -Eq '^[[:space:]]*ssid[[:space:]]*=[[:space:]]*""' "$INI"; then
            if confirm "wlan.ini has no Wi-Fi credentials. Set them now?" y; then
                read -r -p "    Wi-Fi SSID: " WIFI_SSID
                read -r -s -p "    Wi-Fi password: " WIFI_PASS; echo
                # wlan.ini quotes values and has no escape syntax, so a literal
                # double quote cannot be represented. Bail out rather than
                # write a line the firmware would misparse.
                case "$WIFI_SSID$WIFI_PASS" in
                    *'"'*)
                        warn "SSID/password contains a double quote, which wlan.ini cannot represent."
                        warn "Left wlan.ini unchanged -- edit $INI by hand."
                        ;;
                    *)
                        # awk -v keeps odd characters out of any pattern/replacement syntax.
                        awk -v s="$WIFI_SSID" -v p="$WIFI_PASS" '
                            /^[[:space:]]*ssid[[:space:]]*=/     { print "ssid = \"" s "\""; next }
                            /^[[:space:]]*password[[:space:]]*=/ { print "password = \"" p "\""; next }
                            { print }
                        ' "$INI" > "$INI.tmp" && mv "$INI.tmp" "$INI"
                        note "wlan.ini updated."
                        ;;
                esac
            fi
        fi
    else
        note "Skipped."
    fi
fi

if [ "$BUILD_ONLY" -eq 1 ]; then
    step "Build complete (--build-only, device untouched)."
    exit 0
fi

# --- Find the serial port ---------------------------------------------------

if [ -z "$PORT" ]; then
    step "Detecting serial port"
    [ -n "$PY" ] || die "Could not find a Python interpreter to parse the port list. Pass --port explicitly."

    PORT_LIST="$("$PIO" device list --json-output 2>/dev/null | "$PY" -c '
import json, sys

try:
    devices = json.load(sys.stdin)
except Exception:
    sys.exit(0)

hints = ("usbserial", "wchusb", "SLAB", "ttyUSB", "ttyACM", "usbmodem")
for device in devices:
    port = device.get("port") or ""
    hwid = device.get("hwid") or ""
    if "USB" in hwid.upper() or any(h in port for h in hints):
        print("%s\t%s" % (port, device.get("description") or ""))
')"

    # bash 3.2 (macOS default) has no mapfile, so build the array by hand.
    CANDIDATES=()
    while IFS= read -r line; do
        [ -n "$line" ] && CANDIDATES[${#CANDIDATES[@]}]="$line"
    done <<< "$PORT_LIST"

    if [ "${#CANDIDATES[@]}" -eq 0 ]; then
        die "No USB serial port found. Check that:
  - the board is plugged in with a DATA cable (charge-only cables are silent),
  - the USB-serial driver is installed (CP2102 / CH340 / FTDI),
  - no other program (a serial monitor, Arduino IDE) is holding the port.
Then rerun, or pass the port explicitly: --port /dev/ttyUSB0"
    elif [ "${#CANDIDATES[@]}" -eq 1 ]; then
        PORT="${CANDIDATES[0]%%$'\t'*}"
        note "Found ${CANDIDATES[0]}"
    else
        warn "Multiple ports found:"
        i=0
        while [ "$i" -lt "${#CANDIDATES[@]}" ]; do
            printf '      [%d] %s\n' "$i" "${CANDIDATES[$i]}"
            i=$((i + 1))
        done
        read -r -p "    Which one? (number) " choice
        case "$choice" in
            ''|*[!0-9]*) die "Invalid selection '$choice'." ;;
        esac
        [ "$choice" -lt "${#CANDIDATES[@]}" ] || die "Invalid selection '$choice'."
        PORT="${CANDIDATES[$choice]%%$'\t'*}"
    fi
fi

note "Using port: $PORT"
note "Bare AI-Thinker module on a USB-TTL adapter? Tie GPIO0 to GND and tap reset now."

# --- Erase + upload ---------------------------------------------------------

if [ "$DO_ERASE" -eq 0 ]; then
    if confirm "Erase flash first? (recommended on a first flash; wipes stored config)" n; then
        DO_ERASE=1
    fi
fi

if [ "$DO_ERASE" -eq 1 ]; then
    step "Erasing flash on $PORT"
    run "$PIO" run -t erase -e esp32cam -d "$CODE_DIR" --upload-port "$PORT"
fi

step "Uploading to $PORT"
run "$PIO" run -t upload -e esp32cam -d "$CODE_DIR" --upload-port "$PORT"

step "Flash complete."
note "Remove the GPIO0 jumper (if any), insert the prepared SD card, then reset the board."
if [ -z "$SD_CARD" ]; then
    note "SD card not prepared. Copy the contents of $REPO_PATH/sd-card to a FAT32 card and set your Wi-Fi in wlan.ini."
fi

if [ "$DO_MONITOR" -eq 1 ] || confirm "Open the serial log now?" y; then
    step "Monitoring $PORT at 115200 baud (Ctrl+C to quit)"
    "$PIO" device monitor -p "$PORT" -b 115200
fi
