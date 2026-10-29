#include "colladadocuments.hpp"

#include <components/resource/assimpscene.hpp>

#include <assimp/Importer.hpp>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/vector3.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace testing;
    using namespace ColladaTesting;

    constexpr float tolerance = 1e-4f;

    // Matches the extension Resource::PhysicsShapeManager passes (Misc::getFileExtension, without the dot).
    constexpr std::string_view daeExtension = "dae";

    const aiScene* read(Assimp::Importer& importer, std::string_view content)
    {
        std::string error;
        const aiScene* scene = Resource::readAssimpScene(importer, content, daeExtension, 0, error);
        EXPECT_NE(scene, nullptr) << error;
        return scene;
    }

    std::string makeStaticBoxDocument()
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mNodes = makeNode("Box", "Box", makeMatrix(identityMatrix) + makeInstanceGeometry("box"));
        return makeColladaDocument(parts);
    }

    // Four vertices with distinct positions and distinct weights; vertex 2 has three influences so that vcount
    // entries differ from each other.
    const std::vector<std::string> skinJoints = { "Root", "Arm", "Hand" };

    const std::vector<SkinnedVertex> skinVertices = {
        { { 0, 0, 0 }, { { "Root", 1.0f } } },
        { { 1, 0, 0 }, { { "Root", 0.25f }, { "Arm", 0.75f } } },
        { { 1, 2, 0 }, { { "Root", 0.1f }, { "Arm", 0.5f }, { "Hand", 0.4f } } },
        { { 0, 2, 3 }, { { "Hand", 1.0f } } },
    };

    std::string makeSkinnedDocument(std::string_view vcountPrefix)
    {
        std::vector<Position> positions;
        for (const SkinnedVertex& vertex : skinVertices)
            positions.push_back(vertex.mPosition);

        const std::string hand = makeNode("Hand", "Hand", makeMatrix(identityMatrix), true);
        const std::string arm = makeNode("Arm", "Arm", makeMatrix(identityMatrix) + hand, true);
        const std::string root = makeNode("Root", "Root", makeMatrix(identityMatrix) + arm, true);
        const std::string mesh
            = makeNode("Mesh", "Mesh", makeMatrix(identityMatrix) + makeInstanceController("skin", "Root"));

        DocumentParts parts;
        parts.mGeometries = makeGeometry("mesh", positions, { 0, 1, 2, 0, 2, 3 });
        parts.mControllers = makeSkinController("skin", "mesh", skinJoints, skinVertices, vcountPrefix);
        parts.mNodes = root + mesh;
        return makeColladaDocument(parts);
    }

    const SkinnedVertex* findVertex(const aiVector3D& position)
    {
        for (const SkinnedVertex& vertex : skinVertices)
            if (std::abs(vertex.mPosition.mX - position.x) < tolerance
                && std::abs(vertex.mPosition.mY - position.y) < tolerance
                && std::abs(vertex.mPosition.mZ - position.z) < tolerance)
                return &vertex;
        return nullptr;
    }

    TEST(ResourceAssimpSceneTest, readAssimpScene_should_read_dae)
    {
        Assimp::Importer importer;
        const aiScene* scene = read(importer, makeStaticBoxDocument());

        ASSERT_NE(scene, nullptr);
        ASSERT_NE(scene->mRootNode, nullptr);
        EXPECT_EQ(scene->mNumMeshes, 1u);
    }

    TEST(ResourceAssimpSceneTest, readAssimpScene_for_invalid_content_should_return_null_and_set_error)
    {
        Assimp::Importer importer;
        std::string error;

        const aiScene* scene
            = Resource::readAssimpScene(importer, "this is not a COLLADA document", daeExtension, 0, error);

        EXPECT_EQ(scene, nullptr);
        EXPECT_FALSE(error.empty());
    }

    TEST(ResourceAssimpSceneTest, readAssimpScene_should_name_nodes_by_collada_name_not_id)
    {
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mNodes = makeNode("Box-node-id", "BoxName", makeMatrix(identityMatrix) + makeInstanceGeometry("box"));
        Assimp::Importer importer;

        const aiScene* scene = read(importer, makeColladaDocument(parts));

        ASSERT_NE(scene, nullptr);
        ASSERT_NE(scene->mRootNode, nullptr);
        EXPECT_NE(scene->mRootNode->FindNode("BoxName"), nullptr);
        EXPECT_EQ(scene->mRootNode->FindNode("Box-node-id"), nullptr);
    }

    TEST(ResourceAssimpSceneTest, readAssimpScene_for_model_without_meshes_should_not_generate_skeleton_mesh)
    {
        const std::string child = makeNode("Child-id", "Child", makeMatrix(makeTranslationMatrix(0, 0, 1)), true);
        DocumentParts parts;
        parts.mNodes = makeNode("Parent-id", "Parent", makeMatrix(identityMatrix) + child, true);
        Assimp::Importer importer;

        const aiScene* scene = read(importer, makeColladaDocument(parts));

        ASSERT_NE(scene, nullptr);
        EXPECT_EQ(scene->mNumMeshes, 0u);
        ASSERT_NE(scene->mRootNode, nullptr);
        EXPECT_NE(scene->mRootNode->FindNode("Parent"), nullptr);
        EXPECT_NE(scene->mRootNode->FindNode("Child"), nullptr);
    }

    struct ResourceAssimpSceneVcountTest : TestWithParam<std::string_view>
    {
    };

    TEST_P(ResourceAssimpSceneVcountTest, readAssimpScene_should_give_each_vertex_its_own_weights)
    {
        Assimp::Importer importer;
        const aiScene* scene = read(importer, makeSkinnedDocument(GetParam()));

        ASSERT_NE(scene, nullptr);
        ASSERT_GE(scene->mNumMeshes, 1u);
        unsigned checkedVertices = 0;
        for (unsigned meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh& mesh = *scene->mMeshes[meshIndex];
            ASSERT_TRUE(mesh.HasBones());

            // Vertex index -> (bone name -> weight), from assimp's per-bone weight lists.
            std::vector<std::map<std::string, float>> actual(mesh.mNumVertices);
            for (unsigned boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex)
            {
                const aiBone& bone = *mesh.mBones[boneIndex];
                for (unsigned i = 0; i < bone.mNumWeights; ++i)
                {
                    ASSERT_LT(bone.mWeights[i].mVertexId, mesh.mNumVertices);
                    actual[bone.mWeights[i].mVertexId][bone.mName.C_Str()] += bone.mWeights[i].mWeight;
                }
            }

            for (unsigned vertexIndex = 0; vertexIndex < mesh.mNumVertices; ++vertexIndex)
            {
                const aiVector3D& position = mesh.mVertices[vertexIndex];
                SCOPED_TRACE("vertex at (" + std::to_string(position.x) + ", " + std::to_string(position.y) + ", "
                    + std::to_string(position.z) + ")");
                const SkinnedVertex* expected = findVertex(position);
                ASSERT_NE(expected, nullptr);

                std::map<std::string, float> expectedWeights;
                for (const auto& [joint, weight] : expected->mWeights)
                    expectedWeights[joint] += weight;

                ASSERT_EQ(actual[vertexIndex].size(), expectedWeights.size());
                for (const auto& [joint, weight] : expectedWeights)
                {
                    SCOPED_TRACE(joint);
                    const auto found = actual[vertexIndex].find(joint);
                    ASSERT_NE(found, actual[vertexIndex].end());
                    EXPECT_NEAR(found->second, weight, tolerance);
                }
                ++checkedVertices;
            }
        }
        // Two triangles: every corner is checked.
        EXPECT_GE(checkedVertices, 4u);
    }

    INSTANTIATE_TEST_SUITE_P(VcountPrefixes, ResourceAssimpSceneVcountTest,
        Values(std::string_view(), std::string_view(" "), std::string_view("\n"), std::string_view("\n      "),
            std::string_view("\r\n\t ")));

    TEST(ResourceAssimpSceneTest, hasSkinnedMeshes_should_be_true_for_scene_with_skinned_mesh)
    {
        Assimp::Importer importer;
        const aiScene* scene = read(importer, makeSkinnedDocument({}));

        ASSERT_NE(scene, nullptr);
        EXPECT_TRUE(Resource::hasSkinnedMeshes(*scene));
    }

    TEST(ResourceAssimpSceneTest, hasSkinnedMeshes_should_be_false_for_scene_with_only_static_meshes)
    {
        Assimp::Importer importer;
        const aiScene* scene = read(importer, makeStaticBoxDocument());

        ASSERT_NE(scene, nullptr);
        EXPECT_FALSE(Resource::hasSkinnedMeshes(*scene));
    }

    TEST(ResourceAssimpSceneTest, sAssimpYUpToZUp_should_map_y_up_to_z_up)
    {
        const aiVector3D result = Resource::sAssimpYUpToZUp * aiVector3D(1, 2, 3);

        EXPECT_NEAR(result.x, 1, tolerance);
        EXPECT_NEAR(result.y, -3, tolerance);
        EXPECT_NEAR(result.z, 2, tolerance);
    }

    TEST(ResourceAssimpSceneTest, sAssimpYUpToZUp_should_undo_root_rotation_of_z_up_dae)
    {
        Assimp::Importer importer;
        const aiScene* scene = read(importer, makeStaticBoxDocument());

        ASSERT_NE(scene, nullptr);
        ASSERT_NE(scene->mRootNode, nullptr);
        const aiMatrix4x4 combined = Resource::sAssimpYUpToZUp * scene->mRootNode->mTransformation;
        const aiMatrix4x4 identity;
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                EXPECT_NEAR(combined[row][column], identity[row][column], tolerance) << row << ", " << column;
    }

    TEST(ResourceAssimpSceneTest, getColladaDescriptions_should_return_trimmed_descriptions_in_document_order)
    {
        const std::string extra
            = "<extra><technique profile=\"OpenSceneGraph\"><Descriptions>"
              "<Description>bodypart</Description>"
              "<Description>  alphatest GEQUAL 0.5 material\n</Description>"
              "</Descriptions></technique></extra>";
        const std::string secondExtra
            = "<extra><technique profile=\"OpenSceneGraph\"><Descriptions>"
              "<Description>\n\tsecond node\r\n</Description>"
              "</Descriptions></technique></extra>";
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mNodes = makeNode("First", "First", makeMatrix(identityMatrix) + makeInstanceGeometry("box") + extra)
            + makeNode("Second", "Second", makeMatrix(identityMatrix) + secondExtra);

        EXPECT_THAT(Resource::getColladaDescriptions(makeColladaDocument(parts)),
            ElementsAre("bodypart", "alphatest GEQUAL 0.5 material", "second node"));
    }

    TEST(ResourceAssimpSceneTest, getColladaDescriptions_for_document_without_descriptions_should_return_empty)
    {
        EXPECT_THAT(Resource::getColladaDescriptions(makeStaticBoxDocument()), IsEmpty());
    }
}
