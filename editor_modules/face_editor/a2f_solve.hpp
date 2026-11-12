#pragma once

// The blendshape solve for the Audio2Face generator (task 0147 slice 2). The networks output a face mesh (a skin
// displacement per vertex); the .facecurves format stores blendshape weights, so each frame's mesh is fitted to the
// 52 ARKit poses of the model's bs_skin file.
//
// RECALLED-VERIFY, INFERRED: the formulation is a port of the feasibility gate's Python reference
// (I:\Projects\OpenEden-work\a2f\bssolve.py, analyze.py; task 0071 "Stage a results"), itself inferred from the
// parameter names in bs_skin_config*.json, NOT taken from NVIDIA's source. Over the frontal-mask vertices, with B_i the
// pose deltas and t the target displacement from the neutral face, it minimises
//   ||sum_i w_i B_i - t||^2 + l2 ||w||^2 + lt ||w - wPrev||^2 + ls * sum_pairs (w_a - w_b)^2 + l1 * sum w
// with 0 <= w <= 1 and inactive poses fixed at 0, by coordinate descent on the P x P normal matrix. The exact scaling
// of each term is not in the published files; only a comparison with NVIDIA's weights would confirm it.
//
// Pure: standard library only (the npz reader is a_npz).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace face_editor
{
    struct SolveConfig
    {
        double mL2 = 0.0;
        double mL1 = 0.0;
        double mTemporal = 0.0;
        double mSymmetry = 0.0;
        std::vector<bool> mActive; ///< one per pose; false = fixed at 0
        std::vector<int> mSymmetryGroup; ///< one per pose; poses sharing an id >= 0 form a pair; -1 = none
    };

    /// The poses of a bs_skin file reduced to its frontal-mask vertices.
    struct BlendshapeSet
    {
        std::vector<std::string> mNames; ///< the poses without "neutral", in file order (52 ARKit names)
        std::vector<std::uint32_t> mMask; ///< vertex indices
        std::size_t mVertexCount = 0; ///< of the full mesh
        std::vector<float> mNeutralMasked; ///< mask.size() * 3
        std::vector<float> mPosesMasked; ///< mNames.size() rows of mask.size() * 3: deltas from the neutral
        SolveConfig mConfig;

        std::size_t rowLength() const { return mMask.size() * 3; }
    };

    /// Reads \a npzName (poseNames, neutral, one array per pose, frontalMask) and \a configName
    /// (blendshape_params) from \a folder. False with \a error when anything is missing or inconsistent.
    bool loadBlendshapeSet(const std::filesystem::path& folder, const std::string& npzName,
        const std::string& configName, BlendshapeSet& out, std::string& error);

    /// The solve for one set. Holds the P x P normal matrix; calls are const and allocation free.
    class BlendshapeSolver
    {
    public:
        explicit BlendshapeSolver(const BlendshapeSet& set);

        std::size_t poseCount() const { return mP; }
        std::size_t rowLength() const { return mRow; }

        /// b[i] = sum_k B_i[k] * target[k] (target: rowLength() floats, the masked displacement from the neutral).
        void project(const float* target, double* b) const;

        /// One frame. \a wPrev and \a wOut have poseCount() entries and may be the same array.
        void solve(const double* b, const double* wPrev, double* wOut, int iterations = 100) const;

        const BlendshapeSet& set() const { return mSet; }

    private:
        const BlendshapeSet& mSet;
        std::size_t mP = 0;
        std::size_t mRow = 0;
        std::vector<double> mA; ///< P x P, B B^T
        std::vector<std::pair<std::size_t, std::size_t>> mPairs;
    };

    /// For a network whose mesh is linear in its output (v2.3: mean + sum_k c_k * basis_k), b is linear in c, so the
    /// per-frame cost is P x K instead of P x rowLength x K: b = b0 + G c, with G[i][k] = B_i . basis_k and
    /// b0 = B . (base - neutral). Built once per run.
    class LinearProjection
    {
    public:
        /// \a baseMinusNeutral: rowLength floats. \a basis: coefficientCount rows of rowLength floats.
        LinearProjection(const BlendshapeSolver& solver, const std::vector<float>& baseMinusNeutral,
            const std::vector<float>& basis, std::size_t coefficientCount);

        std::size_t coefficientCount() const { return mK; }
        /// b = b0 + G c.
        void project(const float* coefficients, double* b) const;

    private:
        std::size_t mP = 0;
        std::size_t mK = 0;
        std::vector<double> mB0;
        std::vector<double> mG; ///< P x K
    };
}
