---
name: chmbl-build-and-env
description: Recreate the open-chmbl development environment from scratch and build everything — ESP-IDF release-v5.3 install, per-firmware idf.py builds (transmitter/brake_light/logger, esp32c3 + esp32 targets), the host-side golden-test harness, and the Python tooling env — plus the known build traps (TWAI `driver` dep, -Werror=format, COMPONENTS=main trimming, stale build dirs) and what "matches CI" means. Load when setting up a fresh machine/container, when a build fails to configure or compile, when switching idf.py targets, or when reproducing the CI matrix locally. Do NOT load for flashing/console/field operation (chmbl-run-and-operate), for interpreting tool output (chmbl-diagnostics-and-tooling), for Kconfig/tunable semantics (chmbl-config-and-flags), or for runtime bug triage (chmbl-debugging-playbook).
---

# open-chmbl: build and environment runbook

Goal: from a bare Linux machine (or fresh container) to (a) all five firmware
images building, (b) the host golden test passing, and (c) the Python tools
runnable — with every known trap called out before you hit it.

Facts verified against the repo as of 2026-07-07 (branch
`claude/skill-library-continuity-mib7ua`, HEAD `9b4ff74`). Commands marked
**VERIFIED-RUN** were executed in this environment with the output shown.
Commands marked **from-docs** are derived from the repo's READMEs, CI workflow,
and official ESP-IDF documentation — they could not be executed here (no
ESP-IDF in this container) and idf.py output is deliberately not invented.

## Jargon (once)

| Term | Meaning here |
|---|---|
| **ESP-IDF** | Espressif's official SDK/build system for ESP32-family chips. Provides `idf.py` (a wrapper over CMake + Ninja + toolchains). |
| **target** | The chip a firmware is configured for: `esp32c3` (RISC-V, the product chip) or `esp32` (classic Xtensa, interim dev hardware / the WROVER-KIT logger). |
| **sdkconfig / Kconfig** | ESP-IDF's compile-time configuration system. `Kconfig.projbuild` declares project options; `sdkconfig.defaults[.<target>]` are committed defaults; the generated `sdkconfig` file is gitignored and board-specific. |
| **component** | ESP-IDF's build unit. Each firmware has one app component, `main/`. Dependencies are declared in `main/CMakeLists.txt` (`PRIV_REQUIRES`). |
| **managed component** | A component fetched from the Espressif component registry by the IDF Component Manager, declared in `main/idf_component.yml`, downloaded into gitignored `managed_components/`. |
| **TWAI** | "Two-Wire Automotive Interface" — Espressif's name for their CAN 2.0 controller. This project uses it strictly listen-only. |
| **CAN / ESP-NOW / DBC / .trc** | Vehicle bus / Espressif's connectionless Wi-Fi protocol / CAN signal-definition file format / PCAN trace capture format. Full domain treatment: `chmbl-can-reference`. |
| **SMC** | State Machine Compiler (smc.sourceforge.net), a Java tool that compiles a `.sm` state-machine model to C. Planned for DE-09; not yet wired in (see §6). |

## When NOT to use this skill

- Flashing images, serial consoles, the `chmbl>` CLI, pairing, logger field use → `chmbl-run-and-operate`.
- What a Kconfig option or FSM tunable *means* / how to add one → `chmbl-config-and-flags`.
- Interpreting `golden_check.py` / `trc_viz.py` / `gen_profile.py` results → `chmbl-diagnostics-and-tooling`.
- A build that used to work now fails and you want symptom→cause triage → `chmbl-debugging-playbook` (this skill lists the traps; that one has the full stories).
- What counts as passing evidence / adding CI rows → `chmbl-validation-and-qa`.

## 0. What "everything builds" means (CI parity)

CI (`.github/workflows/firmware-build.yml`) is the definition of green. It has
**five firmware matrix rows** plus **one host job**:

| # | Job | Project path | Target |
|---|---|---|---|
| 1 | build brake_light (esp32c3) | `brake_light/software` | esp32c3 |
| 2 | build brake_light (esp32) | `brake_light/software` | esp32 |
| 3 | build logger (esp32) | `logger/software` | esp32 |
| 4 | build transmitter (esp32c3) | `transmitter/software` | esp32c3 |
| 5 | build transmitter (esp32) | `transmitter/software` | esp32 |
| 6 | `can-decode-golden` (host) | gen_profile staleness diff + host harness build + `tools/golden_check.py` | ubuntu, Python 3.12 |

