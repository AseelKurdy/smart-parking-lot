# Tests for the Smart Parking firmware (`firmware/smart_parking_v9`)

Team: Aseel Kurdy, Nayef Habiballah, Osama Abdulhalim

Everything here runs on the PC. Nothing needs the board, but the main suite can also drive the real board.
The expected values (distances, thresholds, unit) are read from the `.ino`, so the tests follow the measured model.

| What | How to run | What it checks |
|---|---|---|
| **Main suite** `run_tests.ps1` | `powershell -File run_tests.ps1 -Sim` | The 38 original hardware tests (parser, assignment, A* distances vs. a hand formula, timeouts, stress, fuzz) plus 16 newer ones: sensors and LED (`SIM-1..10`, incl. the measured thresholds and the optional exit lane) and web payload / picture / `status` counter (`VIS-1..6`). `-Slow` adds the 60 s timeout tests, `-Soak` the 5-min noise test and the 500-car id-limit test, `-Only "A-"` runs one group. Writes `report.md`; the last complete run is in `last_full_run.txt`. |
| **Model fuzz** `fuzz_model.py` | `python fuzz_model.py --seeds 200 --ops 800` | An independent Python re-implementation of the rules generates random operations; after every one the firmware's state (`/state` payload) must equal the model's. Also runs the 500-car id limit. |
| **Edge cases** `edge_cases.py` | `python edge_cases.py` | 3-digit car ids in the text picture, more than 16 waiting cars (payload cap), worst-case payload size, picture == payload. |
| **ESP32 compile check** `esp32_syntax_check.ps1` | `powershell -File esp32_syntax_check.ps1` | Compiles the sketch (syntax/type check, no linking) with the *real* Xtensa compiler and the *real* Arduino-ESP32 core 3.0.1 headers, using the same flags as the IDE. `-Sketch <path>` for another sketch, `-Warnings` for -Wall -Wextra. |

## How the simulator works (`host_sim/`)

`host_sim.exe` is the real `.ino` compiled on the PC (g++), unmodified, with stand-ins for the hardware:
serial port (stdin/stdout), `millis()`/`delay()` (real clock, or a fake clock in `--script` batch mode),
the two HC-SR04 sensors (`~entrance <cm>`, `~spot <cm>`; noise: `~glitch <pct>` bursts of <2 cm / no-echo,
`~spike <pct> [cm]` single believable-but-false readings), the LED (`~led`) and the web handlers
(`~http state`, `~http root`). Defaults are the measured empty readings: entrance 30 cm, spot 25.8 cm.

`build.ps1` builds variants of a sketch: text picture on (`-TextMap`, what the tests parse), as shipped
(WiFi page only), WiFi off (`-NoWeb`), and the optional two-lane entrance sensor (`-ExitLane`: exit ~22 cm,
entry ~26 cm). `run_tests.ps1 -Sim` builds them into `%TEMP%\parking_sim` automatically.

## Running the suite on the real board

`powershell -File run_tests.ps1 -Port COM10` - close the Serial Monitor first. The tests read `status` as the
text picture, and the shipped build prints only a summary there, so set `const bool TEXT_MAP = true;` before
flashing for the test run (and set it back afterwards). `SIM-*` and `VIS-*` are skipped on a real board.

## Known limits of the simulation

* No baud-rate emulation: the 9600-baud blocking of a printed picture (~1.4 s) and the 256-byte RX buffer are not modelled.
* No WiFi/radio: the web handlers are called directly; the page itself was checked separately in a browser.
* Sensor noise is a simple model, not measured data (the real noise figures are in the hardware test notes).
* The ESP32 compile check does not link or run anything: it proves the sketch compiles against the real core, not that it behaves on the chip.
