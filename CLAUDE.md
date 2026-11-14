# CLAUDE.md

This repo is a ZMK firmware user config (`zmk-config`) for a wireless split keyboard, and also a place to **learn ZMK**. The user is new to ZMK, so teach as you go.

## Project facts

- Hardware: Lily58 split keyboard (upstream ZMK shield `lily58`).
  - Shield: `lily58_left` / `lily58_right`
  - Controller board: Nice!Nano v2 (`nice_nano_v2`, confirmed)
  - Left half: Nice!View display (`nice_view` shield with `nice_view_adapter`; verify exact shield names against the pinned ZMK revision).
  - Right half: a second Nice!View (same `nice_view_adapter` + `nice_view` shields as the left). The planned Azoteq TPS43 touchpad was dropped (out of stock), so there is no touchpad, I2C device or pointing config any more.
    - The Lily58 shield's SSD1306 OLED node stays in the devicetree but is inert on both halves (the adapter disables I2C0, which shares TWI0/SPI0 with the display SPI).
    - Right display content comes from the custom shield `nice_view_awesomes` (see below): battery, link status, Caps Lock, Num Lock, and "The Awesomes!" in faux bold.
- Connectivity: Bluetooth (BLE) wireless split; left is central, right is peripheral.
- Features: Nice!View on both halves (left = stock ZMK status screen, right = custom screen), ZMK Studio. Encoders are not planned unless the user says otherwise.
- Build method: **local builds with `west`** (not GitHub Actions).

## Working style (learning-oriented)

- Explain the "why" briefly when introducing a ZMK concept (devicetree, behaviors, layers, combos, hold-taps, sensors, split roles). Link to the relevant ZMK docs page rather than pasting long excerpts.
- Prefer small, reviewable changes. Show what changed in the keymap and what it does on the keyboard.
- When there are several valid ZMK approaches (e.g. `&mt` vs `&lt` vs a custom hold-tap), say which you picked and why.
- Don't invent shield, pin or behavior names. Verify against the ZMK docs (https://zmk.dev/docs) or the module/shield source before using them. If unsure, say so.
- Ask before flashing instructions that could brick a bootloader or before changing board/shield definitions.

## Repository layout

```
config/
  west.yml                 # ZMK v0.3 only
  lily58.conf              # shared Kconfig (TX power, debounce, ZMK Studio, HID indicators)
  lily58_left.conf         # left only: CONFIG_ZMK_DISPLAY=y (Nice!View)
  lily58.keymap            # copied from the reference config; encoder sensor-bindings removed
  boards/shields/nice_view_awesomes/   # custom right-half Nice!View screen
    Kconfig.shield, Kconfig.defconfig, nice_view_awesomes.conf, CMakeLists.txt
    custom_status_screen.c # defines zmk_display_status_screen()
    status.c / status.h    # battery, link, Caps/Num Lock, "The Awesomes!" (faux bold)
    util.c / util.h / bolt.c  # copied from ZMK's nice_view widgets (MIT)
build.yaml                 # left: lily58_left nice_view_adapter nice_view + snippet studio-rpc-usb-uart; right: lily58_right nice_view_adapter nice_view nice_view_awesomes; settings_reset
```

How the right screen works: `nice_view_awesomes` must be listed **after** `nice_view`. Its `.conf` sets `CONFIG_NICE_VIEW_WIDGET_STATUS=n` so ZMK's stock peripheral screen isn't compiled, and its CMakeLists builds our own `zmk_display_status_screen()`. Caps/Num Lock come from the host to the left (central) half, which forwards them to the right over BLE: `CONFIG_ZMK_HID_INDICATORS=y` and `CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS=y` must be set on both halves (they are in `lily58.conf`). LVGL has no bold font, so bold is drawn by printing the text twice 1 px apart. Fonts: Montserrat 10 ("The") and 12 ("Awesomes!"); 14 would overflow the 68 px wide screen.

Status: **the right-half Nice!View config has not been built or flashed yet** (the earlier touchpad config built; this machine currently has no west workspace or Zephyr SDK). Verify with a build, then check on hardware that Caps/Num Lock update on the right display. The left half Nice!View is as before: the build uses `nice_view` on SPI0 as `zephyr,display`, identical to the reference Nice!View config (typeractivexyz/lily58-wireless-view-zmk-config).

