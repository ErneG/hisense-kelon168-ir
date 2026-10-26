# Capture Log

## Hardware

- Board: ESP32-C6 Super Mini
- Arduino board profile: `esp32:esp32:makergo_c6_supermini:CDCOnBoot=cdc`
- IR GPIO: 20
- Library: `IRremoteESP8266` 2.9.0
- Indoor unit label: `TG25VE0AG`, batch `NO.K-CFJG`
- Online parts/catalog cross-reference: `TG25VE0AG` maps to the Hisense
  `AST-09UW4RVETG00A` / Mini Apple Pie family, which IRremoteESP8266 lists as
  `KELON168`.

## Findings

- Receiver module LED blinked on remote presses.
- Without `CDCOnBoot=cdc`, the sketch did not print to the USB serial monitor.
- With USB CDC enabled, GPIO20 internal pull-up enabled, and decode tolerance
  raised to 35%, valid captures decode as `KELON168`.
- Best capture reliability was with the remote around 40-60 cm from the
  receiver; very close aiming sometimes produced noisy/partial frames.
- Raw replay of a captured frame from the physical remote successfully
  controlled the heat pump.
- Generated `IRKelon168Ac` commands compiled and transmitted but did not control
  the heat pump in the first quick test.
- Later replay tests confirmed raw `sendKelon168()` works. The working protocol
  is KELON168, but the integration should use a custom state builder because the
  library's high-level API does not expose every DG11R1/R2-01 command variant.

## Valid Captures

### Capture 1

```text
Protocol  : KELON168
Bits      : 168
KELON168_STATE_HEX=83060473000080220000000000D500010000280029
KELON168_STATE_BYTES=83 06 04 73 00 00 80 22 00 00 00 00 00 D5 00 01 00 00 28 00 29
```

### Capture 2

```text
Protocol  : KELON168
Bits      : 168
KELON168_STATE_HEX=83060050000080220000000000F20006000038003E
KELON168_STATE_BYTES=83 06 00 50 00 00 80 22 00 00 00 00 00 F2 00 06 00 00 38 00 3E
```

This second state was replayed through `replay_kelon168` and worked.

## Working State Map

These are labeled from the remote display after each press. Treat rows marked
`needs repeat` as usable clues, not final protocol constants.

| Remote display / action | State hex | Confidence |
| --- | --- | --- |
| Replay-proven state, heat pump accepted it | `83060050000080220000000000F20006000038003E` | confirmed by replay |
| Power off from heat 21 C auto fan | `83060450000080320000000000E600010000280029` | captured; replay turned unit off |
| Power on to heat 23 C from off | `83060470000080000000000000F400010000380039` | library-shaped candidate; replay turned unit on |
| Recovered power-on candidate, failed replay | `83060450000080320000000000E600010000380039` | do not use |
| Heat 22 C auto fan after temp up | `83060060000080340000000000D40002000038003A` | recovered checksum, needs repeat |
| Heat 21 C auto fan after temp down | `83060050000081000000000000D10002000038003A` | captured |
| Heat, fan 5 bars after fan from auto | `83060150000081010000000000D100110000380029` | captured |
| Heat, fan 4 bars | `83060150000081030000000000D300110040380069` | captured |
| Heat 19 C, fan 3 bars | `83060230000081080000000000BB00110000380029` | recovered, needs repeat |
| Heat 19 C, fan 2 bars | `830603300000810B0000000000B900110040380069` | captured |
| Heat 19 C, fan 1 bar | `830603300000810C0000000000BE00110000380029` | captured |
| Heat 19 C, fan auto / 5 bars shown | `830600300000810D0000000000BC00110000380029` | captured |

## Mode Frames

