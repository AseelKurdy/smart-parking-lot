"""Parking-lot geometry and the A* distance, ported from the ESP32 firmware.

demo_lot() reproduces the map of the prototype firmware before the physical
model was measured (the MAP0/MAP1 grid of v7-v9: one lane per floor, ramp at
the far end, same step costs, same heuristic), so its distances can be checked
against what that firmware printed at boot. The current firmware uses the road
network measured on the 1:100 model instead (distances in cm); the simulation
keeps real-world garage dimensions in metres on purpose. garage() builds a
larger multi-storey lot in the same map format for the report's main scenario.

Map cells: '.' lane   'P' parking stall   'E' entrance   'R' ramp   '#' blocked

Every lot also has a `route`: the order in which a driver with no information
searches it (see garage()). A stall is entered only sideways from the lane
next to it, as in 90-degree parking.
"""
import heapq
from dataclasses import dataclass

DIRECTIONS = ((-1, 0), (1, 0), (0, -1), (0, 1))


@dataclass
class Lot:
    name: str
    floors: list          # floors[f][r] is one row of the map, as a string
    entrance: tuple       # (floor, row, col)
    ramps: list           # two-way links between floors: [((f, r, c), (f2, r2, c2)), ...]
    spots: list           # stall cells, in spot-index order
    route: list           # lane cells in the order a driver without information searches them
    lane_step: float      # row move: from one stall to the next along the lane (m)
    lane_to_stall: float  # column move: from the lane into a stall (m)
    ramp_length: float    # one ramp between two floors (m)

    def cell(self, node):
        f, r, c = node
        return self.floors[f][r][c]

    def neighbours(self, node):
        f, r, c = node
        rows, cols = len(self.floors[f]), len(self.floors[f][0])
        for dr, dc in DIRECTIONS:
            nr, nc = r + dr, c + dc
            if not (0 <= nr < rows and 0 <= nc < cols):
                continue
            ch = self.floors[f][nr][nc]
            if ch == "#" or (ch == "P" and dr):
                continue  # blocked, or a stall approached from its front/back instead of from the lane
            yield (f, nr, nc), (self.lane_step if dr else self.lane_to_stall)
        for a, b in self.ramps:
            if node == a:
                yield b, self.ramp_length
            elif node == b:
                yield a, self.ramp_length

    def step(self, a, b):
        """Cost of one move between neighbouring cells a and b."""
        if a[0] != b[0]:
            return self.ramp_length
        return self.lane_step if a[1] != b[1] else self.lane_to_stall

    def access(self, spot):
        """The lane cell a stall is entered from."""
        f, r, c = spot
        for nc in (c - 1, c + 1):
            if 0 <= nc < len(self.floors[f][r]) and self.floors[f][r][nc] not in "P#":
                return (f, r, nc)
        raise ValueError(f"stall {spot} has no lane next to it")


def heuristic(lot, node, goal):
    """Weighted Manhattan distance + one ramp per floor of difference (as in the firmware)."""
    return (abs(node[1] - goal[1]) * lot.lane_step
            + abs(node[2] - goal[2]) * lot.lane_to_stall
            + abs(node[0] - goal[0]) * lot.ramp_length)


def a_star(lot, goal, start=None):
    """Driving distance in metres from start (default: the entrance) to goal; -1 if unreachable.

    Same rules as the firmware: a stall can only be entered as the destination,
    and nodes may be reopened (no closed set). The open list is a heap instead
    of the firmware's linear scan, which changes the speed, not the result.
    """
    start = start or lot.entrance
    g = {start: 0.0}
    heap = [(heuristic(lot, start, goal), start)]
    while heap:
        f, node = heapq.heappop(heap)
        if f > g[node] + heuristic(lot, node, goal):
            continue  # stale entry: this node was reached more cheaply later
        if node == goal:
            return g[node]
        for nxt, step in lot.neighbours(node):
            if lot.cell(nxt) == "P" and nxt != goal:
                continue  # no driving through a stall
            t = g[node] + step
            if t < g.get(nxt, float("inf")):
                g[nxt] = t
                heapq.heappush(heap, (t + heuristic(lot, nxt, goal), nxt))
    return -1.0


