#include "a2f_solve.hpp"

#include "a2f_npz.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace face_editor
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::uintmax_t kMaxConfigBytes = 1u << 20;

        bool readNumber(const nlohmann::json& params, const char* key, double& out, std::string& error)
        {
            const auto it = params.find(key);
            if (it == params.end() || !it->is_number())
            {
                error = std::string("blendshape_params has no number \"") + key + "\"";
                return false;
            }
            out = it->get<double>();
            return true;
        }

        bool loadConfig(const fs::path& path, std::size_t poses, SolveConfig& cfg, std::string& error)
        {
            std::error_code ec;
            const auto size = fs::file_size(path, ec);
            if (ec || size > kMaxConfigBytes)
            {
                error = path.generic_string() + ": cannot read the file"
                    + (ec ? " (" + ec.message() + ")" : " (too large)");
                return false;
            }
            std::ifstream in(path, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            nlohmann::json root = nlohmann::json::parse(text.str(), nullptr, false);
            if (root.is_discarded() || !root.is_object() || !root.contains("blendshape_params")
                || !root["blendshape_params"].is_object())
            {
                error = path.generic_string() + ": not a JSON object with blendshape_params";
                return false;
            }
            const nlohmann::json& p = root["blendshape_params"];
            std::string why;
            if (!readNumber(p, "strengthL2regularization", cfg.mL2, why)
                || !readNumber(p, "strengthL1regularization", cfg.mL1, why)
                || !readNumber(p, "strengthTemporalSmoothing", cfg.mTemporal, why)
                || !readNumber(p, "strengthSymmetry", cfg.mSymmetry, why))
            {
                error = path.generic_string() + ": " + why;
                return false;
            }
            double numPoses = 0;
            if (!readNumber(p, "numPoses", numPoses, why) || static_cast<std::size_t>(numPoses) != poses)
            {
                error = path.generic_string() + ": numPoses is "
                    + (why.empty() ? std::to_string(static_cast<long long>(numPoses)) : "missing")
                    + " but the poses file has " + std::to_string(poses);
                return false;
            }
            const auto active = p.find("bsSolveActivePoses");
            if (active == p.end() || !active->is_array() || active->size() != poses)
            {
                error = path.generic_string() + ": bsSolveActivePoses is not a list of " + std::to_string(poses);
                return false;
            }
            cfg.mActive.clear();
            for (const auto& v : *active)
            {
                if (!v.is_number())
                {
                    error = path.generic_string() + ": bsSolveActivePoses holds a non-number";
                    return false;
                }
                cfg.mActive.push_back(v.get<double>() != 0.0);
            }
            cfg.mSymmetryGroup.assign(poses, -1);
            const auto sym = p.find("bsSolveSymmetryPoses");
            if (sym != p.end())
            {
                if (!sym->is_array() || sym->size() != poses)
                {
                    error = path.generic_string() + ": bsSolveSymmetryPoses is not a list of " + std::to_string(poses);
                    return false;
                }
                for (std::size_t i = 0; i < poses; ++i)
                {
                    const auto& v = (*sym)[i];
                    if (!v.is_number())
                    {
                        error = path.generic_string() + ": bsSolveSymmetryPoses holds a non-number";
                        return false;
                    }
                    cfg.mSymmetryGroup[i] = static_cast<int>(v.get<double>());
                }
            }
            return true;
        }
    }

    bool loadBlendshapeSet(const fs::path& folder, const std::string& npzName, const std::string& configName,
        BlendshapeSet& out, std::string& error)
    {
        NpzFile npz;
        if (!npz.open(folder / npzName, error))
            return false;
        BlendshapeSet set;
        std::vector<std::string> names;
        if (!npz.readStrings("poseNames", names, error))
            return false;
        if (names.size() < 2 || names[0] != "neutral")
        {
            error = (folder / npzName).generic_string() + ": poseNames must start with \"neutral\" and list poses";
            return false;
        }
        std::vector<float> neutral;
        NpyInfo info;
        if (!npz.readFloats("neutral", neutral, info, error))
            return false;
        if (info.mShape.size() != 2 || info.mShape[1] != 3 || info.mShape[0] == 0)
        {
            error = (folder / npzName).generic_string() + ": neutral is not a V x 3 array";
            return false;
        }
        set.mVertexCount = info.mShape[0];
        std::vector<std::int32_t> mask;
        if (!npz.readInt32s("frontalMask", mask, info, error))
            return false;
        if (mask.empty())
        {
            error = (folder / npzName).generic_string() + ": frontalMask is empty";
            return false;
        }
        for (const std::int32_t v : mask)
            if (v < 0 || static_cast<std::size_t>(v) >= set.mVertexCount)
            {
                error = (folder / npzName).generic_string() + ": mask index " + std::to_string(v)
                    + " lies outside the mesh of " + std::to_string(set.mVertexCount) + " vertices";
                return false;
            }
        for (const std::int32_t v : mask)
            set.mMask.push_back(static_cast<std::uint32_t>(v));
        const std::size_t row = set.rowLength();
        set.mNeutralMasked.resize(row);
        for (std::size_t m = 0; m < set.mMask.size(); ++m)
            for (std::size_t c = 0; c < 3; ++c)
                set.mNeutralMasked[m * 3 + c] = neutral[static_cast<std::size_t>(set.mMask[m]) * 3 + c];
        set.mNames.assign(names.begin() + 1, names.end());
        set.mPosesMasked.resize(set.mNames.size() * row);
        std::vector<float> pose;
        for (std::size_t p = 0; p < set.mNames.size(); ++p)
        {
            if (!npz.readFloats(set.mNames[p], pose, info, error))
                return false;
            if (pose.size() != set.mVertexCount * 3)
            {
                error = (folder / npzName).generic_string() + ": pose " + set.mNames[p]
                    + " has another size than neutral";
                return false;
            }
            for (std::size_t m = 0; m < set.mMask.size(); ++m)
                for (std::size_t c = 0; c < 3; ++c)
                    set.mPosesMasked[p * row + m * 3 + c] = pose[static_cast<std::size_t>(set.mMask[m]) * 3 + c];
        }
        if (!loadConfig(folder / configName, set.mNames.size(), set.mConfig, error))
            return false;
        out = std::move(set);
        error.clear();
        return true;
    }

    BlendshapeSolver::BlendshapeSolver(const BlendshapeSet& set)
        : mSet(set)
        , mP(set.mNames.size())
        , mRow(set.rowLength())
    {
        mA.assign(mP * mP, 0.0);
        for (std::size_t i = 0; i < mP; ++i)
        {
            const float* bi = set.mPosesMasked.data() + i * mRow;
            for (std::size_t j = i; j < mP; ++j)
            {
                const float* bj = set.mPosesMasked.data() + j * mRow;
                double s = 0;
                for (std::size_t k = 0; k < mRow; ++k)
                    s += static_cast<double>(bi[k]) * bj[k];
                mA[i * mP + j] = s;
                mA[j * mP + i] = s;
            }
        }
        // H = A + (l2 + lt) I, plus the symmetry penalty ls * (w_a - w_b)^2 of every group of exactly two poses.
        std::map<int, std::vector<std::size_t>> groups;
        for (std::size_t i = 0; i < mP && i < set.mConfig.mSymmetryGroup.size(); ++i)
            if (set.mConfig.mSymmetryGroup[i] >= 0)
                groups[set.mConfig.mSymmetryGroup[i]].push_back(i);
        for (const auto& [id, members] : groups)
            if (members.size() == 2)
                mPairs.emplace_back(members[0], members[1]);
        for (std::size_t i = 0; i < mP; ++i)
            mA[i * mP + i] += set.mConfig.mL2 + set.mConfig.mTemporal;
        for (const auto& [a, b] : mPairs)
        {
            const double s = 2.0 * set.mConfig.mSymmetry;
            mA[a * mP + a] += s;
            mA[b * mP + b] += s;
            mA[a * mP + b] -= s;
            mA[b * mP + a] -= s;
        }
    }

    void BlendshapeSolver::project(const float* target, double* b) const
    {
        for (std::size_t i = 0; i < mP; ++i)
        {
            const float* bi = mSet.mPosesMasked.data() + i * mRow;
            double s = 0;
            for (std::size_t k = 0; k < mRow; ++k)
                s += static_cast<double>(bi[k]) * target[k];
            b[i] = s;
        }
    }

    void BlendshapeSolver::solve(const double* b, const double* wPrev, double* wOut, int iterations) const
    {
        const SolveConfig& c = mSet.mConfig;
        std::vector<double> g(mP), w(mP);
        for (std::size_t i = 0; i < mP; ++i)
        {
            const bool active = i < c.mActive.size() && c.mActive[i];
            g[i] = b[i] + c.mTemporal * wPrev[i] - c.mL1;
            w[i] = active ? wPrev[i] : 0.0;
        }
        for (int sweep = 0; sweep < iterations; ++sweep)
            for (std::size_t i = 0; i < mP; ++i)
            {
                if (!(i < c.mActive.size() && c.mActive[i]))
                {
                    w[i] = 0.0;
                    continue;
                }
                const double* h = mA.data() + i * mP;
                if (!(h[i] > 0.0))
                {
                    w[i] = 0.0;
                    continue;
                }
                double r = g[i];
                for (std::size_t j = 0; j < mP; ++j)
                    if (j != i)
                        r -= h[j] * w[j];
                w[i] = std::clamp(r / h[i], 0.0, 1.0);
            }
        std::copy(w.begin(), w.end(), wOut);
    }

    LinearProjection::LinearProjection(const BlendshapeSolver& solver, const std::vector<float>& baseMinusNeutral,
        const std::vector<float>& basis, std::size_t coefficientCount)
        : mP(solver.poseCount())
        , mK(coefficientCount)
    {
        const std::size_t row = solver.rowLength();
        const BlendshapeSet& set = solver.set();
        mB0.assign(mP, 0.0);
        mG.assign(mP * mK, 0.0);
        for (std::size_t i = 0; i < mP; ++i)
        {
            const float* bi = set.mPosesMasked.data() + i * row;
            double s = 0;
            for (std::size_t m = 0; m < row; ++m)
                s += static_cast<double>(bi[m]) * baseMinusNeutral[m];
            mB0[i] = s;
            for (std::size_t k = 0; k < mK; ++k)
            {
                const float* bk = basis.data() + k * row;
                double t = 0;
                for (std::size_t m = 0; m < row; ++m)
                    t += static_cast<double>(bi[m]) * bk[m];
                mG[i * mK + k] = t;
            }
        }
    }

    void LinearProjection::project(const float* coefficients, double* b) const
    {
        for (std::size_t i = 0; i < mP; ++i)
        {
            double s = mB0[i];
            const double* g = mG.data() + i * mK;
            for (std::size_t k = 0; k < mK; ++k)
                s += g[k] * coefficients[k];
            b[i] = s;
        }
    }
}
