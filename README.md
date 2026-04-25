# 🍕 DoughBox

A repurposed domestic refrigerator converted into a precision fermentation controller for cold-proofing pizza and bread dough. The DoughBox provides active temperature and climate control managed by a custom PCB, ESPHome firmware, and Home Assistant automations — with a bespoke HTML dashboard served via Fully Kiosk Browser on a wall-mounted Lenovo M8 tablet.

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

The DoughBox is a full-stack maker project spanning custom PCB hardware, embedded firmware, and home automation software. The primary use case is cold-proofing pizza dough at 3°C with a secondary warm-proof stage at ~24°C, with a scheduled fermentation controller that transitions between stages automatically.

Climate control uses bang-bang (on/off) logic implemented in ESPHome's `thermostat` platform. A predictive heater cutoff layer prevents overshoot during the CT→RT transition — this fires early based on an empirically derived thermal coast delta and is implemented in firmware, not HA.

---

## Hardware

### Controller Board

Custom PCB designed in EasyEDA, fabricated by JLCPCB.

| Reference | Component | Notes |
|---|---|---|
| U1 | Seeed XIAO ESP32-C3 | Castellated module, single core, Wi-Fi |
| U2, U3 | *(formerly P82B96DR I2C bus extenders)* | **Permanently removed** — do not refit |
| BME280 ×2 | Temperature / Humidity / Pressure sensors | I2C addresses 0x76 and 0x77, short local bus only |
| Q1 | AO3401A P-channel MOSFET (SOT-23) | Gate driven via NPN level-shifter; +5V gate pull-up |
| Relay module | 4-channel SRD-05VDC-SL-C type | Active-HIGH logic via integrated optocoupler/NPN driver |
| GPIO9 | Relay board power rail switch | Held LOW during boot to prevent relay chatter at power-up |

### Controlled Loads

| Load | Switch Entity | Rated Power | Notes |
|---|---|---|---|
| Compressor | `switch.doughbox_compressor` | 65 W | 5-minute restart lockout after turn-off |
| Ceramic fan heater | `switch.doughbox_heater` | 500 W | Interlocked with compressor; firmware blocks turn-on above 28°C |
| Dehumidifier | `switch.doughbox_dehumidifier` | — | Humidity bang-bang, 30 s interval |
| Mains power (Zigbee smart plug) | `switch.appliance_doughboxpower` | — | Relay switching only — no power metering used |

### Relay Logic

All relay GPIOs use `inverted: false` in ESPHome. The integrated optocoupler/NPN driver is active-HIGH: GPIO HIGH → relay energised.

### Remote Display

A **Lenovo M8 tablet (TB8505F, 1280×800)** running **Fully Kiosk Browser** in kiosk mode, pointed at `http://192.168.0.210:8123/local/htmldoughbox-tablet.html`. This is the primary user interface. There is no local OLED display; no ribbon cable; no I2C bus extenders fitted.

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
│   ├── esphome-web-90e2ac.yaml     # Controller firmware (ESPHome)
│   └── doughbox_prefs.h            # NVS flash persistence (mode, temp setpoints)
└── homeassistant/
    ├── automations_doughbox.yaml   # Doughbox automations (append to automations.yaml)
    ├── lovelace/
    │   └── doughbox_dashboard.yaml # Lovelace panel dashboard
    └── www/
        ├── htmldoughbox.html       # Mobile/desktop HTML dashboard
        └── htmldoughbox-tablet.html # Tablet (Lenovo M8) HTML dashboard
```

### Deployment paths on Home Assistant

| File | HA Path |
|---|---|
| `esphome-web-90e2ac.yaml` | `/config/esphome/esphome-web-90e2ac.yaml` |
| `doughbox_prefs.h` | `/config/esphome/doughbox_prefs.h` |
| `automations_doughbox.yaml` | Append contents to `/config/automations.yaml` |
| `doughbox_dashboard.yaml` | `/config/lovelace/` (or paste into dashboard YAML editor) |
| `htmldoughbox.html` | `/config/www/htmldoughbox.html` |
| `htmldoughbox-tablet.html` | `/config/www/htmldoughbox-tablet.html` |

---

## ESPHome Firmware

**File:** `esphome/esphome-web-90e2ac.yaml`

### Key features

- Dual BME280 sensors (0x76, 0x77) publishing individual and averaged temperature/humidity entities
- `thermostat` climate platform (`climate.doughbox_climate`) with `HEAT_COOL` mode and bang-bang control
- **Cold Ferment preset:** `low: 2.5°C / high: 4.5°C` — centred on 3°C operating target
- **Warm Proof preset:** `low: 26°C / high: 28°C`
- Compressor restart lockout: `min_cooling_off_time: 300s` (5 minutes)
- Heater/compressor interlock — cannot run simultaneously
- **Heater turn-on guard:** firmware blocks heater activation if box temp > 28°C (prevents thermal runaway regardless of instruction source)
- **Over-temperature protection:** `binary_sensor.doughbox_over_temperature` — triggers above 30°C, immediately cuts all loads via `on_press` lambda. Hard limit enforced in firmware independent of HA
- **Predictive heater cutoff:** 5-second interval lambda cuts heater when `current_temp + 7.0°C ≥ target_temp_high`. Derived from empirical thermal coast data (loaded box: +6.53°C coast; empty box: +7.5°C coast). Prevents the 500W heater overshooting target during CT→RT transition
- Humidity bang-bang controller (30 s interval) driving the dehumidifier relay
- Flash persistence via `doughbox_prefs.h` — saves mode and temperature setpoints to NVS so the controller resumes after a power cut without HA intervention
- Boot relay suppression: GPIO9 holds the relay board power rail LOW during startup; raised after GPIO initialisation to prevent relay chatter
- MQTT integration for out-of-band state publish (`doughbox/state`, 2s interval) and command receipt (`doughbox/cmd`)
- `reboot_timeout: 6h` — short WiFi losses do not trigger ESP reboot
- OTA updates enabled
- Web server on port 80 (diagnostic access via `http://doughbox.local`)

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

