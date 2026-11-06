# CLAUDE.md

This repo is a ZMK firmware user config (`zmk-config`) for a wireless split keyboard, and also a place to **learn ZMK**. The user is new to ZMK, so teach as you go.

## Project facts

- Hardware: Lily58 split keyboard (upstream ZMK shield `lily58`).
  - Shield: `lily58_left` / `lily58_right`
  - Controller board: Nice!Nano v2 (`nice_nano_v2`, confirmed)
  - Left half: Nice!View display (`nice_view` shield with `nice_view_adapter`; verify exact shield names against the pinned ZMK revision).
  - Right half: Azoteq TPS43 touchpad (IQS5xx family, I2C). Needs an external driver module in `west.yml` (e.g. a `zmk-driver-azoteq-iqs5xx` module), a devicetree overlay and a pointing input listener. The touchpad is on the **right (peripheral)** half, so input must be forwarded to the central (left) over the split link. Check the ZMK input/pointing docs and the module README for the required split config.
    - Wiring: SDA = P0.17, SCL = P0.20 (the Nice!Nano's default I2C0 pins, pro-micro pins 2 and 3, so `&pro_micro_i2c` should apply; verify in the board's devicetree).
    - RDY is wired to **P0.22 (pro_micro 4)** (decided). RST is **not** wired (decided; RST is normally optional, but confirm the chosen driver doesn't require `reset-gpios`). IQS5xx devices only accept I2C transactions inside a communication window signalled by RDY, and ZMK drivers are typically interrupt-driven from RDY. In devicetree: `rdy-gpios = <&pro_micro 4 GPIO_ACTIVE_HIGH>;` (check the driver's property name and polarity).
    - Right-half GPIO usage (derived from ZMK v0.3 `lily58` shield + `nice_nano` `pro_micro` map; reference config: github.com/typeractivexyz/lily58-wireless-zmk-config, which pins ZMK `v0.3` and has no touchpad):
      - Used by matrix: rows pro_micro 5-9 (P0.24, P1.00, P0.11, P1.04, P1.06); cols pro_micro 10, 16, 14, 15, 18, 19 (P0.09, P0.10, P1.11, P1.13, P1.15, P0.02).
      - Used by I2C0 (OLED header, now the touchpad): pro_micro 2 / 3 (P0.17 SDA, P0.20 SCL).
      - Claimed by the shield's encoder node (`left_encoder`): pro_micro 21 / 20 (P0.31, P0.29). It has no `status = "disabled"` in the dtsi, so it may also be active on the right half; disable it in the right overlay before reusing those pins.
      - Probably free: pro_micro 4 (P0.22), pro_micro 1 (P0.06), and pro_micro 20/21 once the encoder is disabled. Avoid pro_micro 0 (P0.08), the TRRS data pin. Decided: RDY on pro_micro 4 (P0.22), RST unconnected. Confirm P0.22 is reachable on the PCB/controller before soldering.
    - The Lily58 shield enables an SSD1306 OLED at `0x3c` on the same I2C bus and sets it as `zephyr,display`. The right half has no OLED, so remove/disable that node in the right overlay. The left half uses Nice!View (SPI) instead.
    - ZMK `v0.3` (latest release, v0.3.0) already ships `zmk,input-listener`, so the reference config's `v0.3` pin is fine for pointing.
    - Driver module (researched): `AYM1607/zmk-driver-azoteq-iqs5xx` (compatible `azoteq,iqs5xx`, I2C addr `0x74`; README says tested on TPS43). In its source, `rdy-gpios` is **required** (`GPIO_DT_SPEC_INST_GET`) and `reset-gpios` is **optional** (`GPIO_DT_SPEC_INST_GET_OR ... {0}`), so RDY on P0.22 with RST unconnected is supported. Alternative: `stelmakhdigital/zmk_driver_azoteq` (compatible `azoteq,tps43`, both GPIOs optional, enable `CONFIG_INPUT_TPS43=y`, documents the split `zmk,input-split` setup for a peripheral-side touchpad). Add the chosen module as a project in `config/west.yml`. Not yet built or tested here.
    - Split: because the touchpad is on the peripheral (right) half, use `zmk,input-split` on the right and a matching proxy plus `zmk,input-listener` on the central (left), as in the stelmakhdigital README. Verify against ZMK's input-processor and split docs.
    - Nice!View is only on the left half, so the right half's I2C pins don't clash with its SPI pins; still check they don't clash with the Lily58 matrix pins.
