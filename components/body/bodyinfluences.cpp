#include "bodyinfluences.hpp"

#include <algorithm>

namespace Body
{
    WeightList normalisedWeights(WeightList raw)
    {
        std::sort(raw.begin(), raw.end());
        WeightList merged;
        for (const auto& [bone, weight] : raw)
        {
            if (!merged.empty() && merged.back().first == bone)
            {
                merged.back().second += weight;
            }
            else
            {
                merged.emplace_back(bone, weight);
            }
        }
        double sum = 0;
        for (const auto& entry : merged)
        {
            sum += entry.second;
        }
        WeightList result;
        for (const auto& [bone, weight] : merged)
        {
            if (weight > 0 && sum > 0)
            {
                result.emplace_back(bone, weight / sum);
            }
        }
        return result;
    }

    CutWeights cutToFourWeights(const WeightList& weights)
    {
        WeightList sorted = weights;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second)
            {
                return a.second > b.second;
            }
            return a.first < b.first;
        });
        if (sorted.size() > maxSkinInfluences)
        {
            sorted.resize(maxSkinInfluences);
        }
        double sum = 0;
        for (const auto& entry : sorted)
        {
            sum += entry.second;
        }
        CutWeights result;
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            result.mBone[i] = sorted[i].first;
            result.mWeight[i] = static_cast<float>(sum > 0 ? sorted[i].second / sum : 0.0);
        }
        return result;
    }
}