The ESPHome node integrates via the native ESPHome API (not MQTT for sensor data). The MQTT broker is used only for out-of-band state publish and command receipt.

### Input helpers required

```yaml
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
    initial: 2.5
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

number:
  # published by ESPHome
  doughbox_target_humidity:
    min: 40
    max: 85
    step: 5
    initial: 70
```

---

## Dashboards

Both dashboards share the same HA REST API authentication pattern: an embedded Long-Lived Access Token (LLAT) constant `EMBEDDED_TOKEN` with `localStorage` fallback.

### Mobile dashboard (`htmldoughbox.html`)

Standalone HTML page served from HA's `/local/` directory.

**Features:**
- Live temperature/humidity chart (Chart.js) with 24-hour rolling history
- Fermentation schedule controls (cold ferment end datetime, warm proof duration/temp)
- Manual start/skip/stop buttons calling HA scripts
- Relay status strip (compressor, heater, dehumidifier, mains power)
- Dough probe temperature
- Cost tracking — fixed-wattage model: compressor 65W, heater 500W, controller 5W × runtime, × live Octopus Agile rate
- Over-temperature banner (fires if `binary_sensor.doughbox_over_temperature` is `on`, or if box temp > 30°C as fallback)

**Access:** `http://192.168.0.210:8123/local/htmldoughbox.html`

### Tablet dashboard (`htmldoughbox-tablet.html`)

Optimised for Lenovo M8 TB8505F (1280×800) running Fully Kiosk Browser in landscape.

