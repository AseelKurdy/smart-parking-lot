"""Model-based fuzz of the parking firmware through the host simulator (batch mode, fake clock, no sleeping).

An independent Python re-implementation of the rules (nearest free spot, reservations with a 60 s timeout, a car
that takes someone else's spot -> the displaced car is rerouted or leaves, ids never reused, 500-car id limit ...)
generates random operations. After EVERY operation the firmware's web payload (@jsonout = buildStateJson) must equal
the model: each spot's state / car / OK flag / seconds left, the next recommended spot, the waiting list and the
`status` counter. The spot chosen by every `new` is checked from the log as well.

  python fuzz_model.py [--sim path\\to\\sim.exe] [--seeds 25] [--ops 600] [--v8-log-diff <exe>]

--v8-log-diff <exe>: also run every script through an old v8 build (a harness that accepts the same script lines)
and compare the event log lines ([ENTRY] [PARK] [LEAVE] [REROUTE] [TIMEOUT] [NEXT] ...) with the new firmware's.
Exit code 0 = no divergence.
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile

TIMEOUT_MS = 60000
SPOTS = 14
MAX_CARS = 500
MAX_WAIT_LISTED = 16


class Model(object):
    def __init__(self):
        self.next_id = 0
        self.in_lot = set()
        self.res = {}      # spot -> (car, reserved_at_ms)
        self.occ = {}      # spot -> (car, correct)
        self.now = 0
        self.seq = 0

    def copy(self):
        c = Model()
        c.next_id, c.in_lot, c.now, c.seq = self.next_id, set(self.in_lot), self.now, self.seq
        c.res, c.occ = dict(self.res), dict(self.occ)
        return c

    def free_spot(self):
        for i in range(SPOTS):
            if i not in self.occ and i not in self.res:
                return i
        return -1

    def reserve(self, car):
        s = self.free_spot()
        if s >= 0:
            self.res[s] = (car, self.now)
        return s

    def spot_of_res(self, car):
        for s, (c, _) in self.res.items():
            if c == car:
                return s
        return -1

    def spot_of_occ(self, car):
        for s, (c, _) in self.occ.items():
            if c == car:
                return s
        return -1

    def new(self):
        """Returns the spot the new car is sent to, or None if the car is refused (lot full / id limit)."""
        if self.free_spot() < 0 or self.next_id >= MAX_CARS:
            return None
        cid = self.next_id
        self.next_id += 1
        self.in_lot.add(cid)
        return self.reserve(cid)

    def park(self, spot, car):
        if spot < 0 or spot >= SPOTS or car < 0 or car not in self.in_lot:
            return
        if self.spot_of_occ(car) >= 0 or spot in self.occ:
            return
        own = self.spot_of_res(car)
        displaced = self.res[spot][0] if (spot in self.res and self.res[spot][0] != car) else -1
        if own >= 0:
            del self.res[own]
        self.res.pop(spot, None)
        self.occ[spot] = (car, own == spot)
        if displaced >= 0 and self.reserve(displaced) < 0:
            self.in_lot.discard(displaced)          # nowhere to go: the displaced car leaves the lot

    def leave(self, car):
        if car < 0 or car not in self.in_lot:
            return
        at = self.spot_of_occ(car)
        if at >= 0:
            del self.occ[at]
        else:
            r = self.spot_of_res(car)
            if r >= 0:
                del self.res[r]
        self.in_lot.discard(car)

    def advance(self, ms):
        self.now += ms
        for s in [s for s, (_, t) in self.res.items() if self.now - t >= TIMEOUT_MS]:
            del self.res[s]                          # the car stays in the lot, only its reservation expires

    def left(self, s):
        age = self.now - self.res[s][1]
        return 0 if age >= TIMEOUT_MS else (TIMEOUT_MS - age) // 1000

    def waiting(self):
        parked = set(c for c, _ in self.occ.values())
        return sorted(c for c in self.in_lot if c not in parked)

    def compare(self, js):
        """Differences between the firmware payload and the model (empty list = identical)."""
        d, sp = [], js['sp']
        for i in range(SPOTS):
            if i in self.occ:
                want = (2, self.occ[i][0], 1 if self.occ[i][1] else 0, 0)
                got = (sp[i]['s'], sp[i]['car'], sp[i]['ok'], sp[i]['left'])
            elif i in self.res:
                want = (1, self.res[i][0], self.left(i))
                got = (sp[i]['s'], sp[i]['car'], sp[i]['left'])
            else:
                want = (0, -1, 0)
                got = (sp[i]['s'], sp[i]['car'], sp[i]['left'])
            if want != got:
                d.append('spot %d: model %s firmware %s' % (i, want, got))
        if js['next'] != self.free_spot():
            d.append('next: model %d firmware %d' % (self.free_spot(), js['next']))
        w = self.waiting()
        if js['waitN'] != len(w):
            d.append('waitN: model %d firmware %d' % (len(w), js['waitN']))
        want_w = []
        for c in w[:MAX_WAIT_LISTED]:
            s = self.spot_of_res(c)
            want_w.append((c, s, self.left(s) if s >= 0 else 0))
        got_w = [(x['car'], x['spot'], x['left']) for x in js['wait']]
        if want_w != got_w:
            d.append('wait list: model %s firmware %s' % (want_w, got_w))
        if js['seq'] != self.seq:
            d.append('seq: model %d firmware %d' % (self.seq, js['seq']))
        return d


JUNK = ['zzz', 'new 1', 'new 5 6', 'status 1', 'park', 'park 1', 'park a b', 'park 1 2 3', 'park 99999999999 1',
        'leave', 'leave x', 'leave 1 2', 'cancel', 'cancel q', 'check 0', 'park 1.5 2', 'park 0x1 1', 'leave -',
        '   ', 'PARK 1', 'help', 'a' * 20, 'leave 000000000000000000']


def make_script(seed, nops):
    """Returns (script lines, [(description, expected model after the op, expected spot for 'new' or 'n/a')])."""
    rng = random.Random(seed)
    m = Model()
    lines, snaps = [], []

    def after(desc, new_spot='n/a'):
        lines.append('@jsonout')
        snaps.append((desc, m.copy(), new_spot))

    for _ in range(nops):
        r = rng.random()
        cars = sorted(m.in_lot)
        if r < 0.26:
            lines.append('> new')
            after('new', m.new())
        elif r < 0.55:
            car = rng.choice(cars) if cars and rng.random() < 0.85 else rng.randint(-1, m.next_id + 2)
            own = m.spot_of_res(car)
            spot = own if (own >= 0 and rng.random() < 0.4) else rng.randint(-1, SPOTS)
            lines.append('> park %d %d' % (spot, car))
            m.park(spot, car)
            after('park %d %d' % (spot, car))
        elif r < 0.75:
            car = rng.choice(cars) if cars and rng.random() < 0.9 else rng.randint(-1, m.next_id + 2)
            verb = 'leave' if rng.random() < 0.7 else 'cancel'
            lines.append('> %s %d' % (verb, car))
            m.leave(car)
            after('%s %d' % (verb, car))
        elif r < 0.84:
            verb = rng.choice(['status', 'STATUS', 'Status'])
            lines.append('> ' + verb)
            m.seq += 1
            after(verb)
        elif r < 0.92:
            junk = rng.choice(JUNK)
            lines.append('> ' + junk)
            after('junk %r' % junk)
        else:
            ms = rng.choice([1000, 5000, 20000, 39000, 59000, 59999, 60000, 61000, 90000])
            lines += ['@advance %d' % ms, '@tick']
            m.advance(ms)
            after('advance %d ms' % ms)
    return lines, snaps


def run_script(exe, lines):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write('\n'.join(lines) + '\n')
        path = f.name
    try:
        cmd = [exe, '--script', path] if os.path.basename(exe).startswith('sim_') else [exe, path]
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600).stdout.decode('ascii', 'replace')
    finally:
        os.unlink(path)


def verify(out, snaps, label):
    """Compare the payload after every op with the model, and the spot of every accepted `new` with the log."""
    jsons = [l[7:] for l in out.splitlines() if l.startswith('[JSON] ')]
    if len(jsons) != len(snaps):
        return '%s: %d payloads for %d operations (firmware crashed or printed garbage?)' % (label, len(jsons), len(snaps))
    entries = [int(x) for x in re.findall(r'^\[ENTRY\]\s+car #\d+\s+-> go to spot (\d+) ', out, re.M)]
    want_entries = [ns for (_, _, ns) in snaps if ns not in ('n/a', None)]
    for i, ((desc, model, _), js) in enumerate(zip(snaps, jsons)):
        try:
            payload = json.loads(js)
        except ValueError as e:
            return '%s op #%d (%s): payload is not valid JSON (%s)' % (label, i, desc, e)
        diff = model.compare(payload)
        if diff:
            return '%s op #%d (%s) diverged: %s' % (label, i, desc, '; '.join(diff[:3]))
    if entries != want_entries:
        return '%s: spots chosen by `new` differ from the model: firmware %s model %s' % (label, entries[:12], want_entries[:12])
    return None


KEEP = ('[ENTRY]', '[PARK]', '[LEAVE]', '[REROUTE]', '[TIMEOUT]', '[NEXT]', '[ERROR]', '[HELP]', '[STATUS]', '[SENSOR]', '[WARN]', '[NOTE]')


def events(out):
    """Event log lines printed in response to the script (everything after the first command; boot output differs by design)."""
    body = out.partition('\n>>>>>>>> ')[2]
    return [l for l in body.splitlines() if l.startswith(KEEP)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sim', default=os.path.join(os.environ.get('TEMP', '.'), 'parking_sim', 'sim_noweb.exe'))
    ap.add_argument('--seeds', type=int, default=25)
    ap.add_argument('--ops', type=int, default=600)
    ap.add_argument('--first-seed', type=int, default=1)
    ap.add_argument('--v8-log-diff', default='')
    a = ap.parse_args()
    bad, total = 0, 0
    for seed in range(a.first_seed, a.first_seed + a.seeds):
        lines, snaps = make_script(seed, a.ops)
        total += len(snaps)
        out = run_script(a.sim, lines)
        err = verify(out, snaps, 'seed %d' % seed)
        if err:
            bad += 1
            print('FAIL', err)
            continue
        if a.v8_log_diff:
            e9 = events(out)
            e8 = events(run_script(a.v8_log_diff, lines))
            if e9 != e8:
                bad += 1
                for i, (x, y) in enumerate(zip(e8, e9)):
                    if x != y:
                        print('LOG DIFF seed %d at event %d:\n   old: %s\n   new: %s' % (seed, i, x, y))
                        break
                else:
                    print('LOG DIFF seed %d: %d old events vs %d new' % (seed, len(e8), len(e9)))

    # id limit: 520 new+leave cycles, the firmware must refuse cleanly after 500 and stay consistent
    m, lines, snaps = Model(), [], []
    for _ in range(520):
        lines.append('> new')
        lines.append('@jsonout')
        snaps.append(('new', None, m.new()))
        snaps[-1] = ('new', m.copy(), snaps[-1][2])
        last = m.next_id - 1
        lines += ['> leave %d' % last, '@jsonout']
        m.leave(last)
        snaps.append(('leave %d' % last, m.copy(), 'n/a'))
    out = run_script(a.sim, lines)
    err = verify(out, snaps, 'id-limit run')
    if not err and 'car id limit reached' not in out:
        err = 'id-limit run: the "car id limit reached" error never appeared'
    if err:
        bad += 1
        print('FAIL', err)
    total += len(snaps)
    print('%d operations over %d seeds + id-limit run: %s' % (total, a.seeds, 'every payload matches the model' if not bad else '%d PROBLEM(S)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
