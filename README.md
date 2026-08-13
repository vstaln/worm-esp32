# 🪱 Worm Life — C. elegans on an ESP32 + OLED

A tiny worm lives a full life on a 128×64 OLED, driven by its **real
302-neuron connectome** (Cook et al. 2019, via OpenWorm's c302 data) running
as a live graded-potential neural network at 1 kHz on the ESP32.

The worm **hunts food, gets hungry, dwells and eats, lays eggs, ages, and
dies** — of starvation or old age — then is reborn. Two worms share the
arena; eggs hatch into new worms.

## Hardware

- ESP32 (any DevKit — the dual-core 240 MHz + FPU + 320 KB SRAM is plenty)
- 128×64 SSD1306 OLED, I2C — SDA = GPIO 21, SCL = GPIO 22 (edit
  `firmware/worm_life/render.cpp` to change), powered from 3V3

## What's real, what's stylised (honesty note)

- **Real**: the complete hermaphrodite connectome — 446 nodes (302 neurons
  + body-wall/pharyngeal/vulval muscles), 4,666 chemical + 2,577 gap
  junctions, neurotransmitter signs (GABA → inhibitory), pharyngeal pump
  circuit (NSM/M4/M1/M5), egg-laying circuit (HSN → vulval muscles).
- **Stylised**: connectomes give synapse *counts*, not weights; C. elegans
  neurons are mostly non-spiking. We run a graded (Izhikevich-free)
  sigmoid model with fan-in normalized weights — the wiring is real, the
  dynamics are a lively approximation.

## Layout

```
firmware/worm_life/     Arduino sketch (brain, world, render, main)
  brain.cpp/.h          the 302-neuron graded network, 1 kHz
  world.cpp/.h          arena, food, hunger, eggs, death, rebirth
  render.cpp/.h         SSD1306: wiggly worm + food + status line
  connectome_data.c/.h  baked connectome arrays (generated)
tools/
  convert_connectome.py downloads + converts the connectome to C arrays
  host_test/            desktop harness: run the full life sim on a PC
build/                  compiled firmware + merged flash image
scripts/flash_laptop.sh one-shot esptool flasher (run on a computer)
```

## Building

Toolchain lives in the proot Ubuntu container (phone) — `arduino-cli`,
ESP32 core 3.3.11, U8g2:

```sh
cd /root/worm-esp32
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir build firmware/worm_life
```

## Host simulation (no hardware needed)

```sh
g++ -O2 -I tools/host_stubs -I firmware/worm_life \
    tools/host_test/main.c firmware/worm_life/brain.cpp \
    firmware/worm_life/world.cpp firmware/worm_life/connectome_data.c \
    -lm -o /tmp/worm_test
/tmp/worm_test 300000        # simulate 300 s of worm life, print trace
```

Verified behaviour (host): hunt → dwell-eat → satiate → roam + lay eggs →
hatch → age → die (old age ~245 s or starvation) → reborn. Population 2,
8-egg cap, 4 food patches, metabolism 2.0/s.

## Flashing

The phone (unrooted, no termux-usb) cannot flash the ESP directly. Options:

1. **Laptop (recommended)** — plug the ESP32 into the Void laptop,
   transfer `build/worm_life_merged.bin`, then:
   ```sh
   pip install esptool
   ./scripts/flash_laptop.sh        # auto-detects the port
   ```
2. **Any desktop Chrome** — web.espressif.com or a Web-Serial esptool page;
   no drivers needed, same merged binary.
3. **WiFi OTA** — after one serial flash, future updates can go over the
   network; not yet wired into the sketch.

## Tuning knobs

- `firmware/worm_life/world.h` — speeds, gains, hunger metabolism, food
  count, egg cap, lifespan
- `firmware/worm_life/brain.cpp` — network params (gain, tonic, bias,
  noise) via `BrainParams`
- Host harness is the fast loop: tune → `/tmp/worm_test 300000` → check
  the trace → then rebuild firmware.
