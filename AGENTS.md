# AGENTS.md

Embedded firmware (nRF52 C) + Vue 2 web app repo for Pixl.js — a fork of the original Espruino Pixl.js focused on Amiibo emulation. No JS/C test runner exists — verification is building the firmware.

## Layout

- `fw/` — firmware (nRF52 SDK Makefile build).
- `web/` — Vue 2 + Element UI browser app (BLE file transfer companion).
- `hw/` — hardware (KiCad). `hw/RevA|RevB/RevC` = LCD PCB revisions; `hw/OLED/` = separate OLED board by @xiaohai (gitlab.com/xiaohai/pixl.js). Two independent board designs share one firmware via `BOARD=` define.
- `assets/` — README/device photos (referenced by README.md).
- `gh-pages/` — prebuilt static bundle for the GitHub Pages site (build artifacts, not source).
- `pixl.js.code-workspace`, `.vscode/` — VS Code workspace: `tasks.json` runs `make all flash_openocd -j8`.
- `docs/{en,zh,it}/` — user/build docs; `docs/en/03-Build-Firmware.md` is the build reference.

## Device & emulation stack (bottom-up)

nRF52832 SoC (BLE radio + NFC-A Type 2 tag peripheral) + SPI display (LCD ST7789 / OLED SSD1306 per board) + SPI flash.

