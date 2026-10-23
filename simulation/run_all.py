"""Runs every experiment for the report and writes results/*.csv and figures/*.png.

Run from this folder:  python run_all.py              (takes a few minutes)
                       python run_all.py --figures    (only redraw the figures from results/)
"""
import sys
from pathlib import Path

import matplotlib
import numpy as np
import pandas as pd

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402  (backend must be chosen first)
import matplotlib.ticker as mticker  # noqa: E402

from lot import demo_lot, garage  # noqa: E402
from params import BENCHMARKS, PARAMS, derived  # noqa: E402
from sim import LotModel, arrival_rate_for, simulate, summarize, to_costs  # noqa: E402

HERE = Path(__file__).parent
RESULTS, FIGURES = HERE / "results", HERE / "figures"

REPS = 20     # replications per setting
CARS = 3000   # measured cars per replication (after warm-up)
T975 = 2.093  # Student t quantile for a 95% confidence interval with REPS - 1 = 19 degrees of freedom
OCCUPANCIES = [0.50, 0.60, 0.70, 0.80, 0.85, 0.90, 0.95, 0.98]

BASE = {k: v.value for k, v in PARAMS.items()}
HEADLINE_OCC = BASE["target_occupancy"]
P_BASE = BASE["search_visibility_p"]                # driver sees every free stall they pass
P_LOW = PARAMS["search_visibility_p"].sweep[0]      # driver misses half of them

# Chart styling: reference palette from the dataviz guidelines, light mode for print
INK, INK2, MUTED, GRID, AXIS = "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
plt.rcParams.update({
    "font.family": "sans-serif", "font.sans-serif": ["Segoe UI", "DejaVu Sans"], "font.size": 9,
    "axes.edgecolor": AXIS, "axes.labelcolor": INK2, "axes.titlecolor": INK, "axes.titlesize": 10,
    "axes.titlelocation": "left", "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "axes.grid.axis": "y", "grid.color": GRID, "grid.linewidth": 0.6,
    "xtick.color": MUTED, "ytick.color": MUTED, "xtick.labelcolor": INK2, "ytick.labelcolor": INK2,
    "legend.frameon": False, "lines.linewidth": 2, "lines.markersize": 5,
    "figure.facecolor": "white", "axes.facecolor": "white", "savefig.dpi": 200,
})


# ---------------------------------------------------------------- experiments

def build_models(values):
    d = derived(values)
    geom = (d["lane_step_m"], d["lane_to_stall_m"], d["ramp_length_m"])
    return {
        "demo": LotModel(demo_lot(*geom)),
        "garage": LotModel(garage(int(values["garage_floors"]), int(values["garage_aisles_per_floor"]),
                                  int(values["garage_rows_per_aisle"]), *geom)),
    }


def run(model, occupancy, p, values, reps=REPS, cars=CARS):
    """`reps` replications at one setting -> one row per replication."""
    stay = values["mean_stay_min"]
    rate = arrival_rate_for(occupancy, model.n, stay)
    warmup = max(500, int(4 * rate * stay))   # several mean stays, so the lot reaches steady state
    rows = []
    for seed in range(reps):
        rep = simulate(model, rate, stay, values["stay_gamma_shape"], p, cars, warmup, seed)
        rows.append(summarize(rep) | {"seed": seed, "arrival_rate_per_min": rate})
    df = pd.DataFrame(rows)
    for s in ("assigned", "cruising", "random"):
        df[f"{s}_total_m"] = df[f"{s}_search_m"] + df[f"{s}_exit_m"]
    df["saved_vs_cruising_m"] = df["cruising_total_m"] - df["assigned_total_m"]
    df["saved_vs_random_m"] = df["random_total_m"] - df["assigned_total_m"]
    df["entry_reduction_vs_cruising"] = 1 - df["assigned_search_m"] / df["cruising_search_m"]
    return df


def aggregate(df, keys):
    """Mean and 95% CI half-width of every metric, per setting."""
    metrics = [c for c in df.columns if c not in keys + ["seed"]]
    rows = []
    for setting, g in df.groupby(keys, sort=False):
        row = dict(zip(keys, setting if isinstance(setting, tuple) else (setting,)))
        for m in metrics:
            row[m] = g[m].mean()
            row[f"{m}_ci95"] = T975 * g[m].std(ddof=1) / np.sqrt(len(g))
        rows.append(row)
    return pd.DataFrame(rows)


def per_car_costs(metres, values):
    return to_costs(metres, values, derived(values)["search_speed_mps"])


