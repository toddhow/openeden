"""Foot slip per scenario phase from an openmw.log written with `creativemotion animation debug = true` (task 0107).

For each frame of the player the ankles' world positions (CMFEET_L, CMFEET_R) give a horizontal foot velocity. A foot
is *planted* when its ankle height above the actor's origin is within PLANT_MARGIN of that foot's low level (the 5th
percentile over the run); a planted foot should not move over the ground. The slip is the foot's horizontal speed in
those frames, in units per second (Morrowind units, about 1.4 cm each).

The ankles are taken from the CMFEETIK lines (after the leg solve of foot planting and foot lock) when the log has
them, else from CMFEET_L/R of the CMTRACE line (the animated pose).

Usage: python -I slip.py <openmw.log> [--csv out.csv] [--margin units]
"""
import math
import re
import sys

PLANT_MARGIN = 1.5
LINE = re.compile(r'CMTRACE ref=(\S+) dt=([\d.]+).*?pos=\(([-\d. ]+)\) moved=\(([-\d. ]+)\)'
                  r'.*?loco=\(speed ([\d.]+) phase (\d+)\) animspeed=([\d.]+)')
FOOT = re.compile(r'CMFEET_([LR])=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)')
YAW = re.compile(r' yaw=([-\d.]+)')
GROUP0 = re.compile(r' g0=(\S+?)[ (]')
PHASE = re.compile(r'CMPHASE (\S+) t=')
IKFEET = re.compile(r'CMFEETIK L=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) R=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)')


def pct(values, q):
    if not values:
        return float('nan')
    s = sorted(values)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))]


def parse(path):
    frames = []
    phase = 'pre'
    for raw in open(path, errors='replace'):
        m = PHASE.search(raw)
        if m:
            phase = m.group(1)
            continue
        k = IKFEET.search(raw)
        if k and frames and not frames[-1].get('ik'):
            # the ankles after the leg solve replace the animated ones of the frame just read (same frame)
            v = [float(x) for x in k.groups()]
            frames[-1]['feet'] = {'L': tuple(v[0:3]), 'R': tuple(v[3:6])}
            frames[-1]['ik'] = True
            continue
        m = LINE.search(raw)
        if not m or 'player' not in m.group(1).lower():
            continue
        feet = {k: (float(x), float(y), float(z)) for k, x, y, z in FOOT.findall(raw)}
        if len(feet) != 2:
            continue
        pos = [float(v) for v in m.group(3).split()]
        yaw = YAW.search(raw)
        g0 = GROUP0.search(raw)
        frames.append(dict(phase=phase, dt=float(m.group(2)), pos=pos, loco=float(m.group(5)),
                           locophase=int(m.group(6)), anim=float(m.group(7)), feet=feet,
                           yaw=float(yaw.group(1)) if yaw else 0.0, group=g0.group(1) if g0 else '?'))
    return frames


TELEPORT_JUMP = 20.0 # an actor origin moving farther than this in one frame was teleported: the frame is skipped


def analyse(frames, margin=PLANT_MARGIN):
    """One row per frame of a phase: the stance slip. Of the ankles that are low (within `margin` of that ankle's
    own low level, the 5th percentile over the run) the slower one is the stance foot; its horizontal speed over the
    ground is the slip (zero for a foot that holds still). Frames with no low ankle (a flight phase) and frames
    around a teleport have no row. `along` and `lateral` split the stance foot's velocity along and across the
    actor's facing."""
    low = {}
    for foot in 'LR':
        low[foot] = pct([f['feet'][foot][2] - f['pos'][2] for f in frames], 0.05)
    rows = []
    for a, b in zip(frames, frames[1:]):
        dt = b['dt']
        if dt <= 0 or a['phase'] != b['phase']:
            continue
        jump = math.hypot(b['pos'][0] - a['pos'][0], b['pos'][1] - a['pos'][1])
        if jump > TELEPORT_JUMP:
            continue
        yaw = b['yaw']
        fwd = (math.sin(yaw), math.cos(yaw))
        right = (math.cos(yaw), -math.sin(yaw))
        best = None
        for foot in 'LR':
            if b['feet'][foot][2] - b['pos'][2] > low[foot] + margin:
                continue
            vx = (b['feet'][foot][0] - a['feet'][foot][0]) / dt
            vy = (b['feet'][foot][1] - a['feet'][foot][1]) / dt
            speed = math.hypot(vx, vy)
            if best is None or speed < best[0]:
                best = (speed, vx * fwd[0] + vy * fwd[1], vx * right[0] + vy * right[1], foot)
        if best is None:
            continue
        rows.append(dict(phase=b['phase'], foot=best[3], planted=True, slip=best[0], along=best[1], lateral=best[2],
                         body=jump / dt, loco=b['loco'], anim=b['anim'], group=b['group']))
    return rows, low


