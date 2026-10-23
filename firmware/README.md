# Firmware

Two sketches share the same logic; only the unit shown in the output differs.

| Folder | Purpose | Distance unit in the output |
|---|---|---|
| `smart_parking_v9/` | **The sketch that runs on the model.** | centimetres of the 1:100 model |
| `smart_parking_v9_research/` | The same firmware for the report and the simulation. | metres of the full-size lot (1 cm on the model = 1 m) |

Each folder holds the sketch (`.ino`) and `parking_page.h`, the live web page stored in flash (PROGMEM). Keep both files together: the Arduino IDE needs the folder name to match the sketch name.

## Build

* Arduino IDE 2.x with the **esp32** boards package by Espressif, version **3.0.1**.
* Board: *ESP32 Dev Module*. Serial Monitor: 9600 baud, *New Line* line ending.
* No external libraries: only `WiFi.h` and `WebServer.h` from the ESP32 core.

## Wiring

| Signal | ESP32 pin |
|---|---|
| Entrance sensor (HC-SR04) Trig / Echo | 27 / 14 |
| Spot-0 sensor (HC-SR04) Trig / Echo | 26 / 25 |
| Status LED | 2 (on-board) |

The HC-SR04 runs from 5 V and its Echo pin outputs 5 V; a voltage divider in front of the ESP32 input is recommended.

## Where things are

The sketch is organised in numbered sections; the parts you are most likely to touch:

| Section | Contents |
|---|---|
| `[1]` Hardware and sensors | pins, the measured sensor thresholds (`GATE_*`, `SPOT_ARRIVE_CM`, `SPOT_LEAVE_CM`), sample interval, confirmation counts, reservation timeout |
| `[2]` Model geometry | the measured lengths of the model (`ENT_TO_FLOOR0_CM`, `ENT_TO_RAMP_CM`, `RAMP_CM`, `FLOOR1_TO_FIRST_CM`, stall and car sizes), `DIST_UNIT` |
| `[4]` Road network | `buildRoadNetwork()`, `heuristic()`, `aStarDistance()`, `computeSpotDistances()` |
| `[4ב]` Picture and JSON | the text picture for the Serial Monitor and the `/state` payload |
| `[7]` Sensors | `classifyGate()`, hysteresis and confirmation for spot 0, the LED |

Two switches at the end of section `[1]`:

* `ENABLE_WEB_VIEW` (default 1): 0 disables Wi-Fi and the Serial Monitor shows the text picture instead.
* `TEXT_MAP` (default `false`): `true` also prints the text picture while the web page is on. The automated tests need `true` when they run against the real board.

## Calibrating the sensors

Run `check 10` in the Serial Monitor to see ten seconds of raw readings from both sensors, then adjust the thresholds in section `[1]`:

* entrance: the wall reads ~30 cm and a passing car ~22 cm; a car is counted below `GATE_CAR_BELOW_CM` (25 cm);
* spot 0: an empty stall reads 25–26 cm, a parked car 22.5–24.5 cm depending on its height; a car arrives below `SPOT_ARRIVE_CM` (24.7 cm) and leaves above `SPOT_LEAVE_CM` (24.9 cm).

If spot 0 flickers, raise the sensor so that an empty stall reads farther away, then widen the gap between the two thresholds.