def cars_per_year(row, values):
    """Cars that park in one year: arrival rate x opening hours x 365, minus those turned away."""
    return row["arrival_rate_per_min"] * 60 * values["operating_hours_per_day"] * 365 * (1 - row["turned_away_share"])


def annual_ils(saved_m, row, values):
    c = per_car_costs(saved_m, values)
    return (c["fuel_ils"] + c["time_ils"]) * cars_per_year(row, values)


def pick(agg, lot, p, occ):
    return agg[(agg["lot"] == lot) & (agg["p"] == p) & (agg["target_occupancy"] == occ)].iloc[0]


# ---------------------------------------------------------------- figures

def style_axis(ax, ylabel, xlabel):
    ax.set_ylabel(ylabel)
    ax.set_xlabel(xlabel)
    ax.tick_params(length=0)
    ax.set_ylim(bottom=0)


def label_line_ends(ax, x_end, ends):
    """Direct labels at the right end of each line, nudged apart so they never overlap."""
    lo, hi = ax.get_ylim()
    gap = (hi - lo) * 0.07
    placed = []
    for y, text in sorted(ends):
        if placed and y - placed[-1] < gap:
            y = placed[-1] + gap
        placed.append(y)
        ax.annotate(text, (x_end, y), xytext=(6, 0), textcoords="offset points",
                    va="center", color=INK2, fontsize=8)


def fig_driving_vs_occupancy(agg, names):
    series = [("assigned", P_BASE, BLUE, "Assigned spot (this system)", "Assigned"),
              ("cruising", P_BASE, ORANGE, "Searching alone, p = 1", "p = 1"),
              ("cruising", P_LOW, AQUA, f"Searching alone, p = {P_LOW:g}", f"p = {P_LOW:g}")]
    fig, axes = plt.subplots(1, 2, figsize=(6.5, 3.4))
    for ax, lot in zip(axes, ["demo", "garage"]):
        ends = []
        for strategy, p, color, label, short in series:
            d = agg[(agg["lot"] == lot) & (agg["p"] == p)]
            x = d["occupancy"].to_numpy() * 100
            y, ci = d[f"{strategy}_total_m"].to_numpy(), d[f"{strategy}_total_m_ci95"].to_numpy()
            ax.fill_between(x, y - ci, y + ci, color=color, alpha=0.15, linewidth=0)
            ax.plot(x, y, color=color, marker="o", label=label, zorder=3 if strategy == "assigned" else 2)
            ends.append((y[-1], short))
        if lot == "demo":   # assigned and p = 1 coincide here; say so in one label
            ends = [(y, "Assigned, p = 1" if t == "Assigned" else t) for y, t in ends if t != "p = 1"]
        ax.axvline(HEADLINE_OCC * 100, color=AXIS, linewidth=1, zorder=1)
        ax.annotate(f"{HEADLINE_OCC:.0%}", (HEADLINE_OCC * 100, 1), xycoords=("data", "axes fraction"),
                    xytext=(3, -2), textcoords="offset points", va="top", color=MUTED, fontsize=8)
        style_axis(ax, "Metres driven per visit", "Occupancy (%)")
        ax.set_title(names[lot].split(",")[0] + ")" if "," in names[lot] else names[lot], fontsize=9)
        ax.set_xticks([50, 60, 70, 80, 90, 100])
        ax.set_xlim(48, 122)
        ax.spines["bottom"].set_bounds(48, 100)
        label_line_ends(ax, x[-1], ends)
    handles, labels = axes[1].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=3, bbox_to_anchor=(0.5, 1.0), fontsize=7.5)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    fig.savefig(FIGURES / "fig1_driving_vs_occupancy.png")
    plt.close(fig)


def fig_savings_vs_occupancy(agg, name):
    d = agg[(agg["lot"] == "garage") & (agg["p"] == P_BASE)]
    x = d["occupancy"].to_numpy() * 100
    saved, ci = d["saved_vs_cruising_m"].to_numpy(), d["saved_vs_cruising_m_ci95"].to_numpy()
    mid, lo, hi = (per_car_costs(v, BASE) for v in (saved, saved - ci, saved + ci))
    panels = [("seconds", "Time saved per car (s)", 1),
              ("litres", "Fuel saved per car (mL)", 1000),
              ("co2_kg", "CO₂ saved per car (g)", 1000),
              ("fuel_ils", "Fuel cost saved per car (agorot)", 100)]
    fig, axes = plt.subplots(2, 2, figsize=(6.5, 4.8))
    for ax, (key, title, scale) in zip(axes.flat, panels):
        ax.fill_between(x, lo[key] * scale, hi[key] * scale, color=BLUE, alpha=0.15, linewidth=0)
        ax.plot(x, mid[key] * scale, color=BLUE, marker="o")
        style_axis(ax, "", "Occupancy (%)")
        ax.set_xticks([50, 60, 70, 80, 90, 100])
        ax.set_title(title, fontsize=9)
    fig.suptitle(f"{name.split(',')[0]}): saving per car vs. a driver searching alone (p = 1)",
                 x=0.01, ha="left", color=INK, fontsize=9.5, fontweight="bold")
    fig.tight_layout()
    fig.savefig(FIGURES / "fig2_savings_vs_occupancy.png")
    plt.close(fig)