If the layout differs from this, update this file.

## Build commands

Workspace is the repo root (`.west/` points at `config/`); `west update` already fetched ZMK, Zephyr and modules into `zmk/`, `zephyr/`, `modules/`, `zmk-driver-azoteq-iqs5xx/`. Tooling:

- Python venv: `.venv/` (west + Zephyr requirements). Activate with `source .venv/bin/activate`.
- `setuptools<81` (nanopb's protoc wrapper needs `pkg_resources`) plus `protobuf`/`grpcio-tools` are in the venv for ZMK Studio builds.
- `cmake<3.32` is installed in the venv, because Zephyr 3.5 fails to configure with CMake 4.x (system cmake is 4.4.5). Keep the venv on PATH when building.
- Zephyr SDK 0.16.3 in `~/zephyr-sdk-0.16.3` (ARM toolchain + host tools incl. `dtc`; registered via `setup.sh -c`).

```bash
source .venv/bin/activate

# left half (central, Nice!View)
west build -p -s zmk/app -d build/left  -b nice_nano_v2 -S studio-rpc-usb-uart -- -DSHIELD="lily58_left nice_view_adapter nice_view" -DZMK_CONFIG="$PWD/config"

# right half (peripheral, Nice!View with custom screen)
west build -p -s zmk/app -d build/right -b nice_nano_v2 -- -DSHIELD="lily58_right nice_view_adapter nice_view nice_view_awesomes" -DZMK_CONFIG="$PWD/config"

# reset firmware (flash on both halves if pairing breaks)
west build -p -s zmk/app -d build/reset -b nice_nano_v2 -- -DSHIELD=settings_reset -DZMK_CONFIG="$PWD/config"
```

- Output is `build/<half>/zephyr/zmk.uf2`. Flash by double-tapping reset and copying the file to the NICENANO drive.
- Use `-p` (pristine) after changing `west.yml`, board or shield, or Kconfig that doesn't seem to apply.
- Split halves are built and flashed **separately**. If halves won't pair after flashing, flash `settings_reset` on both, then reflash.
- `.gitignore` already excludes `build/`, `.west/`, `.venv/`, `zmk/`, `zephyr/`, `modules/`, `bootloader/`, `tools/` and the driver checkout.

## ZMK conventions in this repo

- Keymap is devicetree: layers live under `keymap { ... }`, each with `bindings = <...>;`. Keep bindings aligned in rows matching the physical layout so diffs are readable.
- Define custom behaviors (hold-taps, tap-dances, macros), combos and conditional layers in dedicated blocks above `keymap`, with comments explaining intent.
- Name layers with `#define` constants (e.g. `#define BASE 0`) rather than bare numbers.
- Both halves use Nice!View; the right one needs the custom `nice_view_awesomes` shield listed after `nice_view`.
- Nice!View: it uses SPI and is power hungry on the display side; mention `CONFIG_ZMK_DISPLAY=y` and related options in the `.conf` when relevant.
- Encoders: not planned. If added later, configure `sensor-bindings` per layer and enable `CONFIG_EC11=y`.
- BLE split: the left half is central by default; keep Kconfig settings that must match on both halves in the shared `.conf`.
- Pin the ZMK revision in `config/west.yml` (a tag or commit rather than a moving `main`) so builds are reproducible. Mention it when bumping.

## Useful references

- ZMK docs: https://zmk.dev/docs
- Keycodes: https://zmk.dev/docs/keymaps/list-of-keycodes
- Behaviors: https://zmk.dev/docs/keymaps/behaviors
- Config options: https://zmk.dev/docs/config
- Split keyboards / pairing: https://zmk.dev/docs/features/split-keyboards
- Displays (Nice!View): https://zmk.dev/docs/features/displays
- Lily58 shield: https://zmk.dev/docs/hardware (search "Lily58")
- Keymap editor (visual): https://nickcoutsos.github.io/keymap-editor/

## Open items for the user

- Touchpad dropped (out of stock); re-add via git history (commit b3ced64) if it comes back.
- Which layers/features are wanted first.