All firmware rows use `espressif/esp-idf-ci-action@v1` with
`esp_idf_version: release-v5.3`. **"Green locally" = all 5 firmware builds on
release-v5.3 AND the host golden job (§4).** Building only your current target
is not parity — the `-Werror=format` incident (`4571558`) broke all four
transmitter/brake_light rows at once from code that looked fine locally.

## 1. Host prerequisites

| Tool | Needed for | Version notes (as of 2026-07-07) |
|---|---|---|
| git, cmake ≥ 3.16, C compiler | host harness (§4); ESP-IDF needs cmake too | cmake 3.28 verified working here |
| Python 3 | tools/, golden test | CI pins 3.12; 3.11 verified working here |
| ESP-IDF release-v5.3 + its prerequisites | firmware builds (§2–3) | pin to release-v5.3 to match CI |
| `uv` (optional) | `tools/trc_viz.py` self-contained run (§5) | uv 0.8.17 verified here |
| JRE (Java) | **future** SMC pre-build step only (§6) | not needed for anything that builds today |

## 2. Install ESP-IDF release-v5.3 (from-docs — cannot be executed in this container)

The firmware READMEs (`transmitter/software/README.md`,
`brake_light/software/README.md`, `logger/software/README.md`) say "Requires
ESP-IDF (v5.3+ / v5.2+) with the IDF environment sourced" and link Espressif's
get-started guide. CI pins `release-v5.3`; **use that exact branch locally** so
you hit the same component graph CI does (see the `esp_driver_twai` trap, §7).

Official install.sh/export.sh flow (from Espressif's get-started docs, labeled
from-docs):

```bash
# One-time: OS prerequisites (Ubuntu/Debian; see Espressif docs for others)
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Clone the pinned release branch
mkdir -p ~/esp && cd ~/esp
git clone -b release-v5.3 --recursive https://github.com/espressif/esp-idf.git

# Install toolchains for both chips this repo targets
cd ~/esp/esp-idf
./install.sh esp32,esp32c3

# Per-shell: put idf.py on PATH (repeat in every new shell, or alias it)
. ~/esp/esp-idf/export.sh
```

Sanity check: `idf.py --version` should report a 5.3.x version string
(UNVERIFIED here — confirm on your machine).

## 3. Build each firmware (from-docs; commands match the READMEs + CI matrix)

General shape, per project, with the IDF environment sourced:

```bash
cd <project>/software
idf.py set-target <target>   # writes/regenerates sdkconfig for that chip
idf.py menuconfig            # optional: per-board GPIO overrides (see below)
idf.py build
```

### 3.1 transmitter (`transmitter/software`) — targets esp32c3 and esp32

```bash
cd transmitter/software
idf.py set-target esp32c3    # product target; or esp32 for interim dev hw
idf.py build
```

### 3.2 brake_light (`brake_light/software`) — targets esp32c3 and esp32

```bash
cd brake_light/software
idf.py set-target esp32c3    # or esp32
idf.py build
```

### 3.3 logger (`logger/software`) — target esp32 only (ESP-WROVER-KIT v4.1)

```bash
cd logger/software
idf.py set-target esp32
idf.py build
```

The logger is the only firmware with a **managed dependency**:
`logger/software/main/idf_component.yml` declares

```yaml
dependencies:
  idf:
    version: ">=5.2"
  espressif/button: "^4"
```

On the first configure (any of `set-target` / `build` / `idf.py reconfigure`)
the IDF Component Manager contacts the Espressif component registry, resolves
`espressif/button` (the debounced `iot_button` component, any 4.x), downloads
it into `managed_components/` and writes `dependencies.lock` — both gitignored
(`logger/software/.gitignore`). This needs network access once; afterwards the
cache is reused. If `managed_components/` gets corrupted or you change
`idf_component.yml`, delete `managed_components/` + `dependencies.lock` and run
`idf.py reconfigure` to re-fetch. Everything else the logger uses (TWAI, FATFS,
SDMMC, timers) is ESP-IDF itself via `PRIV_REQUIRES` in `main/CMakeLists.txt`.

