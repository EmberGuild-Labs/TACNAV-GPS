# TACNAV-GPS

A tactical GPS application for the Flipper Zero, designed for use with the NEO-6M GPS module. TACNAV-GPS displays real-time location data directly on your Flipper — coordinates, speed, heading, satellite count, and more — all fully offline with no internet required.

---

## Features

- 📍 Real-time GPS coordinates (latitude & longitude)
- 🧭 Heading and speed display
- 🛰️ Satellite count and fix status indicator
- 📴 Fully offline — no Wi-Fi or internet needed
- 🎨 Cleaner, more polished UI than the stock Momentum UART GPS app
- ⚡ Lightweight FAP built with `ufbt`

---

## Hardware Required

| Component | Details |
|---|---|
| Flipper Zero | Running Momentum firmware (recommended) |
| NEO-6M GPS Module | UART, 9600 baud |
| Jumper Wires | For connecting to GPIO header |
| Optional: Perfboard | For a clean, permanent add-on board |
| Optional: U.FL Antenna | For improved GPS signal reception |

---

## Wiring (NEO-6M → Flipper Zero GPIO)

| NEO-6M Pin | Flipper Zero Pin |
|---|---|
| VCC | 3.3V (Pin 9) |
| GND | GND (Pin 8 or 18) |
| TX | RX / Pin 14 |
| RX | TX / Pin 13 |

> ⚠️ The NEO-6M runs on 3.3V logic — safe to wire directly to the Flipper Zero GPIO header without a level shifter.

---

## Building & Installing

### Prerequisites
- [`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt) installed and configured
- Flipper Zero connected via USB

### Build
```bash
ufbt
```

### Deploy to Flipper
```bash
ufbt launch
```

Or manually copy the compiled `.fap` from `dist/` to your SD card at:
```
SD Card/apps/GPIO/
```

---

## Firmware Compatibility

Primarily developed and tested on **Momentum firmware**. Should also be compatible with Unleashed and RogueMaster. Official stock firmware may restrict GPIO/UART serial access needed for this app.

---

## Version History

### V2 (Current)
- Redesigned UI — cleaner and more polished than V1 and the stock Momentum UART GPS app
- Improved GPS data parsing and display layout
- Better handling of GPS fix status
- General stability and performance improvements

### V1
- Initial release as a Flipper Zero FAP
- Basic UART GPS display using the NEO-6M module
- Functional but minimal UI

---

## Project Structure

```
TACNAV-GPS/
├── application.fam        # App manifest
├── tacnav_gps.c           # Main application source
├── README.md
└── assets/                # Wiring diagrams and reference images
```

---

## License

MIT License — see `LICENSE` for details.

---

> Built by [EmberGuild-Labs](https://github.com/EmberGuild-Labs)
