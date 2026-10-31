// OSG's results at commit 3b1289a02f for the inputs of the tests next door, recorded before OSG left the engine
// (task 0013). One row per evaluation, in the order the test makes them. Do not regenerate: OSG is gone.

const double kGolden5_0[] = {
    1.0, -2.0, 3.0,
    -4.0, -2.0, 0.5,
    -4.0, -2.0, 0.5,
};
MathConventions::GoldenSite kGolden5 {
    { "BoxConventions.expandByPoint", kGolden5_0, 3, 3 },
};

const double kGolden6_0[] = {
    1.0, -2.0, 3.0,
    1.0, 5.0, 3.0,
    1.0, 5.0, 10.0,
};
MathConventions::GoldenSite kGolden6 {
    { "BoxConventions.expandByPoint", kGolden6_0, 3, 3 },
};

