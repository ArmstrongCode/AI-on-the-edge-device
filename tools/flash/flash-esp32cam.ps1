<#
.SYNOPSIS
    Clone, build and flash AI-on-the-edge-device onto an ESP32-CAM (Windows).

.DESCRIPTION
    One-shot helper: fetches the repo at the requested branch, initialises the
    submodules, builds the esp32cam environment with PlatformIO, detects the
    serial port and uploads. Optionally prepares the SD card as well.

    Run it from the PlatformIO terminal in VS Code, or from any PowerShell
    window -- the PlatformIO CLI is located automatically under ~\.platformio.

.EXAMPLE
    .\flash-esp32cam.ps1
    Clone/update, build, then prompt before erasing and uploading.

.EXAMPLE
    .\flash-esp32cam.ps1 -Port COM5 -Erase -SdCard E:\ -Monitor
    Non-interactive port, erase first, prepare the SD card, open the log after.

.EXAMPLE
    .\flash-esp32cam.ps1 -BuildOnly
    Compile only; never touches the device.
#>
[CmdletBinding()]
param(
    # Where to clone to. Kept short by default: ESP-IDF builds generate deep
    # paths and Windows' MAX_PATH limit bites on long parent directories.
    [string]$Path = (Join-Path $env:USERPROFILE 'aiotedge'),

    [string]$Branch = 'claude/wonderful-carson-1v9sea',

    [string]$RepoUrl = 'https://github.com/ArmstrongCode/AI-on-the-edge-device.git',

    # Serial port, e.g. COM5. Auto-detected when omitted.
    [string]$Port,

    # Fallback for flaky CH340/CP2102 adapters, e.g. 115200.
    [int]$UploadSpeed,

    # Drive or folder to copy the sd-card/ contents to, e.g. E:\
    [string]$SdCard,

    # Erase the whole flash before uploading. Wipes stored config.
    [switch]$Erase,

    # Compile only, no device access.
    [switch]$BuildOnly,

    # Open the serial log once the upload succeeds.
    [switch]$Monitor,

    # Assume yes for every confirmation prompt.
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$Message) Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Write-Note { param([string]$Message) Write-Host "    $Message" -ForegroundColor DarkGray }

function Confirm-Step {
    param([string]$Question, [bool]$DefaultYes = $false)
    if ($Yes) { return $true }
    $suffix = if ($DefaultYes) { '[Y/n]' } else { '[y/N]' }
    $answer = Read-Host "$Question $suffix"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $DefaultYes }
    return $answer -match '^\s*y'
}

# Run an external command and stop the script if it reports failure.
function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    Write-Note "$Exe $($Arguments -join ' ')"
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Exe $($Arguments -join ' ')' failed with exit code $LASTEXITCODE."
    }
}

function Resolve-Tool {
    param([string[]]$Names, [string[]]$Fallbacks, [string]$Hint)
    foreach ($name in $Names) {
        $found = Get-Command $name -ErrorAction SilentlyContinue
        if ($found) { return $found.Source }
    }
    foreach ($candidate in $Fallbacks) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    throw $Hint
}

# --- Locate the tools -------------------------------------------------------

Write-Step 'Locating git and PlatformIO'

$git = Resolve-Tool -Names @('git') -Fallbacks @() -Hint @'
git was not found on PATH. Install it from https://git-scm.com/download/win
and reopen the terminal.
'@

$penv = Join-Path $env:USERPROFILE '.platformio\penv\Scripts'
$pio = Resolve-Tool -Names @('pio', 'platformio') -Fallbacks @(
    (Join-Path $penv 'platformio.exe'),
    (Join-Path $penv 'pio.exe')
) -Hint @'
The PlatformIO CLI was not found. Open VS Code, wait for the PlatformIO IDE
extension to finish its first-run install, then run this script again from the
PlatformIO terminal (the terminal icon in the bottom status bar).
'@

Write-Note "git:        $git"
Write-Note "platformio: $pio"

# --- Fetch the source -------------------------------------------------------

$gitDir = Join-Path $Path '.git'

if (Test-Path $gitDir) {
    Write-Step "Updating existing checkout at $Path"
    Invoke-Checked $git @('-C', $Path, 'fetch', 'origin', $Branch)
    Invoke-Checked $git @('-C', $Path, 'checkout', '-B', $Branch, "origin/$Branch")
} elseif (Test-Path $Path) {
    throw "$Path already exists but is not a git checkout. Remove it or pass -Path <other-dir>."
} else {
    Write-Step "Cloning $RepoUrl ($Branch) into $Path"
    # core.longpaths keeps deep ESP-IDF component paths from tripping MAX_PATH.
    Invoke-Checked $git @('-c', 'core.longpaths=true', 'clone', '--branch', $Branch, $RepoUrl, $Path)
}

Write-Step 'Initialising submodules (esp-tflite-micro, esp32-camera, ...)'
Write-Note 'First run downloads a few hundred MB; be patient.'
Invoke-Checked $git @('-C', $Path, 'submodule', 'update', '--init', '--recursive')

# --- Build ------------------------------------------------------------------

$codeDir = Join-Path $Path 'code'
if (-not (Test-Path (Join-Path $codeDir 'platformio.ini'))) {
    throw "No platformio.ini under $codeDir -- the checkout looks incomplete."
}

if ($UploadSpeed) {
    Write-Note "Overriding upload speed: $UploadSpeed baud"
    $env:PLATFORMIO_UPLOAD_SPEED = "$UploadSpeed"
}

Write-Step 'Building firmware (esp32cam)'
Write-Note 'The first build also downloads the ESP-IDF toolchain (~1 GB).'
Invoke-Checked $pio @('run', '-e', 'esp32cam', '-d', $codeDir)

