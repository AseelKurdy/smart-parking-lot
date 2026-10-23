"""Event-driven parking simulation: assigned spot (the firmware's rule) vs. drivers searching alone.

Arrivals are a Poisson process and stays are gamma-distributed (see params.py).
Every strategy sees exactly the same cars - same arrival times, same stays
(common random numbers) - so the difference between strategies comes from the
strategy alone, not from luck.

Modelling choices (stated in the report):
* A search lasts seconds to a minute while a stay lasts ~1.5 hours, so the lot
  is taken as unchanged during one car's search.
* The exit is at the entrance, so leaving a stall costs its shortest distance
  back to the entrance, whatever the strategy that chose it.
* A car is admitted whenever a stall is free, whatever the strategy, so all
  strategies always have the same number of cars inside.
"""
import heapq
from dataclasses import dataclass

import numpy as np

from lot import a_star, dijkstra


class LotModel:
    """Per-lot distances the strategies need, computed once."""

    def __init__(self, lot):
        self.lot = lot
        self.n = len(lot.spots)
        self.depth = lot.lane_to_stall
        self.dist = np.array([a_star(lot, s) for s in lot.spots])  # entrance -> stall, shortest path
        self.nearest_first = np.argsort(self.dist, kind="stable")   # ties -> lower index, like the firmware

        # the searching driver: distance along lot.route at which each stall is first passed
        along = np.concatenate(([0.0], np.cumsum([lot.step(a, b) for a, b in zip(lot.route, lot.route[1:])])))
        first_passed = {}
        for cell, d in zip(lot.route, along):
            first_passed.setdefault(cell, d)
        access = [lot.access(s) for s in lot.spots]
        self.pos = np.array([first_passed[a] for a in access])
        self.route_end = float(self.pos.max())       # no point searching past the last stall
        self.forward = np.argsort(self.pos, kind="stable")
        self.backward = self.forward[::-1].copy()

        # the random searcher: shortest paths between the lane cells in front of the stalls
        self.from_entrance = self.dist - self.depth
        self.between = np.array([[d[b] for b in access] for d in (dijkstra(lot, a) for a in access)])


# Each strategy gets the current occupancy and returns (stall or -1 if it left, metres driven).

def assigned(occ, m, rng, p):
    """The firmware rule: the nearest free stall by A* distance. A full lot is announced at the gate."""
    free = ~occ[m.nearest_first]
    if not free.any():
        return -1, 0.0
    spot = m.nearest_first[free.argmax()]
    return spot, m.dist[spot]


def cruising(occ, m, rng, p):
    """Searches aisle by aisle along the lot's route and takes a free stall when it sees one.

    Each free stall the driver passes is noticed with probability p (p = 1: sees
    every one; p < 1: some are hidden by parked cars or pillars). A driver who
    reaches the end of the route without parking drives back along it and takes
    the first free stall they pass. In a full lot they find out only after that
    round trip.
    """
    if occ.all():
        return -1, 2 * m.route_end
    take = ~occ[m.forward] & (rng.random(m.n) < p)
    if take.any():
        spot = m.forward[take.argmax()]
        return spot, m.pos[spot] + m.depth
    spot = m.backward[(~occ[m.backward]).argmax()]
    return spot, 2 * m.route_end - m.pos[spot] + m.depth


def random_search(occ, m, rng, p):
    """No information at all: drives to stalls in random order until one is free.

    This is v7's baseline, but measured by the shortest path from where the
    driver actually is, and never re-checking a stall. In a full lot the driver
    checks every stall and drives back out.
    """
    order = rng.permutation(m.n)
    free = np.flatnonzero(~occ[order])
    visited = order[: free[0] + 1] if free.size else order
    driven = m.from_entrance[visited[0]] + m.between[visited[:-1], visited[1:]].sum()
    if free.size == 0:
        return -1, float(driven + m.from_entrance[visited[-1]])
    return visited[-1], float(driven + m.depth)


STRATEGIES = {"assigned": assigned, "cruising": cruising, "random": random_search}


