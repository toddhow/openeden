#ifndef OPENEDEN_COMPONENTS_RESOURCE_ASSIMPSCENE_H
#define OPENEDEN_COMPONENTS_RESOURCE_ASSIMPSCENE_H

#include <string>
#include <string_view>
#include <vector>

#include <assimp/matrix4x4.h>

struct aiScene;

namespace Assimp
{
    class Importer;
}

namespace Resource
{
    // openeden-vsg: OpenMW 0.51 read non-NIF models (COLLADA) with OpenSceneGraph's plugins. The VSG build reads them
    // with assimp; the model, animation and collision readers share these settings so that all three see the same
    // scene.

    /// assimp converts every model to Y up at its root node; the game world is Z up, as OSG's loaders made it in
    /// OpenMW 0.51. This undoes assimp's Z_UP-to-Y_UP rotation (ColladaLoader::InternReadFile):
    /// (x, y, z) -> (x, -z, y).
    extern const aiMatrix4x4 sAssimpYUpToZUp;

    /// Reads a model from memory with OpenEden's importer settings: no generated skeleton meshes, and COLLADA nodes
    /// named by their name rather than their id. The scene is owned by the importer. Returns null and sets error on
    /// failure.
    const aiScene* readAssimpScene(Assimp::Importer& importer, std::string_view content, std::string_view extension,
        unsigned int postProcessFlags, std::string& error);

    /// Whether any mesh of the scene is skinned. As in OpenMW 0.51 (SceneManager::loadNode), node, bone and animation
    /// target names of such a COLLADA model have their underscores replaced with spaces ("Bip01_Spine" is
    /// "Bip01 Spine").
    bool hasSkinnedMeshes(const aiScene& scene);

    /// The texts of the <Description> elements an OpenSceneGraph-profile <extra> attaches to COLLADA nodes, in
    /// document order. OpenMW 0.51 read "bodypart" and "alphatest MODE VALUE material" from them
    /// (ColladaDescriptionVisitor); assimp does not read them.
    std::vector<std::string> getColladaDescriptions(std::string_view content);
}

#endif
