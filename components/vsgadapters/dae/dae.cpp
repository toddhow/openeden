#include "dae.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <vsg/nodes/DepthSorted.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/contents.hpp>
#include <components/animation/meta.hpp>
#include <components/animation/skin.hpp>
#include <components/animation/transform.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/pipeline/sets.hpp>
#include <components/resource/assimpscene.hpp>
#include <components/vsgutil/bounds.hpp>
#include <components/vsgutil/cullnode.hpp>
#include <components/vsgutil/group.hpp>
#include <components/vsgutil/id.hpp>
#include <components/vsgutil/name.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/share.hpp>
#include <components/vsgutil/shareimage.hpp>

namespace vsgAdapters
{
    namespace
    {
        // The skinning shaders read at most four bone influences per vertex (vec4 bone indices and weights).
        constexpr unsigned int sMaxBoneInfluences = 4;

        // Post-processing for rendering. FlipUVs: COLLADA texture coordinates have their origin at the bottom left
        // (OpenGL), the images and the NIF reader's coordinates at the top left.
        constexpr unsigned int sPostProcessFlags = aiProcess_Triangulate | aiProcess_SortByPType
            | aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights | aiProcess_FlipUVs
            | aiProcess_ValidateDataStructure;

        vsg::mat4 toVsg(const aiMatrix4x4& m)
        {
            // assimp matrices are row major with the translation in the fourth column; vsg::mat4 takes columns.
            return vsg::mat4(
                m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
        }

        void setTransform(Anim::Transform& transform, const aiMatrix4x4& matrix)
        {
            aiVector3D scaling;
            aiQuaternion rotation;
            aiVector3D position;
            matrix.Decompose(scaling, rotation, position);
            transform.translation = vsg::vec3(position.x, position.y, position.z);
            transform.scale = vsg::vec3(scaling.x, scaling.y, scaling.z);
            transform.setAttitude(vsg::quat(rotation.x, rotation.y, rotation.z, rotation.w));
        }

        // Alpha test modes as stored in Pipeline::Options::alphaTestMode, which uses NiAlphaProperty's values.
        uint32_t getAlphaTestMode(std::string_view name)
        {
            static const std::pair<std::string_view, uint32_t> modes[] = { { "LESS", 1 }, { "EQUAL", 2 },
                { "LEQUAL", 3 }, { "GREATER", 4 }, { "NOTEQUAL", 5 }, { "GEQUAL", 6 }, { "NEVER", 7 } };
            for (const auto& [modeName, mode] : modes)
                if (Misc::StringUtils::ciEqual(name, modeName))
                    return mode;
            return 0;
        }

        struct AlphaTest
        {
            uint32_t mMode = 0;
            float mCutoff = 1.f;
        };

        class daeImpl
        {
            using MaterialValue = vsg::Value<Pipeline::Data::Material>;

            struct MeshOptions : public Pipeline::Options
            {
                vsg::Descriptors descriptors;
                vsg::Descriptors textures;
                Pipeline::Data::Material material = Pipeline::Material::createDefault();
                bool depthSorted = false;
            };

            const vsg::Options& mOptions;
            const vsg::ref_ptr<const vsg::Options>& mImageOptions;
            const Pipeline::Builder& mBuilder;
            const Pipeline::Override* mOverride;
            const int mDepthSortedBin;
            std::string mFilename;
            const aiScene* mScene = nullptr;
            const aiNode* mCollisionNode = nullptr;
            bool mRenameBones = false;
            bool mBodyPart = false;
            std::map<std::string, AlphaTest> mAlphaTests;
            Anim::Contents mContents;

        public:
            daeImpl(const vsg::Options& options, const vsg::ref_ptr<const vsg::Options>& imageOptions,
                const Pipeline::Builder& builder, int depthSortedBin)
                : mOptions(options)
                , mImageOptions(imageOptions)
                , mBuilder(builder)
                , mOverride(Pipeline::Override::get(options))
                , mDepthSortedBin(depthSortedBin)
            {
                options.getValue("filename", mFilename);
            }

            vsg::ref_ptr<vsg::Object> load(std::istream& stream)
            {
                const std::string content(std::istreambuf_iterator<char>(stream), {});
                handleDescriptions(Resource::getColladaDescriptions(content));

                Assimp::Importer importer;
                std::string error;
                mScene = Resource::readAssimpScene(importer, content, "dae", sPostProcessFlags, error);
                if (mScene == nullptr)
                {
                    warn("!readAssimpScene: " + error);
                    return {};
                }
                mRenameBones = Resource::hasSkinnedMeshes(*mScene);
                mCollisionNode = findCollisionNode(*mScene->mRootNode);

                vsg::Group::Children nodes;
                if (mBodyPart)
                {
                    // As 0.51's ColladaDescriptionVisitor: a body part keeps only its skinned meshes, which are
                    // attached to the actor's skeleton by bone name.
                    handleBodyPartNode(*mScene->mRootNode, nodes);
                }
                else if (auto node = handleNode(*mScene->mRootNode, true))
                    nodes.emplace_back(node);
                vsgUtil::removeGroup(nodes);
                if (nodes.empty())
                    return {};

                vsg::ref_ptr<vsg::Node> ret;
                if (!mContents.contains(Anim::Contents::DynamicBounds))
                {
                    ret = vsgUtil::createCullNode(nodes);
                    vsgUtil::addLeafCullNodes(ret, 2);
                }
                else
                {
                    ret = vsgUtil::getNode(nodes);
                    vsgUtil::addLeafCullNodes(ret);
                }
                if (!mContents.empty())
                {
                    auto meta = vsg::ref_ptr{ new Anim::Meta(mContents) };
                    meta->attachTo(*ret);
                }
                return ret;
            }

        private:
            void warn(const std::string& msg) const { std::cerr << msg << " (" << mFilename << ")" << std::endl; }

            template <class T>
            void share(vsg::ref_ptr<T>& obj)
            {
                vsgUtil::share_if(mOptions.sharedObjects, obj);
            }

            std::string getName(const aiString& name) const
            {
                std::string_view view(name.C_Str(), name.length);
                return mRenameBones ? Misc::StringUtils::underscoresToSpaces(view) : std::string(view);
            }

            void handleDescriptions(const std::vector<std::string>& descriptions)
            {
                for (const std::string& description : descriptions)
                {
                    std::vector<std::string> parts;
                    std::istringstream partStream(description);
                    for (std::string part; std::getline(partStream, part, ' ');)
                        parts.emplace_back(part);
                    if (!parts.empty() && parts[0] == "bodypart")
                        mBodyPart = true;
                    else if (parts.size() > 3 && parts[0] == "alphatest")
                        mAlphaTests[parts[3]] = AlphaTest{ getAlphaTestMode(parts[1]),
                            Misc::StringUtils::toNumeric<float>(parts[2], 0.f) };
                }
            }

            const aiNode* findCollisionNode(const aiNode& node) const
            {
                if (Misc::StringUtils::ciEqual(std::string_view(node.mName.C_Str(), node.mName.length), "Collision"))
                    return &node;
                for (unsigned int i = 0; i < node.mNumChildren; ++i)
                    if (const aiNode* found = findCollisionNode(*node.mChildren[i]))
                        return found;
                return nullptr;
            }

            vsg::ref_ptr<vsg::Node> handleNode(const aiNode& node, bool isRoot)
            {
                // As 0.51 (SceneManager::loadNode), the first node named "Collision" is not drawn.
                if (&node == mCollisionNode)
                    return {};

                auto transform = Anim::Transform::create();
                vsgUtil::setName(*transform, getName(node.mName));
                setTransform(
                    *transform, isRoot ? Resource::sAssimpYUpToZUp * node.mTransformation : node.mTransformation);
                transform->subgraphRequiresLocalFrustum = false;

                for (unsigned int i = 0; i < node.mNumMeshes; ++i)
                    if (auto mesh = handleMesh(*mScene->mMeshes[node.mMeshes[i]]))
                        transform->addChild(mesh);
                for (unsigned int i = 0; i < node.mNumChildren; ++i)
                    if (auto child = handleNode(*node.mChildren[i], false))
                        transform->addChild(child);
                return transform;
            }

            void handleBodyPartNode(const aiNode& node, vsg::Group::Children& nodes)
            {
                if (&node == mCollisionNode)
                    return;
                for (unsigned int i = 0; i < node.mNumMeshes; ++i)
                {
                    const aiMesh& mesh = *mScene->mMeshes[node.mMeshes[i]];
                    if (!mesh.HasBones())
                        continue;
                    if (auto child = handleMesh(mesh))
                        nodes.emplace_back(child);
                }
                for (unsigned int i = 0; i < node.mNumChildren; ++i)
                    handleBodyPartNode(*node.mChildren[i], nodes);
            }

            void handleSkin(const aiMesh& mesh, MeshOptions& options, vsg::DataList& dataList)
            {
                auto ctrl = Anim::Skin::create();
                auto boneIndices = vsg::vec4Array::create(mesh.mNumVertices);
                auto weights = vsg::vec4Array::create(mesh.mNumVertices);
                std::vector<unsigned int> influenceCounts(mesh.mNumVertices, 0);
                ctrl->bones.reserve(mesh.mNumBones);
                for (unsigned int b = 0; b < mesh.mNumBones; ++b)
                {
                    const aiBone& bone = *mesh.mBones[b];
                    // The offset matrix takes mesh space to bone space in the bind pose: the inverse bind matrix.
                    const vsg::mat4 invBindMatrix = toVsg(bone.mOffsetMatrix);
                    vsg::vec3 boundsMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max());
                    vsg::vec3 boundsMax = -boundsMin;
                    for (unsigned int w = 0; w < bone.mNumWeights; ++w)
                    {
                        const aiVertexWeight& weight = bone.mWeights[w];
                        if (weight.mVertexId >= mesh.mNumVertices || weight.mWeight <= 0.f)
                            continue;
                        const aiVector3D& v = mesh.mVertices[weight.mVertexId];
                        const vsg::vec4 transformed = invBindMatrix * vsg::vec4(v.x, v.y, v.z, 1.f);
                        const vsg::vec3 boneSpace(transformed.x, transformed.y, transformed.z);
                        boundsMin = vsg::vec3(std::min(boundsMin.x, boneSpace.x), std::min(boundsMin.y, boneSpace.y),
                            std::min(boundsMin.z, boneSpace.z));
                        boundsMax = vsg::vec3(std::max(boundsMax.x, boneSpace.x), std::max(boundsMax.y, boneSpace.y),
                            std::max(boundsMax.z, boneSpace.z));
                        unsigned int& count = influenceCounts[weight.mVertexId];
                        if (count < sMaxBoneInfluences)
                        {
                            (*boneIndices)[weight.mVertexId][count] = static_cast<float>(b);
                            (*weights)[weight.mVertexId][count] = weight.mWeight;
                            ++count;
                        }
                    }
                    vsg::dsphere bounds;
                    if (boundsMin.x <= boundsMax.x)
                    {
                        const vsg::vec3 center = (boundsMin + boundsMax) * 0.5f;
                        bounds = vsg::dsphere(vsg::dvec3(center), vsg::length(boundsMax - center));
                    }
                    ctrl->bones.push_back({ invBindMatrix, bounds, getName(bone.mName) });
                }
                ctrl->boneIndices = boneIndices;
                ctrl->boneWeights = weights;
                dataList.emplace_back(boneIndices);
                dataList.emplace_back(weights);

                // With the G-buffer, the previous frame's matrices follow the current ones (Anim::Skin).
                auto boneMatrices
                    = vsg::mat4Array::create(ctrl->bones.size() * (Anim::Skin::keepPreviousMatrices ? 2 : 1));
                ctrl->attachTo(*boneMatrices);
                options.descriptors.emplace_back(vsg::DescriptorBuffer::create(
                    boneMatrices, Pipeline::Descriptors::BONE_BINDING, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
                options.shader.addMode(Pipeline::Mode::SKIN);
                mContents.add(Anim::Contents::Skins | Anim::Contents::Controllers | Anim::Contents::DynamicBounds);
            }

            void handleMaterial(const aiMaterial& material, MeshOptions& options)
            {
                aiColor4D diffuse(1.f, 1.f, 1.f, 1.f);
                aiColor4D ambient(1.f, 1.f, 1.f, 1.f);
                aiColor4D emissive(0.f, 0.f, 0.f, 1.f);
                float opacity = 1.f;
                int twoSided = 0;
                material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
                material.Get(AI_MATKEY_COLOR_AMBIENT, ambient);
                material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
                material.Get(AI_MATKEY_OPACITY, opacity);
                material.Get(AI_MATKEY_TWOSIDED, twoSided);

                auto& data = options.material;
                data.diffuse = vsg::vec4(diffuse.r, diffuse.g, diffuse.b, diffuse.a * opacity);
                data.ambient = vsg::vec4(ambient.r, ambient.g, ambient.b, 1.f);
                data.emissive = vsg::vec4(emissive.r, emissive.g, emissive.b, 1.f);
                if (data.diffuse.a < 1.f)
                {
                    options.blend = true;
                    options.depthSorted = true;
                }
                if (twoSided)
                    options.cullMode = VK_CULL_MODE_NONE;

                aiString materialName;
                material.Get(AI_MATKEY_NAME, materialName);
                auto alphaTest = mAlphaTests.find(std::string(materialName.C_Str(), materialName.length));
                if (alphaTest != mAlphaTests.end())
                {
                    options.alphaTestMode = alphaTest->second.mMode;
                    data.alphaTestCutoff = alphaTest->second.mCutoff;
                }

                aiString texturePath;
                aiTextureMapMode mapModes[2] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
                unsigned int uvIndex = 0;
                if (material.GetTexture(
                        aiTextureType_DIFFUSE, 0, &texturePath, nullptr, &uvIndex, nullptr, nullptr, mapModes)
                    != AI_SUCCESS)
                    return;
                if (texturePath.length > 0 && texturePath.data[0] == '*')
                {
                    warn(std::string("Embedded textures are not supported: ") + texturePath.C_Str());
                    return;
                }
                auto image = vsgUtil::readImage(texturePath.C_Str(), mImageOptions);
                if (!image)
                    return;
                auto sampler = mBuilder.createSampler();
                sampler->addressModeU = mapModes[0] == aiTextureMapMode_Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                                              : VK_SAMPLER_ADDRESS_MODE_REPEAT;
                sampler->addressModeV = mapModes[1] == aiTextureMapMode_Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                                              : VK_SAMPLER_ADDRESS_MODE_REPEAT;
                options.textures.emplace_back(vsgUtil::sharedDescriptorImage(
                    mOptions.sharedObjects, sampler, image, Pipeline::Descriptors::DIFFUSE_UNIT));
                options.shader.addMode(Pipeline::Mode::DIFFUSE_MAP);
                if (uvIndex != 0)
                    options.nonStandardUvSets[Pipeline::Descriptors::DIFFUSE_UNIT] = uvIndex;
            }

            vsg::ref_ptr<vsg::Node> handleMesh(const aiMesh& mesh)
            {
                // After aiProcess_SortByPType each mesh holds one primitive type; lines and points are not drawn.
                if (mesh.mPrimitiveTypes != aiPrimitiveType_TRIANGLE || mesh.mNumVertices == 0)
                    return {};

                MeshOptions options;
                vsg::DataList dataList;

                auto vertices = vsg::vec3Array::create(mesh.mNumVertices);
                for (unsigned int i = 0; i < mesh.mNumVertices; ++i)
                    (*vertices)[i] = vsg::vec3(mesh.mVertices[i].x, mesh.mVertices[i].y, mesh.mVertices[i].z);
                options.shader.addMode(Pipeline::Mode::VERTEX);
                dataList.emplace_back(vertices);

                if (mesh.HasNormals())
                {
                    auto normals = vsg::vec3Array::create(mesh.mNumVertices);
                    for (unsigned int i = 0; i < mesh.mNumVertices; ++i)
                        (*normals)[i] = vsg::vec3(mesh.mNormals[i].x, mesh.mNormals[i].y, mesh.mNormals[i].z);
                    vsgUtil::setID(*normals, static_cast<int>(Pipeline::Mode::NORMAL));
                    options.shader.addMode(Pipeline::Mode::NORMAL);
                    dataList.emplace_back(normals);
                }

                if (mesh.HasBones())
                    handleSkin(mesh, options, dataList);

                for (unsigned int uvSet = 0; mesh.HasTextureCoords(uvSet); ++uvSet)
                {
                    auto texCoords = vsg::vec2Array::create(mesh.mNumVertices);
                    for (unsigned int i = 0; i < mesh.mNumVertices; ++i)
                        (*texCoords)[i] = vsg::vec2(mesh.mTextureCoords[uvSet][i].x, mesh.mTextureCoords[uvSet][i].y);
                    options.shader.addMode(Pipeline::Mode::TEXCOORD);
                    dataList.emplace_back(texCoords);
                    options.numUvSets = uvSet + 1;
                }

                std::vector<uint32_t> triangles;
                triangles.reserve(static_cast<std::size_t>(mesh.mNumFaces) * 3);
                for (unsigned int i = 0; i < mesh.mNumFaces; ++i)
                {
                    const aiFace& face = mesh.mFaces[i];
                    if (face.mNumIndices == 3)
                        triangles.insert(triangles.end(), face.mIndices, face.mIndices + 3);
                }
                if (triangles.empty())
                    return {};
                vsg::ref_ptr<vsg::Data> indices;
                if (mesh.mNumVertices <= std::numeric_limits<uint16_t>::max())
                {
                    auto shortIndices = vsg::ushortArray::create(triangles.size());
                    std::copy(triangles.begin(), triangles.end(), shortIndices->begin());
                    indices = shortIndices;
                }
                else
                {
                    auto intIndices = vsg::uintArray::create(triangles.size());
                    std::copy(triangles.begin(), triangles.end(), intIndices->begin());
                    indices = intIndices;
                }

                handleMaterial(*mScene->mMaterials[mesh.mMaterialIndex], options);

                auto geom = vsg::VertexIndexDraw::create();
                geom->assignArrays(dataList);
                geom->assignIndices(indices);
                geom->indexCount = static_cast<uint32_t>(triangles.size());
                geom->instanceCount = 1;

                auto stateGroup = createStateGroup(geom, options);
                if (options.depthSorted)
                    return vsg::DepthSorted::create(mDepthSortedBin, vsgUtil::getBounds(stateGroup), stateGroup);
                return stateGroup;
            }

            vsg::ref_ptr<vsg::DescriptorBuffer> getMaterialDescriptor(MeshOptions& options)
            {
                if (mOverride && mOverride->material)
                    return mOverride->material;
                auto data = MaterialValue::create(options.material);
                if (mOverride && mOverride->materialData)
                    mOverride->materialData(data->value());
                share(data);
                auto descriptor = vsg::DescriptorBuffer::create(
                    data, Pipeline::Descriptors::MATERIAL_BINDING, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                share(descriptor);
                return descriptor;
            }

            vsg::ref_ptr<vsg::StateGroup> createStateGroup(vsg::ref_ptr<vsg::Node> child, MeshOptions& options)
            {
                if (mOverride && mOverride->pipelineOptions)
                    mOverride->pipelineOptions(options);
                options.descriptors.emplace_back(getMaterialDescriptor(options));
                options.shader.addMode(Pipeline::Mode::MATERIAL);

                auto bindGraphicsPipeline = mBuilder.graphics->getOrCreate(options);
                vsg::ref_ptr<vsg::PipelineLayout> layout = bindGraphicsPipeline->pipeline->layout;

                vsg::Descriptors descriptors = options.descriptors;
                descriptors.insert(descriptors.end(), options.textures.begin(), options.textures.end());
                // The bone matrices change every frame, so a skinned mesh's descriptor set is its own.
                const bool dynamic = options.shader.hasMode(Pipeline::Mode::SKIN);
                auto descriptorSet = vsg::DescriptorSet::create(layout->setLayouts[Pipeline::TEXTURE_SET], descriptors);
                if (!dynamic)
                    share(descriptorSet);
                auto bindDescriptorSet = vsg::BindDescriptorSet::create(
                    VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, descriptorSet);
                if (!dynamic)
                    share(bindDescriptorSet);

                auto stateGroup = vsg::StateGroup::create();
                stateGroup->children = { child };
                stateGroup->stateCommands = { bindGraphicsPipeline, bindDescriptorSet };
                return stateGroup;
            }
        };
    }

    vsg::ref_ptr<vsg::Object> dae::read(std::istream& stream, vsg::ref_ptr<const vsg::Options> options) const
    {
        if (!vsg::compatibleExtension(options.get(), ".dae"))
            return {};
        try
        {
            return daeImpl(*options, mImageOptions, mBuilder, mDepthSortedBin).load(stream);
        }
        catch (std::exception& e)
        {
            std::string filename;
            options->getValue("filename", filename);
            std::cerr << "!dae::read(" << filename << "): " << e.what() << std::endl;
            return {};
        }
    }

    bool dae::getFeatures(Features& features) const
    {
        features.extensionFeatureMap[".dae"] = READ_ISTREAM;
        return true;
    }
}
