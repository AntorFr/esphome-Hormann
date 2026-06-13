# ESPHome Hörmann Garage Door Controller

ESPHome component to control Hörmann garage doors over the HCP1 protocol.

✅ **Status: full, working UAP1 emulator** — state read-out **AND** command
(impulse / open / close / light / venting / stop). Validated end-to-end on a **WeAct
CAN485** (isolated RS485 transceiver). See [`investigation/README.md`](investigation/README.md)
for the field notes (traps, measurements, what worked).

⚠️ **USE AT YOUR OWN RISK!** This project interacts directly with your garage door operator.

## Required hardware

### Parts
- **ESP32** (ESP32-DevKit, NodeMCU-32S, etc.)
- **RS485 module** with DE/RE direction control (or EN/RTS) — see the Hardware section below
- Dupont wires
- 5V supply for the ESP32

### ⚠️ Important note on RS485 modules

> **To COMMAND the door you need a module with DE direction control.**
>
> The component drives the **DE** pin to transmit (and generates the long "sync break" the master
> requires, via a baud-switch to 9600 — this is implemented). An **auto-direction** module (HW-519
> 4-pin, no DE) only allows **READING** the state: the component does not transmit without a `de_pin`.
>
> Recommended modules: **MAX3485 / SP3485 / ST485** (with DE/RE), or an **isolated CAN+RS485**
> module (e.g. WeAct CAN485). For an **isolated** transceiver, read the dedicated section below (A/B swap).

### Hörmann operator connector

The Hörmann operator exposes a connector with the following pinout:

| Pin | Function |
|-----|----------|
| 1 | +24VDC (output) |
| 2 | +24VDC (output) |
| 3 | GND |
| 4 | Reserved |
| 5 | RS485 DATA- (B) |
| 6 | RS485 DATA+ (A) |

## Wiring

There are two kinds of RS485 modules:
- **4-pin version** (VCC, GND, TX, RX) — auto-direction → **read-only** (cannot command, no DE).
- **Version with DE/RE, EN or RTS** — manual direction control ✅ **required to command**.

### Option 1: Auto-direction RS485 module (4 pins) — read-only

> ⚠️ Without a DE pin the component **cannot transmit** → state monitoring only, no command.

If your module only has **VCC, GND, TX, RX**, it handles the RS485 direction automatically.

```
ESP32                HW-519              Hörmann operator
┌─────────┐         ┌───────┐           ┌─────────────┐
│    3.3V ├─────────┤ VCC   │           │             │
│     GND ├─────────┤ GND   │           │  Pin 3 (GND)│
│  GPIO17 ├─────────┤ RX    │     A ────┤  Pin 6 (A+) │
│  GPIO16 ├─────────┤ TX    │     B ────┤  Pin 5 (B-) │
└─────────┘         └───────┘           └─────────────┘
```

> ⚠️ **Mind the crossover**: ESP32 TX (GPIO17) → module RX, and module TX → ESP32 RX (GPIO16).

**YAML config:**
```yaml
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: GPIO17   # -> module DI/RX
  rx_pin: GPIO16   # -> module RO/TX
  # No de_pin with auto-direction -> read-only
```

### Option 2: RS485 module with DE/RE, EN or RTS ✅ Recommended

If your module has **DI, RO, DE, RE** pins (or **TX, RX, EN/RTS**), you must control direction manually.

> 💡 **Compatible modules**:
> - MAX3485 / SP3485 with DE/RE (native 3.3V)
> - Modules with an EN (Enable) or RTS pin
> - MAX485 with DE/RE (needs 5V, check 3.3V compatibility)

```
ESP32                RS485 module        Hörmann operator
┌─────────┐         ┌─────────────┐     ┌─────────────┐
│    3.3V ├─────────┤ VCC         │     │             │
│     GND ├─────────┤ GND         │     │  Pin 3 (GND)│
│  GPIO17 ├─────────┤ DI/RX       │     │             │
│  GPIO16 ├─────────┤ RO/TX       │  A ─┤  Pin 6 (A+) │
│   GPIO4 ├────┬────┤ DE/EN/RTS   │  B ─┤  Pin 5 (B-) │
│         │    └────┤ RE (if present)   │             │
└─────────┘         └─────────────┘     └─────────────┘
```

> 💡 **Tip**: you can tie DE and RE together on a single GPIO, or use two separate GPIOs.

**YAML config:**
```yaml
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: GPIO17
  rx_pin: GPIO16
  de_pin: GPIO4
  re_pin: GPIO5  # or the same pin as de_pin if tied together
```

### Full diagram