- `fw/application/src/hal/` — HAL: `hal_nfc_t2t.c` (NFC Type 2 tag on nRF's NFCT peripheral), `hal_{internal,spi}_flash.c`, `hal_spi_bus.c`. errata workarounds live in `hal_nfc_t2t.c` per `nfc_fixes.h`.
- `fw/application/src/ntag/` — NTAG21x tag emulation core (`ntag_emu_v2.c`: emulated tag state machine over HAL NFC; `ntag_store.c`: tag dump persistence). Amiibo emulation = writing an Amiibo dump into this emulated NTAG.
- `fw/application/src/amiidb/` — Amiibo database (generated via `amiibo_db_gen.py`): lookup by character/game/series (`db_amiibo.c`, `db_game.c`, `db_link.c`, `db_search.c`).
- `fw/components/` submodules: mlib (containers), tlsf (allocator), cwalk (paths), spiffs + littlefs (filesystems), ChameleonUltra (forked for card-emulation protocol compat).
- `fw/application/src/mod/` — device modules: `vfs/` (VFS layer over spiffs/littlefs drivers; device storage browsed over BLE by `web/`), `ble/` (`ble_main.c` BLE stack glue, `ble_amiibolink.c` AmiiboLink service, `ble_df_driver.c` DFU-style driver service), `df/` (data-transfer protocol core: `df_core.c` + per-protocol handlers `df_proto_vfs.c`, `df_proto_info.c`), `settings.c`, `cache.c`.
- `fw/application/src/mui/` — mini UI framework (`mui_core.c`, `mui_canvas.c`, `mui_element.c`, `mui_anim.c`) drawn to the SPI display; locale strings from `src/i18n/`.
- `fw/application/src/core/` — mini-app registry + launcher (`mini_app_registry.c`, `mini_app_launcher.c`). Apps in `src/app/`: `amiibo` (emulator), `amiibolink`, `amiidb`, `ble` (file transfer, pairs with `web/`), `chameleon` (card emulator, port layer adapts ChameleonUltra), `desktop`, `game`, `player`, `settings`, `status_bar`.
- `fw/application/src/main.c` — C entrypoint; `src/boards/board_{oled,lcd}.h` hold per-board pin maps.

BLE protocols documented in `docs/en/05+1-ble_protocol.md` (file transfer) and `05+2-amiibolink_ble.md` (AmiiboLink) — keep web/ and firmware in sync with these when changing either side.

## Firmware build

Requires nRF52 SDK via env var `NRF52_SDK_ROOT` (Makefile: `SDK_ROOT := $(NRF52_SDK_ROOT)`); nothing builds without it. Easiest path is the preconfigured Docker image `solosky/nrf52-sdk:latest` (`fw/docker/Dockerfile`).

```
git submodule update --init --recursive   # required: fw/components/* are submodules
cd fw && make all BOARD=OLED RELEASE=1    # or BOARD=LCD
```

- `BOARD ?= OLED` by default (fw/application/Makefile:13); selected via `BOARD_$(BOARD)` define.
- Build order is fixed: bootloader → app → ota (`make all`). Clean with `make clean` (removes `fw/_build/`).
- Outputs: `fw/_build/pixjs_all.hex` (firmware), `fw/_build/pixjs_ota_vXXXX.zip` (OTA package).
- `APP_VERSION` defaults to GitHub run number in CI; set manually when building locally.
- Flash targets (fw/application/Makefile): `make flash` (binary via nrfjprog), `make flash_softdevice`, `make flash_ocd` (openocd, configs in `fw/application/openocd/`).
- SDK config: `fw/application/config/sdk_config.h`; linker scripts in `fw/ld/`.

## Bootloader + DFU

- `fw/bootloader/` — secure DFU bootloader. `make privgen` generates `priv.pem` (keep safe — OTA packages are signed with it); `make settingsgen` after building app+bl.
- OTA package via nrfutil: `--hw-version 52`, `--sd-req 0x0103` (softdevice s112), `--application-version` must increment each release (see `fw/bootloader/readme.md`).
- DFU update flow: hold button while installing battery → BLE device "pixl dfu" → nrf Connect → DFU tab → transfer zip. Unstable battery voltage causes mysterious interrupts.

## Codegen (do not hand-edit generated data)

`fw/application/Makefile` invokes `fw/scripts/*.py` during build (Python 3, deps in `fw/scripts/requirements.txt`):
- `amiibo_db_gen.py` — Amiibo database → generated C sources. Source data: `fw/data/amiidb_{amiibo,game,link}.csv` (character/game/series names, EN+ZH).
- `i18n_gen.py` — translations → `fw/application/src/i18n/` (`*.c` per locale, e.g. `zh_Hans.c`); edit source translation files (`fw/data/i18n.csv`), not generated output.
- `font_data_gen.py` — CJK/font data; runs `bdfconv` binaries shipped in `fw/scripts/` (per-OS: `bdfconv.exe`, `bdfconv_linux`, `bdfconv_macos_universal`) over `fw/data/*.bdf` + `chinese3.txt`/`gb2312a.txt`.
- `resource_gen.py`, `version_gen.py` — app icons/resources/version. Icons source: `fw/resources/{aseprite,bmp}/app_*_32x32.{aseprite,bmp}`.
- Standalone (not invoked by build): `amiibo_tree_gen.py`, `key_data_gen.py`, `video_clip_gen.py`.

## Web app

`cd web && npm install && npm run dev` (webpack-dev-server, old toolchain: webpack 2, babel 6 — needs old Node; don't "modernize" casually). `npm run build` → `web/dist/`. `web/Makefile` wraps install/dev/build.

- `web/src/lib/` — BLE protocol implementation: `pixl.ble.js` (BLE transport), `pixl.proto.js` (pixl data-transfer protocol); mirrors the firmware `mod/df/` protocol.
- `web/src/i18n/` — web translations (`*_js` per locale), separate files from firmware i18n.
- `web/src/router/` — vue-router routes; `RealApp.vue`/`App.vue` app shells.

## Conventions

- Firmware follows `.clang-format` at repo root / `fw/.clang-format`.
- Docs exist in en/zh/it — update all three (or at least en) when changing user-facing docs. Docs are trilingual; firmware data (CSVs, i18n) is bilingual EN+ZH.
- GPL 2.0: no Nintendo-licensed assets (keys, raw Amiibo data) in source. `key_retail.bin` is user-supplied at runtime (uploaded to device storage root), never committed.
- CI: `.github/workflows/pixl.js-fw.yml` builds both boards on `solosky/nrf52-sdk:latest` with `APP_VERSION=$GITHUB_RUN_NUMBER`.
- `fw/docs/` — internal firmware notes (`fw_readme.txt`, `notes.txt`), not user docs.