@dataclass
class Replication:
    search: dict        # strategy -> metres driven per measured car (to the stall, or before leaving)
    exit: dict          # strategy -> metres from the stall back to the exit (0 if the car left)
    parked: np.ndarray  # True if the car found a stall (identical for every strategy)
    occupancy: np.ndarray  # share of stalls taken when the car arrived


def simulate(model, arrival_rate, mean_stay, stay_shape, p, n_cars, warmup_cars, seed):
    """One replication. arrival_rate in cars/min, mean_stay in minutes.

    The first warmup_cars cars only fill the lot up to its steady state and are
    not measured.
    """
    demand = np.random.default_rng(seed)   # arrivals and stays, shared by all strategies
    search_rng = {name: np.random.default_rng([seed, k]) for k, name in enumerate(STRATEGIES)}
    occ = {name: np.zeros(model.n, bool) for name in STRATEGIES}
    spot_of = {name: {} for name in STRATEGIES}
    departures = []   # heap of (departure time, car)

    search = {name: np.zeros(n_cars) for name in STRATEGIES}
    exit_ = {name: np.zeros(n_cars) for name in STRATEGIES}
    parked = np.zeros(n_cars, bool)
    occupancy = np.zeros(n_cars)

    t = 0.0
    for car in range(warmup_cars + n_cars):
        t += demand.exponential(1.0 / arrival_rate)
        stay = demand.gamma(stay_shape, mean_stay / stay_shape)
        while departures and departures[0][0] <= t:
            _, gone = heapq.heappop(departures)
            for name in STRATEGIES:
                occ[name][spot_of[name].pop(gone)] = False

        in_lot = len(departures)
        admitted = in_lot < model.n
        k = car - warmup_cars
        for name, choose in STRATEGIES.items():
            spot, driven = choose(occ[name], model, search_rng[name], p)
            assert (spot >= 0) == admitted
            if admitted:
                occ[name][spot] = True
                spot_of[name][car] = spot
            if k >= 0:
                search[name][k] = driven
                exit_[name][k] = model.dist[spot] if admitted else 0.0
        if admitted:
            heapq.heappush(departures, (t + stay, car))
        if k >= 0:
            parked[k] = admitted
            occupancy[k] = in_lot / model.n   # Poisson arrivals see the time average (PASTA)
    return Replication(search, exit_, parked, occupancy)


def summarize(rep):
    """Averages of one replication: per parked car, plus the cars that found the lot full."""
    out = {"occupancy": rep.occupancy.mean(), "turned_away_share": 1.0 - rep.parked.mean()}
    full = ~rep.parked
    for name in STRATEGIES:
        out[f"{name}_search_m"] = rep.search[name][rep.parked].mean()
        out[f"{name}_exit_m"] = rep.exit[name][rep.parked].mean()
        out[f"{name}_full_lot_m"] = rep.search[name][full].mean() if full.any() else 0.0
    return out


def erlang_b(servers, load):
    """Probability that an arriving car finds every stall taken (Erlang loss formula, M/G/c/c)."""
    b = 1.0
    for k in range(1, servers + 1):
        b = load * b / (k + load * b)
    return b


def arrival_rate_for(occupancy, servers, mean_stay):
    """Poisson arrival rate (cars/min) whose long-run occupancy equals `occupancy`.

    Occupancy = carried load / stalls = load * (1 - ErlangB) / stalls, which
    grows with the offered load, so a bisection finds the load.
    """
    lo, hi = 0.0, 1000.0 * servers
    for _ in range(200):
        load = (lo + hi) / 2
        if load * (1 - erlang_b(servers, load)) / servers < occupancy:
            lo = load
        else:
            hi = load
    return load / mean_stay


def to_costs(metres, values, speed_mps):
    """Metres driven -> seconds, litres of fuel, kg CO2, fuel cost and time value (ILS)."""
    seconds = metres / speed_mps
    hours = seconds / 3600
    litres = hours * values["fuel_rate_l_per_h"]
    return {
        "seconds": seconds,
        "litres": litres,
        "co2_kg": litres * values["co2_kg_per_l"],
        "fuel_ils": litres * values["fuel_price_ils_per_l"],
        "time_ils": hours * values["value_of_time_ils_per_h"],
    }
