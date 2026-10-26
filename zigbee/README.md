# hisense-ir-ac (Zigbee)

ESP-IDF firmware for an ESP32-C6 Super Mini. The board is a Zigbee router. It controls the Hisense TG25VE0AG heat pump over IR (KELON168 frames). There is no Wi-Fi.

- IR LED module signal: GPIO20 by default (Kconfig `HAC_IR_GPIO`; `HAC_IR_INVERT` inverts the output). RMT TX, 38 kHz carrier, 50% duty.
- BOOT button (GPIO9): hold 5 s at runtime to do a factory reset. A double reset within 4 s also does a factory reset (same policy as zigbee-led; USB port resets do not count).
- Codec: `main/hisense_codec.{c,h}` is an unchanged copy of `flipper/hisense_ac/`. A host test fails if the copies differ.

## Zigbee model (endpoint 1, HA Thermostat 0x0301)

| Cluster | Attributes |
| --- | --- |
| Basic | manufacturer `ErnestsDIY`, model `hisense-ir-ac`, mains powered |
| Identify | standard |
| Thermostat 0x0201 | system_mode off/cool/heat/fan_only/dry, occupied heating and cooling setpoints (16-30 C, step 1, rounded), local_temperature 0x8000 (unknown), control sequence 4 |
| Fan Control 0x0202 | fan_mode low/medium/high/auto (mirror of the 6-speed value) |
| `hisenseAc` 0xFC20 (custom, no manufacturer code) | 0x0000 fanSpeed enum8 (0 auto, 1 low, 2 mid_low, 3 medium, 4 mid_high, 5 high); 0x0001 turbo, 0x0002 economy, 0x0003 sleep, 0x0004 vswing, 0x0005 hswing, 0x0006 display (bool); 0x0010 resend enum8 (1 state, 2 vswing pulse, 3 hswing pulse, 4 display pulse); 0x0020 heatSetpoint, 0x0021 coolSetpoint int16 (0.01 C) |

Fan speed choice: the standard fan_mode has only 3 speeds plus auto. A custom enum attribute holds all 6 speeds. The Z2M climate `fan_mode` maps to it directly. A Multistate Value cluster would need a second endpoint and custom Z2M code anyway.

Setpoint choice: Z2M makes a low/high range in Home Assistant when the climate has two setpoints. So the climate has one setpoint (`current_heating_setpoint`). It uses the heating setpoint in heat mode (and when off) and the cooling setpoint in the other modes. Both real setpoints are also separate number entities. Z2M writes both setpoints to `hisenseAc` 0x0020/0x0021, not to the thermostat cluster. Reason: the esp-zigbee stack (ZBOSS `check_value_thermostat_server`) rejects a thermostat write when cooling < heating + 10 x min_setpoint_dead_band, and a write to the dead band accepts only 10-25 (1-2.5 C). So a pair such as heat 24 / cool 19 can never pass on 0x0201. The custom cluster has no value check. The firmware mirrors both values to the thermostat setpoints for display and reporting (local writes skip the check). Boot logs the thermostat limits and dead band.

Double reset ignores USB (reason 11) and JTAG resets, so opening the USB-Serial-JTAG port does not count. Power-on and EN-pin resets still count.

## Send rules

- Every write compares the wanted state with the last sent state (`main/ac_logic.c`).
- Order: power (0x01), mode (0x06), temp (0x02), fan (0x11), turbo (0x04), economy (0x0C), sleep (0x03), vswing (0x07), hswing (0x08), display (0x00).
- Off to any mode sends power-on first. When the unit is off, other changes wait until it is on again.
- Side effects as on the remote: sleep or economy on sets fan low; turbo on in heat sets 30 C; off restores. The preset frame carries the new fan or temp, so no extra fan or temp frame is sent.
- Toggles send a frame only when the tracked state changes. `resend` with a `*_pulse` value sends one toggle frame and keeps the tracked state. Use it when the unit and HA disagree.
- Frames go to a queue. An IR task sends them 400 ms apart. The Zigbee task never blocks.
- State (wanted and sent) is saved in NVS. Boot loads it and sends nothing. Clock bytes are 00:00.

## Build, test, flash

```bash
. ~/esp/esp-idf/export.sh            # ESP-IDF v5.5.3
idf.py set-target esp32c6 && idf.py build
cmake -S host_tests -B host_tests/build && cmake --build host_tests/build && ctest --test-dir host_tests/build
idf.py -p /dev/cu.usbmodem1101 flash
```

The app partition is 1.5 MB (`partitions.csv`). The router build is about 1.14 MB.

## Zigbee2MQTT

1. Copy `z2m/hisense-ir-ac.js` to `/config/zigbee2mqtt/external_converters/` and restart Z2M (2.x).
2. Permit join in Z2M. The board retries network steering every few seconds until it joins.
3. Home Assistant shows a climate entity (system mode, setpoint, 6 fan speeds), switches for turbo, economy, sleep, vswing, hswing and display, two setpoint numbers, and a `resend` select.
