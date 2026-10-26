# Hisense AC (Flipper Zero)

Flipper Zero app. It controls a Hisense heat pump over IR (KELON168, 21-byte frame).
Built for official firmware 1.5.1 (SDK API 88.2).

## Build

Needs `ufbt` with the release SDK (1.5.1).

    ufbt update --channel=release
    ufbt                # writes dist/hisense_ac.fap
    make test           # host test of the codec (62 frames, 3 timing arrays)

## Install

- USB: `ufbt launch` (runs the app on the connected Flipper).
- Manual: copy `dist/hisense_ac.fap` to `/ext/apps/Infrared/` on the SD card.

## Controls

Up/Down select an item. Left/Right change a value. Each change sends one frame at once.
OK on V-Swing, H-Swing, Display or Resend state sends that command. Back exits.

- Last sent: the last command and the clock time sent.
- Power, Mode (Heat/Cool/Dry/Fan), Temp (16..30 C), Fan (Auto/High/Mid-High/Mid/Mid-Low/Low).
- Turbo, Economy, Sleep: Off/On.
- Resend state: sends the Temp command (0x02) with the full state.

Remote side effects copied from captures:

- Sleep on or Economy on: fan goes to Low. Turn both off: the old fan comes back.
- Turbo on in Heat mode: temp goes to 30. Turbo off: the old temp comes back.
- A manual Fan or Temp change clears the saved old value.

The clock bytes come from the Flipper RTC at each send. Set the Flipper date and time first.

State is saved to `/ext/apps_data/hisense_ac/state.bin` on each change and loaded at start.

## Signal

Carrier 38000 Hz, duty 50 percent. 343 raw timings per frame, starting with a mark.
Sent with `infrared_send_raw_ext`. The codec (`hisense_codec.c`) matches `flipper_ir.py`.

## Known limits

- Auto mode is hidden. It is not verified.
- V-Swing, H-Swing and Display are toggles. The app keeps no state for them.
- Turbo in Cool mode is not verified. The app does not change temp there.
- Timer frames (0x05, 0x1D) are not supported.
- The state file and the unit can differ if you use the real remote.