```
                                                      ┌─────────────────────┐
┌─────────────────┐         ┌─────────────────┐      │  Hörmann operator   │
│     ESP32       │         │   RS485 module  │      │                     │
│                 │         │                 │      │ ┌─────────────────┐ │
│  3.3V ──────────┼─────────┤ VCC             │      │ │ 1 │ +24VDC      │ │
│                 │         │                 │      │ │ 2 │ +24VDC      │ │
│  GND ───────────┼────┬────┤ GND         GND ├──────┼─│ 3 │ GND         │ │
│                 │    │    │                 │      │ │ 4 │ Reserved    │ │
│  GPIO17 (TX) ───┼────┼────┤ RX/DI           │      │ │ 5 │ DATA- (B) ──┼─┼─── B
│                 │    │    │                 │      │ │ 6 │ DATA+ (A) ──┼─┼─── A
│  GPIO16 (RX) ───┼────┼────┤ TX/RO       A ──┼──────┼─┘                 │ │
│                 │    │    │             B ──┼──────┼───────────────────┘ │
│  GPIO4* ────────┼────┼────┤ DE*             │      │                     │
│  GPIO5* ────────┼────┼────┤ RE*             │      └─────────────────────┘
│                 │    │    │                 │
└─────────────────┘    │    └─────────────────┘
                       │
                       └── A common ground matters!

* Only for modules with DE/RE control
```

### Option 3: ISOLATED RS485 transceiver (e.g. WeAct CAN485 — CA-IS2092A) ✅ Validated

**Isolated** RS485 transceivers have an **aggressive internal fail-safe**. But the Hörmann bus
drives **asymmetrically** (DATA+ swings, DATA- stays near GND): the isolated receiver reads the
weak "space" level as idle and **decodes nothing** (0 frames, 0 breaks), where a **non-isolated**
transceiver decodes fine on the same wires.

**Solution: physically SWAP A and B**, and compensate in software with `ab_inverted: true`.

```
Hörmann bus              WeAct CAN485 (RS485 terminal)
┌──────────────┐         ┌───────────────────────────┐
│ pin 6  DATA+  ├─────────┤ B-   (swapped!)            │
│ pin 5  DATA-  ├─────────┤ A+   (swapped!)            │
│ pin 3  GND    ├─────────┤ GND  (Mass)                │
└──────────────┘         └───────────────────────────┘
   Board switches: PUPD = OFF, 120 Ω termination = OFF (the master already biases/terminates)
   WeAct pinout: DI(TX)=GPIO22, RO(RX)=GPIO21, DE=GPIO17, WS2812=GPIO4
```

```yaml
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: GPIO22       # DI
  rx_pin: GPIO21       # RO
  de_pin: GPIO17       # DE
  ab_inverted: true    # A/B physically swapped -> software re-inversion RX+TX
  reply_delay_us: 3800 # reply latency tuned to a real UAP1 (~3.84 ms)
```

> Full config: [example_can485.yaml](example_can485.yaml). The detailed "why" (scope measurements,
> diagnosis): [investigation/README.md](investigation/README.md).

## Installation

### Option 1: From GitHub (recommended)

Just add the external component to your YAML:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/AntorFr/esphome-Hormann
      ref: main  # or a specific tag like v1.0.0
    components: [ hormann_hcp1 ]
```

### Option 2: Local installation

#### 1. Clone the repository

```bash
git clone https://github.com/AntorFr/esphome-Hormann.git
cd esphome-Hormann
```

#### 2. Use the local path

```yaml
external_components:
  - source:
      type: local
      path: components
```

### Configure secrets

```bash
cp secrets.yaml.example secrets.yaml
# Edit secrets.yaml with your WiFi / API / OTA values
```

### Compile and flash

```bash
# With the ESPHome CLI
esphome run example_hcp1.yaml

# Or with the ESPHome Dashboard
# Add example_hcp1.yaml to the dashboard
```

## YAML configuration

### Minimal config (from GitHub)

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/AntorFr/esphome-Hormann
    components: [ hormann_hcp1 ]

# No `uart:` block — the component drives the UART port directly (native ESP-IDF).
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: 17         # -> RS485 module DI/RX
  rx_pin: 16         # -> RS485 module RO/TX
  de_pin: GPIO4      # EN or DE (required to command)
  ab_inverted: auto  # auto-detect A/B polarity (RX+TX)

cover:
  - platform: hormann_hcp1
    name: "Garage Door"
    hormann_hcp1_id: hormann
```

### Config with DE/RE control

```yaml
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: 17
  rx_pin: 16
  de_pin: GPIO4
  re_pin: GPIO5      # or the same pin as de_pin if tied together (EN)
  ab_inverted: auto
```

### A/B polarity (RS485 inversion)

Depending on the RS485 module and wiring, the A/B pair may be inverted relative to the Hörmann bus.
Since A and B form **a single differential pair**, an inversion affects **RX and TX together** — so
the component handles both with one setting:

```yaml
hormann_hcp1:
  id: hormann
  ab_inverted: auto   # auto (default) | true | false
```

- **`auto`** (default): at boot the component listens to the bus for ~5 s. If it decodes no valid
  frame but sees many breaks/frame errors, it automatically inverts RX **and** TX
  (`RXD_INV | TXD_INV`) and re-checks. The decision is logged.
