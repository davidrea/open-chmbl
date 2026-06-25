# Safety & regulatory

**Read this before building or riding with anything.** Open-CHMBL is a hobbyist,
auxiliary device that interacts with a safety-critical vehicle and is worn on a
piece of life-saving protective equipment. The constraints below are requirements,
not suggestions.

---

## 1. Legal / regulatory (varies by jurisdiction)

- **Auxiliary, never a replacement.** Open-CHMBL does **not** substitute for the
  motorcycle's factory brake light, which remains the legally required device.
- **Helmet-mounted lighting may be restricted or prohibited.** Some jurisdictions
  forbid attaching anything to a certified helmet (it can void DOT/ECE
  certification); others regulate or ban auxiliary/rear lighting on helmets
  entirely. **Confirm legality for your location before road use.** Until then,
  treat the project as **track / off-road / educational**.
- **Color & direction.** Rear lighting is red; red must not be visible to the front.
  Follow local rules on lamp color and placement.
- **No flashing brake lights.** Flashing/strobing stop lamps are illegal in many US
  states and under ECE rules. That's why:
  - All patterns are **steady**, rate-limited against strobing.
  - The `DECEL` (engine-braking courtesy) cue is **disabled by default** and, even
    when enabled, is steady — never flashing.
- **Liability.** A device that influences how following traffic reacts carries real
  liability. Document clearly that builders assume responsibility; this is not a
  certified product.

---

## 2. Functional safety (the bike side)

- **CAN is strictly listen-only.** The TWAI controller runs in silent mode: no ACK,
  no transmit. We must never perturb the motorcycle's ECUs. Verify before connecting
  to a real bike. (See [can-profiles.md](can-profiles.md).)
- **No parasitic battery drain.** If the diagnostic port is always-on 12 V, the
  transmitter must deep-sleep / shed load when the bike is off (target < 1 mA) so it
  can't flatten the motorcycle battery.
- **Robust input protection.** Reverse-polarity protection, TVS, and a fuse on the
  12 V input to survive automotive transients and load dumps.
- **Fail honest.** The system never fabricates a brake signal. On any internal fault
  the watchdog resets rather than latching a stale state.

---

## 3. Helmet & rider safety (the helmet side)

- **Never modify the helmet shell.** No drilling, no screws. Use a **non-penetrating**
  mount (adhesive pad or strap). Penetrating the shell can compromise its protective
  function and certification.
- **Minimize mass and offset.** Added weight on a helmet increases neck loading and
  rotational injury risk in a crash. Keep the unit **light and low-profile**, mass
  close to the shell.
- **Breakaway mounting.** The mount should **detach/shear** under impact rather than
  snag or transmit load to the head/neck. No rigid hooks or protrusions that can
  catch. A [magnetic shear-release mount](design/explorations/mounting-magnetic.md) is
  one **future-state** way to get this — the magnetic interface *is* the breakaway, and
  anything bonded to a helmet is still **VHB tape only, never a drilled fastener**. The
  hold force must be tuned to release below the snag/neck-load threshold; the new
  failure mode to characterize is unintended detachment at speed (lost device / road
  debris, not a rider-injury path).
- **Implanted cardiac devices (pacemakers, ICDs, CRT).** The neodymium magnets in
  a [magnetic shear-release mount](design/explorations/mounting-magnetic.md) produce
  strong local fields (surface Br ≈ 1.2–1.4 T at the pole face). The accepted
  interference threshold for most implanted cardiac devices is **≈ 0.5 mT (5 gauss)**
  at the device — above which a pacemaker may switch to an asynchronous fixed-rate mode
  and an ICD may be inhibited from delivering therapy.

  The two mount variants sit very differently relative to a typical sub-clavicular
  implant (upper chest, roughly 5–10 cm below the collarbone):

  - **Garment / backpack shoulder mount (Exploration A):** magnets rest against the
    upper back, with only 15–25 cm of body tissue between them and a typical implant
    site. Field modelling (dipole approximation) places the 0.5 mT boundary at
    approximately **7–10 cm** from a single 20 mm dia. × 4 mm thick N42 disc magnet. With two or more
    magnets, or a thinner rider, the field at implant depth cannot be guaranteed to stay
    below this threshold. **Persons with any implanted cardiac device must not use the
    garment / backpack shoulder mount.**

  - **Helmet mount (Exploration B):** magnets are on the rear of the helmet,
    approximately **40–60 cm** from the upper chest — well into the far-field regime
    where the stray field from even multiple magnets is a small fraction of 0.5 mT.
    This is the **safe configuration** for riders with implanted devices.

  Until the magnetic mount is experimentally characterized, anyone with a pacemaker,
  ICD, or other magnetically sensitive implant should **use only the helmet-mount
  configuration** and confirm acceptability with their device's manufacturer or
  cardiologist before riding with any magnet-based mount.

- **Battery safety.** LiPo on/near the head demands a **protected cell**, proper
  charge IC, over-current/over-discharge protection, and a sealed, vented enclosure.
  Don't charge unattended near the helmet's foam.
- **No blinding.** Ambient-light auto-dimming is a **safety requirement**: at night a
  full-brightness bar would dazzle following riders/drivers; in daylight it must
  still be visible.
- **Weatherproofing.** IP65+; condensation and vibration are constant.

---

## 4. Link-failure behavior (the system side)

- On lost radio link the brake_light shows a **distinct link-lost indication**
  (steady running light + slow fault blink). It must **never**:
  - go silently dark (rider/traffic think the device is fine when it isn't), or
  - **latch a fake `BRAKE`** (crying wolf trains following traffic to ignore it).
- Stale/old packets are dropped; the heartbeat model means *absence* of packets is
  the fault signal.

---

## 5. Explicit non-goals (and why)

- **No inertial brake detection.** Accelerometer/gyro deceleration sensing on the
  helmet/light is both a **patented approach we're avoiding** and less trustworthy
  than the bike's own brake switch. (A future IMU, if ever added, would be only for
  diagnostics like fallen-helmet detection — never to assert braking.)
- **No tapping the brake-light wiring.** Also patented, and it modifies bike wiring.
- **Not a certified safety device.** This is open hardware for builders who accept
  the responsibility.
