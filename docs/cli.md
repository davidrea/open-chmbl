# Developer CLI

Each device exposes a **serial command-line shell** (over the USB/UART console) used
during development to **fake** inputs and **view** internal I/O and state. This is the
backbone of the [isolation-first build strategy](design/README.md): by overriding a
module's inputs and reading its outputs over the CLI, each design element can be
exercised on the bench without the rest of the system present.

> The CLI is a **development/debug** interface. It is disabled or gated in production
> builds (a build flag), and it never sends anything onto the motorcycle CAN bus.

---

## 1. The source-override model (why the CLI works)

Every external I/O signal in the firmware is read through a small indirection with two
sources:

```
            ┌─────────────┐
real I/O ──▶│             │
            │  signal     │──▶ consumed by firmware
fake (CLI)─▶│  (source:   │
            │  real|fake) │
            └─────────────┘
```

- **`real`** — the value comes from the actual peripheral (CAN frame, ADC, radio).
- **`fake`** — the value comes from a CLI-injected constant until cleared.

Switching a signal's source to `fake` lets you drive a downstream module
deterministically; switching it back to `real` restores normal operation. Outputs are
always **viewable** regardless of source. This single mechanism is what makes
"implement one design element in isolation" practical:

| To test in isolation… | Fake these inputs | View these outputs |
|------------------------|-------------------|--------------------|
| ESP-NOW link (DE-01) | TX: `state`; | BL: `link`, `render` |
| Auto-brightness (DE-02) | BL: `ambient` | BL: `render` (brightness) |
| Link-loss failsafe (DE-03) | (stop TX heartbeat) | BL: `link`, `render` |
| Status indicator (DE-10) | BL: `ind` (force code/color) | BL: `ind show` |
| CAN decode (DE-08) | TX: replay capture / `can` frames | TX: `sig` (decoded) |
| Braking state machine (DE-09) | TX: `sig` (wheel/clutch/gear) | TX: `state` (state + accel + timers) |

---

## 2. Common command grammar

`<domain> <verb> [args]`. Shared across both devices:

| Command | Purpose |
|---------|---------|
| `help [domain]` | List commands. |
| `version` | Firmware version / build flags. |
| `reset` | Soft reset the device. |
| `config show` | Dump current config (NVS-backed). |
| `config set <key> <value>` | Set a config value. |
| `config save` | Persist config to NVS. |
| `pair start` / `pair clear` / `pair status` | Manage the ESP-NOW peer + key. |
| `stats` | One-shot dump of link / health counters. |
| `log <level>` | Set console log verbosity. |

Conventions: signals that support faking accept `... source real|fake` and
`... set <value>` (which implies `source fake`); `... show` reads the live value and
its current source.

---

## 3. Transmitter CLI