### 3.4 Target switching regenerates sdkconfig (trap)

`idf.py set-target <t>` **deletes and regenerates `sdkconfig`** from
`sdkconfig.defaults` + `sdkconfig.defaults.<target>` and resets the build
directory. Consequences:

- Any `menuconfig` change you made and did not port into a defaults file is
  **lost** when you switch targets. The generated `sdkconfig` is gitignored on
  purpose; durable settings belong in `sdkconfig.defaults[.<target>]`
  (see `chmbl-config-and-flags`).
- Switching targets by editing `sdkconfig` by hand, or building after a
  half-switched state, produces confusing CMake cache errors. The clean reset is:

  ```bash
  idf.py fullclean          # or: rm -rf build sdkconfig sdkconfig.old
  idf.py set-target <t>
  idf.py build
  ```

Per-target committed defaults (verified): common `sdkconfig.defaults` sets
`CONFIG_LOG_DEFAULT_LEVEL_INFO=y`; `sdkconfig.defaults.esp32c3` routes the
console to USB Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, enumerates
as `/dev/ttyACM*`); `sdkconfig.defaults.esp32` uses UART0
(`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, `/dev/ttyUSB*` @ 115200). The logger has
a single `sdkconfig.defaults` (esp32 only) adding `CONFIG_SPIRAM=y` and
`CONFIG_FATFS_LFN_STACK=y`.

### 3.5 menuconfig: per-board GPIO options

`idf.py menuconfig` → the project's own menu. Option semantics live in
`chmbl-config-and-flags`; for bring-up you mainly need the pins:

| Project | Menu | Key options (defaults) |
|---|---|---|
| transmitter | *Transmitter configuration* | `CHMBL_STATE_GPIO` (8 on C3, 2 on classic — the devkit LED); `CHMBL_CAN_TX_GPIO`/`CHMBL_CAN_RX_GPIO` (C3: 4/5, classic: 21/22); `CHMBL_NET_CHANNEL` (6); `CHMBL_CLI` (y) |
| brake_light | *Brake_light configuration* | `CHMBL_LIGHT_GPIO` (8 on C3, 2 on classic); `CHMBL_NET_CHANNEL` (6, must match transmitter); `CHMBL_CLI` (y) |
| logger | *CAN logger configuration* + *Status LED* | button IO33, CAN TX/RX IO26/IO27, 500 kbit/s, listen-only; status LED GPIO0 |

On the classic ESP32 avoid GPIO6–11 (wired to SPI flash — noted in the
Kconfig help text).

Flashing and consoles are out of scope here → `chmbl-run-and-operate`.

## 4. Host-side golden-test harness (VERIFIED-RUN in this container)

This is the part of CI you can always run without hardware or ESP-IDF. It
builds the transmitter's decoder core (`can_decode.c` +
`bike_profile_triumph_tr.c` — pure C, no IDF headers) into a host binary
`trc_replay`, replays the committed ride capture through it, and asserts
agreement with python-cantools.

```bash
cd /path/to/open-chmbl

# 1. Python deps (once)
pip install -r tools/requirements.txt

# 2. Build the host harness
cmake -S transmitter/software/test_host -B transmitter/software/test_host/build \
      -DCMAKE_BUILD_TYPE=Release
cmake --build transmitter/software/test_host/build

# 3. Run the golden comparison
python3 tools/golden_check.py
```

**Actual output recorded 2026-07-07** (step 2 ends with
`[100%] Built target trc_replay`; step 3 takes ~3 s):

```
frames=315869 accel_mphps_min=-4.73 accel_mphps_max=4.00 wheel_speed_mph_last=0.00
PASS: 183944 signal values identical between the C decoder and cantools
```

Reading it: the first line is `trc_replay`'s stderr summary (315,869 frames in
`logger/40mph_drive_cycle.trc`); the `PASS` line is golden_check's verdict over
183,944 compared signal values (exit 0). Any `MISMATCH`/`MISSING`/`EXTRA`
lines followed by `FAIL: N mismatch(es) ...` mean the C decoder and cantools
disagree — interpretation and tolerances (rel 1e-4 / abs 1e-3) are covered in
`chmbl-diagnostics-and-tooling`. Note: the row/frame counts above are for the
current committed capture + DBC and will change if either changes.

The harness compiles with `-Wall -Wextra -Werror`
(`transmitter/software/test_host/CMakeLists.txt`) — warnings in `can_decode.c`
fail the host build before they ever reach the target toolchain.

CI's golden job also checks the **generated profile is not stale** before the
harness. Reproduce locally (VERIFIED-RUN, exits 0 with no diff today):

```bash
python3 tools/gen_profile.py profiles/triumph_tr.dbc \
  --name "Triumph Speed 400 / Scrambler 400X (TR-series)" \
  --bitrate 500000 --symbol bike_profile_triumph_tr \
  --out /tmp/bike_profile_triumph_tr.c
diff -u transmitter/software/main/bike_profile_triumph_tr.c /tmp/bike_profile_triumph_tr.c
```

Never hand-edit `bike_profile_triumph_tr.c`; regenerate it from the DBC.

## 5. Python tooling environments — two distinct models

**Model A — `tools/requirements.txt`** (for `golden_check.py`,
`gen_profile.py`, and CI):

```
cantools>=39
python-can>=4.3
numpy>=1.26
dearpygui>=1.11
```

`pip install -r tools/requirements.txt` (CI uses Python 3.12; 3.11 verified
working). Prefer a venv on a real workstation.

**Model B — `tools/trc_viz.py` is self-contained** via a PEP 723 inline
metadata block (`# /// script ... ///` at the top of the file declaring the
same four deps, `requires-python = ">=3.11"`). Run it with uv and **no prior
pip install**:

