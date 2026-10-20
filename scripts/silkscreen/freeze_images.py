"""Freeze built Silkscreen firmware images into self-contained flash folders.

Usage (from anywhere):
  python -I -X utf8 scripts/silkscreen/freeze_images.py [--repo crosspoint-reader|crossink] [--prefix P] [--build] env [env ...]

For each env it creates silkscreen-firmware/flash/<P><env>/ with the four pieces `pio run -t upload` writes:
  bootloader.bin (0x0), partitions.bin (0x8000), boot_app0.bin (0xe000), <P><env>.bin (0x10000)
plus flash_args (`esptool --chip esp32s3 write-flash @flash_args`), SHA256SUMS.txt, BUILD.txt and a one-step
flash-<P><env>.ps1 that uses PlatformIO's esptool. The previous app image of that folder is kept as <P><env>-prev.bin.
It copies what is in .pio/build/<env>/ (build first with `pio run -e <env>`, or pass --build to run it here, one env at
a time and not beside another pio run: use scripts/silkscreen/pio_locked.sh). Nothing outside flash/ is touched.
boot_app0.bin and the bootloader/partitions of flash/silkscreen-test are the verified set: a build whose own
bootloader or partition table differs from it is reported (the images are still written from the build).
"""
import argparse, hashlib, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))  # silkscreen-firmware
FLASH = ROOT + "/flash"
VERIFIED = FLASH + "/silkscreen-test"

PS1 = r'''# Flashes the Silkscreen firmware image "{name}" ({repo} {head}, frozen {stamp}) from the four binaries next
# to this script, with the esptool that PlatformIO already installed. It writes the same four pieces that
# `pio run -t upload` writes:
#   0x0     bootloader.bin     the second-stage bootloader (ESP32-S3: at offset 0)
#   0x8000  partitions.bin     the partition table (nvs, otadata, app0, app1, spiffs, coredump)
#   0xe000  boot_app0.bin      the boot-slot selector (otadata reset: boot the first app slot)
#   0x10000 {name}.bin
# The NVS area (0x9000-0xDFFF) is left alone. Settings, books and the battery log live on the SD card and are
# not touched.
#
# "invalid header: 0xffffffff" repeating on the serial console means the chip's ROM found NO bootloader at
# offset 0 (erased flash): this script writes all four pieces, so it fixes that. -AppOnly writes just the boot
# selector and the app, for a board that already runs a Silkscreen/CrossPoint/CrossInk image.
#
# Close any serial monitor first. Plug the board in with USB (hold POWER to wake it if it sleeps).
#
# Run it from any folder:
#   powershell -ExecutionPolicy Bypass -File .\flash-{name}.ps1
# Optional port (esptool finds the board by itself when omitted):
#   powershell -ExecutionPolicy Bypass -File .\flash-{name}.ps1 -Port COM5
# Without this script: esptool --chip esp32s3 write-flash @flash_args   (run inside this folder)
param([string]$Port = "", [switch]$AppOnly)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$py   = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$et   = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
$bl   = Join-Path $here "bootloader.bin"
$pt   = Join-Path $here "partitions.bin"
$boot = Join-Path $here "boot_app0.bin"
$app  = Join-Path $here "{name}.bin"

foreach ($f in @($py, $et, $bl, $pt, $boot, $app)) {{
  if (-not (Test-Path $f)) {{ Write-Error "missing: $f"; exit 2 }}
}}

$flashArgs = @($et, "--chip", "esp32s3")
if ($Port -ne "") {{ $flashArgs += @("--port", $Port) }}
if ($AppOnly) {{
  $flashArgs += @("write-flash", "0xe000", $boot, "0x10000", $app)
}} else {{
  $flashArgs += @("write-flash", "0x0", $bl, "0x8000", $pt, "0xe000", $boot, "0x10000", $app)
}}

& $py @flashArgs
exit $LASTEXITCODE
'''


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def freeze(repo, env, prefix, head):
    b = f"{ROOT}/{repo}/.pio/build/{env}"
    name = prefix + env
    out = f"{FLASH}/{name}"
    os.makedirs(out, exist_ok=True)
    for f in ("bootloader.bin", "partitions.bin", "firmware.bin"):
        if not os.path.exists(f"{b}/{f}"):
            sys.exit(f"missing {b}/{f}: build {env} first")
    if os.path.exists(f"{out}/{name}.bin"):
        shutil.copyfile(f"{out}/{name}.bin", f"{out}/{name}-prev.bin")
    for f in ("bootloader.bin", "partitions.bin"):
        if os.path.exists(f"{VERIFIED}/{f}"):
            same = sha(f"{b}/{f}") == sha(f"{VERIFIED}/{f}")
            print(f"[{name}] {f}: the build's own {'equals' if same else 'DIFFERS FROM'} the verified copy", flush=True)
        shutil.copyfile(f"{b}/{f}", f"{out}/{f}")
    if not (os.path.exists(f"{out}/boot_app0.bin") and os.path.exists(f"{VERIFIED}/boot_app0.bin")
            and os.path.samefile(f"{VERIFIED}/boot_app0.bin", f"{out}/boot_app0.bin")):
        shutil.copyfile(f"{VERIFIED}/boot_app0.bin", f"{out}/boot_app0.bin")
    shutil.copyfile(f"{b}/firmware.bin", f"{out}/{name}.bin")
    with open(f"{out}/flash_args", "w", newline="\n") as f:
        f.write("--flash-mode dio --flash-freq 80m --flash-size 16MB\n")
        f.write(f"0x0 bootloader.bin\n0x8000 partitions.bin\n0xe000 boot_app0.bin\n0x10000 {name}.bin\n")
    with open(f"{out}/SHA256SUMS.txt", "w", newline="\n") as f:
        for n in ("bootloader.bin", "partitions.bin", "boot_app0.bin", f"{name}.bin"):
            f.write(sha(f"{out}/{n}") + "  " + n + "\n")
    with open(f"{out}/BUILD.txt", "w", newline="\n") as f:
        f.write(f"{name}: {repo} {head}, frozen {time.strftime('%Y-%m-%d %H:%M')}, app {os.path.getsize(f'{out}/{name}.bin')} bytes\n")
    with open(f"{out}/flash-{name}.ps1", "w", encoding="utf-8", newline="\r\n") as f:
        f.write(PS1.format(name=name, repo=repo, head=head, stamp=time.strftime("%Y-%m-%d")))
    print(f"[{name}] frozen -> {out}  app {os.path.getsize(f'{out}/{name}.bin')} B sha256 {sha(f'{out}/{name}.bin')[:16]}",
          flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default="crosspoint-reader")
    ap.add_argument("--prefix", default="", help="folder/app name prefix, e.g. crossink-")
    ap.add_argument("--build", action="store_true", help="run `pio run -e env` first (not beside another pio run)")
    ap.add_argument("envs", nargs="+")
    a = ap.parse_args()
    head = subprocess.run(["git", "-C", f"{ROOT}/{a.repo}", "rev-parse", "--short", "HEAD"], capture_output=True,
                          text=True).stdout.strip()
    envvars = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
    for env in a.envs:
        if a.build:
            t0 = time.time()
            print(f"[{env}] building ...", flush=True)
            rc = subprocess.run(["pio", "run", "-d", f"{ROOT}/{a.repo}", "-e", env], env=envvars).returncode
            print(f"[{env}] pio exit {rc} after {time.time()-t0:.0f}s", flush=True)
            if rc != 0:
                sys.exit(f"build of {env} failed")
        freeze(a.repo, env, a.prefix, head)


main()
