"""Collects everything the report needs into report_data.json.

All numbers in the report come from here: the parameters and sources from
simulation/params.py and the results from simulation/results/*.csv, so the
report always matches the last simulation run.

Run from this folder:  python export_data.py
"""
import json
import re
import sys
from pathlib import Path

import pandas as pd

HERE = Path(__file__).parent
SIM = HERE.parent / "simulation"
sys.path.insert(0, str(SIM))

from lot import demo_lot, garage  # noqa: E402
from params import BENCHMARKS, PARAMS, REFERENCES, derived  # noqa: E402
from sim import LotModel  # noqa: E402

LABELS = {
    "stall_width_m": "Stall width",
    "stall_depth_m": "Stall length",
    "aisle_width_m": "Aisle width (two-way)",
    "ramp_max_grade": "Maximum ramp gradient",
    "ramp_transition_m": "Ramp transition length",
    "floor_rise_m": "Floor-to-floor height",
    "search_speed_kmh": "Driving speed inside the lot",
    "fuel_rate_l_per_h": "Fuel use at crawl speed",
    "co2_kg_per_l": "CO₂ per litre of gasoline",
    "fuel_price_ils_per_l": "Gasoline price (95, self-service)",
    "value_of_time_ils_per_h": "Value of travel time (non-work)",
    "mean_stay_min": "Mean parking duration",
    "stay_gamma_shape": "Parking-duration shape (gamma)",
    "target_occupancy": "Headline occupancy",
    "operating_hours_per_day": "Opening hours per day",
    "garage_floors": "Garage floors",
    "garage_aisles_per_floor": "Aisles per floor",
    "garage_rows_per_aisle": "Stalls per side of an aisle",
    "search_visibility_p": "Chance a searching driver notices a free stall (p)",
    "reservation_timeout_s": "Reservation timeout (prototype)",
}

GROUPS = [
    ("Garage geometry", ["stall_width_m", "stall_depth_m", "aisle_width_m", "ramp_max_grade",
                         "ramp_transition_m", "floor_rise_m"]),
    ("Driving, fuel and money", ["search_speed_kmh", "fuel_rate_l_per_h", "co2_kg_per_l",
                                 "fuel_price_ils_per_l", "value_of_time_ils_per_h"]),
    ("Demand", ["mean_stay_min", "stay_gamma_shape", "target_occupancy", "operating_hours_per_day"]),
    ("Simulated garage and driver", ["garage_floors", "garage_aisles_per_floor", "garage_rows_per_aisle",
                                     "search_visibility_p"]),
    ("Prototype", ["reservation_timeout_s"]),
]

SHORT = {
    "ARGONNE2013": "Gaines, Rask & Keller (2013)",
    "CAICEDO2009": "Caicedo (2009)",
    "CBS2023": "Israel CBS (2023)",
    "EPA_GHG": "U.S. EPA (2026)",
    "GENG2011": "Geng & Cassandras (2011)",
    "HUANG2020": "Huang et al. (2020)",
    "LEVY2013": "Levy, Martens & Benenson (2013)",
    "LU2010": "Lu et al. (2010)",
    "MOE2026": "Israel Ministry of Energy (2026)",
    "MOT2000": "Israel Ministry of Transport (2000)",
    "PAIDI2022": "Paidi et al. (2022)",
    "PRAT2021": "Nohal PRAT (2021)",
    "PURINTON2012": "Purinton (2012)",
    "SAKI2024": "Saki & Hagen (2024)",
    "SHOUP2006": "Shoup (2006)",
    "SURPRIS2014": "Surpris, Liu & Vincenzi (2014)",
    "VANOMMEREN2012": "van Ommeren, Wentink & Rietveld (2012)",
    "assumption": "Assumption",
    "derived": "Derived",
}


def cite(text):
    """Replace source keys inside free text with readable citations."""
    return re.sub(r"\b(" + "|".join(k for k in SHORT if k.isupper() or "_" in k) + r")\b",
                  lambda m: SHORT[m.group(1)], text)


def records(path):
    return json.loads(pd.read_csv(path).to_json(orient="records"))


def main():
    results = SIM / "results"
    d = derived()
    values = {k: v.value for k, v in PARAMS.items()}
    demo = LotModel(demo_lot(d["lane_step_m"], d["lane_to_stall_m"], d["ramp_length_m"]))
    big = LotModel(garage(int(values["garage_floors"]), int(values["garage_aisles_per_floor"]),
                          int(values["garage_rows_per_aisle"]),
                          d["lane_step_m"], d["lane_to_stall_m"], d["ramp_length_m"]))

    data = {
        "params": [{"group": g, "key": k, "label": LABELS[k], "value": PARAMS[k].value, "unit": PARAMS[k].unit,
                    "source": SHORT[PARAMS[k].source], "where": PARAMS[k].where,
                    "verified": PARAMS[k].verified, "note": cite(PARAMS[k].note), "sweep": list(PARAMS[k].sweep)}
                   for g, keys in GROUPS for k in keys],
        "derived": d,
        "benchmarks": [{"key": k, "value": v[0], "source": SHORT[v[1]], "where": v[2], "note": v[3]}
                       for k, v in BENCHMARKS.items()],
        "references": sorted(REFERENCES.values(), key=lambda s: s.lower()),
        "lots": {
            "demo": {"stalls": demo.n, "nearest_m": float(demo.dist.min()), "farthest_m": float(demo.dist.max())},
            "garage": {"stalls": big.n, "nearest_m": float(big.dist.min()), "farthest_m": float(big.dist.max()),
                       "route_m": big.route_end},
        },
        "headline": records(results / "headline.csv"),
        "validation": records(results / "validation.csv"),
        "sensitivity": records(results / "sensitivity.csv"),
        "sweep": records(results / "occupancy_sweep.csv"),
    }
    assert set(LABELS) == set(PARAMS), "every parameter needs a label and a group"
    out = HERE / "report_data.json"
    out.write_text(json.dumps(data, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