```bash
uv run tools/trc_viz.py logger/40mph_drive_cycle.trc                    # GUI — needs a display
uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check   # no display needed
```

**dearpygui needs a display**: the GUI path fails in headless
containers/CI. `--headless-check` avoids importing dearpygui entirely (it is
imported only inside the GUI function) and prints a ride summary + a
peak-speed sanity check. **Actual `--headless-check` output recorded
2026-07-07** (VERIFIED-RUN via `uv run`):

```
duration        :    220.1 s
grid points     :    11005  @ 50 Hz
peak speed      :     43.2 mph
peak rpm (live) :     4522 rpm
peak throttle   :     29.8 %
gears seen      : [0, 1, 2, 3, 4]
accel range     : -6.8 .. +21.0 mph/s
FSM transitions :       29
brake light on  :     89.0 s (40% of ride)
peak-speed sanity (43 mph ± 3): PASS
```

(These numbers depend on the tool's current default tunables and the committed
capture; treat the *shape* as stable, the values as current-as-of-date.
Interpretation → `chmbl-diagnostics-and-tooling`.)

## 6. Future requirement: JRE for the SMC pre-build step (documented, NOT yet wired)

`docs/firmware.md §4` and `transmitter/software/README.md` specify that the
DE-09 braking state machine will be authored as
`transmitter/software/state_machine/brake_fsm.sm` and compiled to C at build
time by SMC (`tools/smc/Smc.jar`) via a CMake `add_custom_command` — requiring
**a JRE on every build host** (`find_program(JAVA_EXECUTABLE java REQUIRED)`).

**Status as of 2026-07-07: neither `brake_fsm.sm` nor `tools/smc/` exists in
the repo, and no CMakeLists references SMC or java.** DE-09 is unimplemented
(status 🔲). Today's builds need **no** Java. When the DE-09 campaign lands
this step (`chmbl-de09-campaign`), fresh environments will additionally need a
JRE (OpenJDK 21 is present in this container and suffices for `java -jar`).
Do not add the JRE dependency to CI/readme claims before the step actually
exists.

## 7. Known build traps

