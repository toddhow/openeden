#include "apps/opencs/model/mesh/meshtree.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace CSMMesh
{
    namespace
    {
        // Root
        //  |- Body (NiTriShape)
        //  |   `- Hand (NiNode)
        //  `- Head (NiTriShape)
        struct Sample
        {
            MeshTree tree;
            NodeId root, body, hand, head;

            Sample()
            {
                root = tree.addNode(kNoNode, "Root", "NiNode");
                body = tree.addNode(root, "Body", "NiTriShape");
                hand = tree.addNode(body, "Hand", "NiNode");
                head = tree.addNode(root, "Head", "NiTriShape");
            }
        };

        TEST(CSMMeshMeshTreeTest, emptyTreeHasNoNodesOrRoots)
        {
            const MeshTree tree;
            EXPECT_EQ(tree.size(), 0u);
            EXPECT_TRUE(tree.roots().empty());
            EXPECT_TRUE(tree.depthFirst().empty());
            EXPECT_TRUE(tree.filter("").empty());
        }

        TEST(CSMMeshMeshTreeTest, addNodeStoresNameTypeParentAndChildrenInInsertionOrder)
        {
            Sample s;
            EXPECT_EQ(s.tree.size(), 4u);

            const MeshNode& root = s.tree.node(s.root);
            EXPECT_EQ(root.name, "Root");
            EXPECT_EQ(root.type, "NiNode");
            EXPECT_EQ(root.parent, kNoNode);
            ASSERT_EQ(root.children.size(), 2u);
            EXPECT_EQ(root.children[0], s.body);
            EXPECT_EQ(root.children[1], s.head);

            EXPECT_EQ(s.tree.node(s.hand).parent, s.body);
            EXPECT_EQ(s.tree.node(s.body).parent, s.root);
        }

        TEST(CSMMeshMeshTreeTest, newNodesUseTheDefaults)
        {
            Sample s;
            const MeshNode& node = s.tree.node(s.body);
            EXPECT_TRUE(node.visible);
            EXPECT_EQ(node.translation, (std::array<double, 3>{ 0, 0, 0 }));
            EXPECT_EQ(node.scale, (std::array<double, 3>{ 1, 1, 1 }));
            EXPECT_EQ(node.vertexCount, 0u);
            EXPECT_EQ(node.triangleCount, 0u);
            EXPECT_TRUE(node.textures.empty());
            EXPECT_TRUE(node.flags.empty());
        }

        TEST(CSMMeshMeshTreeTest, addNodeReturnsDistinctIds)
        {
            Sample s;
            EXPECT_NE(s.root, s.body);
            EXPECT_NE(s.body, s.hand);
            EXPECT_NE(s.hand, s.head);
            EXPECT_NE(s.root, s.head);
        }

        TEST(CSMMeshMeshTreeTest, addNodeWithUnknownParentThrowsOutOfRange)
        {
            Sample s;
            EXPECT_THROW(s.tree.addNode(1000, "Bad", "NiNode"), std::out_of_range);
            EXPECT_THROW(s.tree.addNode(-5, "Bad", "NiNode"), std::out_of_range);
            EXPECT_EQ(s.tree.size(), 4u);
        }

        TEST(CSMMeshMeshTreeTest, unknownNodeIdThrowsOutOfRange)
        {
            Sample s;
            EXPECT_THROW(s.tree.node(1000), std::out_of_range);
            EXPECT_THROW(s.tree.node(kNoNode), std::out_of_range);
            EXPECT_THROW(s.tree.mutableNode(1000), std::out_of_range);
            const MeshTree empty;
            EXPECT_THROW(empty.node(0), std::out_of_range);
        }

        TEST(CSMMeshMeshTreeTest, mutableNodeChangesAreSeenByNode)
        {
            Sample s;
            MeshNode& node = s.tree.mutableNode(s.body);
            node.vertexCount = 12;
            node.triangleCount = 5;
            node.flags = "Hidden";
            node.textures.push_back("tx_a");
            EXPECT_EQ(s.tree.node(s.body).vertexCount, 12u);
            EXPECT_EQ(s.tree.node(s.body).triangleCount, 5u);
            EXPECT_EQ(s.tree.node(s.body).flags, "Hidden");
            ASSERT_EQ(s.tree.node(s.body).textures.size(), 1u);
        }

        TEST(CSMMeshMeshTreeTest, furtherRootsAreAllowedAndListedInOrder)
        {
            Sample s;
            const NodeId second = s.tree.addNode(kNoNode, "Second", "NiNode");
            const NodeId child = s.tree.addNode(second, "Child", "NiNode");
            EXPECT_EQ(s.tree.node(second).parent, kNoNode);

            const std::vector<NodeId> roots = s.tree.roots();
            ASSERT_EQ(roots.size(), 2u);
            EXPECT_EQ(roots[0], s.root);
            EXPECT_EQ(roots[1], second);

            // depth first visits the first root's subtree, then the second root's
            const std::vector<NodeId> order = s.tree.depthFirst();
            const std::vector<NodeId> expected{ s.root, s.body, s.hand, s.head, second, child };
            EXPECT_EQ(order, expected);
        }

        TEST(CSMMeshMeshTreeTest, depthFirstPutsParentsBeforeChildrenInInsertionOrder)
        {
            Sample s;
            const std::vector<NodeId> expected{ s.root, s.body, s.hand, s.head };
            EXPECT_EQ(s.tree.depthFirst(), expected);
        }

        TEST(CSMMeshMeshTreeTest, depthFirstIsDeterministic)
        {
            Sample a;
            Sample b;
            EXPECT_EQ(a.tree.depthFirst(), b.tree.depthFirst());
            EXPECT_EQ(a.tree.depthFirst(), a.tree.depthFirst());
        }

        TEST(CSMMeshMeshTreeTest, setVisibleChangesOnlyTheOwnFlag)
        {
            Sample s;
            s.tree.setVisible(s.body, false);
            EXPECT_FALSE(s.tree.node(s.body).visible);
            EXPECT_TRUE(s.tree.node(s.hand).visible);
            EXPECT_TRUE(s.tree.node(s.root).visible);
        }

        TEST(CSMMeshMeshTreeTest, effectiveVisibilityNeedsOwnFlagAndAllAncestors)
        {
            Sample s;
            EXPECT_TRUE(s.tree.isEffectivelyVisible(s.hand));

            s.tree.setVisible(s.body, false);
            EXPECT_FALSE(s.tree.isEffectivelyVisible(s.body));
            EXPECT_FALSE(s.tree.isEffectivelyVisible(s.hand)); // ancestor hidden
            EXPECT_TRUE(s.tree.isEffectivelyVisible(s.head)); // sibling unaffected
            EXPECT_TRUE(s.tree.isEffectivelyVisible(s.root));

            s.tree.setVisible(s.body, true);
            s.tree.setVisible(s.hand, false);
            EXPECT_TRUE(s.tree.isEffectivelyVisible(s.body));
            EXPECT_FALSE(s.tree.isEffectivelyVisible(s.hand));

            s.tree.setVisible(s.hand, true);
            s.tree.setVisible(s.root, false);
            EXPECT_FALSE(s.tree.isEffectivelyVisible(s.root));
            EXPECT_FALSE(s.tree.isEffectivelyVisible(s.hand));
            EXPECT_FALSE(s.tree.isEffectivelyVisible(s.head));
        }

        TEST(CSMMeshMeshTreeTest, pathJoinsNamesWithSlashesAndShowsEmptyNamesAsUnnamed)
        {
            Sample s;
            EXPECT_EQ(s.tree.path(s.root), "Root");
            EXPECT_EQ(s.tree.path(s.body), "Root/Body");
            EXPECT_EQ(s.tree.path(s.hand), "Root/Body/Hand");

            const NodeId anon = s.tree.addNode(s.head, "", "NiNode");
            EXPECT_EQ(s.tree.path(anon), "Root/Head/<unnamed>");

            const NodeId anonRoot = s.tree.addNode(kNoNode, "", "NiNode");
            EXPECT_EQ(s.tree.path(anonRoot), "<unnamed>");
        }

        TEST(CSMMeshMeshTreeTest, pathOfUnknownNodeThrows)
        {
            Sample s;
            EXPECT_THROW(s.tree.path(1000), std::out_of_range);
        }

        TEST(CSMMeshMeshTreeTest, findByNameMatchesSubstringsCaseInsensitivelyByDefault)
        {
            Sample s;
            const NodeId second = s.tree.addNode(s.root, "bip01 Head", "NiNode");

            const std::vector<NodeId> found = s.tree.findByName("head");
            const std::vector<NodeId> expected{ s.head, second };
            EXPECT_EQ(found, expected);

            EXPECT_EQ(s.tree.findByName("HAN"), std::vector<NodeId>{ s.hand });
            EXPECT_TRUE(s.tree.findByName("nothing").empty());
        }

        TEST(CSMMeshMeshTreeTest, findByNameCanBeCaseSensitive)
        {
            Sample s;
            s.tree.addNode(s.root, "bip01 head", "NiNode");
            const std::vector<NodeId> found = s.tree.findByName("Head", false);
            ASSERT_EQ(found.size(), 1u);
            EXPECT_EQ(found[0], s.head);
            EXPECT_TRUE(s.tree.findByName("HEAD", false).empty());
        }

        TEST(CSMMeshMeshTreeTest, filterWithEmptyTextReturnsEveryNode)
        {
            Sample s;
            EXPECT_EQ(s.tree.filter(""), s.tree.depthFirst());
        }

        TEST(CSMMeshMeshTreeTest, filterKeepsMatchesAndTheirAncestorsInDepthFirstOrder)
        {
            Sample s;
            // "Hand" matches only the hand; its ancestors Body and Root come along, Head does not
            const std::vector<NodeId> expected{ s.root, s.body, s.hand };
            EXPECT_EQ(s.tree.filter("Hand"), expected);
        }

        TEST(CSMMeshMeshTreeTest, filterMatchesTheTypeAsWellAsTheName)
        {
            Sample s;
            // NiTriShape is the type of Body and Head; Root is their ancestor; Hand is neither match nor ancestor
            const std::vector<NodeId> expected{ s.root, s.body, s.head };
            EXPECT_EQ(s.tree.filter("NiTriShape"), expected);
        }

        TEST(CSMMeshMeshTreeTest, filterWithNoMatchIsEmpty)
        {
            Sample s;
            EXPECT_TRUE(s.tree.filter("zzz").empty());
        }

        TEST(CSMMeshMeshTreeTest, filterListsAnAncestorOnlyOnce)
        {
            Sample s;
            // both Body and Head match "Bo|He"? use a text matching two siblings under one parent
            s.tree.addNode(s.root, "Tail", "NiTriShape");
            const std::vector<NodeId> result = s.tree.filter("NiTriShape");
            int rootCount = 0;
            for (NodeId id : result)
                if (id == s.root)
                    ++rootCount;
            EXPECT_EQ(rootCount, 1);
        }

        TEST(CSMMeshMeshTreeTest, subtreeTotalsSumVerticesAndTrianglesAndCountNodes)
        {
            Sample s;
            s.tree.mutableNode(s.body).vertexCount = 10;
            s.tree.mutableNode(s.body).triangleCount = 4;
            s.tree.mutableNode(s.hand).vertexCount = 6;
            s.tree.mutableNode(s.hand).triangleCount = 2;
            s.tree.mutableNode(s.head).vertexCount = 100;
            s.tree.mutableNode(s.head).triangleCount = 50;

            const auto body = s.tree.subtreeTotals(s.body);
            EXPECT_EQ(body.nodes, 2u); // itself and the hand
            EXPECT_EQ(body.vertices, 16u);
            EXPECT_EQ(body.triangles, 6u);

            const auto all = s.tree.subtreeTotals(s.root);
            EXPECT_EQ(all.nodes, 4u);
            EXPECT_EQ(all.vertices, 116u);
            EXPECT_EQ(all.triangles, 56u);

            const auto leaf = s.tree.subtreeTotals(s.head);
            EXPECT_EQ(leaf.nodes, 1u);
            EXPECT_EQ(leaf.vertices, 100u);
        }

        TEST(CSMMeshMeshTreeTest, subtreeTotalsCountDistinctTexturesIgnoringCase)
        {
            Sample s;
            s.tree.mutableNode(s.body).textures = { "textures/Tx_Wood.dds", "textures/tx_rock.dds" };
            s.tree.mutableNode(s.hand).textures = { "TEXTURES/TX_WOOD.DDS" };
            s.tree.mutableNode(s.head).textures = { "textures/tx_skin.dds" };

            EXPECT_EQ(s.tree.subtreeTotals(s.body).textures, 2u);
            EXPECT_EQ(s.tree.subtreeTotals(s.root).textures, 3u);
            EXPECT_EQ(s.tree.subtreeTotals(s.head).textures, 1u);
        }

        TEST(CSMMeshMeshTreeTest, subtreeTotalsOfUnknownNodeThrows)
        {
            Sample s;
            EXPECT_THROW(s.tree.subtreeTotals(1000), std::out_of_range);
        }
    }
}
