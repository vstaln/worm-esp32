# 🪱 Worm Life — Continuation Handoff

> Read this FIRST. It is the entire context for the next agent working on this
> project. Everything below is the current honest state as of 2026-08-13.

## What this project is

A C. elegans life simulation on an **ESP32** driving a **128×64 SSD1306 OLED**.
The worm's **full real 302-neuron connectome** (Cook et al. 2019, from OpenWorm
`c302` data: 446 nodes incl. muscles, 4,666 chemical + 2,577 gap junctions)
runs as a **graded-potential neural network** at 250 Hz on the ESP32. The
display shows the worm living: hunting food, getting hungry, dwelling to eat,
laying eggs, aging, dying (starvation or old age), being reborn.

**Honesty note (non-negotiable):** the connectome wiring, neurotransmitter
signs and muscle groups are REAL (from the published dataset). The dynamics
are a stylized approximation: connectomes give synapse *counts* not weights,
and C. elegans neurons are mostly non-spiking. We run a mean-centred sigmoid
model with fan-in-normalised weights.

## Hardware & wiring (verified working)

- ESP32 DevKit (38-pin) + SSD1306 128×64 I2C OLED
- OLED VCC → 3V3, GND → GND, SCL → GPIO 22, SDA → GPIO 21
- ESP32 plugs into a PC's USB (power + flashing). The phone (Android,
  unrooted, termux-usb dead) **cannot** flash it — use the laptop/PC.

## Current status — WHAT WORKS / WHAT'S BROKEN

### ✅ Verified working (host harness, no hardware needed)
Full life cycle simulated on the host: born → chemotaxis hunt (scale-invariant
weathervane steer `(L−R)/(L+R)` + pirouette reversals on smoothed odour drop)
→ dwell+eat (NSM/M4/M1/M5 pharyngeal pump circuit; **MC is unconnected in this
dataset — do not inject MC**) → satiate → roam + lay eggs (HSN → vulval) →
hatch (population 2) → age → die (old age ~245 s or starvation) → reborn.

### ⚠️ BROKEN ON HARDWARE: task watchdog crash every ~11 s
**Symptom** (seen on serial): boots ("🪱 worm life boot", world runs, eats),
then `E task_wdt: IDLE0 (CPU 0) ... Aborting` + reboot loop.

**Root cause:** the 1 kHz brain task overran its 1 ms budget — one full
connectome step measured **~2,725 µs** on the ESP32 (compiled `-Os`, branchy
two-stream loops, libm `expf`). `vTaskDelayUntil` overshot → brain task never
yielded → IDLE0 starved → FreeRTOS watchdog abort.

**Fix already applied (committed 69660de, NOT yet verified on hardware):**
1. Fast sigmoid: 256-entry LUT + linear interpolation (replaces `expf`)
2. Packed edge arrays: one contiguous 8-byte struct per edge (src,dst,w),
   built once at init; branchless chem/gap accumulation
3. `BRAIN_DT_MS 4` → brain runs at **250 Hz**, Euler increment dt-scaled
   (host-verified: identical life-cycle behaviour at dt=2 and dt=4)
4. `#pragma GCC optimize("O2","fast")` on `brain.cpp` ONLY (rest of core stays
   `-Os` — do NOT rebuild the whole core with different flags, it takes
   ~30 min on the phone and made the phone thermally crash-loop)
5. Fixed `brains_us` printf (`%.0f` on uint32 → `%lu`)

**NEXT AGENT'S #1 JOB:** build → flash → read serial for ≥60 s → confirm
(a) no watchdog, (b) `brains_us < 4000`. If still over budget, next lever is
running the brain at 250→125 Hz (BRAIN_DT_MS 8, re-verify host) or pruning
near-zero edges.

### 🚨 Phone environment is unstable
The Android phone was **crash-looping** (~2 min uptime cycles) under the heavy
`-O2` core rebuild — likely thermal. **Do all builds on the laptop/PC**, not
the phone. The phone's proot still holds the repo + toolchain
(`/root/worm-esp32`, arduino-cli + esp32 core 3.3.11 + U8g2 2.36.19 installed).

## How to build & flash (do this on the laptop/PC)

### Option A — laptop (Void, root, reachable from phone via `pc` ssh alias)
esptool is already installed there: `/opt/esptool/bin/esptool.py`.
arduino-cli is NOT yet installed on the laptop — install it:

