-- The plan of the W5 scenario (task 0127 W-S4). Replaced per run: this default only looks at the horizon at night.
return {
    timescale = 0,
    height = 3000,
    shots = {
        { hour = 23.0, yaw = 0.0, pitch = -0.3, wait = 5 },
    },
}
