# ESPHome climate for Hisense KELON168 heat pumps

External ESPHome component `hisense_kelon168`. It controls a Hisense heat pump
(remote DG11R1-01, 21-byte KELON168 frame) from Home Assistant through an ESP32
IR LED. It can also listen to the real remote through an optional IR receiver.

The frame codec is a copy of `../flipper/hisense_ac/hisense_codec.[ch]`. That
codec is verified against captures and against the real unit.

## Files

- `components/hisense_kelon168/` - component (`climate.py`, `.h`, `.cpp`, codec copy, decoder).
- `hisense-heatpump.yaml` - example config with substitutions.
- `secrets.yaml.example` - copy to `secrets.yaml` (git-ignored) and fill in.
- `Makefile` - `make check-sync` (codec copies equal), `make test` (host decoder test), `make compile`.

## Wiring

Board: ESP32-C6 Super Mini (pin source: `../README.md` and `../control_kelon168/control_kelon168.ino`).

| IR LED module | Super Mini |
| --- | --- |
| DATA | GPIO20 (`ir_tx_pin`) |
| VCC | 3V3 |
| GND | GND |

Optional IR receiver module (not needed to control the unit): DATA to GPIO19
(`ir_rx_pin`, a new choice, not from the old sketches), 3V3, GND. The old
sketches shared GPIO20 for both modules, one at a time. ESPHome needs both at
once, so the receiver gets its own pin. To enable it, uncomment the
`remote_receiver` block and `receiver_id` in the yaml.

Point the LED at the indoor unit. Keep the receiver away from the LED, or the
receiver sees its own frames. The component ignores frames for 800 ms after it sends.

## Flashing

```
cd esphome
cp secrets.yaml.example secrets.yaml   # then edit
esphome compile hisense-heatpump.yaml
esphome run hisense-heatpump.yaml      # first flash over USB, then OTA
```

Board is `esp32-c6-devkitc-1` with the `esp-idf` framework.
Add the device in Home Assistant through the ESPHome integration.

## Use

Use one ESP32 per indoor unit. Each unit gets its own yaml (change `name`).

- Modes: Off, Heat, Cool, Dry, Fan only. Temperature 16-30, step 1.
- Fan: Auto, Low, Medium, High, plus custom "Mid-Low" and "Mid-High".
- Presets: none, Eco (economy), Boost (turbo), Sleep.
- Swing: Off, Vertical, Horizontal, Both.
- Buttons: Toggle V swing, Toggle H swing, Toggle display, Resend state.

The component keeps the last state it sent. On each change it sends only the
needed frames, in this order: power, mode, temperature, fan, preset, swing.
Frames are queued 400 ms apart without blocking the loop.

The unit side effects are copied: Eco and Sleep set fan to Low and restore it
after. Boost in Heat mode sets 30 and restores the old temperature after.

## Limits

- Auto mode is not mapped. A received auto frame keeps the old mode.
- Swing and display are toggles. The frame is the same for on and off. The
  component tracks the state itself and assumes it from the restored state at boot.
  If the unit and Home Assistant disagree, press "Toggle V swing", "Toggle H
  swing" or "Toggle display" until they match.
- After boot the component assumes the unit holds the last restored state. Press "Resend state" to push it.
- Without a receiver, use of the real remote is not seen. The state in Home Assistant then drifts.
- Clock bytes come from `time_id` (Home Assistant time). Without it they are 00:00.
- IR is one-way. The component cannot confirm that the unit got a frame.
