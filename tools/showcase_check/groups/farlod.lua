-- task 0361: the far field (world LOD tiles and object clusters) seen from a height and from far away.
return { start = "@WhiterunWorld,25670,-7633", shots = {
{ name = "aerial25k_s", hour = 12, cam = { 25670, -7633, 25000, 25670, -30000, -2000 }, dwell = 10 },
{ name = "aerial25k_e", hour = 12, cam = { 25670, -7633, 25000, 55000, -7633, -2000 }, dwell = 6 },
{ name = "aerial9k_s", hour = 12, cam = { 25670, -7633, 9000, 25670, -40000, -4000 }, dwell = 6 },
{ name = "aerial9k_n", hour = 12, cam = { 25670, -7633, 9000, 25670, 30000, -2000 }, dwell = 6 },
{ name = "whiterun_from_south", hour = 12, cam = { 25670, -60000, 6000, 25670, -7633, 1500 }, dwell = 12 },
{ name = "whiterun_from_east", hour = 12, cam = { 68000, -9000, 6000, 25670, -6000, 1500 }, dwell = 8 },
{ name = "whiterun_from_ground", hour = 12, cam = { 25670, -42000, 1500, 25670, -5000, 3000 }, dwell = 8 },
{ name = "highhrothgar", hour = 12, cam = { 25670, -7633, 2500, 53500, -41000, 9500 }, dwell = 8 },
{ name = "hold_aerial", hour = 12, cam = { 25670, -7633, 88, 25670, -1633, -2412 }, dwell = 4 },
{ name = "far_south", hour = 12, cam = { 25670, -7633, 5588, 25670, -47633, -7412 }, dwell = 6 },
{ name = "far_east", hour = 12, cam = { 25670, -7633, 5588, 65670, -7633, -7412 }, dwell = 6 },
} }