**Features (in addition to mobile):**
- Full-width chart dominating the right panel
- **Kiosk/distance mode** — after 10 seconds idle, auto-transitions to a full-screen view with 112px temperature figures, 20px axis labels, and status dots readable from across the kitchen. Tap anywhere to return
- Phase-aware chart history window: 12h during Cold Ferment, 4h during Warm Proof, 24h when idle
- Over-temperature banner present in both normal and kiosk views
- Bioluminescent colour scheme: teal (#00e5cc) / sea-green (#00ff87) / purple (#7b5ea7) on abyss black (#030d14)

**Access:** `http://192.168.0.210:8123/local/htmldoughbox-tablet.html`

### HA push notification target

`notify.mobile_app_jasons_iphone`

---

## Automations & Scripts

**File:** `homeassistant/automations_doughbox.yaml`

### Scripts (defined in HA package)

| Script | Action |
|---|---|
| `script.doughbox_start_fermentation` | Sets phase to Cold Ferment, activates schedule, sets climate setpoints |
| `script.doughbox_skip_to_room_temp_proof` | Transitions to Warm Proof at 24°C for 4 hours |
| `script.doughbox_stop` | Cancels schedule, sets phase to Idle, turns climate off |
| `script.doughbox_set_defaults` | Resets schedule datetimes to next Saturday 12:00 |

### Automations

| Automation | Trigger | Action |
|---|---|---|
| `doughbox_ct_to_warm_proof` | `input_datetime.doughbox_cold_ferment_end` reached | Calls `script.doughbox_skip_to_room_temp_proof` |
| `doughbox_ct_to_warm_proof_startup_catchup` | HA start, if CT end already passed | Calls `script.doughbox_skip_to_room_temp_proof` |
| `doughbox_warm_proof_to_complete` | `input_datetime.doughbox_warm_proof_end` reached | Sets phase Complete, turns climate off |
| `doughbox_warm_proof_to_complete_startup_catchup` | HA start, if WP end already passed | Sets phase Complete, turns climate off |
| `doughbox_over_temperature_alert` | `binary_sensor.doughbox_over_temperature` → `on` | Critical iOS push notification (bypasses DND) |
| `doughbox_over_temperature_persistent` | Same sensor `on` for 5+ minutes | Second critical push notification |
| `doughbox_over_temperature_cleared` | `binary_sensor.doughbox_over_temperature` → `off` | All-clear notification, dismisses lock screen alert |
| `doughbox_sensor_failure_alert` | MQTT `doughbox/alert` → `sensor_fail` | Critical iOS push notification |
| `doughbox_sensor_recovery_alert` | MQTT `doughbox/alert` → `sensor_recovery` | Standard iOS push notification |

---

## Entity Reference

| Entity | Description |
|---|---|
| `climate.doughbox_climate` | Main climate controller |
| `sensor.doughbox_temperature_average` | Average of both BME280 temperature readings |
| `sensor.doughbox_humidity_average` | Average of both BME280 humidity readings |
| `sensor.thermdoughprobe_temperature` | Dough probe temperature (NTC, separate sensor) |
| `binary_sensor.doughbox_over_temperature` | Firmware-declared over-temp flag (fires above 30°C) |
| `switch.doughbox_compressor` | Compressor relay |
| `switch.doughbox_heater` | Ceramic fan heater relay |
| `switch.doughbox_dehumidifier` | Dehumidifier relay |
| `switch.appliance_doughboxpower` | Zigbee mains power switch (relay only, no metering) |
| `sensor.doughbox_compressor_lockout_remaining` | Seconds remaining on compressor restart lockout |
| `input_select.doughbox_current_phase` | Current fermentation phase (Idle / Cold Ferment / Warm Proof / Complete) |
| `input_datetime.doughbox_cold_ferment_end` | Scheduled CF end timestamp |
| `input_datetime.doughbox_warm_proof_end` | Scheduled WP end timestamp |
| `input_number.doughbox_cold_ferment_temp` | Target temperature for cold ferment |
| `input_number.doughbox_warm_proof_temp` | Target temperature for room-temp proof |
| `input_number.doughbox_warm_proof_hours` | RT proof duration in hours |
| `number.doughbox_target_humidity` | Target humidity setpoint |
| `sensor.octopus_energy_electricity_19k0200079_2198765140197_current_rate` | Live Octopus Agile electricity rate (£/kWh) |

---

## Known Issues & Deferred Work

### Deferred hardware

- **PCF8574 I2C GPIO expander** — noted as an elegant future approach for encoding front-panel button presses locally, reducing wiring complexity. Not currently planned.

### Notes

- **I2C bus extenders (U2/U3 / P82B96DR)** — permanently removed. The original design used these to buffer a 2-metre I2C run to a remote OLED display. The remote display is now the Lenovo M8 tablet. Both BME280s run on a short local bus. Do not refit U2/U3.
- **ILI9341 3.2" TFT display** — previously noted as a future upgrade. Abandoned in favour of the Lenovo M8 tablet as the sole display interface.
- **Zigbee smart plug power metering** — the plug is dismembered and fitted inside the controller casework for relay switching only. Power metering is not used. Cost tracking uses fixed known wattages (compressor 65W, heater 500W, controller 5W) × relay runtime × live Octopus Agile rate.
- **Predictive heater cutoff** — implemented in firmware (v2, fixed-delta approach). Empirically derived from loaded-box run data (1.75kg dough, 65% hydration): coast rise +6.53°C. `COAST_DELTA = 7.0°C` with safety margin. Fires when `current_temp + 7.0 ≥ target_temp_high`.

---

## Setup Notes

1. Copy `doughbox_prefs.h` into the same ESPHome config directory as the YAML before compiling.
2. Add Wi-Fi credentials to `secrets.yaml`:
   ```yaml
   wifi_ssid: "YourSSID"
   wifi_password: "YourPassword"
   ```
3. The LLAT in `htmldoughbox.html` and `htmldoughbox-tablet.html` is specific to the HA instance. Generate a new one via **HA Profile → Long-Lived Access Tokens** if deploying to a different instance.
4. Home Assistant logs in UTC. The installation is UK-based (GMT/BST). Verify timezone offset when correlating log timestamps against real-world events.
5. The compressor interlock prevents simultaneous compressor and heater operation. The lockout (`min_cooling_off_time: 300s`) applies to restarts only, not initial run.
6. The natural compressor off-time in steady-state CT operation is approximately 30 minutes — well above the 5-minute lockout minimum. The lockout is a safety floor, not a cycle control mechanism.

---

## Hardware Design Files

PCB designed in EasyEDA. Gerbers and BOM available separately (JLCPCB order format). Not included in this repository.

---

*Project by Jason (chilled35) — built with ESPHome, Home Assistant, and way too much pizza dough.*