```sh
# on the laptop (as root):
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
arduino-cli config init
arduino-cli core update-index
arduino-cli core install esp32:esp32      # ~1-2 GB download
arduino-cli lib install U8g2@2.36.19
```

Then build + flash (from the repo dir):

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir build firmware/worm_life
esptool.py --chip esp32 merge_bin -o build/worm_life_merged.bin \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 build/worm_life.ino.bootloader.bin \
  0x8000 build/worm_life.ino.partitions.bin \
  0xe000 ~/.arduino15/packages/esp32/hardware/esp32/3.3.11/tools/partitions/boot_app0.bin \
  0x10000 build/worm_life.ino.bin
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash -z 0x0 build/worm_life_merged.bin
# watch serial:
/opt/esptool/bin/python3 -c "import serial,time; p=serial.Serial('/dev/ttyUSB0',115200); p.setDTR(0); p.setRTS(1); time.sleep(.1); p.setDTR(1); p.setRTS(0); [print(p.readline().decode('utf8','replace'),end='') for _ in range(400)]"
```

### Option B — phone proot (fallback, only if phone is stable)
```sh
cd /root/worm-esp32
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir build firmware/worm_life
# ... same merge/flash, or use scripts/flash_laptop.sh from the laptop
```

## Host simulation (fast iteration loop, no hardware)

```sh
cd /root/worm-esp32
g++ -O2 -I tools/host_stubs -I firmware/worm_life \
    tools/host_test/main.c firmware/worm_life/brain.cpp firmware/worm_life/world.cpp \
    firmware/worm_life/connectome_data.c -lm -o /tmp/worm_test
/tmp/worm_test 120000      # 120 s of worm life, prints 20-sample trace + summary
```
Tune in `firmware/worm_life/world.h` (speeds/gains/metabolism/food/eggs),
`world.cpp` (behaviour), `brain.cpp` (network params via `BrainParams`).
Always re-run the host trace after a change; then rebuild firmware.

## Repository layout

```
firmware/worm_life/     Arduino sketch (brain, world, render, main .ino)
  brain.cpp/.h          graded network over the full connectome (HOT PATH)
  world.cpp/.h          arena, food, hunger, eggs, death, rebirth
  render.cpp/.h         SSD1306 renderer (SDA 21 / SCL 22)
  connectome_data.c/.h  generated connectome arrays (do not edit by hand)
tools/
  convert_connectome.py downloads + converts connectome -> C arrays
  host_test/main.c      host harness
  host_stubs/           esp_random/esp_timer stubs for host builds
build/                  compiled outputs + merged flash image
scripts/flash_laptop.sh one-shot flasher (laptop)
```

## GitHub

Repo **already created**: `vstaln/worm-esp32` (public). Needs the initial
push (the `gh repo create --push` was interrupted by the phone rebooting):

```sh
cd /root/worm-esp32          # or on the laptop
git remote add origin git@github.com:vstaln/worm-esp32.git  # or https
git push -u origin main
```
gh CLI is authenticated as `vstaln` (full scopes) in the phone proot.
MIT license + LICENSE file already in the repo. Commit author: Vstalin Grady.

## Known quirks / gotchas

- **MC (pharyngeal pacemaker) has ZERO edges in this dataset** — injecting it
  does nothing. Eating is driven by NSM/M4/M1/M5.
- The laptop's esptool lives in a venv: `/opt/esptool/bin/esptool.py`.
- Phone `pkill -f arduino-cli` kills your own shell too (cmdline match) —
  use exact process names.
- arduino-cli sketch cache is per-flags; deleting it forces a full core
  rebuild (~30 min on phone, thermal-crashy). Don't.
- ESP32 defaults: SSD1306 I2C address 0x3C; if the screen stays blank try
  0x3D (one-line change in render.cpp).

## Current tunables (world.h)

BASE_SPEED 26, TURN_GAIN 0.15, STEER_GAIN 1.3, REV_TURN 2.2, SENS_ANG 0.6,
SENS_DIST 6, MAX_FOOD 4, MAX_EGGS 8, MAX_WORMS 2, metabolism 2.0/s,
lifespan 150–300 s, starvation death after 8 s at hunger 0.