def fig_tornado(tornado, base_value):
    t = tornado.assign(span=(tornado["high"] - tornado["low"]).abs()).sort_values("span")
    fig, ax = plt.subplots(figsize=(6.5, 0.45 * len(t) + 1.2))
    y = np.arange(len(t))
    for col, color, name, offset in (("low", BLUE, "Low end of range", -0.19),
                                     ("high", ORANGE, "High end of range", 0.19)):
        ax.barh(y + offset, t[col] - base_value, left=base_value, height=0.34, color=color, label=name)
    ax.axvline(base_value, color=INK2, linewidth=1)
    ax.set_yticks(y, [f"{r.label}  ({r.range_text})" for r in t.itertuples()])
    ax.grid(axis="x")
    ax.grid(axis="y", visible=False)
    ax.tick_params(length=0)
    ax.set_xlabel("Yearly saving, fuel + value of time (ILS thousand)")
    ax.xaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f"{v / 1000:,.0f}"))
    ax.set_title(f"Sensitivity of the yearly saving (base case: ILS {base_value / 1000:,.0f}k)", fontsize=9)
    ax.legend(loc="lower right", fontsize=8)
    fig.tight_layout()
    fig.savefig(FIGURES / "fig3_sensitivity.png")
    plt.close(fig)


# ---------------------------------------------------------------- main

def redraw_figures():
    """Redraw the figures from the saved results, without running the simulation again."""
    names = {k: m.lot.name for k, m in build_models(BASE).items()}
    agg = pd.read_csv(RESULTS / "occupancy_sweep.csv")
    tornado = pd.read_csv(RESULTS / "sensitivity.csv")
    fig_driving_vs_occupancy(agg, names)
    fig_savings_vs_occupancy(agg, names["garage"])
    fig_tornado(tornado, tornado["base"].iloc[0])