Realizes [TX-CLI-1…5](feature-functions.md#tx-cli--developer-cli).

| Command | Purpose | FFL |
|---------|---------|-----|
| `sig show` | Show all decoded signals, units, validity, and source (incl. derived `accel` in MPH/s). | TX-CLI-2 |
| `sig set wheel <mph>` | Fake wheel speed (the primary braking input; drives derived `accel`). | TX-CLI-1 |
| `sig ramp wheel <mph/s> [until <mph>]` | Ramp faked wheel speed at a constant rate so the derived `accel` exercises the on/off thresholds (a constant `sig set wheel` gives `accel ≈ 0`). | TX-CLI-1 |
| `sig set clutch <0\|1\|na>` | Fake clutch (or mark unavailable). | TX-CLI-1 |
| `sig set gear <n\|N\|na>` | Fake gear position (`N` = neutral, or mark unavailable). | TX-CLI-1 |
| `sig set throttle <0..100>` | Fake throttle % (diagnostics). | TX-CLI-1 |
| `sig set rpm <n>` | Fake engine RPM (diagnostics). | TX-CLI-1 |
| `sig source can\|fake [name]` | Switch a signal (or all) back to live CAN. | TX-CLI-1 |
| `state show` | Show current state (`OFF`/`BRAKING`/`STOPPED`), emitted `brake_state_t`, derived `accel`, and active timers. | TX-CLI-3 |
| `state force OFF\|BRAKE\|auto` | Force/release the emitted output state. | TX-CLI-3 |
| `can show` | CAN stats: bit rate, frame rate, IDs seen, bus health. | TX-CLI-4 |
| `can replay <name>` | Feed a stored capture through the decoder (bench). | TX-CLI-1 |
| `net show` | ESP-NOW peer, seq, TX rate, send success/fail. | TX-CLI-4 |
| `net rate <hz>` | Set heartbeat rate (dev). | TX-CLI-5 |
| `net send` | Force one heartbeat now. | TX-CLI-4 |
| `power show` | Sleep/wake state, parked-draw mode. | — |

## 4. Brake_light CLI

Realizes [BL-CLI-1…5](feature-functions.md#bl-cli--developer-cli).

| Command | Purpose | FFL |
|---------|---------|-----|
| `in show` | Show the incoming state, its source, and last-rx age. | BL-CLI-1 |
| `in set state OFF\|DECEL\|BRAKE` | Fake the incoming braking state. | BL-CLI-1 |
| `in source link\|fake` | Switch back to the live radio link. | BL-CLI-1 |
| `ambient show` | Show ambient lux + resulting brightness target. | BL-CLI-2 |
| `ambient set <lux>` | Fake ambient light. | BL-CLI-2 |
| `ambient source sensor\|fake` | Switch back to the live sensor. | BL-CLI-2 |
| `batt show` | Show SoC, voltage, charge state. | BL-CLI-3 |
| `batt set <pct>` | Fake state-of-charge. | BL-CLI-3 |
| `render show` | Show effective state, pattern, commanded brightness. | BL-CLI-4 |
| `led test <pattern>` | Drive a fixed pattern (bench LED check). | BL-CLI-4 |
| `link show` | Link state, last-rx age, timeout, failsafe status. | BL-CLI-5 |
| `bright cap <0..100>` | Set the user brightness cap. | BL-CLI-5 |
| `ind show` | Status-indicator: current code, color/blink, source. | BL-CLI-6 |
| `ind test <code\|color\|off>` | Force an indicator code/color (preview a blink code). | BL-CLI-6 |
| `ind source status\|fake` | Switch back to live status aggregation. | BL-CLI-6 |

---

## 5. Implementation notes

> **Status:** first cut landed on **both** devices (DE-00 🟡). The REPL is up with
> `help` and `id` (chip unique ID / base MAC + chip info) on each, plus one
> stand-in domain per device: `light [on|off|toggle]` on `brake_light` and
> `state [off|brake]` on the transmitter. It builds for both `esp32c3` (USB
> Serial/JTAG console) and `esp32` (UART console). The source-override registry
> and the full domain commands below are still to come.

- **Transport:** line-based over the console. On the ESP32-C3 the default is the
  built-in **USB Serial/JTAG** controller — an enumerated virtual COM port over the
  native USB pins that carries JTAG debugging on the same cable simultaneously, so
  no external USB-TTL adapter is needed. UART remains a compile-time fallback. Built
  on ESP-IDF's `esp_console` REPL (tokenizer + command registry); fixed-size buffers.
- **Source registry:** a central table of overridable signals, each with
  `{ source, fake_value, live_getter }`, so `... set` / `... source` / `... show`
  are generic over signal name.
- **Safety:** compiled behind a `CONFIG_CHMBL_CLI` build flag; in production the shell
  is absent. CLI fakes never reach the CAN transmit path (there is none — TX is
  listen-only).
- **Shared:** the shell core (tokenizer, registry, common commands) is shared between
  both devices' firmware; each device registers its own `sig`/`in`/`ambient`/… domains.

The detailed design of the shell framework is design element **DE-00** in
[`docs/design/README.md`](design/README.md).
