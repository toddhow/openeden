#ifndef CSM_MESH_MESHTREE_H
#define CSM_MESH_MESHTREE_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CSMMesh
{
    using NodeId = int;
    constexpr NodeId kNoNode = -1;

    /// One node of a loaded model's scene graph, as the Node Tree and Properties panels show it (task 0043).
    struct MeshNode
    {
        std::string name;
        std::string type;
        NodeId parent = kNoNode;
        std::vector<NodeId> children;
        bool visible = true;
        std::array<double, 3> translation{};
        std::array<double, 3> scale{ 1, 1, 1 };
        std::uint32_t vertexCount = 0;
        std::uint32_t triangleCount = 0;
        std::vector<std::string> textures;
        std::string flags;
    };

    /// A tree of nodes in an arena: ids are indices and stay valid. Plain data, no Qt, no scene graph types.
    /// Ownership: owns its nodes.
    class MeshTree
    {
    public:
        struct Totals
        {
            std::uint32_t nodes = 0;
            std::uint32_t vertices = 0;
            std::uint32_t triangles = 0;
            std::uint32_t textures = 0;
        };

        /// \a parent == kNoNode makes a root. \throws std::out_of_range for any other unknown parent.
        NodeId addNode(NodeId parent, std::string name, std::string type);

        std::size_t size() const { return mNodes.size(); }
        const MeshNode& node(NodeId id) const;
        MeshNode& mutableNode(NodeId id);
        std::vector<NodeId> roots() const;

        void setVisible(NodeId id, bool visible);
        /// The node's own flag and every ancestor's.
        bool isEffectivelyVisible(NodeId id) const;

        std::string path(NodeId id) const;
        std::vector<NodeId> findByName(std::string_view text, bool caseInsensitive = true) const;
        /// Nodes whose name or type contains \a text (case-insensitive), plus their ancestors, depth first.
        std::vector<NodeId> filter(std::string_view text) const;
        Totals subtreeTotals(NodeId id) const;
        /// Parents before children, children in insertion order, roots in insertion order.
        std::vector<NodeId> depthFirst() const;

    private:
        void check(NodeId id) const;

        std::vector<MeshNode> mNodes;
    };
}

#endif