$buildDir = Join-Path $codeDir '.pio\build\esp32cam'
Write-Note "Artifacts: $buildDir"

# --- SD card ----------------------------------------------------------------

if ($SdCard) {
    Write-Step "Preparing SD card at $SdCard"
    if (-not (Test-Path $SdCard)) { throw "SD card path $SdCard not found." }

    $warning = "This overwrites config/, html/ and wlan.ini on $SdCard with the repo versions."
    Write-Host "    $warning" -ForegroundColor Yellow
    if (Confirm-Step 'Continue?' $false) {
        $source = Join-Path $Path 'sd-card\*'
        Copy-Item -Path $source -Destination $SdCard -Recurse -Force
        Write-Note 'Copied.'

        # Fill in Wi-Fi credentials if they are still blank.
        $ini = Join-Path $SdCard 'wlan.ini'
        if ((Test-Path $ini) -and ((Get-Content $ini -Raw) -match '(?m)^\s*ssid\s*=\s*""')) {
            if (Confirm-Step 'wlan.ini has no Wi-Fi credentials. Set them now?' $true) {
                $ssid = Read-Host 'Wi-Fi SSID'
                $secure = Read-Host 'Wi-Fi password' -AsSecureString
                $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
                try {
                    $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
                } finally {
                    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
                }

                # wlan.ini quotes values and has no escape syntax, so a literal
                # double quote cannot be represented. Bail out rather than write
                # a line the firmware would misparse.
                if ($ssid.Contains('"') -or $password.Contains('"')) {
                    Write-Host '    SSID/password contains a double quote, which wlan.ini cannot represent.' -ForegroundColor Yellow
                    Write-Host "    Left wlan.ini unchanged -- edit $ini by hand." -ForegroundColor Yellow
                } else {
                    # Rewrite line by line so odd characters in the password are
                    # never treated as regex replacement syntax.
                    $updated = foreach ($line in (Get-Content $ini)) {
                        if ($line -match '^\s*ssid\s*=')         { 'ssid = "{0}"' -f $ssid }
                        elseif ($line -match '^\s*password\s*=') { 'password = "{0}"' -f $password }
                        else                                     { $line }
                    }
                    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
                    [System.IO.File]::WriteAllLines($ini, [string[]]$updated, $utf8NoBom)
                    Write-Note 'wlan.ini updated.'
                }
            }
        }
    } else {
        Write-Note 'Skipped.'
    }
}

if ($BuildOnly) {
    Write-Step 'Build complete (-BuildOnly, device untouched).'
    exit 0
}

# --- Find the serial port ---------------------------------------------------

if (-not $Port) {
    Write-Step 'Detecting serial port'
    $listing = & $pio device list --json-output 2>$null | Out-String
    if ([string]::IsNullOrWhiteSpace($listing)) {
        throw "'$pio device list' returned nothing. Pass the port explicitly: -Port COM5"
    }
    $devices = $listing | ConvertFrom-Json
    $candidates = @($devices | Where-Object {
        $_.hwid -match 'USB' -or $_.port -match 'USB'
    })

    if ($candidates.Count -eq 0) {
        throw @'
No USB serial port found. Check that:
  - the board is plugged in with a DATA cable (charge-only cables are silent),
  - the USB-serial driver is installed (CP2102 / CH340 / FTDI),
  - no other program (a serial monitor, Arduino IDE) is holding the port.
Then rerun, or pass the port explicitly: -Port COM5
'@
    } elseif ($candidates.Count -eq 1) {
        $Port = $candidates[0].port
        Write-Note "Found $Port ($($candidates[0].description))"
    } else {
        Write-Host '    Multiple ports found:' -ForegroundColor Yellow
        for ($i = 0; $i -lt $candidates.Count; $i++) {
            Write-Host ("      [{0}] {1}  {2}" -f $i, $candidates[$i].port, $candidates[$i].description)
        }
        $choice = Read-Host '    Which one? (number)'
        $index = 0
        if (-not [int]::TryParse($choice, [ref]$index) -or $index -lt 0 -or $index -ge $candidates.Count) {
            throw "Invalid selection '$choice'."
        }
        $Port = $candidates[$index].port
    }
}

Write-Note "Using port: $Port"
Write-Note 'Bare AI-Thinker module on a USB-TTL adapter? Tie GPIO0 to GND and tap reset now.'

# --- Erase + upload ---------------------------------------------------------

$doErase = $Erase
if (-not $doErase) {
    $doErase = Confirm-Step 'Erase flash first? (recommended on a first flash; wipes stored config)' $false
}

if ($doErase) {
    Write-Step "Erasing flash on $Port"
    Invoke-Checked $pio @('run', '-t', 'erase', '-e', 'esp32cam', '-d', $codeDir, '--upload-port', $Port)
}

Write-Step "Uploading to $Port"
Invoke-Checked $pio @('run', '-t', 'upload', '-e', 'esp32cam', '-d', $codeDir, '--upload-port', $Port)

Write-Step 'Flash complete.'
Write-Note 'Remove the GPIO0 jumper (if any), insert the prepared SD card, then reset the board.'
if (-not $SdCard) {
    Write-Note "SD card not prepared. Copy the contents of $Path\sd-card to a FAT32 card and set your Wi-Fi in wlan.ini."
}

if ($Monitor -or (Confirm-Step 'Open the serial log now?' $true)) {
    Write-Step "Monitoring $Port at 115200 baud (Ctrl+C to quit)"
    & $pio device monitor -p $Port -b 115200
}
