---
name: chmbl-can-reference
description: Domain-theory pack for open-chmbl — CAN 2.0 frames/IDs/ACK, why listen-only mode exists, ESP32 TWAI silent mode, DBC syntax and Intel-vs-Motorola bit ordering, PCAN .trc v2.1 format, the Speed 400 reference signal map, ESP-NOW + the 8-byte chmbl_msg_t, and motorcycle vocabulary (clutch/gear/neutral/engine-braking/ride-by-wire). Load when you need to understand, decode, or reason about CAN frames, the DBC profile, .trc captures, the wireless protocol, or bike behavior. Do NOT load for build/flash mechanics (chmbl-build-and-env, chmbl-run-and-operate), tool usage guides (chmbl-diagnostics-and-tooling), or FSM implementation work (chmbl-de09-campaign).
---

# chmbl-can-reference — the domain knowledge this repo assumes

This is the theory pack: everything about CAN, DBC, .trc, TWAI, ESP-NOW, and
motorcycles that the rest of the repo takes for granted, taught against this
repo's actual files. Every quoted constant below was read from the repo on
2026-07-07.

## 1. CAN 2.0 in ten minutes — and why "listen-only" is a hard rule

**CAN** (Controller Area Network) is the two-wire (CAN-H/CAN-L, differential)
broadcast bus that connects a vehicle's ECUs (electronic control units). Key
properties, as they matter here:

- **Frames, not packets to an address.** A CAN 2.0A data frame carries an
  **11-bit identifier** (0x000–0x7FF) and **0–8 payload bytes** (the DLC —
  data length code — says how many). The ID identifies the *message content*,
  not a destination; every node hears every frame. Lower ID = higher priority
  (arbitration is bitwise; a dominant 0 beats a recessive 1). CAN 2.0B adds
  29-bit "extended" IDs — the Speed 400 uses standard 11-bit IDs only (every
  ID in `logger/40mph_drive_cycle.trc` is ≤ 3 hex digits).
- **Bit rate** is a bus-wide constant every node must share. Common automotive
  rates: 125 k / 250 k / 500 k / 1 Mbit/s. The reference bike is **500 kbit/s**
  (confirmed — see `docs/can-profiles.md` §5). A wrong bit rate does not give
  you garbled data; it gives you *error frames and nothing decoded*.
- **The ACK slot — this is the whole reason listen-only exists.** Every CAN
  frame ends with an ACK slot: the transmitter sends it recessive, and *every
  other node that received the frame correctly* drives it dominant. ACKing is
  not optional politeness — a node that ACKs is an active electrical
  participant in the bus protocol. Conversely, a node in **listen-only /
  silent mode** never drives the bus at all: no ACK, no error flags, no
  transmissions. It is electrically invisible except for its receiver load.
  This project's non-negotiable rule ("never ACK/transmit on the bike bus",
  `docs/can-profiles.md` §1) is enforced by running the controller in silent
  mode, not by merely "not sending".
- **Error frames and bus-off, in one paragraph.** CAN nodes police each other:
  a node that detects a malformed frame transmits an **error frame** (six
  dominant bits) that stomps the frame for everyone, forcing a retransmit.
  Each node keeps error counters; a node that keeps erroring goes
  *error-passive* and eventually **bus-off** — it removes itself from the bus.
  This is why a misbehaving add-on device is dangerous on a vehicle: a node
  ACKing at the wrong timing, or transmitting with a wrong bit rate, can
  corrupt traffic between the bike's own safety-relevant ECUs. A listen-only
  node cannot generate error frames, so it cannot do any of this.

**Traffic model:** the Speed 400's diagnostic port carries **free-running
broadcast** traffic — ECUs chatter continuously without being polled
(confirmed experimentally; `docs/can-profiles.md` §5). This matters because a
listen-only device can only work on a broadcast bus; a request/response
(OBD-II/UDS polling) bus would require transmitting, which is forbidden here.
Peak observed rate ≈ 1500 frames/s (comment in
`logger/software/main/logger_main.c`, `can_init()`).

## 2. TWAI — the ESP32's CAN controller, and how this repo configures it