| Trap | Symptom | Cause / fix | Evidence |
|---|---|---|---|
| TWAI component name on IDF 5.3 | CMake configure fails: `Failed to resolve component 'esp_driver_twai'` | The standalone `esp_driver_twai` component only exists on IDF ≥ 5.5. On release-v5.3 (what CI pins), `driver/twai.h` comes from the umbrella `driver` component — the transmitter's `main/CMakeLists.txt` must (and does) list `driver` in `PRIV_REQUIRES`. If you dev on a newer IDF, don't "modernize" this dep. | commit `dd09fef` |
| `-Werror=format` on target builds | All 4 transmitter/brake_light CI rows fail on a printf that host builds accepted | On the Xtensa/RISC-V toolchains `uint32_t` is `long unsigned int`, so `printf("%u", some_uint32)` is a format error under `-Werror`. Use `PRIu32` from `<inttypes.h>` for 32-bit fields (u8/u16 promote to int, `%u` is fine). Host gcc won't catch this — only CI/target builds do. | commit `4571558` |
| COMPONENTS=main trimming | Adding a call into a new IDF subsystem fails to link/compile even though "IDF has it" | Each top-level `CMakeLists.txt` sets `set(COMPONENTS main)`, so ESP-IDF builds ONLY `main`'s declared dependency closure instead of every discoverable component. New subsystem ⇒ add it to `PRIV_REQUIRES` in that firmware's `main/CMakeLists.txt`. Don't remove the trim — it exists to keep builds fast and the dep graph honest. | commits `6b189e6`, `f4ef5a6`; read the comments in `transmitter/software/CMakeLists.txt` |
| Stale build dir / sdkconfig after switching targets or IDF versions | Bizarre CMake cache errors, wrong-console builds, options silently reverting | `set-target` regenerates `sdkconfig` (menuconfig-only changes lost, §3.4). After changing IDF version, target, or defaults files: `idf.py fullclean` (or `rm -rf build sdkconfig sdkconfig.old`) then re-run `set-target` + `build`. For the logger also nuke `managed_components/` + `dependencies.lock` if the component manager misbehaves. | gitignore design in `*/software/.gitignore` |
| dearpygui needs a display | `uv run tools/trc_viz.py <trc>` crashes/does nothing in a headless container | GUI requires a display server. Use `--headless-check` (§5), which never imports dearpygui. | `tools/trc_viz.py` (dearpygui imported only inside `run_gui`) |
| Building one target and calling it green | Local build passes, CI matrix fails | CI is 5 firmware rows + host golden job (§0). Format-string and component-graph errors are target-specific. Before pushing firmware changes, build both targets of the touched firmware and run §4. | incidents `4571558`, `dd09fef` |

Deeper stories behind these (and runtime traps) → `chmbl-debugging-playbook`
and `chmbl-failure-archaeology`.

## 8. Fresh-environment checklist

- [ ] `pip install -r tools/requirements.txt` (or venv equivalent)
- [ ] Host harness builds: `cmake -S transmitter/software/test_host -B transmitter/software/test_host/build -DCMAKE_BUILD_TYPE=Release && cmake --build transmitter/software/test_host/build` → `Built target trc_replay`
- [ ] `python3 tools/golden_check.py` → `PASS: ... signal values identical ...`
- [ ] gen_profile staleness diff (§4) → empty diff
- [ ] `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check` → `peak-speed sanity ... PASS`
- [ ] (with hardware/toolchain) ESP-IDF release-v5.3 installed, `. export.sh` sourced
- [ ] (with toolchain) all 5 CI-matrix builds succeed (§0) — at minimum both targets of any firmware you touched

## Provenance and maintenance

| Fact class | Re-verify with |
|---|---|
| CI matrix rows, pinned IDF version, golden-job steps | `cat .github/workflows/firmware-build.yml` |
| Host harness + golden commands and output shape | run §4 verbatim from the repo root |
| requirements.txt contents / trc_viz PEP 723 block | `cat tools/requirements.txt; head -12 tools/trc_viz.py` |
| Logger managed dependency | `cat logger/software/main/idf_component.yml` |
| COMPONENTS=main trim + PRIV_REQUIRES graphs | `grep -n "COMPONENTS\|PRIV_REQUIRES" */software/CMakeLists.txt */software/main/CMakeLists.txt` |
| SMC/JRE still unwired? | `ls transmitter/software/state_machine tools/smc 2>&1` (errors ⇒ still unwired); `grep -rn "Smc.jar" --include=CMakeLists.txt .` |
| Per-target sdkconfig defaults | `cat {transmitter,brake_light}/software/sdkconfig.defaults* logger/software/sdkconfig.defaults` |
| Kconfig menu names / GPIO defaults | `cat */software/main/Kconfig.projbuild` |
| Trap commits still accurate | `git show --stat dd09fef 4571558 6b189e6 f4ef5a6` |
| headless-check output values | `uv run tools/trc_viz.py logger/40mph_drive_cycle.trc --headless-check` |
