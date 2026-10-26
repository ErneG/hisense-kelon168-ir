# Hisense/Kelon KELON168 IR Test

Receiver-first test firmware for an ESP32-C6 Super Mini and a Hisense/Kelon
heat pump remote such as DG11R1/DG11R2-family controllers.

The first goal is to prove whether the remote decodes as `KELON168` with
`IRremoteESP8266`. After that, the same project can replay a captured 21-byte
state through an IR transmitter.

## Current Result

Confirmed on the ESP32-C6 Super Mini:

- The Hisense remote decodes as `KELON168`.
- GPIO20 works for receiving when the receiver module is connected.
- GPIO20 works for transmitting when the transmitter module is connected.
- Raw replay of a captured `KELON168` frame successfully controlled the heat
  pump.

Captured valid states:

```text
83060473000080220000000000D500010000280029
83060050000080220000000000F20006000038003E
```

The generated `IRKelon168Ac` command path compiles, but its output did not
control the unit in the first test. Use raw replay and additional captures as
the trusted baseline until the generated-byte mapping is verified against this
specific remote.

Later replay tests confirmed that the raw `KELON168` transport is correct.
`IRremoteESP8266`'s `IRKelon168Ac` is still the closest ready-made encoder, but
it must be used with care because each setter changes the command byte. Wake the
unit with a power command first, then send mode/temperature/fan commands as
separate frames. Use raw `sendKelon168()` as the fallback for commands the
library does not expose or reproduce correctly. See `docs/protocol-notes.md`
for the byte map and replay-confirmed frames.

## Hardware

- Board: ESP32-C6 Super Mini, using Arduino FQBN
  `esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc`
- Indoor unit label: `TG25VE0AG`, batch `NO.K-CFJG`
- IR module data pin: board pin marked `20`, treated as GPIO20
- IR module power: `3V3`
- IR module ground: `GND`
- The capture sketch enables the ESP32 internal pull-up on GPIO20 and uses a
  35% decode tolerance to keep borderline frames decodable.

Use only one module at a time on GPIO20:

- Capture phase: connect the IR receiver module.
- Replay phase: connect the IR transmitter module.

## Capture The Remote

Compile and upload:

```sh
arduino-cli compile --fqbn esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc capture_ir
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc capture_ir
arduino-cli monitor -p /dev/cu.usbmodem1101 -c baudrate=115200
```

Point the Hisense remote at the receiver and press one button briefly.

If the protocol is supported, the monitor should include:

```text
Protocol  : KELON168
KELON168_STATE_HEX=...
```

Save the whole capture output for reverse engineering, especially the
`rawData[]` block and the `KELON168_STATE_HEX=` line.

## Replay A Captured State

Swap the receiver module for the transmitter module on the same three pins.

Compile and upload:

```sh
arduino-cli compile --fqbn esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc replay_kelon168
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc replay_kelon168
arduino-cli monitor -p /dev/cu.usbmodem1101 -c baudrate=115200
```

Paste the captured 42 hex digits, with or without spaces, then press Enter.
After it says the state is loaded, type:

```text
send
```

## Control With Generated Kelon168 Commands

After `KELON168` capture is confirmed, the higher-level control sketch can send
generated climate commands:

```sh
arduino-cli compile --fqbn esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc control_kelon168
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc control_kelon168
arduino-cli monitor -p /dev/cu.usbmodem1101 -c baudrate=115200 -c dtr=on -c rts=off
```

Example commands in the serial monitor:

```text
on
cool 22
heat 21
temp 23
fan auto
swing on
light off
off
```

## Notes

- `IRremoteESP8266` 2.9.0 is required for ESP32 Arduino core 3.x support on
  the ESP32-C6.
- `CDCOnBoot=cdc` is required so `Serial` output appears on the USB serial
  port rather than only on UART pins.
- `IRremoteESP8266` 2.9.0 includes detailed `KELON168` support, but this first
  project still captures and replays exact states before relying on generated
  climate commands.
- The first generated power/cool commands did not control the heat pump, while
  an exact captured raw state did. The next firmware slice should derive command
  generation from captures made with this DG11R1-01 remote.
- If the remote decodes as something other than `KELON168`, keep the raw output
  and use it for protocol comparison.