- **`true`**: force RX+TX inversion (A/B wired the opposite of the module's marking).
- **`false`**: no inversion.

> 💡 If there is still no frame after auto-inversion, it is **not** a simple A/B inversion: check
> the wiring and especially the bus **fail-safe biasing** (bias resistors), particularly if a
> 120 Ω termination is present.
>
> 💡 **ISOLATED transceiver** (e.g. CA-IS2092A): `auto` will detect **nothing** (0 breaks, 0 frames).
> You must **physically SWAP A and B** on the terminal **and** set `ab_inverted: true` (see Option 3).
> PUPD = OFF.

### Full configs

- [example_hcp1.yaml](example_hcp1.yaml) — generic **non-isolated** RS485 module (MAX485/SP3485).
- [example_can485.yaml](example_can485.yaml) — **WeAct CAN485** (isolated transceiver, swapped A/B) — **validated config**.

## Home Assistant entities

Once configured, the following entities are exposed in Home Assistant:

### Cover (garage door)
- **Garage Door** — main door control (open/close/stop)

### Light
- **Garage Light** — garage light control

### Binary sensors
- **Light Status** — light state
- **Error** — error indicator
- **Venting Position** — venting position (door slightly open)
- **Pre-warning** — pre-warning before movement

### Buttons
- **Impulse** — impulse (like the remote)
- **Venting Position** — move to venting position
- **Emergency Stop** — emergency stop

## HCP1 protocol

The component emulates a Hörmann UAP1 module to talk to the operator.

### Communication parameters
- **Baud rate**: 19200
- **Data bits**: 8
- **Parity**: none
- **Stop bits**: 1
- **Bus**: RS485 half-duplex

### Addresses
- `0x00` — broadcast
- `0x80` — master (operator)
- `0x28` — UAP1 (our emulator)

### Supported commands
| Action | Description |
|--------|-------------|
| Open | Open the door |
| Close | Close the door |
| Stop | Stop movement |
| Impulse | Impulse (reverses direction) |
| Venting | Venting position |
| Toggle Light | Toggle the light |
| Emergency Stop | Emergency stop |

### Registration & command (the "sync break")

To get **registered** (and therefore able to command), in reply to the master's scan the component
must emit a frame preceded by a **long enough sync break** (~920 µs, like a real UAP1). The component
generates it by sending the leading `0x00` at **9600 baud** then the frame at 19200. Without it, the
master scans in a loop **without ever escalating** to `status_request` → the command is ignored. The
reply latency is tunable via `reply_delay_us` (sensible default: `3800`, tuned to a real UAP1
~3.84 ms). The timing does not need to be µs-precise.

## Troubleshooting

### Nothing is decoded (0 frames on the read side)
1. A/B may be inverted → `ab_inverted: auto` (or `true`). If **0 breaks / 0 frames** on an
   **isolated** transceiver: **physically** swap A/B on the terminal + `ab_inverted: true`,
   PUPD = OFF (see Option 3).
2. Check that **GND** is connected.
3. Enable `sniffer: true` (or DEBUG logs) to see the bus frames.

### Reading works but commands are ignored
The master **scans** our address but **never escalates** to `status_request` (so we're not registered):
1. A **`de_pin`** is required (the component does not transmit without it → read-only).
2. The **sync break** must be long enough (~920 µs) — handled by the component; check
   `reply_delay_us: 3800`.
3. `sniffer: true`: if you see **`STATUS_REQ->us`** frames, you're **registered** (win).
4. Only one responder on address 0x28 (turn off any 2nd emulator / non-passive witness).

### Error 7 on the operator
Error 7 means the operator gets no reply from the "slave" (UAP1). Check:
- The wiring
- The UART configuration
- The DE/RE pins

### Debug logs

Enable verbose logs:

```yaml
logger:
  level: DEBUG
  logs:
    hormann_hcp1: DEBUG
```

## Compatible operators

This component was developed for Hörmann operators using the HCP1 protocol:
- Supramatic E3
- Supramatic E4
- ProMatic 4
- And other models with a UAP1 bus

## References

- [`investigation/README.md`](investigation/README.md) — **field notes**: the 2 traps
  (A/B swap on isolated, sync break), the dead ends, and the tools (Saleae decoder).
- [hoermann_door by stephan192](https://github.com/stephan192/hoermann_door) — reference project (PIC16 + ESP)
- [hgdo by steff393](https://github.com/steff393/hgdo) — ESP8266 + SoftwareSerial (ms timing, long break)
- [Bouni's blog — Hörmann UAP1](https://blog.bouni.de/posts/2018/hoerrmann-uap1/) — protocol reverse engineering

## License

MIT License — see [LICENSE](LICENSE)

## Contributing

Contributions are welcome! Feel free to:
- Report bugs
- Suggest improvements
- Test with different Hörmann operator models

## TODO

- [ ] HCP2 protocol support
- [x] ~~Precise door position~~ — not supported by HCP1 (binary states only)