**TWAI** ("Two-Wire Automotive Interface") is Espressif's name for the ESP32
family's on-chip CAN 2.0 controller (driver: `driver/twai.h` in ESP-IDF). Two
things a newcomer must know:

1. **The chip has only the protocol controller.** It exposes logic-level TX/RX
   pins; it cannot drive the differential CAN-H/CAN-L pair. An external
   **transceiver** — this project uses the **SN65HVD230** (3.3 V, TI) — sits
   between the ESP32 pins and the bus (see `docs/hardware.md`,
   `logger/software/README.md`). Note: even in listen-only mode the driver
   still claims a TX GPIO (the logger's Kconfig help says exactly this).
2. **Listen-only is a driver mode constant**, set at `twai_driver_install()`
   time — not something you can drift into or out of at runtime.

How each firmware actually configures it (quote-verified):

**Transmitter** — `transmitter/software/main/can_rx.c`, `can_rx_init()`:

```c
twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
    (gpio_num_t)CONFIG_CHMBL_CAN_TX_GPIO,
    (gpio_num_t)CONFIG_CHMBL_CAN_RX_GPIO,
    TWAI_MODE_LISTEN_ONLY);
```

Unconditional `TWAI_MODE_LISTEN_ONLY`; there is no code path to normal mode.
Timing comes from the profile bitrate (`TWAI_TIMING_CONFIG_500KBITS()` for the
Triumph profile, 250k supported, anything else warns and falls back to 500k).
It also installs a single SJA1000-style **acceptance filter** computed over
the profile's IDs (`profile_filter()`): bits that differ between any two
profile IDs become don't-care, so it over-accepts but sheds most bus traffic
in hardware; the decoder ignores non-profile IDs anyway.

**Logger** — `logger/software/main/logger_main.c`, `can_init()`:

```c
#if CONFIG_LOGGER_CAN_LISTEN_ONLY
    const twai_mode_t mode = TWAI_MODE_LISTEN_ONLY;
#else
    const twai_mode_t mode = TWAI_MODE_NORMAL;
#endif
```

`LOGGER_CAN_LISTEN_ONLY` is a Kconfig bool, **default y**
(`logger/software/main/Kconfig.projbuild`). The normal/ACK escape hatch exists
only for a two-node *bench* (a lone listen-only node on a bench bus sees
nothing, because nobody ACKs anybody). **Never flash a normal-mode logger
config onto a device that will touch a real bike.** The logger uses
`TWAI_FILTER_CONFIG_ACCEPT_ALL()` (no filtering — it records everything), a
128-entry RX queue to ride out SD write stalls, and leaves the controller
STOPPED until recording starts (the fix for the idle-watchdog crash, commit
`b860f06` — history owned by `chmbl-failure-archaeology`).

**Rule of thumb:** transmitter filters and decodes; logger accepts all and
records. Both are silent on the wire.

## 3. DBC files and the Intel-vs-Motorola trap

A **DBC** file is the automotive-standard text format describing what the bits
in CAN frames mean. This repo's committed ground truth is
`profiles/triumph_tr.dbc`; `tools/gen_profile.py` generates
`transmitter/software/main/bike_profile_triumph_tr.c` from it (never hand-edit
the generated file — CI diffs a regeneration against it).

### Syntax by worked example

Real lines from `profiles/triumph_tr.dbc`:

```
BO_ 258 WHEEL_SPEEDS: 8 ECU
 SG_ WHEEL_SPEED_FRONT : 15|16@0+ (0.0625,0) [0|4095] "km/h" Vector__XXX
```

- `BO_ 258 WHEEL_SPEEDS: 8 ECU` — a message: CAN ID **258 decimal = 0x102**,
  name, **8-byte** payload, sent by node `ECU`. (DBC IDs are decimal — a
  classic source of confusion when the rest of the project talks hex.)
- `SG_ … : 15|16@0+ (0.0625,0) [0|4095] "km/h"` — a signal:
  - `15` = start bit, `16` = length in bits;
  - `@0` = **Motorola / big-endian**, `@1` = **Intel / little-endian**;
  - `+` = unsigned (`-` = signed, two's complement);
  - `(0.0625,0)` = `(scale, offset)` → `physical = raw × scale + offset`.
    0.0625 = 1/16, so this is the "raw/16 → km/h" wheel speed;
  - `[0|4095] "km/h"` = plausible range and unit (informational).

Two more, decoded by hand:

```
 SG_ THROTTLE_PCT : 0|8@1+ (0.392157,0) [0|100] "%" Vector__XXX
```
Intel, start bit 0, 8 bits → that is exactly **payload byte B0** (bit 0 is the
LSB of byte 0). Scale 0.392157 = 100/255, i.e. "raw/2.55 → %". Lives in
message `BO_ 320` = **0x140**.

```
 SG_ GEAR : 24|4@1+ (1,0) [0|6] "" Vector__XXX
```
Intel, start bit 24 = byte 3 bit 0, 4 bits → the **low nibble of B3**, scale
1: gear position, 0 = neutral. Lives in `BO_ 322` = **0x142**.

### Intel vs Motorola — THE trap

DBC numbers every payload bit `pos = byte_index × 8 + bit_in_byte`, where
bit_in_byte 0 is the **LSB** of the byte. Then:

- **Intel (`@1`, little-endian):** `start_bit` is the **LSB** of the value;
  the value's bits occupy `start, start+1, …` counting upward. Multi-byte
  Intel values read low byte first.
- **Motorola (`@0`, big-endian, "sawtooth"):** `start_bit` is the **MSB** of
  the value, and the walk toward the LSB is *sawtooth*: decrement within a
  byte (7←…←0), then jump to **bit 7 of the next byte**. In code, from
  `transmitter/software/main/can_decode.c` `can_sig_extract()`:

  ```c
  raw = (raw << 1) | bit;
  pos = (pos % 8u == 0u) ? pos + 15u : pos - 1u;
  ```

So `WHEEL_SPEED_FRONT : 15|16@0+`: start bit 15 = **byte 1, bit 7** (15 = 1×8
+ 7 — the MSB of B1). Sixteen bits sawtooth from there cover B1 bit7…bit0,
then B2 bit7…bit0. Net effect: **raw = (B1 << 8) | B2** — B1 is the high
byte, matching the decode table's "B1–B2, BE". `RPM_ECU : 23|16@0+` is the
same pattern one message over: B2 high byte, B3 low byte, in 0x146.

**The classic mistake** is treating a Motorola start bit as if it were the
LSB (or byte-swapping a value that was already big-endian). Symptom: values
that are ~256× too big/small, or that jump wildly as the low byte rolls over.
`cantools` (the Python library used by `tools/golden_check.py` and
`tools/trc_viz.py`) interprets `@0`/`@1` per the rules above, and the golden
test exists precisely to catch the C decoder and cantools disagreeing — if
you touch bit-order logic, `tools/golden_check.py` is your referee (usage
owned by `chmbl-diagnostics-and-tooling`).

Note the generated profile carries the DBC numbers verbatim:
`bike_profile_triumph_tr.c` has
`.wheel_speed = { .can_id = 0x102, .bit_start = 15, .bit_len = 16, .byte_order = CAN_SIG_BE, … .scale = 0.0625f }`.
`CAN_SIG_LE`/`CAN_SIG_BE` are defined in
`transmitter/software/main/bike_profile.h`.

## 4. PCAN .trc v2.1 — the capture file format

The logger writes PCAN-View **TRC file version 2.1** — a plain-text,
line-per-frame trace format — chosen because `python-can` reads and writes it
natively. Writer: `logger/software/main/trc_format.c`. The header (every
non-data line starts with `;`):

```
;$FILEVERSION=2.1
;$STARTTIME=43831.0000000
;$COLUMNS=N,O,T,B,I,d,R,L,D
```

`$STARTTIME` is a **placeholder** — the logger has no RTC, so only *relative*
offsets between frames are meaningful. A real data line from
`logger/40mph_drive_cycle.trc` (~315 k lines, ~220 s ride), annotated:

```
  99996     69738.559 DT  1      102 Rx -  8    0D 01 D9 01 DB 00 00 40
  ^^^^^     ^^^^^^^^^ ^^  ^      ^^^ ^^ ^  ^    ^^^^^^^^^^^^^^^^^^^^^^^
  msg #     time [ms] |   bus    ID  dir |  DLC payload bytes B0..B7 (hex)
                      |   (1)   (hex)    reserved
                      DT = data frame (RR = remote frame)
```

Format details from `trc_format_line()`: standard 11-bit IDs print at natural
width (≤ 3 hex digits); extended 29-bit IDs are zero-padded to 8 digits (the
reader's "length > 4 ⇒ extended" rule). Direction is always `Rx` here
(listen-only). Column widths match python-can's `TRCWriter` so round-tripping
is exact.

To load one in Python (deps via `pip install -r tools/requirements.txt`):

```python
import can
for msg in can.TRCReader("logger/40mph_drive_cycle.trc"):
    ...  # msg.arbitration_id, msg.data, msg.timestamp (seconds)
```

## 5. The reference-bike signal map (Speed 400)

Owned in full by `docs/can-profiles.md` §5; summary for working memory. Byte
offsets are 0-based; multi-byte fields big-endian:

| Signal | ID | Location | Decode |
|---|---|---|---|
| wheel_speed front | 0x102 | B1–B2 BE | raw/16 → km/h — **primary braking input** |
| wheel_speed rear | 0x102 | B3–B4 BE | raw/16 → km/h (tracks front; front/rear order *suspected*) |
| clutch_pulled | 0x142 | B5 low nibble ≠ 0 | 0x80 released, 0x8D pulled |
| gear / neutral | 0x142 | B3 low nibble | 0 = N, 1–4 observed gears |
| throttle_pct | 0x140 | B0 | raw/2.55 → % (~2.5 % at idle — ride-by-wire idle air) |
| rpm (live, coarse) | 0x140 | B6 | raw × ~31.4 → rpm; **0 = engine off** (best on/off signal) |
| rpm (ECU filtered) | 0x146 | B2–B3 BE | raw × 0.25 → rpm; ECU *target*, holds idle setpoint when stopped |
| side_stand | 0x481 | B7 bit 0 | 1 = up |
| engine_cutoff | 0x121 | B3 bit 6 AND B6 == 0x28 | kill switch; asserts ~30 ms before rpm decays |
| brake_switch | — | — | **Does not exist on this bus** (confirmed absent) |

**Confidence caveats (do not oversell these):** the map is **empirically
reverse-engineered from a single reference bike**, not Triumph documentation.
The wheel-speed `/16` and both rpm scales are calibrated against the ride's
known speed/rpm envelope and are **approximate** — good enough for the braking
FSM (which needs only the *shape* of wheel speed), not for instrumentation.
The front/rear ordering in 0x102 is suspected, not proven. Also honest gaps:
`docs/can-profiles.md` cites bench captures `logger/throttle.trc` and
`logger/wheel.trc` that are **not committed** — only `40mph_drive_cycle.trc`
is in-repo — and it says to commit captures under
`transmitter/software/captures/` while committed captures actually live under
`logger/`.

### Worked exercise: hand-decode mph from a raw 0x102 frame

Frame (message 99996, above): payload `0D 01 D9 01 DB 00 00 40`.

1. B0 = `0x0D` is a rolling counter (watch consecutive 0x102 frames: 0D, 0E,
   00, 01, 02 …) — ignore it. B7 varies frame-to-frame too (checksum-like) —
   ignore.
2. Front wheel speed = B1–B2 big-endian = `0x01D9` = **473** raw.
3. km/h = 473 × 0.0625 (i.e. /16) = **29.5625 km/h**.
4. mph = 29.5625 × 0.621371 = **18.37 mph** (firmware constant `KMH_TO_MPH
   0.621371f` in `transmitter/software/main/can_decode.h`; the FSM works in
   mph throughout).
5. Cross-check: rear = B3–B4 = `0x01DB` = 475 → 29.69 km/h — near-identical
   to front, as expected while rolling freely.

If your hand decode of B1–B2 gives 3312 raw (`0xD001` — grabbing B0–B1
little-endian), you have just committed the Motorola trap. Re-read §3.

## 6. ESP-NOW and the 8-byte wire message

**ESP-NOW** is Espressif's connectionless 2.4 GHz protocol: raw
vendor-specific Wi-Fi action frames between **MAC-addressed peers**, no AP,
no association, no TCP/IP. Latency is a few ms; payload limit **≈ 250 bytes**
(this project uses 8). Security: a global **PMK** (primary master key)
encrypts per-peer **LMKs** (local master keys); a peer registered with
`encrypt = true` gets CCMP-encrypted frames.

**Why a fixed-rate heartbeat instead of send-on-change:** the TX broadcasts
its state every tick regardless of change, so the RX never has to *infer*
"not braking" from silence — **loss of heartbeat is itself the failure
signal** (link-lost indication, never silent-dark, never a latched fake
BRAKE). This is the fail-honest doctrine made concrete; full failsafe table in
`docs/protocol.md` §4. Rate: docs say 20–50 Hz; code default is **20 Hz**
(`CONFIG_CHMBL_NET_RATE_HZ`, Kconfig range 1–50, runtime-changeable via the
`net rate <hz>` console command — see `transmitter/software/main/net.c`).

The wire struct (`docs/protocol.md` §2, mirrored in code):

```c
typedef struct __attribute__((packed)) {
    uint8_t  version;   /* CHMBL_PROTOCOL_VERSION = 1 */
    uint8_t  msg_type;  /* MSG_HEARTBEAT=0 / MSG_TELEMETRY=1 / MSG_PAIR=2 */
    uint16_t seq;       /* monotonic; RX drops non-newer seq (replay/stale) */
    uint8_t  state;     /* brake_state_t: ST_OFF=0, ST_DECEL=1 (RESERVED), ST_BRAKE=2 */
    uint8_t  flags;     /* bit0: decel_enabled, bit1: tx_low_power */
    uint8_t  tx_health; /* CAN-ok, bus-idle, etc. (diagnostics) */
    uint8_t  reserved;
} chmbl_msg_t;          /* 8 bytes */
```

Verified against both firmwares 2026-07-07:
`transmitter/software/main/protocol.h` and
`brake_light/software/main/protocol.h` are **byte-identical** (two copies by
design, to be promoted to a shared component per roadmap — keep them in sync).
Doc/code drift found, minor:

- `docs/protocol.md` declares the struct field as `brake_state_t state;`
  (with `typedef enum : uint8_t`); the code uses `uint8_t state;` plus a plain
  enum. Same 8-byte layout, no functional drift — but a copy-paste of the doc
  snippet into pre-C23 C will not compile.
- `docs/protocol.md` §1 says pairing "exchanges MACs and a shared key". The
  code exchanges MACs but uses **compiled-in placeholder dev keys**
  (`CHMBL_PMK[16] = "CHMBL-DEV-PMK01"`, `CHMBL_LMK[16] = "CHMBL-DEV-LMK01"` in
  `transmitter/software/main/pairing.c`) — explicitly labeled a bench
  placeholder there; per-pair random key exchange is a documented open item in
  `docs/design/de-01-espnow-link.md`. Do not describe the link as
  production-secure.

`ST_DECEL = 1` is **RESERVED** — the current FSM design never emits it; TX
states BRAKING and STOPPED both map to `ST_BRAKE` on the wire.

## 7. Motorcycle vocabulary the model won't know

These behaviors drive the FSM design (`docs/firmware.md`); the concepts are
owned here.

- **Clutch-in-gear at stops vs neutral.** A stopped motorcycle is either in
  **neutral** (no gear engaged; rider can release the clutch and go nowhere)
  or **in gear with the clutch lever pulled** (releasing the lever launches
  the bike). This is why the FSM's stop-exit "launch" guard is *clutch
  released AND in gear AND rolling*: releasing the clutch in **neutral** must
  NOT count as launching, so the gear/neutral signal gates the whole rule. If
  `gear.can_id == 0` on some future bike, that guard degrades to the 60 s
  stop timeout only.
- **Engine braking.** Rolling off the throttle in gear decelerates the bike
  through engine drag — significant deceleration **with no brake applied and
  no brake light on a stock bike**. Since this project infers braking from
  deceleration, strong engine braking is *intentionally* treated as braking
  (a rider slowing hard is a rider slowing hard); the thresholds/debounce in
  DE-09 keep gentle roll-offs from flickering the light.
- **Why there is no brake signal to read.** The Speed 400's bus carries no
  brake-switch bit (confirmed by repeated captures while working the brakes).
  The two obvious alternatives — tapping the brake-light wire, and an
  IMU/accelerometer — are both fenced off by existing patents, so
  deceleration derived from the bike's own CAN wheel speed is the only
  admissible input. Never propose the fenced-off alternatives.
- **Ride-by-wire.** The throttle grip is a sensor, not a cable; the ECU
  positions the throttle plate electronically. Consequence 1: rider throttle
  demand exists on the bus (0x140 B0). Consequence 2: throttle is nonzero at
  idle (~2.5 % idle-air), and the ECU runs its own fast throttle-control
  dither (the 0x145 B5 decoy signal that swept on the bench but is NOT rider
  throttle).
- **Wheel speed vs road speed.** Front wheel speed tracks road speed except
  under front-wheel lock/lift; the front sensor is the primary because the
  rear slews during clutch work and wheelspin. Frames arrive ~100 Hz
  (consecutive 0x102 lines in the .trc are ~10 ms apart).

## 8. When NOT to use this skill

- Building, flashing, or environment setup → `chmbl-build-and-env`,
  `chmbl-run-and-operate`.
- Running/interpreting `trc_viz.py`, `golden_check.py`, `gen_profile.py` →
  `chmbl-diagnostics-and-tooling` (this skill only explains the formats they
  consume).
- Implementing or tuning the DE-09 braking FSM (incl. the
  `CAN_DECODE_SPEED_HIST` bug) → `chmbl-de09-campaign`; FSM math →
  `chmbl-proof-and-analysis-toolkit`.
- Kconfig options and tunable inventories → `chmbl-config-and-flags`.
- Why decisions were made / invariants → `chmbl-architecture-contract`; past
  investigations and dead ends → `chmbl-failure-archaeology`.
- Debugging a live symptom → `chmbl-debugging-playbook` first; come back here
  only when the fix requires understanding a format.

## 9. Provenance and maintenance

All facts verified against the repo on **2026-07-07** (branch
`claude/skill-library-continuity-mib7ua`). Re-verify before relying on:

| Fact class | Re-verification command |
|---|---|
| Transmitter TWAI mode is listen-only | `grep -n TWAI_MODE transmitter/software/main/can_rx.c` |
| Logger TWAI mode + Kconfig default | `grep -n -A4 LISTEN_ONLY logger/software/main/logger_main.c logger/software/main/Kconfig.projbuild` |
| DBC signal lines / scales | `grep -n 'SG_' profiles/triumph_tr.dbc` |
| Generated profile matches DBC | `python3 tools/gen_profile.py profiles/triumph_tr.dbc --name "Triumph Speed 400 / Scrambler 400X (TR-series)" --bitrate 500000 --symbol bike_profile_triumph_tr --out /tmp/regen.c && diff /tmp/regen.c transmitter/software/main/bike_profile_triumph_tr.c` |
| Bit-extraction (sawtooth) logic | `sed -n '19,55p' transmitter/software/main/can_decode.c` |
| .trc header/line format | `sed -n '1,20p' logger/40mph_drive_cycle.trc` and read `logger/software/main/trc_format.c` |
| Decode table + caveats | read `docs/can-profiles.md` §5 |
| chmbl_msg_t sync between firmwares | `diff transmitter/software/main/protocol.h brake_light/software/main/protocol.h` |
| Heartbeat rate default | `grep -n -A4 NET_RATE_HZ transmitter/software/main/Kconfig.projbuild` |
| Dev PMK/LMK placeholder status | `grep -n 'CHMBL_PMK\|CHMBL_LMK' transmitter/software/main/pairing.c` |
| KMH_TO_MPH constant | `grep -n KMH_TO_MPH transmitter/software/main/can_decode.h` |
| Worked-exercise frame | `grep -n '99996' logger/40mph_drive_cycle.trc` |
