#!/usr/bin/env python3
"""Turns a puddlemap_check scenario file into the Lua table scripts/pmcheck_poses.lua (see check.sh for the format)."""
import sys


def main():
    cfg = {"region": "Bitter Coast Region", "weather": "rain", "hour": 12.0, "timescale": 1.0, "hold": 3.0,
           "first": 4.0, "change_after": None, "change_to": None}
    poses = []
    for raw in open(sys.argv[1], encoding="utf-8"):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        key = parts[0]
        if key == "region":
            cfg["region"] = " ".join(parts[1:])
        elif key == "weather":
            cfg["weather"] = parts[1]
        elif key in ("hour", "timescale", "hold", "first"):
            cfg[key] = float(parts[1])
        elif key == "change_after":
            cfg["change_after"] = float(parts[1])
            cfg["change_to"] = parts[2]
        elif key == "pose":
            poses.append([parts[1], float(parts[2]), float(parts[3]), float(parts[4]), float(parts[5]),
                          float(parts[6]), []])
        elif key == "probe":
            poses[-1][6].append((float(parts[1]), float(parts[2]), float(parts[3])))
        else:
            sys.exit("unknown command: " + line)
    out = ["return {"]
    out.append('  region = "%s",' % cfg["region"])
    out.append('  weather = "%s",' % cfg["weather"])
    out.append("  hour = %r, timescale = %r, hold = %r, first = %r," % (cfg["hour"], cfg["timescale"], cfg["hold"],
                                                                      cfg["first"]))
    if cfg["change_after"] is not None:
        out.append('  change_after = %r, change_to = "%s",' % (cfg["change_after"], cfg["change_to"]))
    out.append("  poses = {")
    for name, x, y, z, yaw, pitch, probes in poses:
        probe = ", ".join("{ %r, %r, %r }" % p for p in probes)
        out.append('    { name = "%s", x = %r, y = %r, z = %r, yaw = %r, pitch = %r, probes = { %s } },'
                   % (name, x, y, z, yaw, pitch, probe))
    out.append("  },")
    out.append("}")
    print("\n".join(out))


main()
