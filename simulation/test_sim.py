"""Checks that the simulation matches the firmware and basic queueing theory.

Run from this folder:  python -m unittest -v
"""
import unittest

import numpy as np

from lot import a_star, demo_lot, dijkstra, garage
from params import PARAMS, derived
from sim import LotModel, arrival_rate_for, erlang_b, simulate

D = derived()
SOURCED = (D["lane_step_m"], D["lane_to_stall_m"], D["ramp_length_m"])
MEAN_STAY = PARAMS["mean_stay_min"].value
SHAPE = PARAMS["stay_gamma_shape"].value


def main_garage():
    return garage(int(PARAMS["garage_floors"].value), int(PARAMS["garage_aisles_per_floor"].value),
                  int(PARAMS["garage_rows_per_aisle"].value), *SOURCED)


class Distances(unittest.TestCase):
    def test_matches_firmware_v7_boot_output(self):
        lot = demo_lot(2.0, 3.0, 10.0)   # the constants the ESP32 used in v7
        self.assertEqual([a_star(lot, s) for s in lot.spots],
                         [5, 7, 9, 11, 13, 15, 17, 27, 29, 31, 33, 35, 37, 39])

    def test_sourced_constants(self):
        lot = demo_lot(*SOURCED)
        dist = [a_star(lot, s) for s in lot.spots]
        for spot, metres in ((0, 7.9), (6, 22.9), (7, 45.9), (13, 60.9)):
            self.assertAlmostEqual(dist[spot], metres)

    def test_a_star_is_optimal(self):
        # the heuristic is admissible, so A* must agree with a plain Dijkstra search
        for lot in (demo_lot(*SOURCED), main_garage()):
            ref = dijkstra(lot)
            for s in lot.spots:
                self.assertAlmostEqual(a_star(lot, s), ref[s])

    def test_search_route_is_drivable_and_passes_every_stall(self):
        for lot in (demo_lot(*SOURCED), main_garage()):
            self.assertEqual(lot.route[0], lot.entrance)
            for a, b in zip(lot.route, lot.route[1:]):
                self.assertIn(b, dict(lot.neighbours(a)))
            self.assertTrue({lot.access(s) for s in lot.spots} <= set(lot.route))

    def test_garage_size(self):
        self.assertEqual(len(main_garage().spots), 216)


class Queueing(unittest.TestCase):
    model = LotModel(demo_lot(*SOURCED))

    def test_erlang_b_known_values(self):
        self.assertAlmostEqual(erlang_b(1, 1.0), 0.5)
        self.assertAlmostEqual(erlang_b(2, 1.0), 0.2)

    def test_blocking_matches_erlang_b(self):
        load = 12.0
        reps = [simulate(self.model, load / MEAN_STAY, MEAN_STAY, SHAPE, 0.5, 4000, 500, seed)
                for seed in range(5)]
        blocked = np.mean([1 - r.parked.mean() for r in reps])
        self.assertAlmostEqual(blocked, erlang_b(self.model.n, load), delta=0.02)

    def test_arrival_rate_hits_target_occupancy(self):
        rate = arrival_rate_for(0.85, self.model.n, MEAN_STAY)
        reps = [simulate(self.model, rate, MEAN_STAY, SHAPE, 0.5, 4000, 500, seed) for seed in range(5)]
        self.assertAlmostEqual(np.mean([r.occupancy.mean() for r in reps]), 0.85, delta=0.02)


class Strategies(unittest.TestCase):
    def test_single_lane_lot_gives_no_advantage_to_a_driver_who_sees_everything(self):
        # in the demo lot the search route passes the stalls in distance order, so a
        # driver who notices every free stall (p = 1) finds the one the system assigns
        model = LotModel(demo_lot(*SOURCED))
        rate = arrival_rate_for(0.9, model.n, MEAN_STAY)
        rep = simulate(model, rate, MEAN_STAY, SHAPE, 1.0, 2000, 500, seed=1)
        np.testing.assert_allclose(rep.search["cruising"][rep.parked], rep.search["assigned"][rep.parked])

    def test_searching_driver_never_beats_the_shortest_path(self):
        model = LotModel(main_garage())
        self.assertTrue(np.all(model.pos + model.depth >= model.dist - 1e-9))

    def test_same_seed_same_result(self):
        model = LotModel(demo_lot(*SOURCED))
        a, b = (simulate(model, 0.15, MEAN_STAY, SHAPE, 0.5, 1000, 200, seed=7) for _ in range(2))
        for name in a.search:
            np.testing.assert_array_equal(a.search[name], b.search[name])


if __name__ == "__main__":
    unittest.main()
