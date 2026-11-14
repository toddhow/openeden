// Task 0147 slice 2: the blendshape solve (a port of the feasibility gate's inferred formulation) on SYNTHETIC targets:
// poses and weights made up here, so the answer is known by construction. Whether the formulation matches NVIDIA's
// solver is NOT tested by anything in this file (task 0071 stage a: pending a reference).
#include "editor_modules/face_editor/a2f_npz.hpp"
#include "editor_modules/face_editor/a2f_solve.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

namespace
{
    using namespace face_editor;
    namespace fs = std::filesystem;

    /// Poses with disjoint support, so the normal matrix is diagonal and every answer can be worked out by hand: pose p
    /// moves vertices [p * 4, p * 4 + 4) of a 4 * P vertex mask along z by (p + 1).
    BlendshapeSet disjointSet(std::size_t poses)
    {
        BlendshapeSet s;
        const std::size_t verts = poses * 4;
        s.mVertexCount = verts + 10;
        for (std::size_t v = 0; v < verts; ++v)
            s.mMask.push_back(static_cast<std::uint32_t>(v + 3));
        s.mNeutralMasked.assign(verts * 3, 0.f);
        for (std::size_t p = 0; p < poses; ++p)
        {
            s.mNames.push_back("pose" + std::to_string(p));
            s.mPosesMasked.resize((p + 1) * verts * 3, 0.f);
            for (std::size_t v = p * 4; v < p * 4 + 4; ++v)
                s.mPosesMasked[p * verts * 3 + v * 3 + 2] = static_cast<float>(p + 1);
        }
        s.mConfig.mActive.assign(poses, true);
        s.mConfig.mSymmetryGroup.assign(poses, -1);
        s.mConfig.mL2 = 1e-4;
        return s;
    }

    std::vector<float> targetOf(const BlendshapeSet& s, const std::vector<double>& w)
    {
        std::vector<float> t(s.rowLength(), 0.f);
        for (std::size_t p = 0; p < w.size(); ++p)
            for (std::size_t k = 0; k < t.size(); ++k)
                t[k] += static_cast<float>(w[p] * s.mPosesMasked[p * s.rowLength() + k]);
        return t;
    }

    std::vector<double> solveOnce(
        const BlendshapeSet& s, const std::vector<float>& target, std::vector<double> prev = {})
    {
        BlendshapeSolver solver(s);
        prev.resize(solver.poseCount(), 0.0);
        std::vector<double> b(solver.poseCount()), w(solver.poseCount());
        solver.project(target.data(), b.data());
        solver.solve(b.data(), prev.data(), w.data());
        return w;
    }

    TEST(A2FSolve, projectIsThePoseRowsTimesTheTargetByHand)
    {
        BlendshapeSet s;
        s.mMask = { 0, 1 };
        s.mVertexCount = 2;
        s.mNames = { "a", "b" };
        s.mNeutralMasked.assign(6, 0.f);
        s.mPosesMasked = { 1, 0, 2, 0, 0, 0, /* b: */ 0, 1, 0, 3, 0, 1 };
        s.mConfig.mActive = { true, true };
        s.mConfig.mSymmetryGroup = { -1, -1 };
        BlendshapeSolver solver(s);
        const float target[6] = { 1, 2, 3, 4, 5, 6 };
        double b[2];
        solver.project(target, b);
        EXPECT_DOUBLE_EQ(b[0], 1 * 1 + 2 * 3); // 7
        EXPECT_DOUBLE_EQ(b[1], 2 * 1 + 3 * 4 + 6 * 1); // 20
    }

    TEST(A2FSolve, recoversSparseWeightsOfAnExactTarget)
    {
        const auto s = disjointSet(6);
        const std::vector<double> truth{ 0.0, 0.8, 0.0, 0.3, 0.0, 1.0 };
        const auto w = solveOnce(s, targetOf(s, truth));
        for (std::size_t p = 0; p < truth.size(); ++p)
            EXPECT_NEAR(w[p], truth[p], 0.01) << p;
    }

    TEST(A2FSolve, theL2TermShrinksTheAnswerByItsClosedForm)
    {
        // One pose, weight 0.5; normal matrix a = |B|^2 = 4 * (p + 1)^2 = 4 (p = 0). With l2 = 4 the optimum of
        // (a w - b) w-form is w = b / (a + l2) = 4 * 0.5 / (4 + 4) = 0.25.
        auto s = disjointSet(1);
        s.mConfig.mL2 = 4.0;
        const auto w = solveOnce(s, targetOf(s, { 0.5 }));
        EXPECT_NEAR(w[0], 0.25, 1e-6);
    }

    TEST(A2FSolve, theL1TermSubtractsFromTheNumerator)
    {
        auto s = disjointSet(1);
        s.mConfig.mL2 = 0.0;
        s.mConfig.mL1 = 0.5;
        // w = (b - l1) / a = (2 - 0.5) / 4 = 0.375
        const auto w = solveOnce(s, targetOf(s, { 0.5 }));
        EXPECT_NEAR(w[0], 0.375, 1e-6);
    }