def dijkstra(lot, start=None):
    """Shortest distance from start to every reachable cell (checks A*, and feeds the random searcher)."""
    start = start or lot.entrance
    dist = {start: 0.0}
    heap = [(0.0, start)]
    while heap:
        d, node = heapq.heappop(heap)
        if d > dist[node]:
            continue
        if lot.cell(node) == "P" and node != start:
            continue  # a stall is a dead end
        for nxt, step in lot.neighbours(node):
            if d + step < dist.get(nxt, float("inf")):
                dist[nxt] = d + step
                heapq.heappush(heap, (d + step, nxt))
    return dist


def demo_lot(lane_step, lane_to_stall, ramp_length):
    """The 14-spot, 2-floor lot of the earlier ESP32 demo firmware (its MAP0 / MAP1 grid, before the model was measured)."""
    map0 = ["RP", ".P", ".P", ".P", ".P", ".P", ".P", "E."]   # floor 0: stalls in column 1
    map1 = ["PR", "P.", "P.", "P.", "P.", "P.", "P.", ".."]   # floor 1: stalls in column 0
    spot_fl = [0] * 7 + [1] * 7
    spot_r = [6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6]
    spot_c = [1] * 7 + [0] * 7
    # one lane per floor: up floor 0 to the ramp, then down floor 1
    route = [(0, r, 0) for r in range(7, -1, -1)] + [(1, r, 1) for r in range(0, 7)]
    return Lot(
        name="Demo lot (14 stalls, 2 floors)",
        floors=[map0, map1],
        entrance=(0, 7, 0),
        ramps=[((0, 0, 0), (1, 0, 1))],
        spots=list(zip(spot_fl, spot_r, spot_c)),
        route=route,
        lane_step=lane_step, lane_to_stall=lane_to_stall, ramp_length=ramp_length,
    )


def garage(floors, aisles, rows, lane_step, lane_to_stall, ramp_length):
    """Multi-storey garage: per floor, parallel two-way aisles with stalls on both sides,
    joined at both ends by cross aisles (the usual car-park layout).

    The search route is how drivers without information search: aisle by aisle
    in a serpentine (up the first aisle, along the cross aisle, down the next,
    ...), then up the ramp where the floor's route ends and the same on the
    next floor in the opposite direction.
    """
    cols = 3 * aisles
    front, back = 0, rows + 1
    floor_map = ["." * cols] + ["P.P" * aisles] * rows + ["." * cols]   # cross aisle, stall rows, cross aisle
    aisle_cols = [3 * k + 1 for k in range(aisles)]

    entrance = (0, front, aisle_cols[0])
    route, ramps, r = [entrance], [], front
    for f in range(floors):
        order = aisle_cols if f % 2 == 0 else aisle_cols[::-1]
        for i, c in enumerate(order):
            end = back if r == front else front
            step = 1 if end > r else -1
            route += [(f, rr, c) for rr in range(r + step, end + step, step)]   # along the aisle
            r = end
            if i + 1 < len(order):
                nc = order[i + 1]
                s = 1 if nc > c else -1
                route += [(f, r, cc) for cc in range(c + s, nc + s, s)]       # along the cross aisle
        if f + 1 < floors:
            here = route[-1]
            landing = (f + 1, here[1], here[2])
            ramps.append((here, landing))
            route.append(landing)

    spots = [(f, rr, c) for f in range(floors) for rr in range(1, rows + 1)
             for c in range(cols) if floor_map[rr][c] == "P"]
    return Lot(
        name=f"Garage ({len(spots)} stalls, {floors} floors x {aisles} aisles)",
        floors=[list(floor_map) for _ in range(floors)],
        entrance=entrance,
        ramps=ramps,
        spots=spots,
        route=route,
        lane_step=lane_step, lane_to_stall=lane_to_stall, ramp_length=ramp_length,
    )
