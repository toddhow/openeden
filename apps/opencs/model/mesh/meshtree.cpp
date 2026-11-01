#include "meshtree.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace CSMMesh
{
    namespace
    {
        std::string lower(std::string_view text)
        {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }
    }

    void MeshTree::check(NodeId id) const
    {
        if (id < 0 || static_cast<std::size_t>(id) >= mNodes.size())
            throw std::out_of_range("unknown mesh node id");
    }

    NodeId MeshTree::addNode(NodeId parent, std::string name, std::string type)
    {
        if (parent != kNoNode)
            check(parent);
        const NodeId id = static_cast<NodeId>(mNodes.size());
        MeshNode node;
        node.name = std::move(name);
        node.type = std::move(type);
        node.parent = parent;
        mNodes.push_back(std::move(node));
        if (parent != kNoNode)
            mNodes[parent].children.push_back(id);
        return id;
    }

    const MeshNode& MeshTree::node(NodeId id) const
    {
        check(id);
        return mNodes[id];
    }

    MeshNode& MeshTree::mutableNode(NodeId id)
    {
        check(id);
        return mNodes[id];
    }

    std::vector<NodeId> MeshTree::roots() const
    {
        std::vector<NodeId> result;
        for (std::size_t i = 0; i < mNodes.size(); ++i)
            if (mNodes[i].parent == kNoNode)
                result.push_back(static_cast<NodeId>(i));
        return result;
    }

    void MeshTree::setVisible(NodeId id, bool visible)
    {
        mutableNode(id).visible = visible;
    }

    bool MeshTree::isEffectivelyVisible(NodeId id) const
    {
        check(id);
        for (NodeId current = id; current != kNoNode; current = mNodes[current].parent)
            if (!mNodes[current].visible)
                return false;
        return true;
    }

    std::string MeshTree::path(NodeId id) const
    {
        check(id);
        std::vector<std::string> parts;
        for (NodeId current = id; current != kNoNode; current = mNodes[current].parent)
            parts.push_back(mNodes[current].name.empty() ? std::string("<unnamed>") : mNodes[current].name);
        std::string result;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        {
            if (!result.empty())
                result += '/';
            result += *it;
        }
        return result;
    }

    std::vector<NodeId> MeshTree::findByName(std::string_view text, bool caseInsensitive) const
    {
        std::vector<NodeId> result;
        const std::string needle = caseInsensitive ? lower(text) : std::string(text);
        for (NodeId id : depthFirst())
        {
            const std::string hay = caseInsensitive ? lower(mNodes[id].name) : mNodes[id].name;
            if (hay.find(needle) != std::string::npos)
                result.push_back(id);
        }
        return result;
    }

    std::vector<NodeId> MeshTree::filter(std::string_view text) const
    {
        const std::vector<NodeId> order = depthFirst();
        if (text.empty())
            return order;
        const std::string needle = lower(text);
        std::set<NodeId> keep;
        for (NodeId id : order)
        {
            if (lower(mNodes[id].name).find(needle) == std::string::npos
                && lower(mNodes[id].type).find(needle) == std::string::npos)
                continue;
            for (NodeId current = id; current != kNoNode; current = mNodes[current].parent)
                if (!keep.insert(current).second)
                    break;
        }
        std::vector<NodeId> result;
        for (NodeId id : order)
            if (keep.count(id))
                result.push_back(id);
        return result;
    }

    MeshTree::Totals MeshTree::subtreeTotals(NodeId id) const
    {
        check(id);
        Totals totals;
        std::set<std::string> textures;
        std::vector<NodeId> stack{ id };
        while (!stack.empty())
        {
            const NodeId current = stack.back();
            stack.pop_back();
            const MeshNode& node = mNodes[current];
            ++totals.nodes;
            totals.vertices += node.vertexCount;
            totals.triangles += node.triangleCount;
            for (const std::string& texture : node.textures)
                textures.insert(lower(texture));
            for (NodeId child : node.children)
                stack.push_back(child);
        }
        totals.textures = static_cast<std::uint32_t>(textures.size());
        return totals;
    }

    std::vector<NodeId> MeshTree::depthFirst() const
    {
        std::vector<NodeId> result;
        result.reserve(mNodes.size());
        std::vector<NodeId> stack;
        const std::vector<NodeId> rootList = roots();
        for (auto it = rootList.rbegin(); it != rootList.rend(); ++it)
            stack.push_back(*it);
        while (!stack.empty())
        {
            const NodeId current = stack.back();
            stack.pop_back();
            result.push_back(current);
            const auto& children = mNodes[current].children;
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back(*it);
        }
        return result;
    }
}
