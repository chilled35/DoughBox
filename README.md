# 🍕 DoughBox

A repurposed domestic refrigerator converted into a precision fermentation controller for cold-proofing pizza and bread dough. The DoughBox provides active temperature, humidity, and climate control managed by a custom PCB, ESPHome firmware, and Home Assistant automations — with a bespoke HTML dashboard served via Fully Kiosk Browser on a wall-mounted Lenovo M8 tablet.

---

## Table of Contents

- [Overview](#overview)
- [Hardware](#hardware)
- [Repository Structure](#repository-structure)
- [ESPHome Firmware](#esphome-firmware)
- [Home Assistant Integration](#home-assistant-integration)
- [Dashboards](#dashboards)
- [Automations & Scripts](#automations--scripts)
- [Entity Reference](#entity-reference)
- [Known Issues & Deferred Work](#known-issues--deferred-work)
- [Setup Notes](#setup-notes)

---

## Overview

The DoughBox is a full-stack maker project spanning custom PCB hardware, embedded firmware, and home automation software. The primary use case is cold-proofing pizza dough at 4°C with a secondary warm-proof stage at ~24°C, with a scheduled fermentation controller that transitions between stages automatically.

Climate control uses bang-bang (on/off) logic implemented in ESPHome's `thermostat` platform — this has proven reliable and is intentionally kept simple. Future work includes predictive anticipatory cutoff layered on top (see [Known Issues & Deferred Work](#known-issues--deferred-work)), but the bang-bang controller is the production control loop and is not to be replaced.

---

## Hardware

### Controller Board

Custom PCB designed in EasyEDA, fabricated by JLCPCB.

| Reference | Component | Notes |
|---|---|---|
| U1 | Seeed XIAO ESP32-C3 | Castellated module, single core, Wi-Fi |
| U2, U3 | *(formerly P82B96DR I2C bus extenders)* | **Permanently abandoned** — see deferred work |
| BME280 ×2 | Temperature / Humidity / Pressure sensors | I2C addresses 0x76 and 0x77, short local bus only |
| Q1 | AO3401A P-channel MOSFET (SOT-23) | Gate driven via NPN level-shifter; +5V gate pull-up |
| Relay module | 4-channel SRD-05VDC-SL-C type | Active-HIGH logic via integrated optocoupler/NPN driver |
| GPIO9 | Relay board power rail switch | Held LOW during boot to prevent relay chatter at power-up |

### Controlled Loads

| Load | Switch Entity | Notes |
|---|---|---|
| Compressor | `switch.doughbox_compressor` | 5-minute restart lockout after turn-off |
| Ceramic fan heater (500 W) | `switch.doughbox_heater` | Interlocked with compressor; blocked above 28°C |
| Dehumidifier | `switch.doughbox_dehumidifier` | Humidity bang-bang, 30 s interval |
| Mains power (Zigbee smart plug) | `switch.appliance_doughboxpower` | Power monitoring; **current plug faulty (~2 W reading)** |

### Relay Logic

All relay GPIOs use `inverted: false` in ESPHome. The integrated optocoupler/NPN driver is active-HIGH: GPIO HIGH → relay energised.

### Remote Display

A **Lenovo M8 tablet** running **Fully Kiosk Browser** in kiosk mode, pointed at `http://192.168.0.210:8123/local/htmldoughbox-tablet.html`. This is the primary user interface. There is no local OLED display; no ribbon cable; no I2C bus extenders fitted.

### Network

| Device | Address |
|---|---|
| Home Assistant | 192.168.0.210 |
| DoughBox controller | 192.168.0.83 (doughbox.local) |
| MQTT broker (Mosquitto) | 192.168.0.210:1883 |

---

## Repository Structure

```
DoughBox/
├── README.md
├── esphome/
│   ├── doughbox-esp32c3.yaml       # Controller firmware (ESPHome)
│   └── doughbox_prefs.h            # NVS flash persistence (mode, temp setpoints)
└── homeassistant/
    ├── automations_doughbox.yaml   # Doughbox section of automations.yaml
    ├── lovelace/
    │   └── doughbox_dashboard.yaml # Lovelace/Mushroom YAML dashboard
    └── www/
        ├── htmldoughbox.html       # Mobile/phone HTML dashboard
        └── htmldoughbox-tablet.html # Tablet (Lenovo M8) HTML dashboard
```

### Deployment paths on Home Assistant

| File | HA Path |
|---|---|
| `doughbox-esp32c3.yaml` | `/config/esphome/doughbox-esp32c3.yaml` |
| `doughbox_prefs.h` | `/config/esphome/doughbox_prefs.h` |
| `automations_doughbox.yaml` | Append contents to `/config/automations.yaml` |
| `doughbox_dashboard.yaml` | `/config/lovelace/` (or paste into dashboard YAML editor) |
| `htmldoughbox.html` | `/config/www/htmldoughbox.html` |
| `htmldoughbox-tablet.html` | `/config/www/htmldoughbox-tablet.html` |

---

## ESPHome Firmware

**File:** `esphome/doughbox-esp32c3.yaml`

### Key features

- Dual BME280 sensors (0x76, 0x77) publishing individual and averaged temperature/humidity entities
- `thermostat` climate platform (`climate.doughbox_climate`) with `HEAT_COOL` mode and bang-bang control
- Compressor restart lockout (5 min) implemented as a template sensor + heater interlock
- Heater blocked above 28°C to prevent thermal runaway during CT→RT transitions
- Humidity bang-bang controller (30 s interval) driving the dehumidifier relay
- Flash persistence via `doughbox_prefs.h` — saves mode and temperature setpoints to NVS so the controller resumes after a power cut without HA intervention
- Boot relay suppression: GPIO9 holds the relay board power rail LOW during startup; raised after GPIO initialisation to prevent relay chatter at power-on
- Sensor watchdog: if both BME280s return NaN for ≥30 s, all loads are cut and an MQTT alert is published to `doughbox/alert`
- MQTT broker integration for sensor failure/recovery alerts (broker: 192.168.0.210)
- `reboot_timeout: 0s` — WiFi loss does not trigger an ESP reboot (prevents climate state loss mid-ferment)
- OTA updates enabled

### Companion header: `doughbox_prefs.h`

Uses ESPHome's native `esphome::preferences` API (NVS slots 1001–1003) to persist three values: active mode (0=OFF, 1=HEAT_COOL), target temp low, target temp high. Called from the `on_boot` lambda after a 5-second stabilisation delay.

### I2C configuration

```yaml
i2c:
  sda: GPIO6
  scl: GPIO7
  scan: true
  id: bus_a
  frequency: 100000
```

Both BME280s are on a short local bus. The P82B96DR bus extenders (U2/U3) are permanently removed; do not refit.

---

## Home Assistant Integration

The ESPHome node integrates via the native ESPHome API (not MQTT for sensor data). The MQTT broker is used only for out-of-band alerts (`doughbox/alert` topic).

### Input helpers required

```yaml
input_boolean:
  doughbox_schedule_active:

input_select:
  doughbox_current_phase:
    options: [Idle, Cold Ferment, Warm Proof, Complete]

input_datetime:
  doughbox_cold_ferment_end:
  doughbox_warm_proof_end:

input_number:
  doughbox_cold_ferment_temp:
    min: 1
    max: 10
    step: 0.5
    initial: 4
  doughbox_warm_proof_temp:
    min: 18
    max: 30
    step: 1
    initial: 24
  doughbox_warm_proof_hours:
    min: 1
    max: 12
    step: 0.5
    initial: 4
```

---

## Dashboards

### Mobile dashboard (`htmldoughbox.html`)

Standalone HTML page served from HA's `/local/` directory. Authenticates via an embedded Long-Lived Access Token (LLAT) constant `EMBEDDED_TOKEN` with localStorage fallback. Features:

- Live temperature/humidity chart (Chart.js) with rolling history
- Fermentation schedule controls (cold ferment end datetime, warm proof duration/temp)
- Manual start/skip/stop buttons calling HA scripts
- Relay status strip (compressor, heater, dehumidifier, mains power)
- Dough probe temperature
- Cost tracking card (Octopus Agile tariff integration — **currently disabled pending replacement Zigbee plug**)
- Overtemperature banner (fires if box temp >30°C or firmware flag set)
- Cache-bust via version query string (`?v=N`) — increment on each deployment

**Access:** `http://192.168.0.210:8123/local/htmldoughbox.html?v=N`

### Tablet dashboard (`htmldoughbox-tablet.html`)

Optimised layout for the Lenovo M8 (Fully Kiosk Browser, landscape). Larger chart, touch-friendly controls. Same auth pattern as the mobile dashboard.

**Access:** `http://192.168.0.210:8123/local/htmldoughbox-tablet.html?v=N`

### HA push notification target

`notify.mobile_app_jasons_iphone`

---

## Automations & Scripts

**File:** `homeassistant/automations_doughbox.yaml`

### Scripts (defined in HA package)

| Script | Action |
|---|---|
| `script.doughbox_start_fermentation` | Sets phase to Cold Ferment, activates schedule, sets climate setpoints from `input_number` helpers |
| `script.doughbox_skip_to_room_temp_proof` | Transitions to Warm Proof at 24°C for 4 hours |
| `script.doughbox_stop` | Cancels schedule, sets phase to Idle, turns climate off |
| `script.doughbox_set_defaults` | Resets schedule datetimes to next Saturday 12:00 |

### Automations

| Automation | Trigger | Action |
|---|---|---|
| `doughbox_ct_to_warm_proof` | `input_datetime.doughbox_cold_ferment_end` time reached | Calls `script.doughbox_skip_to_room_temp_proof` |
| `doughbox_ct_to_warm_proof_startup_catchup` | HA start, if CT end time already passed | Calls `script.doughbox_skip_to_room_temp_proof` |
| `doughbox_warm_proof_to_complete` | `input_datetime.doughbox_warm_proof_end` time reached | Sets phase Complete, turns climate off |
| `doughbox_warm_proof_to_complete_startup_catchup` | HA start, if WP end time already passed | Sets phase Complete, turns climate off |
| `doughbox_sensor_failure_alert` | MQTT `doughbox/alert` → `sensor_fail` | Critical iOS push notification |
| `doughbox_sensor_recovery_alert` | MQTT `doughbox/alert` → `sensor_recovery` | Standard iOS push notification |

---

## Entity Reference

| Entity | Description |
|---|---|
| `climate.doughbox_climate` | Main climate controller |
| `sensor.doughbox_temperature_average` | Average of both BME280 temperature readings |
| `sensor.doughbox_humidity_average` | Average of both BME280 humidity readings |
| `sensor.thermdoughprobe_temperature` | Dough probe temperature (separate sensor) |
| `switch.doughbox_compressor` | Compressor relay |
| `switch.doughbox_heater` | Ceramic fan heater relay |
| `switch.doughbox_dehumidifier` | Dehumidifier relay |
| `switch.appliance_doughboxpower` | Zigbee mains power switch |
| `sensor.appliance_doughboxpower_power` | Instantaneous power draw (W) |
| `sensor.appliance_doughboxpower_energy` | Cumulative energy (kWh) |
| `sensor.octopus_energy_electricity_19k0200079_2198765140197_current_rate` | Octopus Agile current unit rate (£/kWh) |

---

## Known Issues & Deferred Work

### Active / in progress

- **Faulty Zigbee smart plug** — replacement on order. Reads ~2W regardless of load. Cost-tracking dashboard cards are disabled until a working plug is verified. If instantaneous-W readings remain unreliable on the new plug, the cost algorithm should fall back to energy-sensor delta at relay state-change boundaries.

### Deferred hardware

- **I2C bus extenders (U2/U3 / P82B96DR)** — permanently abandoned. The original design used these to buffer a 2-metre I2C run to a remote OLED display. The remote display is now the Lenovo M8 tablet via Fully Kiosk Browser. Both BME280s run on a short local bus with no extenders. Do not refit U2/U3.
- **ILI9341 3.2" TFT (240×320, SPI, resistive touch)** — noted as a future display upgrade. Explicitly deferred until current hardware is finalised.
- **PCF8574 I2C GPIO expander** — noted as an elegant future approach for encoding front-panel button presses locally. Deferred.

### Future firmware work

- **Predictive anticipatory cutoff** — profile heating and cooling rates from collected run data to enable early relay cutoff and reduce overshoot. The fan heater (500 W ceramic) overshoots significantly during CT→RT transitions. The bang-bang controller is to remain intact; anticipatory cutoff is a layer on top, not a replacement.

---

## Setup Notes

1. Copy `doughbox_prefs.h` into the same ESPHome config directory as the YAML before compiling.
2. Add Wi-Fi credentials to `secrets.yaml`:
   ```yaml
   wifi_ssid: "YourSSID"
   wifi_password: "YourPassword"
   ```
3. The LLAT in `htmldoughbox.html` and `htmldoughbox-tablet.html` is specific to the HA instance. Generate a new one via **HA Profile → Long-Lived Access Tokens** if deploying to a different instance.
4. Bump the `?v=N` query string in any Lovelace `webpage` card URL each time the HTML dashboards are updated, to bust the Fully Kiosk / Companion app WebView cache.
5. Home Assistant logs in UTC. The installation is UK-based (GMT/BST). Verify timezone offset when correlating log timestamps against real-world events.
6. The compressor interlock prevents simultaneous compressor and heater operation. The lockout applies to restarts only, not initial run.

---

## Hardware Design Files

PCB designed in EasyEDA. Gerbers and BOM available separately (JLCPCB order format). Not included in this repository.

---

*Project by Jason (chilled35) — built with ESPHome, Home Assistant, and way too much pizza dough.*
