# Test report - smart_parking_v9.ino

Run: 2026-10-24 18:03  Target: the host simulator

**54 passed, 0 failed, 0 errors** of 54

| ID | Group | Result | Title |
|---|---|---|---|
| BOOT-1 | Boot | PASS | Boot banner: 14 spots, A* distances (nearest/farthest) match the .ino constants, 60 s timeout |
| BOOT-2 | Boot | PASS | Clean state after reset: 14 free, next car -> spot 0 |
| SENS-1 | Sensors | PASS | Idle 20 s: no phantom entry/park/leave events (sensor noise) |
| SENS-2 | Sensors | PASS | check command: ~6 readings, valid echoes, idle values beyond thresholds |
| SENS-3 | Sensors | PASS | check [sec]: window length honoured (1, 5, 60 accepted; default 3), stops on time |
| DIST-1 | A* | PASS | Fill the lot: all 14 spot distances match an independent hand formula built from the .ino constants |
| FULL-1 | A* | PASS | Lot full: 15th car turned away, no ID burned, spot reusable after leave |
| A-1 | Assignment | PASS | Regression: first car gets ID 0 (no phantom cars from boot noise) |
| A-2 | Assignment | PASS | Regression: consecutive new cars never share a spot |
| A-3 | Assignment | PASS | Park at assigned spot -> CORRECT, status OCCUPIED/correct, next advances |
| A-4 | Assignment | PASS | Wrong spot: own reservation released immediately and reused by next car |
| A-5 | Assignment | PASS | Taking another car's reserved spot: that car is rerouted and can park CORRECT |
| A-6 | Assignment | PASS | Reroute chain keeps one assignment per car |
| A-7 | Assignment | PASS | Full lot + wrong park: displaced car gets the freed spot (no "no free spot left") |
| A-8 | Assignment | PASS | Same car cannot park twice (regression: double parking) |
| A-9 | Assignment | PASS | Cannot park on an occupied spot; other car keeps its reservation |
| A-10 | Assignment | PASS | Leave: parked / reserved-only / twice / alias cancel / ids never reused |
| A-11 | Assignment | PASS | Leaving a WRONG-parked car frees the spot it really used, not its old one |
| A-12 | Assignment | PASS | Park on a free, unreserved spot by a car with its own reservation |
| A-13 | Assignment | PASS | Input validation: bad spot / id / usage, state untouched |
| P-1 | Parser | PASS | Empty and whitespace-only lines are harmless; next command still works |
| P-2 | Parser | PASS | Unknown command names itself in the error and prints help |
| P-3 | Parser | PASS | Commands are case-insensitive (NEW, Park, LEAVE, Status, CHECK, Help, Cancel) |
| P-4 | Parser | PASS | Number parsing: trailing junk, decimals, hex, signs, extra spaces/tabs |
| P-5 | Parser | PASS | Overflow: huge numbers are rejected, never wrap into a valid id/spot |
| P-6 | Parser | PASS | Regression: token longer than 15 chars is rejected, not split into two arguments |
| P-10 | Parser | PASS | Command word longer than 15 chars: reported as too long, no crash |
| P-7 | Parser | PASS | Very long line (200 chars) and 47/48-char boundary lines do not crash |
| P-8 | Parser | PASS | Line endings: LF and CRLF work; CR-only does not execute |
| P-9 | Parser | PASS | Wrong argument count is rejected with a usage line and changes nothing |
| S-1 | Stress | PASS | Burst of 10 commands in one write: all answered |
| S-2 | Stress | PASS | Burst of 30 status requests: none lost or truncated (RX buffer) |
| S-3 | Stress | PASS | Command latency: response to "new" within 800 ms |
| FUZZ-1 | Model | PASS | Differential fuzz (110 random ops, seed 20260), board vs reference model of the spec |
| TO-1 | Timeout | PASS | Reservation expires after 60 s (+-4); parked car never times out; staggered cars expire in order |
| TO-2 | Timeout | PASS | Full lot, stranded car (timed out) parks on a reserved spot: displaced car must leave cleanly |
| SENS-4 | Sensors | PASS | Idle 5 min: sensor glitch rate (<2 cm / no echo) and phantom [SENSOR] events |
| SOAK-1 | Soak | PASS | ID cap: 500 new+leave cycles; behaviour at id 500 and afterwards |
| SIM-1 | Sensors | PASS | Entrance sensor (car ~22 cm, wall ~30 cm): one car per visit, a car that stays counts once, noise does not re-arm it, the 25 cm threshold is exact |
| SIM-9 | Sensors | PASS | Optional two-lane entrance sensor (exit ~22 cm / entry ~26 cm): exit removes the car at spot 0, then the lowest parked id; empty lot only warns; band edge 24.0 cm |
| SIM-10 | Sensors | PASS | Optional exit lane: a car leaves spot 0 and then drives through the exit lane - counted once (no second car removed) |
| SIM-2 | Sensors | PASS | Spot 0 sensor: reserved car parks CORRECT after ~2 s of confirmation, LED steady; leaving frees the spot |
| SIM-3 | Sensors | PASS | Wrong car at spot 0: newest unparked car parks WRONG, its own reservation is released, LED blinks |
| SIM-4 | Sensors | PASS | Spot 0 BLOCKED: object with no car on its way is never assigned; clears when the object leaves |
| SIM-5 | Sensors | PASS | Hysteresis (measured: empty 25-26 cm; car 2.5 cm high 22.5-23.5 cm, 1.5 cm high 23.5-24.5 cm): arrive below 24.7, leave above 24.9; the dead zone between changes nothing |
| SIM-6 | Sensors | PASS | Noise immunity: bursts of <2 cm / no-echo readings make no phantom events, and a real car still parks once and never "leaves" |
| SIM-7 | Sensors | PASS | Sensor event while a car is only reserved elsewhere is handled once (no double park from the physical spot) |
| SIM-8 | Sensors | PASS | Single false readings (a believable 15 cm on an empty spot / 40 cm on a parked one) are not events: 4 in a row are needed |
| VIS-1 | Visual | PASS | Boot payload (/state): shape, map, 14 free spots, next = spot 0, nothing waiting |
| VIS-2 | Visual | PASS | Payload and text picture agree: states, car ids, OK/WRONG, waiting list, next spot |
| VIS-3 | Visual | PASS | The status counter (seq): +1 per valid status, unchanged by other commands and by a rejected "status x" |
| VIS-4 | Visual | PASS | Waiting list follows the car: appears on new, disappears on park / leave / cancel, keeps arrival order |
| VIS-5 | Visual | PASS | Shipped build (WiFi page up): status prints only [STATUS] + [VISUAL] + [NEXT]; no picture on events; WiFi-off build falls back to the text picture |
| VIS-6 | Visual | PASS | Page file sanity: served page is complete, fetches /state, one script block, balanced tags, no external resources |

