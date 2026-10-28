# Module rules for core/reconcile
#
# Boundary rules:
# - core/reconcile depends only on core/math, core/rig, core/motor, core/arbiter, core/sensors, core/behaviors, and physics/interface.
# - Never include engine headers (OpenMW, OpenEden, Jolt) in public headers.
# - Zero heap allocations on the hot path (verified by opas_alloc_tests).
# - Thread-safe and step-deterministic.
