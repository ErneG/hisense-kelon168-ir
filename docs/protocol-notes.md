# Hisense TG25VE0AG Protocol Notes

## 2026-10-26 Flipper session: confirmed map

Tools:

- Flipper Zero on firmware 1.5.1 is the capture and transmit bench.
- Script: `python-ir-remote-decoder/flipper_ir.py` (commands: capture, guide,
  report, build, sendtest).
- Session data: `python-ir-remote-decoder/captures/session_s1/`. It has 61
  guided steps. All frames are full 168-bit frames with valid checksums.
- Old ESP32 captures: 17 of 28 raw files were truncated in section 3. Older
  inferred frames were built from incomplete data.

Result:

- Frames generated from state work on the real unit. They are not replays.
  The owner tested this from the Flipper app `flipper/hisense_ac` on
  2026-10-26.
- The codec `flipper/hisense_ac/hisense_codec.c` reproduces 62/62 captured
  non-timer frames byte for byte.

Byte map (`b` = byte, numbered from 0):

| Byte / bits | Meaning | Status |
| --- | --- | --- |
| b0-1 | `83 06` constant | Constant |
| b2 bits 0-1 | Fan: 0 auto, 1 high, 2 mid, 3 low | Confirmed |
| b2 bit `0x04` | Power marker. Only on command `0x01`. | Seen |
| b2 bit `0x08` | Sleep | Seen |
| b2 bit `0x80` | Vertical swing marker. Only on command `0x07`. | Seen |
| b3 bits 0-2 | Mode: 0 heat, 2 cool, 3 dry, 4 fan. 1 = auto, not seen. | Confirmed (auto open) |
| b3 bits 4-7 | Temperature minus 16, range 16 to 30 C. Clamps at 30. | Confirmed |
| b5 `0x90` | Turbo | Seen |
| b6 `0x80` and b7 | Remote clock: b6 `0x80` plus hour, b7 minute. b7 counted up 1 per minute during the session. | Hypothesis |
| b6 bit `0x20` | Set on display command | Seen |
| b8 `0x40` / `0x80` | `0x40` on command `0x07`. `0x80` on command `0x08`. | Seen |
| b9 / b10 | Timer fields | Not mapped, out of scope |
| b13 | XOR of b2 to b12 | Checksum |
| b14 `0x20` | Economy | Seen |
| b15 | Command | Seen |
| b17 `0x40` | Fan half-step. Fan 1 + flag = mid-high. Fan 3 + flag = mid-low. | Inferred |
| b18 | `0x38` on, `0x28` off | Seen |
| b20 | XOR of b14 to b19 | Checksum |

Commands:

| Command | Meaning |
| --- | --- |
| `0x00` | Display (toggle) |
| `0x01` | Power |
| `0x02` | Temp |
| `0x03` | Sleep |
| `0x04` | Turbo |
| `0x05` | Timer on |
| `0x06` | Mode |
| `0x07` | Vertical swing (toggle) |
| `0x08` | Horizontal swing (toggle) |
| `0x0C` | Economy |
| `0x11` | Fan |
| `0x1D` | Timer off |

Side effects seen on the remote:

- Sleep on sets fan to low.
- Economy on sets fan to low.
- Turbo on (heat) sets temp to 30. Off restores the previous value.

Toggles:

- Swing V, swing H, and display send the identical frame for on and off.
- The remote shows no swing state. Controllers must track these states.

Open:

- Auto mode code.
- Fan names are inferred from order and combo steps.
- Clock hypothesis.
- Turbo in cool mode.
- Whether repeated power-on is absolute (not tested yet).

## Unit identity

- Indoor unit label: `TG25VE0AG`
- Batch number from label: `NO.K-CFJG`
- Hisense product code found online: `20003095`
- Parts listings map `TG25VE0AG` to internal type
  `AST-09UW4RVETG00A...KSBJC00005544`.
- IRremoteESP8266 lists `Hisense AST-09UW4RVETG00A A/C` as `KELON168`.
- Hisense Mini Apple Pie catalog data lists the standard wireless remote as
  `R2-01`; the local remote is marked `DG11R1-01`, so DG11R1 appears to be a
  close variant of the DG11R2/R2-01 protocol family.

Sources:

- Hisense TG25VE0AG product page:
  https://ua.hisense.com/c/AIR-CONDITIONER-TG25VE0AG-HSN/p/000000000020003095
- PartsNL TG25VE0AG cross-reference:
  https://www.partsnl.nl/apparaten/hisense-ast-09uw4rvetg00ahsn1plksbjc00005544-tg25ve0ag-2122665
- IRremoteESP8266 supported protocols:
  https://github.com/crankyoldgit/IRremoteESP8266/blob/master/SupportedProtocols.md
- Hisense Mini Apple Pie catalog excerpt:
  https://cold-ice-klim.pl/wp-content/uploads/2021/02/katalog_hisense_2020_mini_apple_pie.pdf

## Protocol conclusion

This unit is using the `KELON168` / Whirlpool-compatible 168-bit protocol.
`IRremoteESP8266` is the right transport/timing implementation, but its
high-level `IRKelon168Ac` state model is incomplete for this exact remote:

- It can generate a working power-on frame from a reset/default state.
- Its known KELON168 constants match many command bytes.
- It does not expose horizontal swing; command `0x08` was captured, but replay
  did not reliably validate it as a working horizontal control.
