// Builds Smart_Parking_Report.docx from report_data.json and the simulation figures.
// Every number in the text comes from report_data.json (made by export_data.py),
// so after a new simulation run: python export_data.py && node build_report.js
const fs = require("fs");
const path = require("path");
const {
  Document, Packer, Paragraph, TextRun, HeadingLevel, AlignmentType, Table, TableRow, TableCell,
  WidthType, ShadingType, BorderStyle, ImageRun, Footer, PageNumber, LevelFormat, PageBreak,
} = require("docx");

const HERE = __dirname;
const D = JSON.parse(fs.readFileSync(path.join(HERE, "report_data.json"), "utf8"));
const FIGURES = path.join(HERE, "..", "simulation", "figures");
const OUT = path.join(HERE, "Smart_Parking_Report.docx");

const CONTENT_WIDTH = 9026;   // A4 width 11906 minus two 1-inch margins (DXA)
const HEADER_FILL = "E7ECF3";
const GROUP_FILL = "F4F4F2";
const BORDER = { style: BorderStyle.SINGLE, size: 4, color: "BFBFBF" };

// ------------------------------------------------------------------ numbers

const fmt = (x, dp = 0) => Number(x).toLocaleString("en-US", { minimumFractionDigits: dp, maximumFractionDigits: dp });
const about = (x, step) => fmt(Math.round(x / step) * step);   // rounded to the nearest step, e.g. 100
const trim = (x) => String(Number(Number(x).toFixed(2)));
const P = Object.fromEntries(D.params.map((p) => [p.key, p]));
const val = (k) => P[k].value;

const head = (lot, vs) => D.headline.find((r) => r.lot.startsWith(lot) && r.compared_with.startsWith(vs));
const G1 = head("Garage", "Searching alone, p = 1");
const G05 = head("Garage", "Searching alone, p = 0.5");
const GR = head("Garage", "Random");
const M1 = head("Demo", "Searching alone, p = 1");
const M05 = head("Demo", "Searching alone, p = 0.5");
const MR = head("Demo", "Random");
const yearlyIls = (r) => r.saved_ils_fuel_per_year + r.saved_ils_time_per_year;

const valid = (p, occ) => D.validation.find((r) => r.p === p && r.target_occupancy === occ);
const sweep = (lot, p) => D.sweep.filter((r) => r.lot === lot && r.p === p).sort((a, b) => a.target_occupancy - b.target_occupancy);
const sens = Object.fromEntries(D.sensitivity.map((r) => [r.parameter, r]));
const BASE_ILS = D.sensitivity[0].base;
const OCC = val("target_occupancy");
const SPEED = D.derived.search_speed_mps;
const garageStalls = D.lots.garage.stalls;

// how each published figure (params.BENCHMARKS) is shown in Table 5
const BENCH_VALUE = {
  levy_noticeable_occupancy: (v) => `${fmt(v * 100)}%`,
  levy_search_above_92_s: (v) => `> ${fmt(v)} s`,
  levy_search_at_95_s: (v) => `> ${fmt(v)} s`,
  offstreet_search_s: (v) => `${fmt(v)} s`,
  gps_search_min: (v) => `${trim(v)} min`,
  excess_distance_ratio_peak: (v) => `${trim(v)}× optimal`,
  reservation_time_halved: (v) => `≈ ${fmt(v * 100)}% less`,
  zoning_search_reduction: (v) => `${trim(v * 100)}% less`,
  smart_lot_saving_s: (v) => `${fmt(v)} s`,
  shoup_cruise_min: (v) => `${trim(v)} min`,
};

// ------------------------------------------------------------------ building blocks

// "**bold**" inside a string becomes a bold run; "[[...]]" becomes a highlighted placeholder
function runs(text, base = {}) {
  return text.split(/(\*\*[^*]+\*\*|\[\[[^\]]+\]\])/).filter(Boolean).map((part) => {
    if (part.startsWith("**")) return new TextRun({ ...base, text: part.slice(2, -2), bold: true });
    if (part.startsWith("[[")) return new TextRun({ ...base, text: `[${part.slice(2, -2)}]`, highlight: "yellow" });
    return new TextRun({ ...base, text: part });
  });
}
const para = (text, opts = {}) => new Paragraph({ spacing: { after: 120, line: 276 }, ...opts, children: runs(text) });
const h1 = (text) => new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun(text)] });
const h2 = (text) => new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun(text)] });
const bullet = (text) => new Paragraph({ numbering: { reference: "bullets", level: 0 }, spacing: { after: 60 }, children: runs(text) });
const numbered = (text) => new Paragraph({ numbering: { reference: "numbers", level: 0 }, spacing: { after: 60 }, children: runs(text) });
const caption = (text) => new Paragraph({ spacing: { before: 60, after: 240 }, children: runs(text, { size: 18, italics: true, color: "404040" }) });

