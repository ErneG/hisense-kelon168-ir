# hisense-kelon168-ir

IR control for a Hisense heat pump. It has three parts:

- An ESP32-C6 Zigbee router firmware for Zigbee2MQTT and Home Assistant.
- A Flipper Zero app.
- Protocol notes for the KELON168 IR protocol.

Tested unit: indoor unit `TG25VE0AG`, remote `DG11R1-01`. The protocol is KELON168. One frame is 21 bytes (168 bits).

## What is verified

- The codec reproduces 62 of 62 captured frames byte for byte. Timer frames are not included.
- The ESP32 IR output matches its own log in 15 of 15 frames. A Flipper captured the output to check this.
- The real unit responds to the Flipper app and to the ESP32.

## Hardware

- ESP32-C6 Super Mini.
- An IR LED transmitter module on GPIO20. The pin is set in Kconfig (`HAC_IR_GPIO`).
- For the Flipper app: a Flipper Zero on official firmware 1.5.1.

## Quick start: Zigbee firmware

Needs ESP-IDF 5.5.x.

```sh
. ~/esp/esp-idf/export.sh
cd zigbee
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
```

Change the serial port to match your system.

1. Copy `zigbee/z2m/hisense-ir-ac.js` to `external_converters/` in your Zigbee2MQTT config. Restart Zigbee2MQTT (2.x).
2. Permit join in Zigbee2MQTT. The board retries joining every few seconds.
3. Home Assistant shows a climate entity and switches for turbo, economy, sleep, swing and display.

More detail: [zigbee/README.md](zigbee/README.md).

## Quick start: Flipper app

Needs `ufbt` with the 1.5.1 release SDK.

```sh
cd flipper/hisense_ac
ufbt update --channel=release
ufbt launch
```

`ufbt launch` runs the app on a connected Flipper. Run `make test` to run the host codec test.

More detail: [flipper/hisense_ac/README.md](flipper/hisense_ac/README.md).

## Tests

- Flipper codec: `make test` in `flipper/hisense_ac`. It uses the committed `tests/test_vectors.h`. It needs no other repo.
- To regenerate the vectors, run `make vectors`. This needs the companion repo `python-ir-remote-decoder` (it holds the capture data). Set `IR_DECODER_DIR` to its path, or clone it next to this repo.
- Zigbee host test: see `zigbee/README.md` (CMake and ctest).

## Repo layout

| Path | Content |
| --- | --- |
| `zigbee/` | ESP-IDF Zigbee router firmware and the Zigbee2MQTT converter |
| `flipper/hisense_ac/` | Flipper Zero app and codec with host tests |
| `docs/protocol-notes.md` | Byte map and command list for KELON168 |
| `docs/capture-log.md` | Capture log |
| `tools/ir_pin_probe/` | Small tool to probe the IR pin |
| `capture_ir/`, `replay_kelon168/`, `control_kelon168/` | Legacy Arduino sketches |
| `esphome/` | Experimental ESPHome component (unfinished) |

## Limits

- Auto mode is not mapped.
- Swing and display are toggles. The app and firmware assume their state.
- Timer is not mapped.
- The state can differ from the unit if you also use the real remote.

## Legacy and experimental

- `capture_ir/`, `replay_kelon168/` and `control_kelon168/` are the first Arduino sketches. They use `IRremoteESP8266`. They are kept for reference. Their generated commands did not control the unit reliably. Use the Zigbee firmware or the Flipper app.
- `esphome/` is an unfinished ESPHome climate component. It is experimental. Copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml` and fill in your values.

## License

MIT. See [LICENSE](LICENSE).
