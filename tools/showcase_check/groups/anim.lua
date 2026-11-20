return { start = "@WhiterunWorld,25670,-7633", shots = {
{ name = "p_idle", hour = 12, x = 28670, y = -11633, yaw = 3.14159, tp = { 0, 50, 70, 260 }, dwell = 36, n = 40, step = 0.25 },
{ name = "p_walk", hour = 12, x = 28670, y = -11633, yaw = 3.14159, move = "walk", tp = { 0, 50, 70, 260 }, dwell = 3, n = 30, step = 0.1 },
{ name = "p_run", hour = 12, x = 28670, y = -11633, yaw = 3.14159, move = "run", tp = { 0, 50, 70, 260 }, dwell = 3, n = 30, step = 0.1 },
{ name = "p_runstop", hour = 12, x = 28670, y = -11633, yaw = 3.14159, move = "run", tp = { 0, 50, 70, 260 }, dwell = 3, n = 30, step = 0.1, stopAt = 15 },
{ name = "p_runturn", hour = 12, x = 28670, y = -11633, yaw = 3.14159, move = "run", turn = 1.2, tp = { 0, 50, 70, 260 }, dwell = 3, n = 30, step = 0.1 },
{ name = "g_idle0", hour = 12, x = 25670, y = -7633, yaw = 0.0, follow = { id = "b864594", dx = -300, dy = -100, dz = 120, ldz = 80 }, dwell = 8, n = 30, step = 0.5 },
{ name = "g_idle1", hour = 12, x = 25670, y = -7633, yaw = 0.0, follow = { id = "b864596", dx = -300, dy = -100, dz = 120, ldz = 80 }, dwell = 1, n = 30, step = 0.5 }
} }