function cell(content, width, { header = false, fill, span = 1, align = AlignmentType.LEFT, size = 18 } = {}) {
  const lines = Array.isArray(content) ? content : [content];
  return new TableCell({
    width: { size: width, type: WidthType.DXA },
    columnSpan: span,
    shading: header || fill ? { fill: header ? HEADER_FILL : fill, type: ShadingType.CLEAR, color: "auto" } : undefined,
    margins: { top: 50, bottom: 50, left: 90, right: 90 },
    children: lines.map((line, i) => new Paragraph({
      alignment: align,
      children: runs(String(line), { size: i > 0 && !header ? size - 2 : size, bold: header, color: i > 0 && !header ? "595959" : undefined }),
    })),
  });
}

// rows: array of arrays (cells may be arrays of lines) or {group: "title"} for a shaded group row.
// Returns the table plus an empty paragraph, so the text below does not touch it.
const table = (...args) => [makeTable(...args), new Paragraph({ spacing: { after: 60 }, children: [] })];
function makeTable(widths, header, rows, { align = [] } = {}) {
  const total = widths.reduce((a, b) => a + b, 0);
  const body = rows.map((r) => r.group
    ? new TableRow({ children: [cell(`**${r.group}**`, total, { fill: GROUP_FILL, span: widths.length })] })
    : new TableRow({ children: r.map((c, i) => cell(c, widths[i], { align: align[i] })) }));
  return new Table({
    width: { size: total, type: WidthType.DXA },
    columnWidths: widths,
    borders: { top: BORDER, bottom: BORDER, left: BORDER, right: BORDER, insideHorizontal: BORDER, insideVertical: BORDER },
    rows: [new TableRow({ tableHeader: true, children: header.map((h, i) => cell(h, widths[i], { header: true, align: align[i] })) }), ...body],
  });
}
const tableTitle = (text) => new Paragraph({ spacing: { before: 200, after: 80 }, keepNext: true, children: runs(text, { size: 18, bold: true }) });

function pngSize(file) {
  const b = fs.readFileSync(file);
  return { w: b.readUInt32BE(16), h: b.readUInt32BE(20) };
}
function figure(name, alt) {
  const file = path.join(FIGURES, name);
  const { w, h } = pngSize(file);
  const width = 600;
  return new Paragraph({
    alignment: AlignmentType.CENTER, keepNext: true, spacing: { before: 120 },
    children: [new ImageRun({ type: "png", data: fs.readFileSync(file), transformation: { width, height: Math.round(width * h / w) },
      altText: { title: alt, description: alt, name } })],
  });
}

// ------------------------------------------------------------------ parameter table (point 5)

const UNIT = { "-": "", "ILS/L": " ₪/L", "ILS/h": " ₪/h" };
const unit = (u) => (u in UNIT ? UNIT[u] : ` ${u}`);
const isPct = (k) => ["ramp_max_grade", "target_occupancy"].includes(k);
function paramValue(p) {
  const show = (x) => (isPct(p.key) ? `${trim(x * 100)} %` : `${trim(x)}${unit(p.unit)}`);
  const lines = [show(p.value)];
  if (p.sweep.length) lines.push(`range ${show(p.sweep[0])} – ${show(p.sweep[1])}`);
  return lines;
}
function paramSource(p) {
  if (p.source === "Assumption" || p.source === "Derived") return [`**${p.source}**`];
  return [p.source + (p.where ? `, ${p.where}` : ""), `checked: ${p.verified}`];
}
const paramRows = [];
for (const group of [...new Set(D.params.map((p) => p.group))]) {
  paramRows.push({ group });
  for (const p of D.params.filter((q) => q.group === group)) paramRows.push([p.label, paramValue(p), paramSource(p), p.note]);
}

// ------------------------------------------------------------------ document text

const t3 = "Table 3";
const g85 = valid(1, OCC);
const reductions = D.validation.filter((r) => r.p === 1).map((r) => r.entry_driving_reduction * 100);
const redLo = Math.min(...reductions), redHi = Math.max(...reductions);
const lowOcc = valid(1, 0.5), highOcc = valid(1, 0.98), highOcc05 = valid(0.5, 0.98);

