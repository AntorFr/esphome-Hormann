# Hörmann Supramatic E3 — HCP / UAP1 investigation

Full investigation log for ESPHome integration over the RS485 bus.

## TL;DR

- ✅ **State read-out works**: broadcasts decoded; door state (open/closed/opening/closing/venting),
  light, error, prewarn exposed to Home Assistant.
- ✅ **Command works** (resolved 2026-06-11): the master now escalates to `status_request` and our
  commands (impulse, open, close, light…) drive the door. **The door opens.**
- 🏆 The two locks that took the longest, on a **WeAct CAN485** (isolated CA-IS2092A transceiver):
  1. **Dead RX on the isolated transceiver** → physically swap A/B + `ab_inverted: true` (§4 decies).
  2. **Command ignored (never registered)** → the **sync break was too short** (~470 µs vs a real
     UAP1's ~920 µs) → emit the break `0x00` at 9600 baud (§4 undecies).

> The condensed, English "what you need to know" version is in
> [`investigation/README.md`](investigation/README.md). Below is the full chronological log,
> dead ends included.

---

## 1. Reference implementations consulted

| Repo | Hardware target | Language | Main take-away |
|------|----------------|----------|----------------|
| **stephan192/hoermann_door** | Supramatic 4 / LineaMatic / PIC16+ESP8266 | C (PIC16) + C++ (ESP) | "Canonical" reference. Dedicated PIC16 for the timing/break, ESP just simple UART on top. 3 ms delay via Timer0 1ms. TX break via `SENDB=1; TX1REG=0x00`. CRC table + protocol format. |
| **steff393/hgdo** | Supramatic / ESP8266 direct (no UAP1) | C++/Arduino | Implements *exactly* the same protocol on a bare ESP. **Clever TX-break trick**: switch the UART to 9600 7N1, write `0x00` (= ~1.04 ms low = ~20 bit-times at 19200), back to 19200 8N1, send the frame. Configurable `cfgMasterAddr`. |
| **raintonr/hormann-hcp** | LineaMatic P / Linux + USB-RS485 | Node.js | Same break trick as steff393 (`port.update({baudRate: 9600, dataBits: 7})`). **No delay** between the scan RX and the reply TX. `icAddress = 0x28`, type = `0x14`. |
| **Gifford47/HCPBridgeMqtt** | Supramatic E4 | C++ | Modbus RTU (totally different). Slave ID 2, 16-bit registers. **Not applicable to the E3** but shows Hörmann uses several protocols across generations. |
| **mapero/esphome-hcpbridge** | Hörmann E4 (variant of the above) | ESPHome | Same, Modbus. |
| **ljames8/hormann-hcp-client** | Generic | TypeScript | TS client, same HCP1 protocol as stephan192. |
| **bouni blog** ([blog.bouni.de/posts/2018/hoerrmann-uap1/](https://blog.bouni.de/posts/2018/hoerrmann-uap1/)) | UAP1 reverse engineering | — | Protocol doc source. Confirms: addr 0x28 UAP1, type 0x14, master 0x80 (drive) or 0x8D (device), CRC poly 0x07 init 0xF3, baud 19200 8N1. **But incomplete doc**: does not describe the scan → status_request transition nor the exact timing the master expects. |

---

## 2. HCP1 protocol (per references)

### Addresses
- `0x00`: broadcast (master emits the door state to everyone)
- `0x80`: master drive
- `0x8D`: master device (exact role unclear, sometimes used as an alternate master)
- `0x28`: UAP1 slave (our role)
- `0x10..0x90`: other possible slaves (light barriers, panels, etc.)

### Frame format
```
[ADDR_DEST] [CNT|LEN] [PAYLOAD...] [CRC8]
```
- `CNT|LEN`: high nibble = counter (0-15), low nibble = payload length
- `CRC8`: poly `0x07`, init `0xF3`, over all bytes (CRC over the full frame incl. CRC = 0)

### Expected sequence per the references
1. Master scan: `[0x28][CNT|0x02][0x01][0x80][CRC]`
2. Slave replies: `[0x80][CNT+1|0x02][0x14][0x28][CRC]` (our address + UAP1 type)
3. **If the scan is accepted** → master sends status_request: `[0x28][CNT|0x01][0x20][CRC]`
4. Slave replies: `[0x80][CNT+1|0x03][0x29][LOW][HIGH][CRC]` where `LOW|HIGH` = action code (0x1000 = idle, 0x1001 = open, 0x1002 = close, 0x1004 = impulse, 0x1008 = light, 0x1010 = venting, 0x0000 = stop)
5. Master emits a periodic broadcast: `[0x00][CNT|0x02][D0][D1][CRC]` with D0/D1 = state bits

### Break (sync) between frames
The master emits a sync break (~12 bit-times of line low = ~625 µs at 19200) before each frame, and expects the same from the slave.

---

## 3. Current ESPHome implementation

### Architecture
- Custom `hormann_hcp1` component in pure C++ (`components/hormann_hcp1/`)
- **Native ESP-IDF** framework (not Arduino)
- ESP-IDF UART driver with a dedicated event queue
- Dedicated FreeRTOS task (priority 23, core 1) to parse frames
- Platforms: cover, light, binary_sensor, button

### YAML configuration
```yaml
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: 17       # -> RS485 module DI
  rx_pin: 19       # -> RS485 module RO (not GPIO18 on the S3 = USB)
  de_pin: GPIO4    # -> RS485 module EN (DE/RE tied together)
  slave_addr: 0x28
  master_addr: 0x80
  slave_type: 0x14
  auto_scan: false  # test mode cycling 12 addr/type combos
  de_invert: false
```

### Technical details implemented and validated
- ✅ CRC8 0x07/0xF3 matching the references
- ✅ RX break detection via the ESP-IDF `UART_BREAK` event
- ✅ Tolerant parser: scans every buffer offset to find a valid frame (handles junk bytes at chunk start)
- ✅ Broadcast decoding: cover state, light, error, venting, prewarn, option_relay
- ✅ Callbacks to cover/light/binary_sensor only on state change
- ✅ Optimized logger (WARN level) to avoid slowing the bus task

### Sensitive points of the TX implementation
- RS485 mode: tested with and without `UART_MODE_RS485_HALF_DUPLEX`. Conclusion: **auto mode drops DE too early** (truncated bytes). Back to manual DE control + 600 µs delay after `uart_wait_tx_done` before lowering DE.
- TX break: 3 methods tried:
  1. ❌ `uart_set_line_inverse(UART_SIGNAL_TXD_INV)` + delay → works but the master doesn't recognize it
  2. ❌ Baud-switch 9600 7N1 + `0x00` (steff393/raintonr method) → too slow (~40 ms/TX because of `uart_set_baudrate`) — *NB: later proven false, see §4 undecies*
  3. ✅ `0x00` prefix at 19200 8N1 (= ~470 µs low) → fast (~3 ms total) but maybe an insufficient valid break

---

## 4. Field observations (Supramatic E3)

### Observed bus traffic
```
Cyclic frames every ~5 s:
  00:00:X2:02:02:CRC    <- periodic broadcast (closed door state: d0=0x02, d1=0x02)
  00:28:82:01:80:06     <- scan to us (UAP1 0x28)
  00:28:02:01:80:0D     <- variant with a different counter

More rarely (mysterious):
  00:80:22:01:80:01     <- scan addressed to 0x80 (master drive?)
  00:80:A2:01:80:0A     <- same, counter variant

At boot, a full sweep of addresses 0x10 to 0x90 (master looking for present slaves).
```

### What we confirmed
- ✅ The main board RXes all broadcasts → we can decode the door state
- ✅ The master regularly scans 0x28 → our UAP1 slot is a recognized scan address
- ✅ Our TX **physically reaches** the bus (multiple proofs on the witness side):
  - `tx_diag` test sending 16 × `0xAA` → bytes visible on the witness
  - Master frames corrupted right after our `send_frame` (proof of RS485 overlap)
- ✅ Our `send_frame` latency reduced to **~3.3 ms** after optimization

### What we observe BUT cannot fix
- ❌ **Our scan reply is never received cleanly** by the witness (never a clean `<<< 80:..:14:28:CRC` chunk)
- ❌ Instead we see the next broadcast **CORRUPTED** ~20-50 ms after our TX:
  ```
  Expected:  00:00:92:02:02:62  (normal broadcast cnt=9)
  Observed:  00:C0:C9:86:62:62  or  00:32:D9:02:02:62  or  00:FC:8A:96:02:02:62
  ```
- ❌ **The master never transitions to status_request** — whatever the slave_addr, master_addr, slave_type, timing tested.

### addr/type/timing tests exhausted
Automatic cycling over 12 combos (`auto_scan: true`) for 3 minutes:

| # | slave | master | type | Result |
|---|-------|--------|------|--------|
| 1 | 0x28 | 0x80 | 0x14 | No status_request |
| 2 | 0x29 | 0x80 | 0x14 | No status_request |
| 3 | 0x82 | 0x80 | 0x14 | No status_request |
| 4 | 0x83 | 0x80 | 0x14 | No status_request |
| 5 | 0x81 | 0x80 | 0x14 | No status_request |
| 6 | 0x28 | 0x8D | 0x14 | No status_request |
| 7 | 0x82 | 0x8D | 0x14 | No status_request |
| 8 | 0x28 | 0x80 | 0x10 | No status_request |
| 9 | 0x28 | 0x80 | 0x12 | No status_request |
| 10 | 0x28 | 0x80 | 0x15 | No status_request |
| 11 | 0x28 | 0x80 | 0x16 | No status_request |
| 12 | 0x28 | 0x80 | 0x20 | No status_request |

Delays tried: 0 µs (immediate) / 200 µs / 1.5 ms / 3 ms (PIC recommendation). None changes the behavior.

DE polarity (`de_invert`): tested at `true`, the bus stayed silent → the default polarity (active-HIGH) is the right one.

---

## 4 bis. Comparative hardware analysis (later sessions)

### Stephan192 ("clean board" reference)

`board/RS485Interface.sch` schematic + BOM analyzed. Key parts:
- **IC4 = ST485BDR** — 5V RS485 transceiver (SOIC8), industrial grade
- **R5 = 120 Ω** — termination resistor between A and B (critical!)
- **R15/R16 = 100 Ω** in series on A and B (ESD protection)
- C13/C14 = 100 µF aluminium → solid supply filtering
- IC2 = AOZ1283PI step-down + IC3 = MCP1826S-3302 LDO → dedicated 5V/3.3V supply, isolated from the 24 V bus
- D1 = SS25 Schottky → reverse-polarity protection
- PIC16F15324 as the core (precise 16-bit timers, dedicated UART ISR)

### HGDO ("direct ESP" reference)

Code read, PCB image consulted, wiki cloned locally. Key points:
- **NodeMCU ESP8266** + a separate RS485 module (visible on the photo)
- A DC-DC converter visible (probably MP1584) + an electrolytic cap
- **TX method: SoftwareSerial (not the Hardware UART!)** → CPU-cycle-precise timing control
  ```cpp
  S.begin(9600, SWSERIAL_7N1);   // break baud-switch
  S.write(0x00);
  S.flush();
  S.begin(19200, SWSERIAL_8N1);   // back to normal
  S.write(txData, txLength);
  S.flush();
  ```
- Configurable pinout with **optional inversion** of the TX/RX lines:
  ```cpp
  if (cfgHwVersion == 10) {
    S.begin(19200, SWSERIAL_8N1, PIN_DI, PIN_RO);  // inverted
  } else {
    S.begin(19200, SWSERIAL_8N1, PIN_RO, PIN_DI);
  }
  ```
- **`cfgMasterAddr` exposed in config**:
  ```c
  cfgMasterAddr  // Master address: 128 (0x80) per default, 144 (0x90) for HAP1-HCP-Adapter
  ```
  → **0x90 is a valid master address** on some models! (Never tried in our auto_scan.)

### Our setup (Supramatic E3 + ESP32 + cheap RS485 module)

- **Cheap RS485 module** with a single EN pin (DE and /RE tied internally)
- No schematic, no datasheet — probably a cheap MAX485 on a simple PCB
- **120 Ω termination between A/B: UNKNOWN status** (to check with a multimeter)
- 3.3V from the ESP32 (the module accepts 3.3V or 5V depending on the variant)
- **Native ESP-IDF Hardware UART** (not SoftwareSerial)
- No specific ESD protection on the board side
- Common GND reference with the operator via the bus (to verify)

### Comparison table

| Aspect | stephan192 | hgdo | Our setup |
|---|---|---|---|
| Transceiver | **ST485BDR** | (MAX485/SN65?) | HW-519 / auto-dir module |
| 120 Ω termination A↔B | **YES (R5)** | Unknown | **To verify** |
| Series resistors A/B | 100 Ω (R15/R16) | Unknown | None |
| Transceiver supply | clean isolated 5V | 5V NodeMCU | probably 3.3V |
| DE and /RE | **Separate** | Tied | Tied ("EN") |
| TX method | PIC Hardware UART + ISR | **SoftwareSerial** | ESP-IDF Hardware UART |
| TX/RX inversion | No | **Configurable** | No |

### Hardware conclusions

1. **Missing termination** → likely cause #1. Without 120 Ω between A/B, the line reflects, the Supramatic master can receive corrupted bits on the downward TX side and reject our frame. **Immediate check**: multimeter between A and B at idle → should read ~60 Ω (2 × 120 in parallel) or ~120 Ω. If > 10 kΩ → no termination.

2. **SoftwareSerial vs ESP-IDF Hardware UART**: hgdo likely has more predictable timing. Our Hardware UART + driver + FreeRTOS queue + scheduling can introduce a few-ms jitter between `write_bytes` and the actual emission (and `uart_wait_tx_done()` returns before the shift register finishes). On a bus where the master only waits ~3-5 ms, that's critical.

3. **`master_addr = 0x90`** untested. Add it to auto_scan or test directly.

4. **Auto-direction / tied-EN module** is less flexible than one with separate DE and /RE. In particular you can't keep RX active during TX to check the echo and detect collisions.

---

## 5. Hypotheses for the next steps

### A0. (NEW) Master at `0x90` instead of `0x80`
**Why:** hgdo exposes a configurable `cfgMasterAddr` with `0x80` (default) or `0x90` ("HAP1-HCP-Adapter"). Our auto_scan tried 0x80 and 0x8D but **not 0x90**. The Supramatic E3's master_device may differ from the master_drive we see emitting the scans.

**To test:** just edit the YAML with `master_addr: 0x90` and watch for a `Status request`.

### A1. (NEW) No 120 Ω termination on our bus
**Why:** stephan192 has R5=120 Ω explicitly between A/B. Our cheap module probably has no built-in termination (or it's off by default). Without termination, over a few meters of cable, reflections corrupt the bits on the receiver side. The Supramatic master gets our frame with a few flipped bits → CRC fail → silent rejection.

**To test:**
- Measure the resistance between A and B with a multimeter. Powered bus → cut the operator supply first.
  - ~60 Ω: correct termination (2 × 120 Ω in parallel at the ends)
  - ~120 Ω: termination at a single end (acceptable)
  - kΩ or MΩ: no termination → add a 120 Ω between A and B on the RS485 module (or enable the jumper if present)
- If no jumper, solder a 120 Ω resistor across the module's A/B pins

### A2. (NEW) ESP-IDF Hardware UART less predictable than SoftwareSerial
**Why:** hgdo uses SoftwareSerial on the ESP8266 — each bit is emitted manually by the CPU with cycle-precise timing. Our ESP-IDF Hardware UART has a driver, buffers, and goes through the FreeRTOS scheduler — the latency between `uart_write_bytes()` and the actual emission can vary from 0 to several ms (and `uart_wait_tx_done()` returns before the shift register finishes).

**To test:**
- Implement a "bit-bang TX" mode on GPIO17 in `send_frame` when replying to the master. Disable the UART during the bit-bang, then re-enable it for the next RX.
- Or: precisely measure (with a witness and a µs timestamp) how long after `uart_write_bytes` the bytes actually appear on the line.

### A. Our TX IS emitted but the master rejects our frames
**Why:** the master may expect a "real" break (12+ bit-times i.e. > 625 µs); our `0x00` at 19200 (~470 µs low) might be insufficient.

**To test:**
- Regenerate the baud-switch break trick (9600 7N1) **but** pre-cache the baud config to avoid the ~40 ms of `uart_set_baudrate`. Possible with direct ESP32 UART register access.
- Or: use `uart_write_bytes_with_break(port, frame, len, brk_len=20)` which should generate a clean trailing hardware break. The break would follow our frame instead of preceding it, but the master might still see it as the delimiter of the *next* frame (ours).
- Or: pure bit-bang on GPIO17 — temporarily disable the UART, force the GPIO low for 700 µs, re-enable the UART, write_bytes.

### B. The reply timing doesn't match what the E3 expects
**Why:** the bouni doc says the scan→reply timing is unspecified. The stephan192 PIC waits 3 ms, raintonr replies immediately, HGDO 3 ms too. **But the E3 may expect a very precise window** (say 1 ms ± 0.5 ms) outside of which our reply is ignored.

**To test:**
- Measure on the witness **how long after its scan the master starts transmitting the next broadcast** (gives the available window size)
- Emit within that window at different offsets (500 µs, 1 ms, 2 ms, 5 ms) and find the one that does NOT corrupt the next broadcast

### C. The E3 protocol differs subtly from the historical UAP1
**Why:** same patterns but maybe:
- E3 `slave_type` differs from 0x14 (we tested 0x10..0x20 but not exhaustively)
- The expected scan-reply format might be 6 bytes instead of 5 (with a version/sub-type byte)
- A non-standard slave address (other than 0x28 and what we tried)
- A missed initial **handshake** step (an init sequence requiring N consistent consecutive replies before the master "validates" the slave)

**To test:**
- Buy a real Hörmann UAP1 panel and observe its exchanges with an RS485 witness → we'd know exactly what to reply
- Brute-force every `slave_type` from 0x00 to 0xFF with a wider auto_scan
- Log whether the master changes anything in its behavior after our first N replies (maybe it needs N=10 identical replies before it starts the status_request)

### D. The E3 bus has security/auth
**Why:** on some recent models, Hörmann introduced encryption/handshake to prevent unauthorized clones. The E3 is recent.

**To test:**
- Capture a long history with a real UAP1 connected to see if there is a boot init sequence (maybe a challenge-response)
- Look for whether recent (2023+) DIY Hörmann projects mention a protocol change

### E. Unsuitable RS485 module
**Why:** our module has DE/RE tied (a single EN pin). When we TX, our receiver is disabled, so we can't do collision avoidance. And without separate RE control, we can't listen during a TX to verify it went through.

**To test:**
- Switch to a module with separate DE and /RE (e.g. a real HW-519 with distinct DE and /RE). Better direction handling and echo checking.
- Check the bus biasing (ideally 120 Ω between A and B at both physical ends)
- Check our TX's electrical level with a scope (Vdiff must cleanly exceed ±200 mV)

---

## 6. Tools set up for the next session

### Main component
- [components/hormann_hcp1/hormann_hcp1.cpp](components/hormann_hcp1/hormann_hcp1.cpp): ESPHome component with a full HCP1 parser
- [components/hormann_hcp1/__init__.py](components/hormann_hcp1/__init__.py): config schema with `slave_addr`, `master_addr`, `slave_type`, `auto_scan`, `de_invert`
- `tx_diag()` function (no button for now) for manual raw-TX testing

### Witness
- An ESP32-S3 + RX-only RS485 module dumping all raw bus traffic via `uart.debug`
- Wiring: DE/RE → GND, RX → GPIO18, A and B in parallel on the bus
- Essential to observe what really happens on the line when our main board TXes

### Main board YAML
- An operational ESP32 + RS485 module config on GPIO4 (DE) / 17 (TX) / 19 (RX)
- Logger at `WARN` to avoid slowing the bus task

> Note: the witness/garage-3 helper YAMLs referenced here were removed after the work was done;
> see `example_hcp1.yaml` / `example_can485.yaml` for the current configs.

---

## 4 ter. Session 2026-05-30 results — polarity inversion

### Problem found: inverted A/B polarity

**Symptom:** the master was completely silent on the ESP side while it was emitting normally. The witness in uart.debug mode saw structured but CRC-invalid data.

**Cause:** the A/B polarity of our RS485 modules (cheap HW-519) is **inverted** relative to what the Hörmann bus expects. Two possible origins (not distinguished, both give the same symptom):
- The RS485 module has A and B silkscreened per a non-EIA485 convention (common on cheap modules)
- The operator connector (Pin5/Pin6) uses the opposite convention to our modules

**How it shows up:**
- NORMAL polarity (correct) → UART idle = 1 (mark) → decodable frames, valid CRCs
- INVERTED polarity (our case):
  - One way: the UART sees idle = 0 = continuous break → `uart_debug` outputs nothing, the custom component flushes in a loop without decoding
  - The other way: the UART can frame the bytes BUT the data bits are all complemented (XOR 0xFF) → CRC always invalid → nothing decoded

**Confirmed software fix:** `uart_set_line_inverse(port, UART_SIGNAL_RXD_INV)` on the ESP-IDF side, or `inverted: true` on the rx_pin in native ESPHome.

**Result after the fix:** perfectly valid HCP1 frames on both ESPs:
```
00:00:X2:02:02:CRC   <- door-state broadcasts (every ~70 ms)
00:8X:X2:01:80:CRC   <- master scans to the slaves
00:28:X2:01:80:CRC   <- scan to our UAP1 address (0x28)
```

### Hardware confirmed in place
- ✅ 120 Ω termination added between A and B (measured before: ~3 kΩ → no termination)
- ✅ RS485 module VCC moved from 3.3 V to 5 V
- ✅ Common GND confirmed between ESP and operator

### What remains blocked
- ❌ **TX to the master**: our TX probably also needs to be inverted (`UART_SIGNAL_TXD_INV`) so the master decodes our scan-replies. Next step.
- ❌ The master still doesn't move to `status_request` — but with a working RX and a TX possibly to fix, it's now unblockable.

---

## 4 quater. Session 2026-05-31 results — idle/break symptom in the marking orientation (termination without bias)

### Hardware correction (vs §4bis)
The RS485 chip is **not an auto-direction module** as assumed in §4bis: it's an **SP3485 (TTL, 3.3 V chip) with an EN input** (DE and /RE tied). It's a real transceiver. EN is driven by GPIO4 (`de_pin`, `de_invert: false`): low = RX, high = TX. EN is fine (otherwise the decoding orientation wouldn't decode either).

### Refined polarity model (XOR)
The A/B wiring and `inverted:` (on the native uart component's rx_pin, e.g. witness.yaml) are **two inversions that combine as XOR**. Only their sum `N` matters:

| Wiring | inverted | N = wiring ⊕ inverted | Observed result |
|--------|----------|------------------------|-----------------|
| A/B (marking) | false | 0 | **nothing** (see anomaly below) |
| B/A (swapped) | false | 1 | frames but invalid CRC |
| B/A (swapped) | true  | 0 | **frames + valid CRC** ✅ (current witness config) |
| A/B (marking) | true  | 1 | untested — prediction: nothing (to confirm) |

→ The config that decodes today: **swapped wiring (B/A) + `inverted: true`**.

### What blocks the command: the **TX** polarity (not the "nothing")
Decisive argument, by **read/write asymmetry**. RX and TX share the **same differential pair**:
- We **read** the master (decode OK in B/A + inversion) → the RX polarity is correctly compensated.
- We **cannot** command it (master silent, never a `status_request`).

If we only compensate RX and **not TX**, then on transmit our frames go out reversed on the bus → the master receives them with all bits complemented → bad CRC → it ignores us → never a `status_request`. **That exactly reproduces the symptom** (read OK / command dead). It's a genuine **bus-level** problem, and that's precisely what `ab_inverted` fixes (couples `RXD_INV | TXD_INV`, see below).

**A test that proves TX without depending on the master (witness = oracle):** the witness decodes the master cleanly (B/A + `inverted:true`), so it can read the right polarity.
1. garage-3: `ab_inverted: true`, fire a known TX burst (`tx_diag`, e.g. `0xAA`).
2. Watch the witness: do our bytes come out **clean** (like the master's) or as **garbage**?
   - Clean → TX at the right polarity → the master *should* be able to read us → re-enable `auto_scan` and watch for the `status_request`.
   - Garbage → TX still inverted → dig deeper.

This isolates TX polarity from the master's (unknown) acceptance logic.

### The "nothing" in the marking orientation: RESOLVED — it was the 5 V (overvoltage)
- **Resolved 2026-05-31**: putting the SP3485 back to **3.3 V** restores the signal in **both polarities**. The 5 V (out of spec, recommended VCC ≤ 3.6 V) pushed the receiver out of its limits and killed reception in one of the two orientations → that was the "nothing", **not** the 120 Ω nor a bias defect.
- **The 120 Ω was indeed not at fault** (confirmed): present in the case that decodes, and symmetric → cannot create an orientation asymmetry. The "termination regression" hypothesis is **dropped**.
- At 3.3 V we recover the **clean XOR** behavior: both orientations frame, only one gives valid CRCs (the other = complemented data). No more "nothing" asymmetry.
- ⚠️ **Consequence for the auto-detector**: at 3.3 V the wrong polarity **frames** (few/no breaks), so the current "many errors → flip" trigger may not be enough (it would see "traffic, 0 valid frame, 0 error" → "no traffic" branch). To harden: also flip on "traffic received but 0 CRC-valid frame". For HW tests, **force `ab_inverted: true|false`** rather than `auto`.

### Hardware hygiene
- **SP3485 at 3.3 V** ✅ DONE — and that was actually the **fix for the "nothing"** (see above), not just hygiene. (Bonus: its RO no longer drives ~5 V into a non-tolerant 3.3 V ESP GPIO.)

### Polarity auto-inverter — IMPLEMENTED (component, 2026-05-31)
The component owns the UART port directly (no `uart:` block), so ESPHome's `inverted: true` trick doesn't apply. Instead we implemented an **`ab_inverted: auto | true | false`** option (default `auto`) that calls `uart_set_line_inverse()`:
- **A single setting for RX *and* TX**: an A/B swap inverts both directions of the same differential pair, so we treat them together (`RXD_INV | TXD_INV`).
- **`auto`**: at boot, listen 5 s; if 0 valid frame + >20 breaks/frame errors → apply RX+TX inversion and re-check 5 s. If it decodes → OK. Otherwise → log "not a simple A/B inversion → check wiring & bias" (= the idle/bias block above, electrical and **independent**: even inverted, if idle is a break in the marking orientation, the fail-safe is still needed).
- **`true`/`false`**: force, disable detection.

**TX insight (potential command unblock):** so far we only compensated RX (`inverted:true` on the witness), **not TX**. In B/A wiring, our scan-replies went out **inverted** → the master received garbage → it never replied to the `status_request`. Coupling `TXD_INV` with `RXD_INV` is probably **the missing link** to unblock the command. To test once RX decodes cleanly (after the bias fix).

---

## 4 quinquies. Session 2026-05-31 (cont.) — RX unblocked, crash hardened, TX COLLISION identified

### Polarity: RESOLVED and confirmed
**Marking-orientation wiring + `ab_inverted: false`** → **100 % valid CRCs** on both boards (garage-3-test GPIO19, witness GPIO18), verified frame by frame. The master scans us cleanly (`28:82:01:80:06`). RX = perfect. (The old "nothing" was the 5 V on the 3.3 V SP3485, see §4quater; the 120 Ω was innocent.)

### "bus-clamp" (RTS) bug — the cause of "0 RX everywhere"
The component assigned `de_pin` (GPIO4) as the UART's **RTS** (`uart_set_pin(..., rts=GPIO4, ...)`) while staying in **manual** DE (no `UART_MODE_RS485_HALF_DUPLEX`). Without that mode, the driver holds RTS **deasserted = HIGH** at idle → SP3485 EN HIGH → **permanent TX mode** → garage-3 **drives the bus continuously** → **all nodes see 0** (witness included, hence the trap: a "global" symptom but a single guilty board). **Fix**: stop assigning DE as RTS (`UART_PIN_NO_CHANGE`), DE purely manual (idle LOW = RX). → bus unclamped, RX OK on both sides (282 frames/10 s).

### `bus_task` crash (core 1) — hardened
Once RX was active, the `bus_task` (priority 23) **flooded the DEBUG logs per byte** (`<<<` + `RX[n]`, big buffers) → core 1 fault → crash-loop → safe mode (ping OK but API refused, port 6053). **Fix**: (1) raw dump moved to `ESP_LOGV` **and** guarded by `#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE` (the `#if` also removes the hex **construction**, not just the log); (2) `bus_task` stack 4096 → 8192. → stable (0 junk on garage-3, no crash).

### Tool: built-in sniffer + junk-log
`sniffer: true` option → logs at INFO only the valid categorized frames (BCAST / SCAN->us / STATUS_REQ->us / ->master), de-duplicated (counter ignored, re-log every 5 s), + a 10 s heartbeat (`valid / junk / breaks`). Plus a **"junk" byte log** (rate-limited 500 ms) to see unparsable buffers. The **witness** runs this component RX-only (DE/RE→GND) = **TX oracle**.

### 🎯 Command blocker = TX COLLISION (seen via the junk-log)
The master scans us, garage-3 replies (`TX took ~4.3 ms`), **but the reply never arrives clean**: the witness sees it as **junk**, e.g. `80:92:D6:62:00` → our first 2 bytes (`80 92`) are **clean**, then a collision from byte 3 (`D6` overlap, `62` broadcast CRC, `00` sync). Each collision **destroys one master frame** (valid 282 → 279, on both sides: the master loses our collision, garage-3 — deaf during its 4.3 ms TX — misses the frame emitted meanwhile). → we emit **~1-2 bytes too late**, on top of the next broadcast. **Not polarity** (RX perfect, same pair). It's the old §4 observation "our TX corrupts the broadcast", **seen directly**.

**Latency contributor**: in `try_parse_buffered`, `sniff_scan_()` does a **blocking** `ESP_LOGI` ("SCAN->us", console write ~ms) **before** `send_frame` → delays the reply.

### Isolating test (done) — the logging was NOT the cause
garage-3 `sniffer:false` (minimal TX latency) → **collision persists** (still 2 junk/10 s, no clean reply, RX OK 280 valid). Telling detail: without the log delay, the junk starts with `FF`/`FC`/`FE` = **collision from byte 0** (vs `80:92` clean then byte 3 *with* the log). → removing the log makes us reply earlier and we collide from the start: **we land on top of the master's traffic no matter what**. (Good news: no ring-buffer refactor needed.)

### Conclusion: we reply too late → eager-reply
We probably only reply on the **next frame's break** (not promptly) → we emit exactly when the master restarts → collision. Our reply is ~3 ms and the inter-frame gap ~17-40 ms: the room exists, we miss the window due to bad triggering.

### Decision implemented: eager-reply + `reply_delay_us`
- **Eager-reply**: we reply **as soon as** the scan/status addressed to us is complete (detected in the `UART_DATA` handler), without waiting for the gap/break. Minimal latency.
- **`reply_delay_us`** (config, default 0): a busy-wait micro-delay before emitting, to **fit** the reply into the master's window (sweep in steps: 0, 200, 500 µs…).
- **Listen-only**: a node without `de_pin` (witness) never drives the bus (`send_frame` no-op) → stays a pure sniffer/oracle.
- Still to understand: `TX took ~4 ms` for 6 bytes (~3.1 ms data) — the `0x00` break + the 600 µs hold after `wait_tx_done`.

---

## 4 sexies. Session 2026-05-31 (cont.) — TX PROVEN CLEAN, the blocker is timing

### Method: isolation by disconnecting the operator
To know whether our TX *really* reaches the bus and *cleanly*, we **disconnected the operator** (master) leaving **garage-3 ↔ witness connected** on A/B. No more master traffic → no collision, no flood → we observe our TX alone, in the clear. (Bonus: VERBOSE is safe again since the bus is quiet.)

### Result: TX perfect
`tx_test:true` (garage-3 emits `DE AD BE EF` ×4 every 2 s). The witness captures **every** burst **intact**:
```
SNIFF JUNK[17]  DE:AD:BE:EF:DE:AD:BE:EF:DE:AD:BE:EF:DE:AD:BE:EF:00
```
12/12 bursts over 24 s, 0 errors. `0 valid` (master absent), `5 junk/10s` = exactly our bursts.

→ **The "marginal/weak TX" hypothesis is REFUTED.** SP3485, DE (GPIO4), levels, wiring: all good. All the garbling seen with the operator connected = **100 % collisions** with the master's traffic.

### Reframing
- The command blocker is **neither** polarity (RX perfect) **nor** the TX signal (proven clean) → it's **purely timing**: our reply collides / misses the master's window.
- The eager-reply's "junk → 0" was **not** a clean reply, just the absence of overlap. The reply tied to the scan (~50 µs) or offset (`reply_delay_us` 8 ms) was never seen clean on the witness **with the master connected** → an observation limit (the witness's log blocks its RX on too-close frames), not a TX defect.
- Minor caveat: `tx_diag` fires from the `loop` task, the real reply from `bus_task` — same UART/GPIO calls, TX clean in both cases.

### Next step: tune the timing
The knob = **`reply_delay_us`** (already implemented). Plan: reconnect the operator, **sweep** `reply_delay_us` (0 → 500 → 1000 → 2000 → 3000 µs) watching the witness for `SNIFF STATUS_REQ->us ***` (master escalation = success; it's the *master's* frame, separate, so observable even if our tied reply stays hard to see).

---

## 4 septies. Session 2026-05-31 (cont.) — drive vs timing SETTLED: it's timing

Doubt raised: the reply emitted by the `bus_task` (`send_frame`) had never been *seen* clean on the witness with the master connected (whereas `tx_diag`, fired from `loop()`, was). Hypothesis to rule out: "the `bus_task` TX doesn't drive the bus" / "our 3.3 V drive is too weak against the master". Tool added: `bustask_tx_test` — fires the **real** scan-reply frame (`80:12:14:28:A7`, valid CRC) **from the `bus_task`**, via the exact `send_frame` path.

### Test A — `bus_task` does emit (operator disconnected)
`bustask_tx_test:true`, operator disconnected, garage-3 ↔ witness. The witness captures, every ~2 s:
```
SNIFF ->master   80:12:14:28:A7   (+2 identical)   <- 0 junk, valid CRC
```
→ **The `bus_task`'s `send_frame` drives the bus perfectly.** The "dead bus_task TX" hypothesis is **refuted**. The TX mechanism is sound end to end (core 1 included).

### "operator unpowered" test (field idea) — ARTIFACT
Bus connected but **master powered off**: garage-3 emits (`TX took ~4.2 ms`, local log) but the witness sees **0/0/0**. Symmetric and reproducible (bus connected → 0; disconnected → 5 valid = our shots).
→ **Protection-diode artifact**: the unpowered master's transceiver presents its A/B ESD diodes toward its Vcc rail = 0 V; driving the bus makes them conduct into the dead rail → **clamp**. Classic "powered-off device dragging down an RS485 segment". **Not representative** of the *powered* master (Vcc present, diodes blocked, normal RS485 idle). **No conclusion** about drive from this test.

### Test B — drive vs timing discriminant (master POWERED ON)
`bustask_tx_test` switched to a **2 s wall-clock timer** (fires asynchronously to traffic, even mid-reception; otherwise, master on, the `bus_task` never hits the idle timeout). ~16 shots over 32 s. Witness result:
```
SNIFF JUNK[6]  80:92:A7:FB:62     <- 80 (our start) … A7 (our CRC) + 62 (broadcast byte)
SNIFF JUNK[6]  80:A9:2A:A7:EF     <- 80 … 2A(≈28 garbled) … A7 (our CRC)
SNIFF JUNK[6]  90:F7:A7:FB:AC     <- A7 (our CRC) mixed in
```
- **Our bytes (`80`, `28`, `A7`) ARRIVE at the witness** even with the master on → if it were a drive defect, they'd be **absent**. **Drive: OK, definitively ruled out.**
- But **systematically superimposed** on master/broadcast bytes (`62`, `02:02`…). ~1 junk per shot, **0/16 clean.**
- 0/16 clean is explained by the **phase alignment**: the `bus_task` checks the timer at the top of the loop, right after handling a received event → we fire ~0 ms after a master frame → we hit exactly the next one (the master emits scan+broadcast **back-to-back**). Random-but-aligned timing, so *not* representative of the real eager-reply (which fires after the scan-to-us = the real window).

### Consolidated verdict
| Hypothesis | Status |
|---|---|
| Polarity / RX | ✅ resolved (see §4 quinquies) |
| TX mechanism (`bus_task`) | ✅ **sound** (Test A + bytes visible with master on) |
| Weak drive / master clamp | ❌ **ruled out** (protection-diode artifact) |
| **Collision / timing** | ✅ **confirmed** — THE blocker |

### Two levers identified for the timing
1. **`reply_delay_us: 1000`** adds 1 ms of useless delay → back to **0**.
2. **TX ~4.3 ms** = `0x00` break (~520 µs) + 5 bytes (~2.6 ms) + 600 µs DE hold + 1 ms delay. **Long** vs the master window (back-to-back scan→broadcast). Ideas: drop the `0x00` break, reduce the 600 µs hold.

### Next step
Turn off `bustask_tx_test`, go back to the **real eager-reply** with `reply_delay_us:0`, and watch the witness for whether the **broadcast right after the scan-to-us gets corrupted** (= our reply lands in it) — then attack the **TX duration**.

---

## 4 octies. Session 2026-05-31/06-01 — 🎯 ROOT CAUSE: we reply ~190× too early (bouni Saleae traces)

We finally analyzed the **logic traces of a REAL UAP1** ([blog.bouni.de](https://blog.bouni.de/posts/2018/hoerrmann-uap1/logic-traces.zip), never opened until now — the blog itself gives **no** timing). The `.logicdata` file (Saleae Logic 1.x) exported to CSV (µs-timestamped TX/RX/Dir transitions), then **UART-decoded by hand** (19200 8N1): Ch3 (Dir) = full bus / master requests, Ch1 (TX) = UAP1 replies. All CRCs valid.

### Real decoded sequence
```
master  00:28:02:01:80:0D   scan -> 0x28
UAP1    00:80:12:14:28:A7    scan-response          (= OUR reply, same CRC)
master  00:00:12:01:02:56    broadcast
master  00:28:21:20:98       status_request -> 0x28 (the master ESCALATES)
UAP1    00:80:X3:29:00:10:CRC status-response
```

### THE number: request→reply latency
End-of-request → start-of-reply measured over 68 cycles:
```
3.92, 3.86, 3.90, 3.82, 3.77, 3.85, 3.72, 3.93, 3.96, 3.90 ...
n=68: min 3.70 / MEDIAN 3.84 / max 4.19 ms
```
**A real UAP1 waits ~3.84 ms (very stable, ±0.2) after the end of the request before emitting its reply** (break 0x00 included, ~4.3 ms duration).

### Root cause of the command failure
Our instrumentation (§4 quinquies/septies) measured a reply latency of **~20 µs** (eager path). **We reply ~190× too early**, right in the middle of the master's **RS485 turnaround** (driver not yet switched to receive) → the master never reads our scan-response → it doesn't register us → **it never escalates to `status_request`**. That's the whole "commands don't go through" symptom from the start.

The earlier `reply_delay_us` sweep had tested **0 / 1 ms / 8 ms**: all **miss** the ~3.8 ms window (too early, too early, too late). The right value had never been tried.

### What the traces ALSO CONFIRM (everything but the timing was right)
- **leading `0x00` break: REAL** on a real UAP1 → ours is correct.
- **`80:12:14:28:A7` byte-for-byte identical** to our scan-response (same CRC).
- **TX duration ~4.3 ms (break included): identical** to ours → "our TX is too long" is **FALSE**, a real UAP1 is just as "slow". Shortening the TX would be a mistake.
- master=0x80, slave=0x28, type=0x14, cmd scan=0x01 / status_req=0x20 / status_resp=0x29, poll ~every ~100 ms: all consistent.

### Fix implemented
**`reply_delay_us: 3800`** (align to the real UAP1's 3.84 ms median), eager-reply kept, `bustask_tx_test:false`. Expected success = the master escalates → garage-3 receives `status_request` → door commands work. If 3800 isn't enough: **finely sweep 3500–4100 µs** (the window seems narrow, ±0.2 ms on the real UAP1).

Reusable method: `.logicdata` → CSV (Logic 1.x, required) → home-made Python UART decoder.

---

## 4 nonies. Session 2026-06-01 — reply_delay 3800 necessary but NOT sufficient; wall = drive/bias

`reply_delay_us:3800` implemented. garage-3 emits at **3814 µs** (confirmed, = real UAP1). **But the master still doesn't escalate** and — via the instrumented witness (`OUR-SCANRESP<<<` / `OUR-STATUSRESP<<<` categories + inter-frame Δµs) — **our reply NEVER appears on the bus with the master present.**

### Master behavior BEFORE registration (bouni startup trace, t=5.5–8.8 s)
The master discovers by a **DESCENDING address sweep** `0x8C→…→0x28→…`, ~57 ms/address, a `00:..:01:02` broadcast between each scan, and **a ~39 ms window after each scan** (same address empty). As soon as it scans `0x28` (1×/sweep ≈ 5.7 s), the UAP1 replies at 3.9 ms and is registered → escalates to `status_request`.

### Our master (witness, dedup lowered to 150 ms + Δµs)
Mostly near-continuous **`00:..:02:02` broadcasts**, with **rare** `0x28` scans. After a `SCAN->us`, the next frame arrives **~26 ms later** → a **26 ms window**, and **no `OUR-SCANRESP` inside it**. Our 3.8 ms fits easily: timing/margin is NOT the obstacle.

### The contradiction that points to the culprit
| Condition | Our TX seen on the witness |
|---|---|
| Master **absent** (Test A) | ✅ clean (`80:12:14:28:A7`) |
| Master present, TX **in collision** (async, Test B) | ✅ as **junk** (mixed bytes) |
| Master present, TX **clean within the window** (eager 3.8 ms) | ❌ **invisible** (neither valid nor junk) |

The only model consistent with all 3: **drive too weak / fail-safe bias.** Alone on the bus → readable. In collision (master + us) → combined differential is enough → read as junk. Alone in the window but with the **master's bias + 120 Ω termination present** → our 3.3 V SP3485 doesn't establish a readable differential at the witness → read as **idle = invisible**. (The "master-off clamp" of §4 septies was indeed a diode artifact; the weak-drive-against-the-powered-master is distinct and fits everything.)

### TO DO ON SITE (not remotely testable)
- **Scope on A/B during our TX** (master on): measure the real differential amplitude when we emit alone in the window.
- Check the **fail-safe bias** (pull-up A / pull-down B) and **termination**: double termination (master + ours?) or insufficient bias can crush our drive.
- Corrective ideas: 5 V transceiver/supply, add/tune **bias resistors**, or a module with a **stronger DE/RE driver**.
- Solid takeaways: timing (3.8 ms) correct; format/break/CRC = real UAP1; what remains is **purely the TX physical layer against the powered master**.

---

## 4 decies. Session 2026-06-10 — WeAct CAN485 (isolated CA-IS2092A): dead RX (board issue) + **PUPD** disturbs the whole bus

Migrated to the **WeAct CAN485 DevBoard** (**isolated CA-IS2092A** RS485 transceiver), config `garage-can485`. Initially, RX **silent: `0 valid, 0 junk, 0 breaks/errs`** continuously, while the **old SP3485 witness, wired to the SAME A/B/GND wires**, decodes **282 valid frames** (`ab_inv=off`, scans `28:82:01:80:06`, BCAST, replies). → bus + polarity + ground **cleared**, the fault is in the isolated board.

### Two DISTINCT findings (don't conflate)
**1. The PUPD switch disturbs the ENTIRE bus.** PUPD = the WeAct's pull-up A / pull-down B bias. **PUPD ON → even the old witness sees nothing** (0 frames). The bias added by the WeAct **clamps the differential for ALL listeners on the segment**, not just itself. → **PUPD must stay OFF**: the Hörmann master already biases; a 2nd bias point (especially on the isolated side) crushes the line.

**2. Even with PUPD OFF, the WeAct still receives NOTHING.** PUPD OFF: witness = **frames OK**, **WeAct = 0/0/0**, on the same wires. → **PUPD is NOT the cause of the WeAct's dead RX** — it's a separate effect. The WeAct has its **own receive-path fault** (isolated transceiver side), independent of PUPD.

### Practical rule (WeAct CAN485 board on the Hörmann bus)
- **PUPD: OFF** (the master already biases — PUPD ON **clamps the bus for ALL listeners**, witness included). Does NOT affect the WeAct's own RX fault (see finding 2).
- **Board 120 Ω termination: NEUTRAL on RX/sniffer** — measured: adding it or not **changes nothing** on reception. ⇒ the observed asymmetry comes **from PUPD alone, not the 120 Ω**. (The "120 Ω OFF" precaution only stays relevant for the **TX drive** against the master, see §4 nonies — not for listening.)
- Bus ground (GND) **connected** (RS485 3-wire A/B/GND) — required on the isolated side.

### Tests ruled out along the way (same session)
- **`listen_only` (DE forced low)** → still 0/0/0: **DE was not the culprit.**
- **Polarity**: `ab_inv=off` is the **right** one (witness proves it) — definitely not `true`.
- **Cause of the WeAct 0/0/0: RESOLVED** (below) = asymmetric bus drive + CA-IS2092A fail-safe → **physical A/B swap + `ab_inverted: true`**.

> ⚠️ Correction of an interim hypothesis: **PUPD does NOT explain** the WeAct's dead RX (silent with PUPD OFF too, while the witness receives on the same wires). PUPD = **a bus nuisance to keep OFF**; the WeAct's dead RX is a **distinct fault** (resolved below).

### ✅ RESOLVED — physical A/B swap + `ab_inverted: true`

**Final scope diagnosis** (ref. GND_ISO, bus active): this Hörmann bus drives **asymmetrically** — **DATA+ (pin 6) does the full swing** (−0.4 → +3.2 V), **DATA- (pin 5) stays near GND** (~0 V, flat). The A−B differential exists (carried by A) → the **non-isolated SP3485 decodes it**. But the **isolated CA-IS2092A, with its more aggressive built-in fail-safe, reads the weak "space" level (−0.4 V) as permanent idle → RO stuck → 0/0/0**. (Isolated supply `VDD5V_ISO` = 5 V measured OK, pins/enable OK, code OK confirmed via **raw UART**: everything was fine except this.)

**Fix**: **physically swap A and B** at the WeAct input → puts the strong swing on the "space" side, the receiver locks. RO then outputs **logically inverted** bytes → compensate with **`ab_inverted: true`**.

**Proof**: raw UART after the swap = a stream of bytes at the bus rate (inverted); then `hormann_hcp1` with `ab_inverted: true` → `sniffer: 280 valid, 0 junk (ab_inv=on)` + TX emitted. **RX on par with the witness.** ✅

**WeAct CAN485 ↔ Hörmann bus (HCP1) wiring:**
| Hörmann bus | WeAct terminal | Note |
|---|---|---|
| pin 6 DATA+ (A) | **B-** | **swapped** |
| pin 5 DATA- (B) | **A+** | **swapped** |
| pin 3 GND | Mass (GND_ISO) | continuity confirmed to operator GND |

+ config `ab_inverted: true`, **PUPD OFF**, 120 Ω termination irrelevant on RX. The (non-isolated) SP3485 witness stays on **normal wiring + `ab_inverted: false`**.

---

## 4 undecies. Session 2026-06-11 — real-UAP1 trace analysis + stephan192 comparison: our protocol is byte-perfect

RX **resolved** (§4 decies) → attacking the **command**. Decoded the Saleae trace of a **real UAP1** (`investigation/track_analyse/UAP1-startup.csv`, script `investigation/track_analyse/decode_uart.py`: decodes 19200 8N1, channel TX=UAP1 idle-high).

### What a real UAP1 does (decoded trace)
| t | UAP1 frame (TX) | meaning |
|---|---|---|
| 5.45 s (boot) | `0B:45:45:30:30:30:34:37:38:2D:30:30:..` = **serial announce "EE000478-00"** | identity broadcast |
| 8.83 s | `80:12:14:28:A7` | **scan-response** |
| 8.90 s + (every ~96 ms) | `80:..:29:00:10:..` (data 0x1000=idle) | **status-response** |

### Byte-by-byte comparison with garage-can (CRCs verified, init 0xF3)
- **Scan-response** `80:12:14:28:A7` → **identical** (CRC OK).
- **Status-response** `80:..:29:00:10` → **identical** (CRC OK).
- **Counter** (scan `0x02`→reply `0x12`, scan `0x82`→`0x92`) → **identical** to the real UAP1.
- **Timing** reply **3840 µs** → already tuned (confirmed by the emitter log `reply lat ~3840us`). The `+0ms` seen on the oracle is an **artifact** (the master hammers the scan `+34 identical` → the "since last scan" latency drops to 0).

### Is the serial announce required? → NO (verified on stephan192)
The real UAP1 **broadcasts its serial number at boot**, but the reference emulator **[stephan192/hoermann_door](https://github.com/stephan192/hoermann_door)** (`pic16/hoermann.c`) **does NOT implement it**: it only (a) reads the **broadcasts** `00:..:02:02` for the door/light state, (b) replies to the **scan** (`0x01`), (c) replies to the **status_request** (`0x20`) with the command data (0x1004=impulse…). **No announce frame.** → the announce is **not** needed to command.

**Our constants + framing are identical to stephan192**: `CMD_SLAVE_SCAN=0x01`, `CMD_SLAVE_STATUS_REQUEST=0x20`, `CMD_SLAVE_STATUS_RESPONSE=0x29`, `UAP1_TYPE=0x14`, same frame construction. **So the protocol is NOT the issue.**

### The real wall, cleanly isolated
Everything is byte-perfect (vs trace AND vs stephan192), timing tuned (3840 µs), 5 V drive, and the **witness confirms our reply is clean on the bus** (`OUR-SCANRESP<<<`). **Yet the master scans 0x28 in a loop and NEVER escalates to `status_request` (0x20)** — whereas a real UAP1/stephan192 gets registered. So the blocker is neither the content, nor the oracle-measured timing, nor the announce.

**Remaining hypotheses (next session):**
- **The master already finished its discovery** before garage-can replied correctly → try a **power-cycle of the operator** with garage-can already active.
- The third-party device `01:80` (operator internal) may occupy the command role.
- **Takeaway**: monitoring (broadcasts) **100 % functional**; the command hinges on this last escalation detail.

### 🎯 HYPOTHESIS #1 (session 2026-06-11, via hgdo): our SYNC BREAK is 2× too short

Comparison with **[steff393/hgdo](https://github.com/steff393/hgdo)** (ESP8266 + SoftwareSerial, **which drives the door**):
- **µs timing is NOT critical**: hgdo replies with `TX_DELAY = 3 ms` via `millis()` (**millisecond** resolution), sent from the Arduino loop (ms jitter) → it works. → **the "PIC µs timing" hypothesis is DROPPED.** Our precise 3840 µs is plenty.
- **The real difference = the sync break.** Measured on the scope (UAP1 trace, script):

  | source | break (low level) | bits @19200 | works? |
  |---|---|---|---|
  | **real UAP1** (scanresp/statusresp) | **~920 µs** | **~18 bits** | ref |
  | **hgdo** (`0x00` @ **9600 7N1**) | ~833 µs | ~16 bits | ✅ |
  | **garage-can** (`0x00` @ **19200**, no baud-switch) | **~470 µs** | **~9 bits** | ❌ |

  → **Our break is half as long.** The witness (a tolerant component) decodes it, but **the strict master doesn't recognize the frame start** → doesn't **frame/register** us → **scans in a loop without escalating.** Fits the whole symptom.

**FIX to implement**: lengthen the break to **~830-920 µs** (≈16-18 bits). hgdo's method: baud-switch to **9600** to emit the `0x00`, then 19200 for the frame (the `send_frame` "baud-switch = 40 ms" comment must be re-checked — hgdo does it per frame). Alternative: hold the TX line low ~900 µs manually (GPIO) before handing back to the UART. **This is lead #1 to unblock the command.**

### ✅✅ RESOLVED (2026-06-11) — THE COMMAND WORKS, THE DOOR OPENS

Fix applied in `send_frame`: the break is emitted as **`0x00` @ 9600 baud** (~937 µs low ≈ 18 bits @19200) then the frame @ 19200 (`uart_set_baudrate` is indeed fast, the "40 ms" was false). **Immediate result**:
- the master **escalates**: moves from the hammered scan to **`STATUS_REQ->us` (0x20) every ~100 ms** → we are **registered like a real UAP1**;
- `impulse` → status-response `80:..:29:04:10` (data 0x1004) → **the door opens.** 🚪✅

**FINAL ROOT CAUSE of the command = sync break 2× too short.** Everything else (scanresp, statusresp, counter, 3840 µs timing, 5 V drive) was already correct. The master requires a break ≈ a real UAP1 (~920 µs) to frame/register the reply.

> 🏆 **SUMMARY: full, working UAP1 emulator** on the WeAct CAN485 (isolated CA-IS2092A) — monitoring (broadcasts) **AND** command (impulse/open/close/light/venting/stop). Two locks lifted this session: (1) RX = physical A/B swap + `ab_inverted:true` (isolated transceiver fail-safe), (2) command = lengthened sync break.

---

## 7. Decision summary

- **State read-out**: works — door state reaches Home Assistant. Enough for monitoring/state-based automations.
- **Command**: **works** (resolved 2026-06-11). The two requirements that took the longest were the A/B physical swap (isolated transceiver) and the long sync break.
- **Useful tools for this kind of debugging**: a Saleae logic analyzer clone (~25€) to compare against a real UAP1, and ideally a scope. The home-made decoder lives in
  [`investigation/track_analyse/`](track_analyse/).