    TEST(A2FSolve, weightsStayInsideZeroAndOne)
    {
        const auto s = disjointSet(3);
        const auto w = solveOnce(s, targetOf(s, { 2.5, -1.0, 0.5 }));
        EXPECT_DOUBLE_EQ(w[0], 1.0);
        EXPECT_DOUBLE_EQ(w[1], 0.0);
        EXPECT_NEAR(w[2], 0.5, 0.01);
    }

    TEST(A2FSolve, anInactivePoseStaysAtZeroWhateverTheTargetAsks)
    {
        auto s = disjointSet(3);
        s.mConfig.mActive = { true, false, true };
        const auto w = solveOnce(s, targetOf(s, { 0.4, 0.9, 0.4 }));
        EXPECT_DOUBLE_EQ(w[1], 0.0);
        EXPECT_NEAR(w[0], 0.4, 0.01);
        EXPECT_NEAR(w[2], 0.4, 0.01);
    }

    TEST(A2FSolve, theTemporalTermPullsTowardThePreviousFrame)
    {
        auto s = disjointSet(1);
        s.mConfig.mL2 = 0.0;
        s.mConfig.mTemporal = 4.0;
        // w = (b + lt * prev) / (a + lt) = (2 + 4 * 1) / (4 + 4) = 0.75 for target 0.5 and previous 1.0.
        const auto w = solveOnce(s, targetOf(s, { 0.5 }), { 1.0 });
        EXPECT_NEAR(w[0], 0.75, 1e-6);
    }

    TEST(A2FSolve, theSymmetryTermPullsAPairTogether)
    {
        // Poses 0 and 1 are paired and asked for 0.2 and 0.8. The pose norms give a_0 = 4 and a_1 = 16, so b = (0.8,
        // 12.8). With ls = 10 the penalty adds 2 ls to both diagonals and -2 ls off the diagonal; the 2 x 2 system
        // [[24, -20], [-20, 36]] w = b has det 464 and w = ((0.8 * 36 + 20 * 12.8) / 464, (24 * 12.8 + 20 * 0.8) / 464)
        // = (0.6138, 0.6966): pulled together from 0.2 / 0.8.
        auto s = disjointSet(2);
        s.mConfig.mSymmetryGroup = { 0, 0 };
        s.mConfig.mSymmetry = 10.0;
        s.mConfig.mL2 = 0.0;
        const auto w = solveOnce(s, targetOf(s, { 0.2, 0.8 }));
        EXPECT_NEAR(w[0], 0.6138, 0.005);
        EXPECT_NEAR(w[1], 0.6966, 0.005);
        s.mConfig.mSymmetry = 0.0;
        const auto free = solveOnce(s, targetOf(s, { 0.2, 0.8 }));
        EXPECT_NEAR(free[0], 0.2, 0.01);
        EXPECT_NEAR(free[1], 0.8, 0.01);
    }

    TEST(A2FSolve, aGroupOfAnyOtherSizeThanTwoIsIgnored)
    {
        auto s = disjointSet(3);
        s.mConfig.mSymmetryGroup = { 5, 5, 5 };
        s.mConfig.mSymmetry = 1000.0;
        const auto w = solveOnce(s, targetOf(s, { 0.2, 0.5, 0.8 }));
        EXPECT_NEAR(w[0], 0.2, 0.01);
        EXPECT_NEAR(w[2], 0.8, 0.01);
    }

    TEST(A2FSolve, solveMayRunInPlaceAndIsDeterministic)
    {
        const auto s = disjointSet(4);
        const auto t = targetOf(s, { 0.3, 0.6, 0.0, 0.9 });
        BlendshapeSolver solver(s);
        std::vector<double> b(4), a(4, 0.1), c(4, 0.1);
        solver.project(t.data(), b.data());
        solver.solve(b.data(), a.data(), a.data()); // in place
        std::vector<double> out(4);
        solver.solve(b.data(), c.data(), out.data());
        EXPECT_EQ(a, out);
    }

    TEST(A2FLinearProjection, equalsTheDirectProjectionOfBasePlusCoefficientsTimesBasis)
    {
        const auto s = disjointSet(5);
        BlendshapeSolver solver(s);
        const std::size_t row = s.rowLength();
        const std::size_t K = 3;
        std::vector<float> base(row), basis(K * row);
        for (std::size_t i = 0; i < row; ++i)
            base[i] = static_cast<float>(std::sin(0.37 * static_cast<double>(i)));
        for (std::size_t i = 0; i < K * row; ++i)
            basis[i] = static_cast<float>(std::cos(0.11 * static_cast<double>(i)));
        LinearProjection lin(solver, base, basis, K);
        EXPECT_EQ(lin.coefficientCount(), K);
        const float c[K] = { 0.5f, -1.25f, 2.0f };
        std::vector<float> target(base);
        for (std::size_t k = 0; k < K; ++k)
            for (std::size_t i = 0; i < row; ++i)
                target[i] += c[k] * basis[k * row + i];
        std::vector<double> direct(5), linear(5);
        solver.project(target.data(), direct.data());
        lin.project(c, linear.data());
        for (std::size_t i = 0; i < 5; ++i)
            EXPECT_NEAR(linear[i], direct[i], 1e-3 * (1.0 + std::fabs(direct[i]))) << i;
    }

