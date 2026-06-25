# Hardware

Two boards: the **transmitter** (bike-side) and the **brake_light** (helmet-side).
Everything here is a first-pass sketch to size the design — not a finalized BOM.

---

## 1. Transmitter (bike-side)

Lives at the diagnostic port. Powered from the bike. Read-only on CAN.

### Block diagram

```
 Diagnostic  ┌──────────────┐   3.3 V   ┌─────────────┐
 connector   │ Reverse-pol. │──────────▶│  Buck reg.   │──▶ 3V3 rail
   12 V ─────▶│  + TVS prot. │           │ (12V→3.3V)   │
   GND  ─────▶└──────────────┘           └─────────────┘
   CAN-H ─────────────────────┐
   CAN-L ─────────────────────│──▶ CAN transceiver ──▶ ESP32 TWAI (listen-only)
                              (SN65HVD230, 3.3 V)        │
                                                          └─▶ ESP-NOW (2.4 GHz)
```

### Parts (sketch)

| Function | Candidate part | Notes |
|----------|---------------|-------|
| MCU + radio | **ESP32-C3** (or ESP32-S3/WROOM) | Has built-in **TWAI** (CAN 2.0) controller; only a transceiver is needed. C3 is cheap, RISC-V, Wi-Fi/BLE. |
| CAN transceiver | **SN65HVD230** (3.3 V) | 3.3 V logic, low power, has a silent-mode pin. Avoid 5 V-only transceivers (TJA1050) unless level-shifting. |
| Power regulation | Automotive-grade **buck** (e.g. TPS54xx), 12 V→3.3 V | Must survive load dumps / 12–14.4 V charging system. Wide-Vin. |
| Input protection | Reverse-polarity FET + **TVS diode** + fuse | Automotive transients are brutal. |
| Ignition sense | Tap switched-12 V or detect bus activity | Used to sleep when bike is off (see parasitic draw). |
| Connector | Bike-specific diagnostic plug | Euro 5 6-pin is common; **verify pinout per bike** — pin assignment is not universal. |
| Enclosure | Potted/sealed, **IP65+** | Engine bay vibration + weather + heat. |

### Power & parasitic draw

- The diagnostic port may provide **constant** or **switched** 12 V depending on the
  bike. If constant, the TX **must** sleep (deep sleep, transceiver disabled) when it
  sees no CAN activity, and ideally cut the buck's load, so it cannot drain the
  motorcycle battery while parked. Target parked draw: **< 1 mA**.
- Wake on CAN bus activity (transceiver wake / RXD edge) or on ignition sense.

---

## 2. Brake_light (helmet-side)

Self-contained, battery powered, charges over USB-C. This is the part the rider
sees and touches, so usability (charging, on/off, brightness) matters.

### Block diagram

```
 USB-C ──▶ Charge IC ──▶ LiPo ──▶ Load-share ──▶ Boost/LDO ──▶ 3V3 ──▶ ESP32-C3
 (5 V)    (w/ load     (1S)       path                        │
          sharing)                                            ├─▶ LED driver ──▶ Red LED bar
 Ambient-light sensor ─────────────────────────────────────▶ │   (constant-current
 Fuel-gauge / ADC ─────────────────────────────────────────▶ │    or addressable)
                                                              └─▶ Mode/pair button + status LED
```

### Parts (sketch)

| Function | Candidate part | Notes |
|----------|---------------|-------|
| MCU + radio | **ESP32-C3** | Same family as TX simplifies firmware/build. |
| Battery | **1S LiPo**, ~1000–2000 mAh | Size for runtime target (below). Protected cell. |
| Charge IC | **MCP73871** (or TP4056 + load-share) | MCP73871 does **load sharing** so the light works while charging. TP4056 is cheaper but no load share. |
| USB-C | USB-C receptacle w/ CC resistors | 5 V sink only; add ESD protection. |
| LED array | High-power **red** LEDs + constant-current driver, **or** WS2812-style addressable | Addressable = easy patterns but more current/complexity. Plain red + CC driver = efficient, simplest legal-color story. |
| Brightness control | PWM dimming + **ambient light sensor** | Night brightness must not blind following drivers; day brightness must be visible in sun. Auto-dim is a real safety feature, not a nicety. |
| Battery monitor | Fuel gauge IC or ADC divider | Low-battery warning pattern + cutoff. |
| User I/O | One button + small status LED | Power, pairing, brightness cycle. |
| Mount | Non-penetrating, **breakaway** | Adhesive pad or strap; see safety doc. **Never drill the helmet.** |
| Enclosure | **IP65+**, low-profile, lightweight | Mass on a helmet contributes to neck load in a crash — minimize it. |

### Runtime budgeting (worked example)

LED current dominates. Rough sizing:

```
Avg LED draw (mixed off/decel/brake, auto-dimmed)  ≈  120 mA
ESP32-C3 (RX mostly, modem on)                     ≈   30 mA
                                                   ----------
System average                                     ≈  150 mA

1500 mAh battery × ~0.85 usable / 150 mA           ≈  8.5 h
```

That's a comfortable full-day-of-riding target. Brake events are brief and bright;
the *average* is what sets runtime, so auto-dimming and an `OFF`/dim-running idle
state matter a lot. Tune the battery size to the LED bar you choose.

---

## 3. Open hardware questions

- Exact diagnostic connector + pinout for the **reference bike** (see
  [`can-profiles.md`](can-profiles.md)).
- LED bar: addressable vs. discrete high-power red — affects driver, current, and
  the legal-color/no-flash story.
- Whether to add a small inertial sensor purely as a *cross-check / tamper or
  fallen-helmet detector* (NOT for brake detection — that's the patented approach we
  avoid). Deferred.
