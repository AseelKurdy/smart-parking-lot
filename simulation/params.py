"""Simulation constants and the published source behind each one.

Single source of truth: sim.py reads the values from here, and the report's
parameter table is generated from PARAMS, so the two can never drift apart.

Every entry records where the number comes from:
  source  - key into REFERENCES, or "assumption" / "derived"
  where   - page / table / section inside that source
  sweep   - (low, high) range for the sensitivity analysis when the value is uncertain

Sources were opened and checked (October 2026). "verified" says how:
full text, abstract only, or official page.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Param:
    value: float
    unit: str
    source: str
    where: str
    note: str
    verified: str = "full text"
    sweep: tuple = ()


PARAMS = {
    # ---------------- geometry (Israeli Ministry of Transport guidelines) ----------------
    "stall_width_m": Param(
        2.50, "m", "MOT2000", "Ch. D, Table 3 p.25; Table 9 p.35",
        "Standard 90-degree stall width at service level 1 (pairs with a 5.80 m aisle)."),
    "stall_depth_m": Param(
        5.00, "m", "MOT2000", "Ch. D, Sec. 4.1 p.11",
        "Minimum 90-degree stall length: 4.75 m design car + 0.25 m."),
    "aisle_width_m": Param(
        5.80, "m", "MOT2000", "Ch. D, Table 3 p.25; Sec. 13.3 p.60",
        "Two-way aisle for 2.50 m stalls at service level 1."),
    "ramp_max_grade": Param(
        0.15, "-", "MOT2000", "Ch. D, Sec. 12.5 p.54",
        "Maximum ramp gradient at service levels 1-2 (17% at level 3)."),
    "ramp_transition_m": Param(
        3.0, "m", "MOT2000", "Ch. D, Sec. 12.5 p.54",
        "Each end of the ramp has a 3 m transition at half the gradient."),
    "floor_rise_m": Param(
        3.0, "m", "assumption", "",
        "No Israeli figure found; MoT only gives 2.20 m clear height. "
        "US garages are typically 3.2-3.5 m floor to floor, per PURINTON2012.",
        verified="n/a", sweep=(2.8, 3.5)),

    # ---------------- driving ----------------
    "search_speed_kmh": Param(
        10.0, "km/h", "MOT2000", "Ch. D, Sec. 11 p.45",
        "MoT design assumption for driving inside a parking facility (a design value, not a measurement). "
        "Shoup (2006) observed ~13 km/h while cruising on-street.",
        sweep=(5.0, 20.0)),

    # ---------------- fuel, emissions, money ----------------
    "fuel_rate_l_per_h": Param(
        1.0, "L/h", "ARGONNE2013", "Table 1; Conclusions",
        "Measured idle consumption 0.279 cc/s = 1.0 L/h (2.5 L sedan); passenger cars 0.76-1.9 L/h. "
        "At crawl speed, consumption is modelled per hour, not per km.",
        sweep=(0.8, 2.0)),
    "co2_kg_per_l": Param(
        2.35, "kg/L", "EPA_GHG", "web page",
        "8,887 g CO2 per gallon of gasoline.", verified="official page"),
    "fuel_price_ils_per_l": Param(
        8.27, "ILS/L", "MOE2026", "p.1",
        "95-octane self-service, October 2026, including VAT.", verified="official page"),
    "value_of_time_ils_per_h": Param(
        27.31, "ILS/h", "PRAT2021", "Table 4.1 p.118",
        "Value of travel time for non-work trips (2020 prices); parking search counts as travel time (p.115).",
        sweep=(25.0, 40.0)),

    # ---------------- demand ----------------
    "mean_stay_min": Param(
        88.0, "min", "HUANG2020", "Table 2 p.6",
        "Mall garage: gamma-distributed stays with means 82.5-93.7 min.",
        sweep=(82.5, 93.7)),
    "stay_gamma_shape": Param(
        1.8, "-", "HUANG2020", "Table 2 p.6",
        "Reported gamma shapes 1.12-2.53; mid-range value chosen.",
        sweep=(1.12, 2.53)),
    # Arrivals are a Poisson process (no numeric constant): HUANG2020 Table 1 p.5,
    # Kolmogorov-Smirnov p = 0.29-0.65 on 5-minute counts. The arrival rate is set
    # from the target occupancy, so occupancy is the swept variable.
    "target_occupancy": Param(
        0.85, "-", "LEVY2013", "pp.788-789",
        "Search effort becomes noticeable above ~85% occupancy and important above 92-93%.",
        sweep=(0.50, 0.98)),

    "operating_hours_per_day": Param(
        12.0, "h", "assumption", "",
        "Typical mall opening hours (10:00-22:00); only used to turn per-car savings into yearly totals.",
        verified="n/a", sweep=(10.0, 16.0)),

    # ---------------- simulated lots ----------------
    "garage_floors": Param(
        3, "floors", "assumption", "",
        "Main scenario: a representative small multi-storey garage.", verified="n/a"),
    "garage_aisles_per_floor": Param(
        3, "aisles", "assumption", "",
        "Parallel two-way aisles per floor, stalls on both sides, cross aisles at both ends.", verified="n/a"),
    "garage_rows_per_aisle": Param(
        12, "rows", "assumption", "",
        "12 stalls per side = 30 m aisles; 3 floors x 3 aisles x 2 sides x 12 = 216 stalls.", verified="n/a"),

    # ---------------- self-search driver (baseline) ----------------
    "search_visibility_p": Param(
        1.0, "-", "assumption", "",
        "Chance that a searching driver notices a free stall they pass. 1.0 = sees every one, the most "
        "favourable case for the driver, so the system's savings are a lower bound.",
        verified="n/a", sweep=(0.5, 1.0)),

    # ---------------- reservation (firmware) ----------------
    "reservation_timeout_s": Param(
        60.0, "s", "derived", "",
        "Demo value. Real-world bound: farthest spot / search speed + parking manoeuvre margin.",
        verified="n/a"),
}


def derived(overrides=None):
    """Values computed from PARAMS (shown in the report with their formula).

    overrides: optional {name: value}, used by the sensitivity analysis.
    """
    p = {k: v.value for k, v in PARAMS.items()} | (overrides or {})
    # Lane centre -> stall centre: half the aisle + half the stall
    lane_to_stall = p["aisle_width_m"] / 2 + p["stall_depth_m"] / 2
    # Ramp: two transitions at half grade, the rest of the rise at full grade
    rise_in_transitions = 2 * p["ramp_transition_m"] * p["ramp_max_grade"] / 2
    ramp_length = 2 * p["ramp_transition_m"] + (p["floor_rise_m"] - rise_in_transitions) / p["ramp_max_grade"]
    return {
        "lane_step_m": p["stall_width_m"],
        "lane_to_stall_m": lane_to_stall,
        "ramp_length_m": ramp_length,
        "search_speed_mps": p["search_speed_kmh"] / 3.6,
    }


# Published figures the model's results are checked against, also quoted in the
# report's literature section. They are NOT inputs: nothing is tuned to match them.
BENCHMARKS = {
    "levy_noticeable_occupancy": (0.85, "LEVY2013", "Abstract; pp.788-789",
                                  "Search effort becomes noticeable above ~85% occupancy."),
    "levy_search_above_92_s": (60, "LEVY2013", "pp.788-789",
                               "Above 92-93% occupancy search exceeds 1 min (agent-based model, on-street)."),
    "levy_search_at_95_s": (120, "LEVY2013", "pp.788-789",
                            "At 95% occupancy search exceeds 2 min and over 5% of drivers fail."),
    "offstreet_search_s": (50, "VANOMMEREN2012", "Sec. 3.2 p.125, footnote 8",
                           "Average search time off-street (30 s on-street); self-reported, n = 11,425."),
    "gps_search_min": (1.5, "SAKI2024", "Abstract",
                       "Mean cruising from GPS ground truth, 3,000+ trips, Germany 2021-23 (abstract only)."),
    "excess_distance_ratio_peak": (2.7, "PAIDI2022", "Abstract",
                                   "Non-optimal cruising distance is 2.7x optimal in an open shopping-centre lot (abstract only)."),
    "reservation_time_halved": (0.5, "GENG2011", "Sec. V, Table I",
                                "Assignment with reservations cut average time-to-park by about half in simulation."),
    "zoning_search_reduction": (0.162, "CAICEDO2009", "Abstract",
                                "Space-availability information in zones cut search time by 16.2% (abstract only)."),
    "smart_lot_saving_s": (11, "SURPRIS2014", "Abstract",
                           "Smart parking in a university lot saved 11 s of search on average (abstract only)."),
    "shoup_cruise_min": (8.1, "SHOUP2006", "Abstract p.479; Table 1 p.480",
                         "Downtown on-street cruising, 16 studies 1927-2001 (not a garage baseline)."),
}


REFERENCES = {
    "ARGONNE2013": "Gaines, L., Rask, E., & Keller, G. (2013). Which is greener: Idle, or stop and restart? "
                   "Argonne National Laboratory. https://afdc.energy.gov/files/u/publication/which_is_greener.pdf",
    "CAICEDO2009": "Caicedo, F. (2009). The use of space availability information in \"PARC\" systems to reduce "
                   "search times in parking facilities. Transportation Research Part C, 17(1), 56-68. "
                   "https://doi.org/10.1016/j.trc.2008.07.001",
    "CBS2023": "Central Bureau of Statistics, Israel. (2023). Vehicle kilometers travelled 2022 (Publication No. 1923). "
               "https://www.cbs.gov.il/he/publications/doclib/2024/nesua22_1923/e_print.pdf",
    "EPA_GHG": "U.S. EPA. (2026). Greenhouse gas emissions from a typical passenger vehicle. "
               "https://www.epa.gov/greenvehicles/greenhouse-gas-emissions-typical-passenger-vehicle",
    "GENG2011": "Geng, Y., & Cassandras, C. G. (2011). A new \"smart parking\" system based on optimal resource "
                "allocation and reservations. Proc. 14th IEEE ITSC. https://people.bu.edu/cgc/Published/ITSC11.pdf",
    "HUANG2020": "Huang, X., Long, X., Wang, J., & He, L. (2020). Research on parking sharing strategies considering "
                 "user overtime parking. PLOS ONE, 15(6), e0233772. https://doi.org/10.1371/journal.pone.0233772",
    "LEVY2013": "Levy, N., Martens, K., & Benenson, I. (2013). Exploring cruising using agent-based and analytical "
                "models of parking. Transportmetrica A, 9(9), 773-797. https://doi.org/10.1080/18128602.2012.664575",
    "LU2010": "Lu, R., Lin, X., Zhu, H., & Shen, X. (2010). An intelligent secure and privacy-preserving parking scheme "
              "through vehicular communications. IEEE Transactions on Vehicular Technology, 59(6), 2772-2785.",
    "MOE2026": "Israel Ministry of Energy and Infrastructure. (2026, September 29). Fuel prices for October 2026. "
               "https://www.gov.il/BlobFolder/news/fuel-october-2026/he/fuel-october-2026.pdf",
    "MOT2000": "Israel Ministry of Transport. (2000). Parking planning guidelines, Chapter D: Planning parking "
               "facilities. https://www.gov.il/BlobFolder/policy/parking_planning_guidelines/he/parking.pdf",
    "PAIDI2022": "Paidi, V., Hakansson, J., Fleyeh, H., & Nyberg, R. G. (2022). CO2 emissions induced by vehicles "
                 "cruising for empty parking spaces in an open parking lot. Sustainability, 14(7), 3742. "
                 "https://doi.org/10.3390/su14073742",
    "PRAT2021": "Israel Ministry of Transport, Ministry of Finance & Ministry of Environmental Protection. (2021). "
                "Nohal PRAT 2021: Guidelines for evaluating transport projects. https://www.gov.il/BlobFolder/"
                "dynamiccollectorresultitem/nohal_prat_21/he/repository-of-answers_ministry-of-transport-and-road-safety_nohal_prat_21.pdf",
    "PURINTON2012": "Purinton, J. (2012, November). Mixed-use parking structure design. The Parking Professional, 6. "
                    "https://www.parking.org/wp-content/uploads/2016/01/TPP-2012-11-Mixed-Use-Parking-Structure-Design.pdf",
    "SAKI2024": "Saki, S., & Hagen, T. (2024). Cruising for parking again. Transportation Research Part A, 183. "
                "https://doi.org/10.1016/j.tra.2024.104045",
    "SHOUP2006": "Shoup, D. C. (2006). Cruising for parking. Transport Policy, 13(6), 479-486. "
                 "https://doi.org/10.1016/j.tranpol.2006.05.005",
    "SURPRIS2014": "Surpris, G., Liu, D., & Vincenzi, D. (2014). How much can a smart parking system save you? "
                   "Ergonomics in Design, 22. https://doi.org/10.1177/1064804614526202",
    "VANOMMEREN2012": "van Ommeren, J. N., Wentink, D., & Rietveld, P. (2012). Empirical evidence on cruising for "
                      "parking. Transportation Research Part A, 46(1), 123-130. https://doi.org/10.1016/j.tra.2011.09.011",
}