    // ---- loading ---------------------------------------------------------------------------------------------------

    fs::path writeFixture(const char* name, bool breakConfig = false, bool badMask = false)
    {
        const fs::path dir = fs::temp_directory_path() / ("oe_a2f_solve_" + std::string(name));
        fs::remove_all(dir);
        fs::create_directories(dir);
        // 5 vertices, mask {1, 3}, poses a, b (+ "neutral").
        std::vector<NpzEntryF32> f;
        f.push_back({ "neutral", { 5, 3 }, std::vector<float>(15, 1.f) });
        std::vector<float> a(15, 0.f), b(15, 0.f);
        a[1 * 3 + 2] = 4.f; // vertex 1 z
        b[3 * 3 + 0] = -2.f; // vertex 3 x
        f.push_back({ "a", { 5, 3 }, a });
        f.push_back({ "b", { 5, 3 }, b });
        const std::string bytes = buildNpz(
            f, { { "frontalMask", { 2 }, { 1, badMask ? 9 : 3 } } }, { { "poseNames", { "neutral", "a", "b" } } });
        std::ofstream(dir / "bs.npz", std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const std::string json = breakConfig
            ? "{\"blendshape_params\": {\"numPoses\": 7}}"
            : "{\"blendshape_params\": {\"strengthL2regularization\": 0.5, "
              "\"strengthTemporalSmoothing\": 0.3, \"strengthL1regularization\": 0.25, "
              "\"strengthSymmetry\": 100.0, \"numPoses\": 2, "
              "\"bsSolveActivePoses\": [1, 0], \"bsSolveSymmetryPoses\": [-1, 3]}}";
        std::ofstream(dir / "bs.json", std::ios::binary) << json;
        return dir;
    }

    TEST(A2FBlendshapeSet, loadsPosesMaskedAndTheConfig)
    {
        const fs::path dir = writeFixture("ok");
        BlendshapeSet s;
        std::string error;
        ASSERT_TRUE(loadBlendshapeSet(dir, "bs.npz", "bs.json", s, error)) << error;
        EXPECT_EQ(s.mNames, (std::vector<std::string>{ "a", "b" }));
        EXPECT_EQ(s.mVertexCount, 5u);
        EXPECT_EQ(s.mMask, (std::vector<std::uint32_t>{ 1, 3 }));
        EXPECT_EQ(s.rowLength(), 6u);
        EXPECT_EQ(s.mNeutralMasked, std::vector<float>(6, 1.f));
        EXPECT_EQ(s.mPosesMasked, (std::vector<float>{ 0, 0, 4, 0, 0, 0, /* b */ 0, 0, 0, -2, 0, 0 }));
        EXPECT_DOUBLE_EQ(s.mConfig.mL2, 0.5);
        EXPECT_DOUBLE_EQ(s.mConfig.mTemporal, 0.3);
        EXPECT_DOUBLE_EQ(s.mConfig.mL1, 0.25);
        EXPECT_DOUBLE_EQ(s.mConfig.mSymmetry, 100.0);
        EXPECT_EQ(s.mConfig.mActive, (std::vector<bool>{ true, false }));
        EXPECT_EQ(s.mConfig.mSymmetryGroup, (std::vector<int>{ -1, 3 }));
    }

    TEST(A2FBlendshapeSet, refusesAConfigThatDisagreesWithThePoses)
    {
        const fs::path dir = writeFixture("badcfg", true);
        BlendshapeSet s;
        std::string error;
        EXPECT_FALSE(loadBlendshapeSet(dir, "bs.npz", "bs.json", s, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(A2FBlendshapeSet, refusesAMaskIndexOutsideTheMesh)
    {
        const fs::path dir = writeFixture("badmask", false, true);
        BlendshapeSet s;
        std::string error;
        EXPECT_FALSE(loadBlendshapeSet(dir, "bs.npz", "bs.json", s, error));
        EXPECT_NE(error.find("mask"), std::string::npos) << error;
    }

    TEST(A2FBlendshapeSet, refusesMissingFiles)
    {
        BlendshapeSet s;
        std::string error;
        EXPECT_FALSE(
            loadBlendshapeSet(fs::temp_directory_path() / "oe_a2f_nonexistent", "bs.npz", "bs.json", s, error));
        EXPECT_FALSE(error.empty());
    }
}