## SENS-2 - PASS - check command: ~6 readings, valid echoes, idle values beyond thresholds
- note: spot sensor idle range: 25.8 .. 25.8 cm (car arrives below 24.7)
- note: entrance sensor idle range: 30.0 .. 30.0 cm (a car is counted below 25)

## P-8 - PASS - Line endings: LF and CRLF work; CR-only does not execute
- note: CR-only terminator is ignored (Serial Monitor must be set to "Newline" or "Both NL & CR")

## S-3 - PASS - Command latency: response to "new" within 800 ms
- note: latency ms: 31, 15, 15, 15, 15, 31

## TO-1 - PASS - Reservation expires after 60 s (+-4); parked car never times out; staggered cars expire in order
- note: A timed out at t=60.3s (created t~0)
- note: late arrival at its ORIGINAL spot counts as WRONG (open design question)

## SENS-4 - PASS - Idle 5 min: sensor glitch rate (<2 cm / no echo) and phantom [SENSOR] events
- note: 599 readings: 0 tiny (<2 cm, impossible for HC-SR04) = 0.0%, 0 no-echo = 0.0%, longest run of tiny readings = 0
- note: valid spot readings: 25.8 .. 25.8 cm

## SOAK-1 - PASS - ID cap: 500 new+leave cycles; behaviour at id 500 and afterwards
- note: After 500 cars the firmware cannot admit any more cars until reboot (carExited[] is never recycled).