const children = [
  // ---------- title page
  new Paragraph({ spacing: { before: 2400, after: 240 }, children: [new TextRun({ text: "Smart Parking System", bold: true, size: 56, color: "1F3864" })] }),
  new Paragraph({ spacing: { after: 600 }, children: [new TextRun({ text: "Assigning the nearest free stall with A*: an ESP32 prototype and a simulation study of the time, fuel and emissions it saves", size: 28, color: "404040" })] }),
  para("Technion – Israel Institute of Technology"),
  para("Authors: Aseel Kurdy, Nayef Habiballah, Osama Abdulhalim"),
  para("Mentor: Ido"),
  para("October 2026"),
  new Paragraph({ children: [new PageBreak()] }),

  // ---------- abstract
  h1("Abstract"),
  para(`Drivers who look for a parking space on their own drive further, burn more fuel and lose time. This project built a smart parking system that assigns every arriving car the nearest free stall, computed with the A* algorithm over a map of the lot, tells the driver where to go, and detects whether the car parked in the stall it was given. A two-floor, 14-stall prototype runs on an ESP32 with two ultrasonic sensors, one at the entrance and one above a physical stall. The same assignment rule was then evaluated in a discrete-event simulation of a ${garageStalls}-stall, three-floor garage, with Poisson arrivals, gamma-distributed parking durations, and constants taken from published sources wherever one exists.`),
  para(`At ${fmt(OCC * 100)}% occupancy, assigning the stall saves **${fmt(G1.saved_m_per_car)} m of driving per car**: ${fmt(G1.saved_s_per_car)} s, ${fmt(G1.saved_ml_fuel_per_car, 1)} mL of fuel and ${fmt(G1.saved_g_co2_per_car)} g of CO₂, compared with a driver who searches aisle by aisle and notices every free stall. Over a year of ${fmt(val("operating_hours_per_day"))}-hour days this adds up to about **${about(G1.saved_hours_per_year, 100)} hours, ${about(G1.saved_litres_per_year, 100)} litres of fuel, ${fmt(G1.saved_kg_co2_per_year / 1000, 1)} tonnes of CO₂ and ₪${about(yearlyIls(G1), 1000)}** in fuel and travel time. The saving grows with occupancy. The model was not tuned to any published figure, and its predicted search time and driving reduction fall within the ranges reported in the literature.`),

  // ---------- 1 introduction
  h1("1. Introduction"),
  h2("1.1 Why searching for parking matters"),
  para("Shoup (2006) reviewed 16 studies made between 1927 and 2001 and found that drivers cruised for parking for 8.1 minutes on average, and that cruising made up 30% of traffic in the congested downtown streets studied. Those figures describe on-street parking in busy city centres. Inside off-street facilities the search is shorter but still real: in a Dutch survey of 11,425 trips, drivers reported an average search of 50 s for off-street parking against 30 s on-street (van Ommeren, Wentink & Rietveld, 2012), and a 2024 GPS study measured a mean of 1.5 minutes of cruising (Saki & Hagen, 2024)."),
  para("Systems that give drivers information or a reserved space reduce this search. In simulation studies, zone-level availability information cut search time in a parking facility by 16.2% (Caicedo, 2009), assigning and reserving spaces roughly halved the time to park (Geng & Cassandras, 2011), and a smart parking system saved 11 s of search per car in a university lot (Surpris, Liu & Vincenzi, 2014). Search effort also depends strongly on occupancy: in an agent-based model it became noticeable above about 85% occupancy and exceeded one minute above 92–93% (Levy, Martens & Benenson, 2013)."),
  h2("1.2 Goals"),
  numbered("Build a prototype that detects a car entering the lot (side-facing sensor) and a car parked in a stall (top-down sensor), assigns each arriving car a stall, tells the driver where to go, and detects whether it parked in the right stall."),
  numbered("Use the same assignment algorithm in a simulation, with real published data, to estimate how much driving, time, fuel, CO₂ and money it saves compared with drivers who search on their own."),

  // ---------- 2 prototype
  h1("2. The prototype system"),
  h2("2.1 Hardware"),
  para("The prototype uses an ESP32 development board and two HC-SR04 ultrasonic sensors powered from the board's 5 V pin. One physical stall (spot 0) has a sensor and the board's built-in blue LED; the other 13 stalls are virtual and are controlled from the Serial Monitor, which lets the system be demonstrated on a full lot with only one sensor per stall."),
  tableTitle("Table 1. Hardware connections"),
  ...table([3200, 2300, 1763, 1763], ["Function", "Device", "Trig", "Echo"], [
    ["Entrance detection (side-facing)", "HC-SR04", "GPIO 27", "GPIO 14"],
    ["Spot 0 detection (top-down)", "HC-SR04", "GPIO 26", "GPIO 25"],
    ["Correct / wrong parking indicator", "Built-in blue LED", "–", "GPIO 2"],
  ]),
  h2("2.2 Lot model and assignment"),
  para("The lot is a 1:100 physical model with two floors of seven stalls each, linked by a ramp, and is stored as a small road network whose segments carry the lengths measured on the model. When a car arrives, the firmware chooses the nearest available stall: not occupied, not reserved for another car and, for spot 0, not blocked by an unknown object. Distance is the driving distance from the entrance found with A* over the network. The heuristic is the stall length (every route ends by driving into the stall) plus, on the goal's own lane, the distance along the lane to the stall; it never overestimates the true distance, so A* returns the shortest path. Driving through a stall is not allowed; a stall can only be the destination. Because the model is fixed, the distance to each stall is computed once at start-up. On the model the ramp starts 17.5 cm from the entrance and is 20 cm long, the first floor-0 stall is 22 cm from the entrance and the first floor-1 stall 7.5 cm past the ramp, and stalls are 2.5 cm wide and 5 cm long (the cars are 4 × 2 × 2.5 cm), so the stalls lie 27.0–42.0 cm (floor 0) and 50.0–65.0 cm (floor 1) from the entrance, which is 27–65 m at full scale; the simulation uses the sourced dimensions of Section 4."),
  h2("2.3 Behaviour"),
  tableTitle("Table 2. How the system responds to each event"),
  ...table([3300, 5726], ["Event", "System response"], [
    ["A car reaches the entrance (sensor or new command)", "The car gets an ID, the nearest available stall is reserved for it, and the driver is told the stall and its distance. The recommendation for the next car is printed."],
    ["The car parks in its own stall", "Reported CORRECT; the LED of spot 0 stays on."],
    ["The car parks in a different free stall", "Reported WRONG SPOT; the LED blinks. The car's own reservation is released immediately."],
    ["The car parks in a stall reserved for another car", "Accepted, because it cannot be prevented physically. The other car is sent to the nearest free stall, or told to leave if the lot is full."],
    ["The car does not arrive within 60 s", "The reservation is released (timeout). The car can still park later and is then counted as WRONG."],
    ["A car leaves (spot 0 sensor or leave command)", "The stall is freed and the car exits the lot."],
    ["The spot 0 sensor sees an object and no car is on its way", "Spot 0 is BLOCKED and is not assigned until it clears."],
  ]),
  h2("2.4 Sensor processing"),
  para("With nothing in the stall, the spot sensor reads 25–26 cm; with the model car it reads 22.5–23.5 cm, about 2.5 cm less. A car is considered to arrive below 24.0 cm and to leave above 24.5 cm; the gap between the two thresholds prevents a reading near one value from flipping the state back and forth, and leaves about 0.5 cm of margin on each side of the measured ranges. A change of state needs four consecutive valid readings, taken every 0.5 s (2 s in total). A reading with no echo, or below the sensor's 2 cm minimum range, is invalid and restarts the count. The two sensors are fired 60 ms apart, the minimum measurement cycle of the HC-SR04, so the echo of one cannot be received by the other. The entrance sensor reads about 30 cm (the wall opposite it) when nothing passes and about 22 cm with a car; a car is counted below 26 cm, after two consecutive readings, and only once while it stays in front of the sensor."),
  h2("2.5 Prototype testing"),
  para("An automated suite of 38 tests, sending commands over the serial port and reading the sensors, was run on the board. 34 passed. The 4 failures had one cause: electrical glitches on the spot sensor's echo line. In 600 readings taken over 5 idle minutes, 4.5% were below the sensor's 2 cm minimum (about 0.1 cm, which corresponds to a pulse far shorter than any real echo) and 2.3% had no echo. These glitches arrived in bursts and could create a phantom car at spot 0. The firmware now treats such readings as invalid and restarts the confirmation count after each one (Section 2.4). After this fix the whole suite passed on the board, 38 of 38. The firmware was then adapted to the measured physical model (Sections 2.2 and 2.4). That version was verified without the board: the unmodified firmware was compiled on a PC with simulated sensors, serial port, LED and web server, and the same 38 tests plus 16 new sensor and web tests ran against it (54 of 54 passed). A model-based test compared the firmware's state with an independent model of the rules after each of 161,040 random operations and found no difference, and the firmware compiles with the ESP32 toolchain. The adapted version has not yet been run on the board. Keeping the spot sensor's Trig and Echo wires apart, and adding a 10 µF capacitor across its supply pins, should remove the glitches at their source."),

  // ---------- 3 simulation method
  h1("3. Simulation method"),
  h2("3.1 Overview"),
  para("The simulation, written in Python, follows individual cars through a parking lot. Arrivals and parking durations are generated once and every strategy is applied to exactly the same cars (common random numbers), so the difference between strategies is caused by the strategy and not by chance. The code, the tests and every constant are in the simulation folder, and one command (python run_all.py) reproduces all results and figures."),
  h2("3.2 Lots"),
  para(`**Demo lot.** The 14-stall, two-floor map of the prototype before the physical model was measured: one lane per floor with stalls on one side, joined by a ramp. With the sourced dimensions the stalls lie ${fmt(D.lots.demo.nearest_m, 1)}–${fmt(D.lots.demo.farthest_m, 1)} m from the entrance.`),
  para(`**Garage (main scenario).** A ${garageStalls}-stall, three-floor garage, the usual multi-storey layout: on each floor, three parallel two-way aisles with 90° stalls on both sides (12 per side, so each aisle is 30 m long), joined at both ends by cross aisles. Ramps link the floors at alternating corners and the entrance is at the front of the first aisle on the ground floor. Stalls lie ${fmt(D.lots.garage.nearest_m, 1)}–${fmt(D.lots.garage.farthest_m, 1)} m from the entrance by the shortest path.`),
  para("Both lots are grids. A step along an aisle costs one stall width (2.50 m), a step across costs half an aisle plus half a stall (5.40 m), and a ramp costs 23.0 m (Table 4). A stall can only be entered from the side, from the aisle next to it, as in 90° parking. Distances are found with the same A* algorithm as in the firmware; a test confirms that, with the prototype's constants, it reproduces the distances the board prints."),
  h2("3.3 Demand"),
  para(`Cars arrive as a Poisson process: in a mall garage, counts of arrivals per 5 minutes passed a Kolmogorov–Smirnov test for the Poisson distribution with p = 0.29–0.65 (Huang et al., 2020). Parking durations follow a gamma distribution with mean ${fmt(val("mean_stay_min"))} min and shape ${trim(val("stay_gamma_shape"))}, from the same study. A car that finds every stall taken leaves. This is the Erlang loss model (M/G/c/c), which has been applied to parking lots before (Lu et al., 2010): with offered load a = λ·E[S] and c stalls, the share of cars turned away is the Erlang-B probability B(c, a), and the average occupancy is a·(1 – B(c, a))/c. For each target occupancy the arrival rate λ is found from this formula.`),
  h2("3.4 Strategies compared"),
  bullet("**Assigned stall (this system).** The car drives by the shortest path to the nearest free stall, exactly the firmware's rule. When the lot is full the driver is told at the entrance and drives nothing."),
  bullet("**Driver searching alone.** The driver searches aisle by aisle, as people do: up the first aisle, along the cross aisle, down the next, then up the ramp and the same on the next floor. Each free stall the driver passes is noticed with probability p, and the driver takes the first one noticed. A driver who reaches the end without parking drives back and takes the first free stall passed. In a full lot the driver finds out only after that round trip. The base case is p = 1: the driver sees every free stall, the most favourable case for the driver, so the system's savings are a lower bound. p = 0.5 is the sensitivity case."),
  bullet("**Random search (no information).** The driver visits stalls in random order, by the shortest path from where they are, until one is free. This was the prototype's original baseline and is reported only as an upper bound."),
  h2("3.5 Measures"),
  para(`For every car the simulation records the metres driven inside the lot: from the entrance to the stall, plus the shortest way back out. Metres are converted to time with the driving speed (${trim(val("search_speed_kmh"))} km/h = ${fmt(SPEED, 2)} m/s), to fuel with the consumption at crawl speed (${trim(val("fuel_rate_l_per_h"))} L/h), to CO₂ with ${trim(val("co2_kg_per_l"))} kg per litre, and to money with the fuel price (₪${trim(val("fuel_price_ils_per_l"))}/L) and the value of travel time (₪${trim(val("value_of_time_ils_per_h"))}/h). Fuel is modelled per hour rather than per kilometre because at crawl speed the engine's consumption is close to idle and depends on time, not distance. The value is not optimistic: ${trim(val("fuel_rate_l_per_h"))} L/h at ${trim(val("search_speed_kmh"))} km/h equals ${trim(100 * val("fuel_rate_l_per_h") / val("search_speed_kmh"))} L per 100 km, close to the 10.3 L per 100 km (9.7 km per litre) that Israel's gasoline cars averaged on the road in 2022 (Central Bureau of Statistics, 2023, Table C).`),
  h2("3.6 Experiments"),
  para("Both lots were run at eight occupancy levels (50, 60, 70, 80, 85, 90, 95 and 98%) for p = 1 and p = 0.5. Each setting has 20 replications with different random seeds. In each replication the lot first fills up to steady state (warm-up of at least four mean parking durations, not measured), and then 3,000 cars are measured. Results are the mean of the 20 replications with a 95% confidence interval (Student t, 19 degrees of freedom). The yearly figures assume the garage is open the hours of Table 3, every day of the year."),
  h2("3.7 Verification"),
  para("Eleven automated tests check the simulation:"),
  bullet("The Python A* reproduces the distances printed by the prototype's earlier firmware (v7: 5, 7, …, 17, 27, …, 39 m) and the expected sourced distances."),
  bullet("A* agrees with an exhaustive shortest-path search (Dijkstra) for every stall of both lots."),
  bullet("The search route can be driven cell by cell and passes every stall."),
  bullet("The share of cars turned away matches the Erlang-B formula, and the chosen arrival rate produces the target occupancy (both within 0.02)."),
  bullet("In the single-lane demo lot, a driver who notices every free stall drives exactly as far as the assigned driver; in the garage, the search route is never shorter than the shortest path."),
  bullet("The same random seed always gives the same result."),
  h2("3.8 Modelling assumptions"),
  bullet("A search lasts seconds to a minute while a stay lasts about 1.5 hours, so the lot is taken as unchanged during one car's search."),
  bullet("The exit is at the entrance; leaving costs the shortest path back, whichever strategy chose the stall."),
  bullet("A car is admitted whenever a stall is free, so every strategy always has the same number of cars inside."),
  bullet("Arrivals have a constant average rate (no daily peaks), and searching drivers do not compete for the same stall."),
  bullet("Assigned drivers go to the stall they were given. The prototype handles drivers who do not (Table 2); the simulation does not."),

  // ---------- 4 parameters
  h1("4. Parameters, sources and assumptions"),
  para(`Table 3 lists every constant the simulation uses: its value, the source and where in it the value appears, how the source was checked, and why the value was chosen. Each source was opened and the number confirmed; the column “checked” says whether this was done in the full text, only in the abstract, or on an official web page. Values without a reliable source are marked Assumption and are given a range that is tested in the sensitivity analysis (Section 5.5).`),
  tableTitle("Table 3. Simulation constants"),
  ...table([2050, 1500, 2500, 2976], ["Parameter", "Value", "Source", "Basis"], paramRows),
  tableTitle("Table 4. Values derived from Table 3"),
  ...table([2600, 1300, 5126], ["Quantity", "Value", "Formula"], [
    ["Step along an aisle", `${fmt(D.derived.lane_step_m, 2)} m`, "= stall width"],
    ["Step from aisle into a stall", `${fmt(D.derived.lane_to_stall_m, 2)} m`, "= aisle width / 2 + stall length / 2 = 5.80/2 + 5.00/2"],
    ["Ramp between floors", `${fmt(D.derived.ramp_length_m, 1)} m`, "= 2 × 3 m transitions (at half gradient) + remaining rise / 15% = 6 + (3.0 – 0.45) / 0.15"],
    ["Driving speed", `${fmt(SPEED, 2)} m/s`, "= 10 km/h / 3.6"],
  ]),
  para("Table 5 lists published figures used only to check the results. None of them is an input, and the model was not tuned to match them."),
  tableTitle("Table 5. Published figures used to check the results"),
  ...table([1400, 4100, 3526], ["Value", "Finding", "Source"],
    D.benchmarks.map((b) => [BENCH_VALUE[b.key](b.value), b.note, `${b.source}, ${b.where}`])),

  // ---------- 5 results
  h1("5. Results"),
  h2("5.1 Distance driven per visit"),
  para(`Figure 1 shows the metres driven inside each lot per visit. In the garage, the assigned driver always drives less than the driver searching alone, and the gap widens as the garage fills. At ${fmt(OCC * 100)}% occupancy the assigned driver drives ${fmt(G1.assigned_entry_m + G1.assigned_exit_m)} m per visit and the searching driver ${fmt(G1.baseline_entry_m + G1.baseline_exit_m)} m. Missing half of the free stalls (p = 0.5) changes little until the garage is nearly full, when free stalls are scarce and missing one is costly.`),
  figure("fig1_driving_vs_occupancy.png", "Metres driven per visit versus occupancy for both lots"),
  caption("Figure 1. Metres driven inside the lot per visit (entry plus exit) against occupancy. Shaded bands are 95% confidence intervals (20 replications of 3,000 cars). In the single-lane demo lot, a driver who notices every free stall (p = 1) drives exactly as far as the assigned driver, so the two lines coincide."),
  h2("5.2 Savings per car and per year"),
  para(`Table 6 converts the garage result at ${fmt(OCC * 100)}% occupancy into time, fuel, CO₂ and money. Per car the amounts are small: ${fmt(G1.saved_s_per_car)} seconds, ${fmt(G1.saved_ml_fuel_per_car, 1)} mL of fuel and less than one shekel. They matter because a garage of this size serves about ${about(G1.cars_per_year, 1000)} cars a year. Most of the money value is the drivers' time; fuel is ${fmt(100 * G1.saved_ils_fuel_per_year / yearlyIls(G1))}% of the total.`),
  tableTitle(`Table 6. Garage (${garageStalls} stalls) at ${fmt(OCC * 100)}% occupancy: assigned stall vs. a driver searching alone (p = 1)`),
  ...table([3800, 2600, 2626], ["Measure", "Per car", "Per year"], [
    ["Driving saved", `${fmt(G1.saved_m_per_car, 1)} ± ${fmt(G1.saved_m_ci95, 1)} m`, `${fmt(G1.saved_m_per_car * G1.cars_per_year / 1000)} km`],
    ["Time saved", `${fmt(G1.saved_s_per_car, 1)} s`, `${fmt(G1.saved_hours_per_year)} h`],
    ["Fuel saved", `${fmt(G1.saved_ml_fuel_per_car, 1)} mL`, `${fmt(G1.saved_litres_per_year)} L`],
    ["CO₂ avoided", `${fmt(G1.saved_g_co2_per_car, 1)} g`, `${fmt(G1.saved_kg_co2_per_year / 1000, 1)} t`],
    ["Fuel cost saved", `₪${fmt(G1.saved_ils_fuel_per_car, 3)}`, `₪${fmt(G1.saved_ils_fuel_per_year)}`],
    ["Value of time saved", `₪${fmt(G1.saved_ils_time_per_car, 3)}`, `₪${fmt(G1.saved_ils_time_per_year)}`],
    ["**Total money value**", `**₪${fmt(G1.saved_ils_fuel_per_car + G1.saved_ils_time_per_car, 2)}**`, `**₪${fmt(yearlyIls(G1))}**`],
  ], { align: [AlignmentType.LEFT, AlignmentType.RIGHT, AlignmentType.RIGHT] }),
  para(`The saving grows steadily with occupancy (Figure 2), from ${fmt(lowOcc.extra_search_s)} s per car at 50% to ${fmt(highOcc.extra_search_s)} s at 98% for p = 1, and to ${fmt(highOcc05.extra_search_s)} s at 98% when the driver misses half of the free stalls. Table 7 gives the underlying numbers.`),
  figure("fig2_savings_vs_occupancy.png", "Time, fuel, CO2 and fuel cost saved per car versus occupancy in the garage"),
  caption("Figure 2. Savings per car in the garage compared with a driver searching alone (p = 1), converted with the constants of Table 3. Shaded bands are 95% confidence intervals."),
  tableTitle("Table 7. Garage results by occupancy (p = 1)"),
  ...table([1500, 1900, 1900, 1900, 1826], ["Occupancy", "Assigned (m/visit)", "Searching (m/visit)", "Saved (m/car)", "Less driving to the stall"],
    sweep("garage", 1).map((r) => [`${fmt(r.occupancy * 100, 1)}%`, fmt(r.assigned_total_m, 1), fmt(r.cruising_total_m, 1),
      `${fmt(r.saved_vs_cruising_m, 1)} ± ${fmt(r.saved_vs_cruising_m_ci95, 1)}`, `${fmt(r.entry_reduction_vs_cruising * 100)}%`]),
    { align: [AlignmentType.LEFT, AlignmentType.RIGHT, AlignmentType.RIGHT, AlignmentType.RIGHT, AlignmentType.RIGHT] }),
  h2("5.3 Other baselines and the demo lot"),
  tableTitle(`Table 8. Saving per car at ${fmt(OCC * 100)}% occupancy, by baseline`),
  ...table([2600, 3000, 1700, 1726], ["Lot", "Compared with", "Saved (m/car)", "Saved (s/car)"], [
    ...[[G1, "Searching alone, p = 1"], [G05, "Searching alone, p = 0.5"], [GR, "Random search (upper bound)"]].map(([r, l]) => [`Garage (${garageStalls} stalls)`, l, fmt(r.saved_m_per_car, 1), fmt(r.saved_s_per_car, 1)]),
    ...[[M1, "Searching alone, p = 1"], [M05, "Searching alone, p = 0.5"], [MR, "Random search (upper bound)"]].map(([r, l]) => ["Demo lot (14 stalls)", l, fmt(r.saved_m_per_car, 1), fmt(r.saved_s_per_car, 1)]),
  ], { align: [AlignmentType.LEFT, AlignmentType.LEFT, AlignmentType.RIGHT, AlignmentType.RIGHT] }),
  para(`In the demo lot, a driver who notices every free stall saves nothing by being assigned: with one lane per floor, the first free stall passed is also the nearest. The system helps there only when drivers miss stalls (${fmt(M05.saved_m_per_car, 1)} m per car at p = 0.5) or when the lot is full: at ${fmt(OCC * 100)}% occupancy ${fmt(M1.turned_away_share * 100)}% of the cars that arrive find the small lot full, and without information each would drive a ${fmt(M1.baseline_m_when_lot_full)} m round trip before leaving. The assigned stall pays off in garages with several aisles and floors, where the shortest path to a free stall differs from the order in which a driver searches. Random search is far worse than both (${fmt(GR.saved_m_per_car)} m more per car in the garage), which shows how much a driver gains just from searching in order.`),
  h2("5.4 Comparison with published figures"),
  bullet(`**Search time.** At ${fmt(OCC * 100)}% occupancy the searching driver spends ${fmt(g85.extra_search_s, 1)} s longer reaching a stall than the assigned driver. This is of the same order as, and below, the 50 s average reported for off-street parking (van Ommeren et al., 2012), as expected for a driver who sees every free stall.`),
  bullet(`**Reduction.** Being assigned cuts the driving to the stall by ${fmt(redLo)}–${fmt(redHi)}% across all occupancy levels, inside the 16–50% range reported for availability-information and reservation systems (Caicedo, 2009; Geng & Cassandras, 2011).`),
  bullet("**Occupancy.** In the garage the saving is already present at 50% occupancy, whereas on-street search becomes noticeable only above about 85% (Levy et al., 2013). The difference is expected: a garage fills floor by floor, so a driver searching alone must cover a full ground floor before going up, while the system sends the driver straight to the first free stall on the next floor. This is exactly the situation that floor- and zone-guidance systems address."),
  h2("5.5 Sensitivity"),
  para(`Figure 3 varies each uncertain constant across its range from Table 3 while keeping the others at their base values. The yearly saving (fuel plus value of time) is most sensitive to the driving speed inside the garage: between ${fmt(sens.search_speed_kmh.high / 1000)} and ${fmt(sens.search_speed_kmh.low / 1000)} thousand shekels for 20 and 5 km/h, around a base of ${fmt(BASE_ILS / 1000)} thousand. The opening hours, the value of time and the fuel use at crawl speed follow. A driver who misses half of the free stalls changes the yearly saving by only ${fmt((sens.search_visibility_p.low / BASE_ILS - 1) * 100)}% at this occupancy. Floor-to-floor height has no effect: both drivers use the same ramps, so a longer ramp adds the same distance to both.`),
  figure("fig3_sensitivity.png", "Sensitivity of the yearly saving to each uncertain constant"),
  caption(`Figure 3. One-at-a-time sensitivity of the yearly saving (fuel plus value of time) for the ${garageStalls}-stall garage at ${fmt(OCC * 100)}% occupancy.`),

  // ---------- 6 discussion
  h1("6. Discussion and limitations"),
  bullet("**The estimate is conservative.** The driver searching alone follows an efficient route, sees every free stall and never circles. Real drivers also skip stalls hoping for a closer one, compete for the same stall and circle: in an open shopping-centre lot, cruising distance at peak times was 2.7 times the optimal (Paidi et al., 2022). The true saving is therefore likely larger than reported here."),
  bullet("**Per car the saving is small; per garage it is not.** Seconds and millilitres per car add up to thousands of hours and litres a year. Most of the money value is drivers' time, valued with the official Israeli appraisal figure."),
  bullet("**Driving speed is the largest uncertainty.** The Ministry of Transport design value of 10 km/h is an assumption, not a measurement; a measured speed inside Israeli garages would narrow the result the most."),
  bullet("**Simplifications.** One garage layout was simulated; arrivals have no daily peaks; searching drivers do not compete; assigned drivers always follow the instruction; and the exit is at the entrance. Daily arrival profiles and drivers who ignore their assignment are natural next steps."),
  bullet("**The prototype.** The prototype demonstrates detection, assignment and wrong-parking detection on one physical stall. Its distances are those of the 1:100 physical model (27–65 cm, i.e. 27–65 m at full scale) rather than the sourced dimensions used in the simulation, its spot sensor works with only about 0.5 cm of margin on each side of its thresholds, and the sensor wiring should be improved to remove electrical glitches (Section 2.5)."),

  // ---------- 7 conclusions
  h1("7. Conclusions"),
  bullet("A working ESP32 prototype detects cars at the entrance and in a stall, assigns the nearest free stall with A*, reports correct and wrong parking, and keeps reservations consistent when drivers do not follow them."),
  bullet(`In a simulated ${garageStalls}-stall garage at ${fmt(OCC * 100)}% occupancy, assigning the stall saves ${fmt(G1.saved_m_per_car)} m, ${fmt(G1.saved_s_per_car)} s, ${fmt(G1.saved_ml_fuel_per_car, 1)} mL of fuel and ${fmt(G1.saved_g_co2_per_car)} g of CO₂ per car, about ₪${fmt(yearlyIls(G1) / 1000)} thousand a year in fuel and time, and the saving grows with occupancy.`),
  bullet("The result was not tuned to any published figure, and it agrees with published search times and reductions. Because the searching driver is modelled favourably, it is a lower bound."),
  bullet("The benefit comes from routing drivers past full aisles and floors; in a small single-lane lot it is limited to drivers who miss free stalls and to cars that would otherwise search a full lot."),

  // ---------- references
  h1("References"),
  ...D.references.map((r) => new Paragraph({ spacing: { after: 100 }, indent: { left: 567, hanging: 567 }, children: runs(r, { size: 20 }) })),
];