- Connectivity: Bluetooth (BLE) wireless split; left is central, right is peripheral.
- Planned features: Nice!View display, TPS43 touchpad. Encoders are not planned unless the user says otherwise.
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
  west.yml                 # ZMK v0.3 + zmk-driver-azoteq-iqs5xx (pinned to commit 27321f0)
  lily58.conf              # shared Kconfig (pointing, TX power, debounce, ZMK Studio)
  lily58_left.conf         # left only: CONFIG_ZMK_DISPLAY=y (Nice!View)
  lily58_left.overlay      # central: enables the touchpad input listener
  lily58_touchpad.dtsi     # shared input-split + disabled input-listener
  lily58.keymap            # copied from the reference config; encoder sensor-bindings removed
  boards/shields/touchpad_right/   # custom shield for the right half's TPS43
    Kconfig.shield
    touchpad_right.overlay # TPS43 on I2C (RDY = pro_micro 4), deletes the shield's OLED node
    touchpad_right.conf    # CONFIG_I2C=y
build.yaml                 # left: lily58_left nice_view_adapter nice_view + snippet studio-rpc-usb-uart; right: lily58_right touchpad_right; settings_reset
```

Why a custom shield: ZMK applies `config/<shield>.overlay` *before* the shield's own overlay, so `/delete-node/ &oled` fails there. A shield listed after `lily58_right` is applied later and can delete it. Don't set `CONFIG_ZMK_DISPLAY` in the shared conf: the right half has no display and fails to compile.

ZMK Studio (`CONFIG_ZMK_STUDIO=y`, locking off) and eager debounce (press 1 ms, release 10 ms) are set in the shared `lily58.conf`, as in the reference config. The `studio-rpc-usb-uart` snippet goes on the **left (central)** build only; it needs `-S studio-rpc-usb-uart` for local builds.

Status: **both halves build** (checked 2025-11-03: left ~46% flash, right ~22%). Not yet flashed or tested on hardware; touchpad behaviour, gestures and pairing are unverified. Left half Nice!View: the build uses `nice_view` on SPI0 as `zephyr,display`; the shield's SSD1306 OLED node stays in the devicetree but its I2C0 bus is disabled (nRF52840 shares TWI0/SPI0), so it is inert. This is identical to the reference Nice!View config (typeractivexyz/lily58-wireless-view-zmk-config). Not tested on hardware.

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

# right half (peripheral, touchpad)
west build -p -s zmk/app -d build/right -b nice_nano_v2 -- -DSHIELD="lily58_right touchpad_right" -DZMK_CONFIG="$PWD/config"

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
- Build both halves with their display/touchpad parts: `build.yaml` entries should be roughly `lily58_left` + `nice_view_adapter nice_view` (left) and `lily58_right` (+ touchpad overlay/shield, right). Confirm against current docs.
- Touchpad: pin the driver module in `west.yml`, add its Kconfig to the right half's config (`lily58_right.conf`), and add a mouse/scroll layer or input processors as needed. Prefer ZMK's current input subsystem docs over older tutorials, since the pointing API has changed between versions.
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
- Pointing devices: https://zmk.dev/docs/features/pointing
- Lily58 shield: https://zmk.dev/docs/hardware (search "Lily58")
- Keymap editor (visual): https://nickcoutsos.github.io/keymap-editor/

## Open items for the user

- Driver module chosen: AYM1607/zmk-driver-azoteq-iqs5xx (builds; untested on hardware).
- Which layers/features are wanted first.
