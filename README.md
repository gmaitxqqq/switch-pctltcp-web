# Switch Parental Control Web (pctltcp-web)

Nintendo Switch homebrew app that provides **mobile web UI** for parental control play timer management.

## Features

- **Mobile Web UI** — Open `http://<Switch-IP>:8080` on any phone browser (iOS Safari / Android Chrome)
- **PC Client Compatible** — TCP server on port 6000, works with existing `swpc_client.py`
- **No Installation Required** — Just open the URL in your browser

## Controls via Web UI

- View real-time timer status (running/paused, remaining time)
- Set daily time limits per weekday (Sunday through Saturday)
- Apply uniform limit to all 7 days
- Start / Pause / Reset play timer

## REST API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Web UI HTML |
| GET | `/api/status` | Timer status JSON |
| GET | `/api/settings` | All 7 day limits JSON |
| POST | `/api/set` | Set all days: `{"minutes": 60}` |
| POST | `/api/set_day` | Set one day: `{"day": 0, "minutes": 60}` |
| GET | `/api/version` | Version string |

## Installation

1. Download `pctltcp-web.nro` from [Releases](https://github.com/gmaitxqqq/switch-pctltcp-web/releases)
2. Copy to SD card: `/switch/pctltcp-web.nro`
3. Launch via Homebrew Menu
4. Open `http://<Switch-IP>:8080` on your phone

## Requirements

- Nintendo Switch with Atmosphere CFW
- Firmware 22.1.0 (tested)
- Both Switch and phone on the same WiFi network

## Build

Requires [devkitPro](https://devkitpro.org/) with Switch support.

```bash
make
```

## Author

**gmaitxqqq**

## License

MIT
