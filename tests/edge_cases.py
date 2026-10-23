"""Targeted edge cases for the v9 picture / payload code (host simulator, batch mode).

  python edge_cases.py [--text-sim path\\sim_text.exe]

E1  three-digit car ids (up to 499) must not break the alignment of the text picture
E2  more cars waiting than the payload lists (cap of 16): waitN is the true total, the list is capped, JSON stays valid
E3  worst-case payload size (14 wrong cars + 16 waiting): must stay small enough for the ESP32 heap and one HTTP reply
E4  the picture and the payload describe the same lot after a nasty mixed sequence (full lot, wrong parks, expiries)
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

CELL = re.compile(r'\|\s*(\d{1,2})\*?\s+(free|>> NEXT <<|//BLOCKED//|RES #(\d+) \d+s|\[#(\d+) (OK|WRONG)\])\s*\|')


def run(exe, lines):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write('\n'.join(lines) + '\n')
        p = f.name
    try:
        return subprocess.run([exe, '--script', p], stdout=subprocess.PIPE, timeout=300).stdout.decode('ascii', 'replace')
    finally:
        os.unlink(p)


def last_status_block(out):
    """The lines printed by the last `status` command."""
    blocks = out.split('>>>>>>>> status')
    return blocks[-1].split('>>>>>>>>')[0].splitlines()


def picture_cells(block):
    cells = {}
    for l in block:
        for m in CELL.finditer(l):
            cells[int(m.group(1))] = m.group(2)
    return cells


results = []


def check(name, ok, detail=''):
    results.append((name, ok, detail))
    print('%-5s %s%s' % ('PASS' if ok else 'FAIL', name, ('   -> ' + detail) if (detail and not ok) else ''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--text-sim', default=os.path.join(os.environ.get('TEMP', '.'), 'parking_sim', 'sim_text.exe'))
    a = ap.parse_args()
    sim = a.text_sim

    # ---- E1: three-digit ids -------------------------------------------------------------------------
    lines = []
    for i in range(480):                     # burn ids 0..479
        lines += ['> new', '> leave %d' % i]
    for i in range(14):                      # cars 480..493 fill the lot
        lines.append('> new')
    lines += ['> park 3 480', '> park 9 485', '> leave 493', '> status']      # wrong parks, a free spot, reserved cars
    out = run(sim, lines)
    blk = last_status_block(out)
    pic = [l for l in blk if l.startswith(('|', '+'))]
    widths = sorted(set(len(l) for l in pic))
    check('E1 picture lines all have the same width with 3-digit ids', len(widths) == 1, 'widths %s' % widths)
    cells = picture_cells(blk)
    check('E1 all 14 stall cells still parse', len(cells) == 14, 'parsed %d cells: %s' % (len(cells), sorted(cells)))
    check('E1 3-digit car id shown in full', any('480' in c for c in cells.values()) and any('#49' in c for c in cells.values()),
          str(cells))
    wl = [l for l in blk if l.strip().startswith('WAITING:')]
    check('E1 WAITING line lists 3-digit ids', bool(wl) and '#4' in wl[0], str(wl))

    # ---- E2/E3: many cars waiting ----------------------------------------------------------------------
    lines = ['> new'] * 14                                       # cars 0..13 reserve every spot
    lines += ['@advance 61000', '@tick']                         # all 14 reservations expire; the cars stay in the lot
    lines += ['> new'] * 14                                      # 14 more cars (ids 14..27) reserve the spots again
    lines += ['> park 7 20', '> park 8 21', '> park 9 22']       # some wrong parks on top
    lines += ['@jsonout', '> status']
    out = run(sim, lines)
    js = [l[7:] for l in out.splitlines() if l.startswith('[JSON] ')][-1]
    try:
        j = json.loads(js)
        check('E2 payload with 28 waiting cars is valid JSON', True)
        check('E2 waitN is the true total (25 = 28 cars - 3 parked)', j['waitN'] == 25, 'waitN=%s' % j['waitN'])
        check('E2 list is capped at 16 entries', len(j['wait']) == 16, 'len=%d' % len(j['wait']))
        check('E2 list is in arrival order, oldest first', [w['car'] for w in j['wait']] == sorted(w['car'] for w in j['wait']))
        check('E2 expired cars show spot -1 / 0 s left', all(w['spot'] == -1 and w['left'] == 0 for w in j['wait'] if w['car'] < 14),
              str([w for w in j['wait'] if w['car'] < 14][:2]))
        check('E3 worst-case payload is small (< 4 KB)', len(js) < 4096, '%d bytes' % len(js))
        print('      (payload with 14 spots and 16 waiting cars: %d bytes)' % len(js))
    except ValueError as e:
        check('E2 payload with 28 waiting cars is valid JSON', False, str(e))
    blk = last_status_block(out)
    wl = [l for l in blk if l.strip().startswith('WAITING:')]
    check('E2 text WAITING line caps at 5 and says how many more', bool(wl) and '+20 more' in wl[0], str(wl))

    # ---- E4: nasty mixed sequence, picture == payload ----------------------------------------------------
    lines = ['> new'] * 16                                       # 14 cars + 2 refused (lot full)
    lines += ['> park 13 0', '> park 0 13', '> park 5 5', '> leave 6', '> new', '> park 6 14', '> park 6 14',
              '@advance 30000', '> new', '@advance 31000', '@tick', '> leave 99', '> status', '@jsonout']
    out = run(sim, lines)
    blk = last_status_block(out)
    cells = picture_cells(blk)
    j = json.loads([l[7:] for l in out.splitlines() if l.startswith('[JSON] ')][-1])
    kinds = {0: 'free', 1: 'RES', 2: 'OCC', 3: 'BLK'}
    bad = []
    for i, p in enumerate(j['sp']):
        c = cells.get(i, '?')
        if p['s'] == 2:
            want = '[#%d %s]' % (p['car'], 'OK' if p['ok'] else 'WRONG')
        elif p['s'] == 1:
            want = None
            if not c.startswith('RES #%d ' % p['car']):
                bad.append((i, c, p))
            continue
        elif p['s'] == 3:
            want = '//BLOCKED//'
        else:
            want = None
            if c not in ('free', '>> NEXT <<'):
                bad.append((i, c, p))
            continue
        if want != c:
            bad.append((i, c, p))
    check('E4 picture and payload agree on all 14 spots', not bad, str(bad[:2]))
    check('E4 exactly one >> NEXT << marker, and it is json.next', list(cells.values()).count('>> NEXT <<') == (1 if j['next'] >= 0 else 0)
          and (j['next'] < 0 or cells.get(j['next']) == '>> NEXT <<'), 'next=%s cells=%s' % (j['next'], cells))

    failed = [r for r in results if not r[1]]
    print('\n%d/%d edge-case checks passed' % (len(results) - len(failed), len(results)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
