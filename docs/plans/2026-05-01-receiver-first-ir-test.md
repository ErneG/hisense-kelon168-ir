# Receiver-First IR Test Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a minimal Arduino test project that proves whether the Hisense
remote decodes as `KELON168`, then replays a captured state through the same
GPIO after swapping the receiver for a transmitter.

**Architecture:** Two independent Arduino sketches avoid mixing capture and
transmit hardware on the same GPIO. `capture_ir` dumps full decode output and a
compact 21-byte hex state. `replay_kelon168` accepts that hex state over serial
and sends it through `IRremoteESP8266`.

**Tech Stack:** Arduino CLI, Espressif Arduino core `esp32:esp32` 3.3.8,
`IRremoteESP8266` 2.9.0, ESP32-C6 Super Mini on GPIO20, USB CDC enabled on
boot.

---

### Task 1: Toolchain

**Files:** none

1. Confirm available disk space.
2. Install `esp32:esp32`.
3. Upgrade `IRremoteESP8266` to 2.9.0 for ESP32 Arduino core 3.x support.
4. Enable `CDCOnBoot=cdc` so serial diagnostics appear over USB.
5. Confirm the C6 Super Mini board profile exists.
6. Confirm the connected port.

### Task 2: Capture Sketch

**Files:**
- Create: `capture_ir/capture_ir.ino`

1. Configure `IRrecv` on GPIO20 with a 1024-entry capture buffer and 50 ms
   timeout for A/C messages.
2. Print protocol, bit count, raw length, human-readable decode, source-code
   output, and a compact `KELON168_STATE_HEX=` value when applicable.
3. Compile for `esp32:esp32:makergo_c6_supermini`.

### Task 3: Replay Sketch

**Files:**
- Create: `replay_kelon168/replay_kelon168.ino`

1. Configure `IRsend` on GPIO20.
2. Accept a 21-byte KELON168 state as 42 hex digits over serial.
3. Send the loaded state only after an explicit `send` command.
4. Compile for `esp32:esp32:makergo_c6_supermini`.

### Task 4: Usage Docs

**Files:**
- Create: `README.md`
- Create: `Makefile`
- Create: `.gitignore`

1. Document receiver wiring and capture commands.
2. Document transmitter swap and replay commands.
3. Include make targets for compile, upload, monitor, and board/port listing.

### Task 5: High-Level Control Sketch

**Files:**
- Create: `control_kelon168/control_kelon168.ino`
- Modify: `Makefile`
- Modify: `README.md`

1. Configure `IRKelon168Ac` on GPIO20.
2. Provide a serial command menu for power, mode, temperature, fan, swing, and
   light.
3. Do not transmit on boot; send only after explicit serial commands.
4. Compile for `esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc`.
