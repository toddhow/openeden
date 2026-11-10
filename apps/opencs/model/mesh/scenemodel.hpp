#ifndef CSM_MESH_SCENEMODEL_H
#define CSM_MESH_SCENEMODEL_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <components/nifscene/scene.hpp>

#include "framemath.hpp"
#include "meshgeometry.hpp"

namespace CSMMesh
{
    /// Which reader the mesh viewer uses for a NIF file (task 0086 stage e).
    enum class NifReader
    {
        Engine, // MWAnim::createObject, as before: Morrowind and everything the engine reader handles
        Scene, // Nif::Scene: Skyrim Special Edition (version 20.2.0.7), which the engine's renderer has no path for
    };

    /// Chooses by the version in the file's first line ("Gamebryo File Format, Version 20.2.0.7"). Only 20.2.0.7 takes
    /// the scene path; anything else, including text that has no version, takes the engine reader, so a Morrowind file
    /// is opened exactly as before and a bad file gets the engine reader's error.
    NifReader chooseNifReader(std::string_view fileStart);

    /// A node of the converted scene. Parents come before their children.
    struct SceneModelNode
    {
        std::string name;
        std::string type; // "Transform" for a NiNode, "Geometry" for a shape
        int parent = -1; // index into SceneModel::nodes
        std::array<double, 3> translation{}; // world
        std::array<double, 3> scale{ 1, 1, 1 }; // world
        std::array<double, 16> world{}; // column-major, translation in 12..14
        int shape = -1; // index into SceneModel::shapes
        std::uint32_t vertexCount = 0;
        std::uint32_t triangleCount = 0;
        std::string flags;
        std::vector<std::string> textures;
    };

    /// One shape ready to draw. Positions and normals are in the model's frame, placed by the shape node's world
    /// transform; a skinned shape is therefore at its bind pose.
    struct SceneModelShape
    {
        int node = -1; // index into SceneModel::nodes
        IndexedMesh mesh;
        std::array<float, 4> colour{ 0.72f, 0.72f, 0.72f, 1.f }; // a flat colour standing in for the material
        bool skinned = false;
        /// The diffuse texture path of the shape's texture set (slot 0), as the file says it; empty when it has none.
        std::string diffuse;
        /// A skin, hair or effect colour: the colour is the material's own and a texture does not replace it.
        bool tinted = false;
        /// `colour` is the mean colour of the diffuse texture (task 0086, texturecolour.hpp).
        bool textured = false;
    };

    /// Plain data converted from a Nif::Scene. No Qt, no GPU.
    struct SceneModel
    {
        std::vector<SceneModelNode> nodes;
        std::vector<SceneModelShape> shapes;
        /// Parent to child, between nodes that are not shapes, in world space: the skeleton of a skeleton file.
        std::vector<std::array<std::array<float, 3>, 2>> hierarchyLines;
        /// Where each skin bone's origin is at the bind pose, in the model's frame (the joint overlay of a body).
        std::vector<std::array<float, 3>> bonePoints;
        /// Texture file names (the diffuse slot of each texture set a shape uses), each once, as the file says them.
        std::vector<std::string> textureNames;
        Aabb bounds;
        std::vector<std::string> warnings;
    };

    /// The colour a shape is drawn in: a skin tint or hair tint shader type uses its tint, an effect shader its base
    /// colour, and anything else (or a zero colour) a neutral grey. Alpha is 1.
    std::array<float, 4> materialColour(const NifScene::ShaderMaterial* material);

    /// Position of a bone's origin in the shape's own frame (the frame its vertices are stored in), from its bind
    /// transform (bone-from-shape: v_bone = s R v_shape + t): -R^T t / s. A zero scale gives the origin.
    std::array<float, 3> bindBonePosition(const NifScene::Transform& bind);

    SceneModel buildSceneModel(const NifScene::Scene& scene);
}

#endif
