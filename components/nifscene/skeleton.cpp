#include "skeleton.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace NifScene::Skeleton
{
    namespace
    {
        std::string lowerCase(const std::string& s)
        {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        bool finite(const Transform& t)
        {
            if (!std::isfinite(t.mScale))
                return false;
            for (float v : t.mTranslation)
                if (!std::isfinite(v))
                    return false;
            for (float v : t.mRotation)
                if (!std::isfinite(v))
                    return false;
            return true;
        }
    }

    Plan plan(const Scene& scene, const Limits& limits)
    {
        Plan result;
        const std::size_t count = scene.mNodes.size();

        // The parent as the plan sees it: outside the scene or the node itself means a root.
        std::vector<std::uint32_t> parent(count, sNone);
        std::vector<std::vector<std::uint32_t>> children(count);
        std::vector<std::uint32_t> roots;
        std::size_t geometryNodes = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const Node& node = scene.mNodes[i];
            if (node.mGeometry != sNone)
            {
                ++geometryNodes;
                continue;
            }
            if (node.mParent < count && node.mParent != i && scene.mNodes[node.mParent].mGeometry == sNone)
            {
                parent[i] = node.mParent;
                children[node.mParent].push_back(static_cast<std::uint32_t>(i));
            }
            else if (node.mParent < count && node.mParent != i)
            {
                // A parent that is a geometry node: its children hang from nothing we keep. Treat as a root.
                roots.push_back(static_cast<std::uint32_t>(i));
            }
            else if (node.mParent >= count)
                roots.push_back(static_cast<std::uint32_t>(i));
            // A self parent is a cycle of one: reachable from no root.
        }
        result.mSkippedGeometry = static_cast<std::uint32_t>(geometryNodes);

        // Depth first from the roots, children in record order. An explicit stack: a deep file cannot overflow ours.
        std::vector<std::uint32_t> planIndex(count, sNone);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> stack; // (scene node, plan index of its parent)
        std::set<std::string> names;
        std::size_t reached = 0;
        for (std::uint32_t root : roots)
        {
            stack.emplace_back(root, sNone);
            while (!stack.empty())
            {
                const auto [at, planParent] = stack.back();
                stack.pop_back();
                if (planIndex[at] != sNone)
                    continue;
                ++reached;
                if (result.mNodes.size() >= limits.mMaxNodes)
                    continue; // past the cap: counted below as unreachable
                const Node& node = scene.mNodes[at];
                BoneNode bone;
                bone.mName = node.mName.substr(0, limits.mMaxNameBytes);
                bone.mParent = planParent;
                bone.mLocal = node.mTransform;
                if (!finite(bone.mLocal))
                {
                    bone.mLocal = Transform{};
                    ++result.mNonFiniteTransforms;
                }
                if (!bone.mName.empty() && !names.insert(lowerCase(bone.mName)).second)
                    ++result.mDuplicateNames;
                planIndex[at] = static_cast<std::uint32_t>(result.mNodes.size());
                result.mNodes.push_back(std::move(bone));
                // Push children reversed so the first child is handled first.
                const auto& kids = children[at];
                for (auto it = kids.rbegin(); it != kids.rend(); ++it)
                    stack.emplace_back(*it, planIndex[at]);
            }
        }
        const std::size_t nonGeometry = count - geometryNodes;
        result.mUnreachable = static_cast<std::uint32_t>(nonGeometry - result.mNodes.size());
        (void)reached;
        return result;
    }

    std::uint32_t find(const Plan& plan, const std::string& name)
    {
        const std::string wanted = lowerCase(name);
        for (std::size_t i = 0; i < plan.mNodes.size(); ++i)
            if (lowerCase(plan.mNodes[i].mName) == wanted)
                return static_cast<std::uint32_t>(i);
        return sNone;
    }
}