| Remote display / action | State hex | Confidence |
| --- | --- | --- |
| Cooling 24 C, fan auto / 5 bars shown, after pressing Mode from heat | `830600820000810F00000000000C0006000038003E` | captured |
| Dry, no temperature shown, fan auto / 5 bars shown, after pressing Mode from cooling | `83060073000081100000000000E20006000038003E` | captured as Whirlpool-compatible Kelon bytes |
| Fan-only, no temperature shown, manual 5 bars, after pressing Mode from dry | `83060174000081110000000000E50006000038003E` | captured |
| Heating 21 C, fan auto / 5 bars shown, after pressing Mode from fan-only | `83060050000081120000000000C30006000038003E` | captured as Whirlpool-compatible Kelon bytes |

## Swing Frames

These captures are useful protocol evidence, but should not be treated as
working controls yet. Later observation showed the horizontal/vertical replay
tests were not reliably changing the intended vane state.

| Remote display / action | State hex | Confidence |
| --- | --- | --- |
| Vertical swing pressed while heating 21 C, fan auto / 5 bars shown; remote UI did not visibly change | `83068050000082024000000000100007000038003F` | recovered checksum; not replay-validated |
| Vertical swing repeat while heating 21 C, fan auto / 5 bars shown | `83068050000082044000000000160007000038003F` | captured twice; not replay-validated |
| Command-7 swing frame observed during horizontal test | `83068050000082054000000000170007000038003F` | captured; user confirmed the press was horizontal; likely related state/counter frame |
| Horizontal swing pressed while heating 21 C, fan auto / 5 bars shown | `830600500000820680000000005400080000380030` | captured as Whirlpool-compatible Kelon bytes; command byte 8; replay not reliable |
| Horizontal swing repeat while heating 21 C, fan auto / 5 bars shown | `830600500000820A80000000005800080000380030` | captured; command byte 8; replay not reliable |

For vertical swing, candidate command markers are byte 2 bit `0x80`, byte 8 bit
`0x40`, and command byte 15 = `0x07`. Byte 7 and checksum byte 13 changed
between captures, consistent with clock/counter fields.

For horizontal swing, candidate command markers are byte 8 bit `0x80` and
command byte 15 = `0x08`. Byte 7 and checksum byte 13 changed
between repeat captures, consistent with clock/counter fields.

## Super Frames

| Remote display / action | State hex | Confidence |
| --- | --- | --- |
| Super enabled, heating 30 C, fan auto / 5 bars shown | unavailable | noisy 145-bit capture; repeat needed |
| Super disabled, heating 21 C, fan auto / 5 bars shown | `830600500000820D0000000000DF0004000038003C` | captured, but verify button label |

## Economy / Feel Frames

| Remote display / action | State hex | Confidence |
| --- | --- | --- |
| Economy/Super-related frame; label corrected by user as Economy, display needs verification | `830600E00090820E0000000000FC0004000038003C` | captured; verify exact button/display |
| Economy button toggled back to Feel from Economy, heating 21 C, fan 1 bar | `830603500000820F0000000000DE200C0000380014` | captured; command byte 12 |

## Candidate Fan Frames

The following frames were captured while cycling fan, but the displayed fan level
was not always confirmed immediately. Keep them for comparison when filling the
final fan map.

| State hex | Decoder note |
| --- | --- |
| `83060250000081030000000000D000110000380029` | IRremote fan 2 / Medium |
| `83060350000081030000000000D100110040380069` | IRremote fan 3 / Low |
| `83060350000081030000000000D100110000380029` | IRremote fan 3 / Low |
| `83060050000081030000000000D200110000380029` | IRremote fan 0 / Auto |
| `83060150000081030000000000D300110000380029` | IRremote fan 1 / High |

## Noisy Captures

Noisy captures had Kelon-shaped timing but decoded as `UNKNOWN`, typically with
bit counts such as 86, 126, 155, 161, or 170. Keep them out of the control
mapping until repeat captures confirm the bytes.

## Next Capture Set

Capture each button/state at least twice and only accept states where both
captures produce the same 21 bytes:

- Power on from off
- Power off from on
- Cool 21, 22, 23
- Heat 21, 22, 23
- Temp + from a known temperature
- Temp - from a known temperature
- Fan auto/min/medium/max
- Swing on/off
- Light/display on/off
- Sleep on/off
- Turbo/super on/off, if present
- iFeel, if present
