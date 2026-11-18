#!/usr/bin/env python3
"""Task 0323: where the hitch time goes, as shares of the time of all frames over the limit (nested scopes are
indented under their parent; a share is that scope's total over the hitch frames divided by the hitch frames' wall time).
usage: attrib.py <trace> [--simstart=S] [--hitch=25]"""
import json, sys, bisect, collections
args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = dict(a[2:].split('=') for a in sys.argv[1:] if a.startswith('--') and '=' in a)
frames, scopes = [], []
for line in open(args[0]):
    d = json.loads(line)
    if 'f' in d: frames.append((d['f'], d['b'], d['e']))
    else: scopes.append((d['n'], d['b'], d['e'], d['v'], d['t']))
frames.sort()
if 'simstart' in opts:
    firstRun = min((s[1] for s in scopes if s[0] == 'run.time'), default=None)
    f0 = next((f for f, b, e in frames if firstRun is not None and b <= firstRun <= e), frames[0][0])
    first = f0 + int(round(float(opts['simstart']) * 60)) + 2
    frames = [f for f in frames if f[0] >= first]
limit = float(opts.get('hitch', 25)) * 1000
main = next((s[4] for s in scopes if s[0] in ('frame.game.run', 'run.world.update')), 0)
ms = sorted((s for s in scopes if s[4] == main), key=lambda s: s[1])
starts = [s[1] for s in ms]
hitch = [f for f in frames if f[2] - f[1] > limit]
total = sum(e - b for _, b, e in hitch)
acc = collections.defaultdict(float)
for _, b, e in hitch:
    lo = bisect.bisect_left(starts, b); hi = bisect.bisect_right(starts, e)
    for s in ms[lo:hi]:
        if s[2] <= e: acc[s[0]] += s[2] - s[1]
tree = [
 ('frame (wall)', None, 0),
 ('world.navigator.wait', 'world.navigator.wait', 1),
 ('grid.total (cell grid change)', 'grid.total', 1),
 ('grid.getexterior', 'grid.getexterior', 2), ('neutral.cell.build (the pack)', 'neutral.cell.build', 3),
 ('cell.load', 'cell.load', 2), ('insert.objects', 'insert.objects', 3), ('obj.render', 'obj.render', 4),
 ('model.create (model reads)', 'model.create', 5), ('model.compile', 'model.compile', 5),
 ('neutralactor.create', 'neutralactor.create', 5), ('neutralactor.compile', 'neutralactor.compile', 5),
 ('npc.create / creature.create', 'npc.create', 5),
 ('obj.physics', 'obj.physics', 4), ('obj.mechanics / obj.lua', 'obj.lua', 4),
 ('incremental.build', 'incremental.build', 2), ('incremental.finish', 'incremental.finish', 2),
 ('terrain.neutral.ensure', 'terrain.neutral.ensure', 2), ('terrain.compile', 'terrain.compile', 2), ('terrain.updateview', 'terrain.updateview', 2),
 ('run.physics', 'run.physics', 1), ('physics.neutral.actors (pack AI/combat)', 'physics.neutral.actors', 2), ('physics.step', 'physics.step', 2),
 ('run.time -> phase.GameSimulation (pack frame: Papyrus ...)', 'phase.GameSimulation', 1),
 ('ext.neutral.apply', 'ext.neutral.apply', 2),
 ('world.rendering.update', 'world.rendering.update', 1), ('render.update.groundcover (neutral grass)', 'render.update.groundcover', 2), ('neutralgrass.pack', 'neutralgrass.pack', 3),
 ('loop.record.submit', 'loop.record.submit', 1), ('compile.vsg', 'compile.vsg', 1),
 ('frame.lua.sync / update', 'frame.lua.sync', 1), ('run.mechanics', 'run.mechanics', 1), ('run.window.focus.sound', 'run.window.focus.sound', 1),
]
print('hitch frames %d (> %.0f ms), %.1f s of wall time' % (len(hitch), limit / 1000, total / 1e6))
for label, key, depth in tree:
    v = total if key is None else acc.get(key, 0)
    if key is not None and v == 0: continue
    print('%s%-52s %9.1f ms  %5.1f %%' % ('  ' * depth, label, v / 1000, 100 * v / max(1, total)))