- Some captured frames decode as `WHIRLPOOL_AC`; the raw bytes are still the
  same Kelon/Hisense frame shape.

## Open-source library comparison

### IRremoteESP8266

This is the closest ready-made implementation and should be the primary base.
Its `IRKelon168Ac` class uses the same 21-byte `83 06 ...` frame shape and the
same command constants seen in our captures:

| Command | Library constant | Capture match |
| --- | --- | --- |
| `0x01` | `kKelon168CommandPower` | yes |
| `0x02` | `kKelon168CommandTemp` | yes |
| `0x04` | `kKelon168CommandSuper` | partly; local button label needs retest |
| `0x06` | `kKelon168CommandMode` | yes |
| `0x07` | `kKelon168CommandSwing` | captured, not behavior-validated |
| `0x0D` | `kKelon168CommandIFeel` | library uses this for Auto/iFeel mode |
| `0x11` | `kKelon168CommandFanSpeed` | yes |

Important API detail: setters change the command byte. Calling
`setPower(true)`, then `setMode(...)`, then `setTemp(...)` does not send a
power command; the final setter wins. That means the first generated-control
test did not actually prove `IRKelon168Ac` is unusable. We need to test it with
separate command phases:

1. `setPower(true); send();`
2. `setMode(...); send();`
3. `setTemp(...); send();`
4. `setFan(...); send();`

This also matches SmartIR-style toggle-device handling: wake first, then send
the desired state.

### arduino-heatpumpir

ToniA `arduino-heatpumpir` has `HisenseHeatpumpIR`, but it targets Hisense AUD
remotes `Y-H1-01`, `Y-H1-02(E)`, `Y-J1`, and `Y-E4-07`. Its encoder sends a
14-byte `87 06 ...` frame with two sections. Our remote sends a 21-byte
`83 06 ...` `KELON168` frame with three sections. So this library is not a
drop-in solution for DG11R1/R2-01.

### SmartIR / code-table integrations

SmartIR-style integrations can be useful later for Home Assistant transport,
but they are table/code based rather than a ready encoder for this exact
DG11R1/TG25VE0AG combination. They are a fallback if we choose to store fixed
raw frames instead of generating them.

## Frame shape

- 21 bytes / 168 bits.
- Three sections: 6 bytes, 8 bytes, 7 bytes.
- Bytes are sent LSB-first within each section.
- Byte 0-1 preamble: `83 06`.
- Checksum 1: byte 13 = XOR of bytes 2 through 12.
- Checksum 2: byte 20 = XOR of bytes 14 through 19.

## Known useful fields

Superseded by the 2026-10-26 section above.

| Field | Meaning |
| --- | --- |
| byte 2 bits 0-1 | fan code low bits |
| byte 2 bit `0x04` | power command marker |
| byte 2 bit `0x80` | candidate vertical swing marker; not replay-validated |
| byte 3 low bits | mode |
| byte 3 high nibble | temperature offset from 16 C |
| byte 5 bits `0x90` | super/turbo flags in IRremoteESP8266 model; local button label still needs verification |
| byte 6-7 | clock/counter fields; change across captures |
| byte 8 bit `0x40` | candidate vertical swing second marker; not replay-validated |
| byte 8 bit `0x80` | candidate horizontal swing marker; not replay-validated |
| byte 15 | command/button code |
| byte 16 bit `0x02` | fan code high bit |
| byte 18 bit `0x10` | target on/off indicator (`0x38` on, `0x28` off with this model) |

## Command bytes observed

| Command | Meaning | Status |
| --- | --- | --- |
| `0x01` | power | replay-confirmed, but frame contents matter |
| `0x02` | temperature | captured |
| `0x04` | super/turbo in library; local Economy/Super labeling needs repeat capture | captured, ambiguous |
| `0x06` | mode | captured |
| `0x07` | candidate vertical swing | captured, but replay not reliable |
| `0x08` | candidate horizontal swing | captured, but replay not reliable; not exposed by library |
| `0x0C` | Economy/Feel toggle-back on this remote | captured |
| `0x11` | fan speed | captured |

## Replay-confirmed frames

| Action | State hex | Result |
| --- | --- | --- |
| Power on to heat 23 C | `83060470000080000000000000F400010000380039` | turned unit on |
| Power off | `83060450000080320000000000E600010000280029` | turned unit off |
| Known accepted full state | `83060050000080220000000000F20006000038003E` | accepted while unit was already on |

## Failed or weak candidates

Superseded by the 2026-10-26 section above.

| Candidate | Result |
| --- | --- |
| `83060450000080320000000000E600010000380039` | inferred power-on; did not turn unit on |
| `83060050000080220000000000F20006000038003E` while unit was off | accepted/beep behavior not enough to wake unit |
| command `0x04` alternating Economy/Super candidates | no visible change during replay test |
| command `0x07` / `0x08` swing candidates | captured, but later observation says horizontal/vertical control is not actually working; do not treat as success |

## Implementation direction

Start from `IRremoteESP8266`'s `IRKelon168Ac` and test it with correct
command-byte sequencing before writing a custom generator. Keep raw
`sendKelon168()` replay as the fallback for commands the high-level class does
not expose or does not reproduce correctly, especially Economy/Feel and
horizontal/vertical swing.