// ------------------------------------------------------------------ document

const doc = new Document({
  creator: "Smart Parking project",
  title: "Smart Parking System",
  styles: {
    default: { document: { run: { font: "Calibri", size: 22 } } },
    paragraphStyles: [
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 32, bold: true, color: "1F3864", font: "Calibri" },
        paragraph: { spacing: { before: 360, after: 120 }, keepNext: true, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 25, bold: true, color: "2E5395", font: "Calibri" },
        paragraph: { spacing: { before: 240, after: 80 }, keepNext: true, outlineLevel: 1 } },
    ],
  },
  numbering: {
    config: [
      { reference: "bullets", levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
        style: { paragraph: { indent: { left: 567, hanging: 283 } } } }] },
      { reference: "numbers", levels: [{ level: 0, format: LevelFormat.DECIMAL, text: "%1.", alignment: AlignmentType.LEFT,
        style: { paragraph: { indent: { left: 567, hanging: 283 } } } }] },
    ],
  },
  sections: [{
    properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 } } },
    footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER,
      children: [new TextRun({ children: [PageNumber.CURRENT], size: 18, color: "808080" })] })] }) },
    children,
  }],
});

Packer.toBuffer(doc).then((buf) => {
  fs.writeFileSync(OUT, buf);
  console.log(`wrote ${OUT}`);
});