def main():
    if "--figures" in sys.argv:
        redraw_figures()
        return
    RESULTS.mkdir(exist_ok=True)
    FIGURES.mkdir(exist_ok=True)
    for old in FIGURES.glob("*.png"):
        old.unlink()
    models = build_models(BASE)
    names = {k: m.lot.name for k, m in models.items()}

    # parameter table for the report (point 5)
    params_rows = [{"name": k, "value": v.value, "unit": v.unit, "source": v.source, "where": v.where,
                    "verified": v.verified, "note": v.note,
                    "sweep_low": v.sweep[0] if v.sweep else "", "sweep_high": v.sweep[1] if v.sweep else ""}
                   for k, v in PARAMS.items()]
    params_rows += [{"name": k, "value": round(v, 3), "source": "derived"} for k, v in derived().items()]
    pd.DataFrame(params_rows).to_csv(RESULTS / "parameters.csv", index=False)

    print("occupancy sweep ...", flush=True)
    sweep = pd.concat([run(m, occ, p, BASE).assign(lot=lot, p=p, target_occupancy=occ)
                       for lot, m in models.items() for p in (P_BASE, P_LOW) for occ in OCCUPANCIES])
    sweep.to_csv(RESULTS / "occupancy_sweep_replications.csv", index=False)
    agg = aggregate(sweep, ["lot", "p", "target_occupancy"])
    agg.to_csv(RESULTS / "occupancy_sweep.csv", index=False)

    # headline numbers at the target occupancy
    head = []
    for lot in models:
        for p, vs, label in ((P_BASE, "cruising", "Searching alone, p = 1"),
                             (P_LOW, "cruising", f"Searching alone, p = {P_LOW:g}"),
                             (P_BASE, "random", "Random search (no information)")):
            row = pick(agg, lot, p, HEADLINE_OCC)
            saved = row[f"saved_vs_{vs}_m"]
            c = per_car_costs(saved, BASE)
            n_year = cars_per_year(row, BASE)
            head.append({
                "lot": names[lot], "compared_with": label, "occupancy": row["occupancy"],
                "assigned_entry_m": row["assigned_search_m"], "assigned_exit_m": row["assigned_exit_m"],
                "baseline_entry_m": row[f"{vs}_search_m"], "baseline_exit_m": row[f"{vs}_exit_m"],
                "saved_m_per_car": saved, "saved_m_ci95": row[f"saved_vs_{vs}_m_ci95"],
                "saved_s_per_car": c["seconds"], "saved_ml_fuel_per_car": c["litres"] * 1000,
                "saved_g_co2_per_car": c["co2_kg"] * 1000, "saved_ils_fuel_per_car": c["fuel_ils"],
                "saved_ils_time_per_car": c["time_ils"],
                "cars_per_year": n_year, "saved_hours_per_year": c["seconds"] * n_year / 3600,
                "saved_litres_per_year": c["litres"] * n_year, "saved_kg_co2_per_year": c["co2_kg"] * n_year,
                "saved_ils_fuel_per_year": c["fuel_ils"] * n_year, "saved_ils_time_per_year": c["time_ils"] * n_year,
                "turned_away_share": row["turned_away_share"],
                "baseline_m_when_lot_full": row[f"{vs}_full_lot_m"],
            })
    headline = pd.DataFrame(head)
    headline.to_csv(RESULTS / "headline.csv", index=False)

    # checks against the literature (nothing was tuned to these)
    speed = derived()["search_speed_mps"]
    validation = []
    for p in (P_BASE, P_LOW):
        for occ in OCCUPANCIES:
            r = pick(agg, "garage", p, occ)
            validation.append({"p": p, "target_occupancy": occ, "occupancy": r["occupancy"],
                               "extra_search_s": (r["cruising_search_m"] - r["assigned_search_m"]) / speed,
                               "entry_driving_reduction": r["entry_reduction_vs_cruising"]})
    validation = pd.DataFrame(validation)
    validation.assign(
        benchmark_offstreet_search_s=BENCHMARKS["offstreet_search_s"][0],
        benchmark_reduction_low=BENCHMARKS["zoning_search_reduction"][0],
        benchmark_reduction_high=1 - BENCHMARKS["reservation_time_halved"][0],
    ).to_csv(RESULTS / "validation.csv", index=False)

    print("sensitivity analysis ...", flush=True)
    base_row = pick(agg, "garage", P_BASE, HEADLINE_OCC)
    base_value = annual_ils(base_row["saved_vs_cruising_m"], base_row, BASE)
    tornado = []
    for key, label in (("search_visibility_p", "Driver notices a free stall"),
                       ("fuel_rate_l_per_h", "Fuel use at crawl speed"),
                       ("search_speed_kmh", "Driving speed in the garage"),
                       ("value_of_time_ils_per_h", "Value of time"),
                       ("operating_hours_per_day", "Opening hours per day"),
                       ("mean_stay_min", "Mean parking duration"),
                       ("floor_rise_m", "Floor-to-floor height")):
        lo_v, hi_v = PARAMS[key].sweep
        out = []
        for v in (lo_v, hi_v):
            values = BASE | {key: v}
            if key == "search_visibility_p":
                r = pick(agg, "garage", v, HEADLINE_OCC)
            elif key == "floor_rise_m":   # changes the ramp, so the distances must be simulated again
                r = aggregate(run(build_models(values)["garage"], HEADLINE_OCC, P_BASE, values).assign(k=0),
                              ["k"]).iloc[0]
            elif key == "mean_stay_min":   # same occupancy, so fewer/more cars per year
                r = base_row.copy()
                r["arrival_rate_per_min"] = base_row["arrival_rate_per_min"] * BASE[key] / v
            else:
                r = base_row
            out.append(annual_ils(r["saved_vs_cruising_m"], r, values))
        unit = "" if PARAMS[key].unit == "-" else f" {PARAMS[key].unit}"
        tornado.append({"parameter": key, "label": label, "range_text": f"{lo_v:g}-{hi_v:g}{unit}",
                        "low": out[0], "high": out[1]})
    tornado = pd.DataFrame(tornado)
    tornado.assign(base=base_value).to_csv(RESULTS / "sensitivity.csv", index=False)

    print("figures ...", flush=True)
    fig_driving_vs_occupancy(agg, names)
    fig_savings_vs_occupancy(agg, names["garage"])
    fig_tornado(tornado, base_value)

    pd.set_option("display.width", 220)
    print(headline[["lot", "compared_with", "saved_m_per_car", "saved_s_per_car", "saved_ml_fuel_per_car",
                    "saved_ils_fuel_per_car", "saved_ils_time_per_car", "cars_per_year"]].round(2).to_string())
    print(validation.round(3).to_string())


if __name__ == "__main__":
    main()