def main(argv):
    margin = PLANT_MARGIN
    csv = None
    args = [a for a in argv[1:]]
    if '--margin' in args:
        i = args.index('--margin')
        margin = float(args[i + 1])
        del args[i:i + 2]
    if '--csv' in args:
        i = args.index('--csv')
        csv = args[i + 1]
        del args[i:i + 2]
    frames = parse(args[0])
    if not frames:
        print('no player CMTRACE frames with feet in', args[0])
        return 1
    rows, low = analyse(frames, margin)
    if csv:
        with open(csv, 'w') as out:
            out.write('phase,foot,planted,slip,body,loco,anim,group\n')
            for r in rows:
                out.write('%s,%s,%d,%.3f,%.3f,%.3f,%.4f,%s\n' % (r['phase'], r['foot'], r['planted'], r['slip'],
                                                                r['body'], r['loco'], r['anim'], r['group']))
    print('frames %d, foot low level L %.2f R %.2f, plant margin %.1f' % (len(frames), low['L'], low['R'], margin))
    print('%-14s %6s %8s %8s %8s %8s %8s %8s' % ('phase', 'planted', 'body u/s', 'slip p50', 'slip p75', 'along', 'lateral', 'ratio50'))
    order = []
    for r in rows:
        if r['phase'] not in order:
            order.append(r['phase'])
    total = []
    for ph in order:
        sel = [r for r in rows if r['phase'] == ph and r['planted']]
        allp = [r for r in rows if r['phase'] == ph]
        if not allp:
            continue
        body = sum(r['body'] for r in allp) / len(allp)
        if not sel:
            print('%-14s %6d %8.1f %8s' % (ph, 0, body, '-'))
            continue
        sl = [r['slip'] for r in sel]
        total += sl if ph not in ('idle', 'end', 'pre') else []
        avg = pct(sl, 0.5)
        al = sum(abs(r['along']) for r in sel) / len(sel)
        la = sum(abs(r['lateral']) for r in sel) / len(sel)
        print('%-14s %6d %8.1f %8.1f %8.1f %8.1f %8.1f %8.2f' % (ph, len(sel), body, avg, pct(sl, 0.75), al, la,
                                                              avg / body if body > 1 else float('nan')))
    # the stops: the slip once the body has (nearly) stopped is the glide of the stance foot into the idle pose
    peak = []
    path = []
    for ph in order:
        if not ph.startswith('stop'):
            continue
        sl = [r['slip'] for r in rows if r['phase'] == ph and r['body'] < 8.0]
        if len(sl) < 5:
            continue
        smooth = [sum(sl[i:i + 5]) / 5 for i in range(len(sl) - 4)]
        peak.append(max(smooth))
        path.append(sum(sl) / 60.0)
    if peak:
        print('STOPS (body below 8 u/s): worst 5-frame mean slip %.1f u/s, mean over the stops of the peaks %.1f u/s, '
              'mean ground covered by the stance foot per stop %.1f units' % (max(peak), sum(peak) / len(peak),
                                                                              sum(path) / len(path)))
    if total:
        print('ALL moving phases: stance foot-frames %d, slip p50 %.1f, p75 %.1f, mean %.1f u/s' % (len(total),
              pct(total, 0.5), pct(total, 0.75), sum(total) / len(total)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
