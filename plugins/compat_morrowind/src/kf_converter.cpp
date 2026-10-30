#include "compat_morrowind/kf_converter.hpp"

#include <components/files/istreamptr.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>

#include <nlohmann/json.hpp>
#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec3f>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace compat_morrowind
{
    namespace
    {
        std::string toLower(std::string_view str)
        {
            std::string res(str);
            for (char& c : res)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return res;
        }

        // Mapping from engine coordinate frame (+Z up, +Y forward) to glTF 2.0 (+Y up, +Z forward):
        // C = Rz(pi) * Rx(pi/2) maps glTF (x, y, z) to (-x, z, y).
        // The inverse C^-1 maps engine (X, Y, Z) to glTF (-X, Z, Y).
        osg::Vec3f toGltfTranslation(const osg::Vec3f& vEngine, float unitsPerMeter)
        {
            const float invScale = 1.0f / (std::max)(0.0001f, unitsPerMeter);
            return osg::Vec3f(-vEngine.x() * invScale, vEngine.z() * invScale, vEngine.y() * invScale);
        }

        osg::Quat toGltfRotation(const osg::Quat& qEngine)
        {
            // qC = (0, sqrt(2)/2, sqrt(2)/2, 0) in (x, y, z, w)
            // qC_inv = (0, -sqrt(2)/2, -sqrt(2)/2, 0)
            // qGltf = qC_inv * qEngine * qC
            constexpr float s = 0.7071067811865475f;
            const osg::Quat qC(0.0f, s, s, 0.0f);
            const osg::Quat qC_inv(0.0f, -s, -s, 0.0f);
            osg::Quat res = qC_inv * qEngine * qC;

            const double lenSq = static_cast<double>(res.x()) * res.x() + static_cast<double>(res.y()) * res.y()
                + static_cast<double>(res.z()) * res.z() + static_cast<double>(res.w()) * res.w();
            if (lenSq > 1e-12)
            {
                const float invLen = static_cast<float>(1.0 / std::sqrt(lenSq));
                res = res * invLen;
            }
            return res;
        }

        void collectBonesFromNode(
            const Nif::NiAVObject* obj, int parentIndex, std::vector<BoneInfo>& bones, std::map<const Nif::NiAVObject*, int>& visited)
        {
            if (!obj)
                return;

            if (visited.find(obj) != visited.end())
                return;

            int currentIndex = static_cast<int>(bones.size());
            visited[obj] = currentIndex;

            BoneInfo bone;
            bone.name = toLower(obj->mName);
            bone.parent = parentIndex;

            osg::Vec3f trans = obj->mTransform.mTranslation;
            osg::Quat rot = obj->mTransform.mRotation.toOsgMatrix().getRotate();
            float scale = obj->mTransform.mScale;

            bone.restTranslation = trans;
            bone.restRotation = rot;
            bone.restScale = (scale > 0.0001f) ? scale : 1.0f;

            if (parentIndex >= 0 && parentIndex < static_cast<int>(bones.size()))
                bones[parentIndex].children.push_back(currentIndex);

            bones.push_back(bone);

            if (obj->mRecordType == Nif::RC_NiNode)
            {
                const auto* node = static_cast<const Nif::NiNode*>(obj);
                for (const auto& child : node->mChildren)
                {
                    if (!child.empty())
                        collectBonesFromNode(child.getPtr(), currentIndex, bones, visited);
                }
            }
        }

        struct ClipGroup
        {
            std::string name;
            float startTime = 0.0f;
            float stopTime = 0.0f;
            std::vector<std::pair<std::string, float>> events;
        };
    } // namespace

    KfConverter::ConversionResult KfConverter::convertKfToGltf(
        std::span<const std::uint8_t> kfBytes,
        std::span<const std::uint8_t> skeletonNifBytes,
        const ConversionOptions& options)
    {
        ConversionResult result;
        if (kfBytes.empty())
        {
            result.errorMessage = "KF byte buffer is empty";
            return result;
        }

        // 1. Parse Skeleton NIF if provided
        std::vector<BoneInfo> bones;
        if (!skeletonNifBytes.empty())
        {
            try
            {
                Nif::NIFFile nifFile(VFS::Path::NormalizedView("skeleton.nif"));
                Nif::Reader reader(nifFile, nullptr);
                auto stream = std::make_unique<std::istringstream>(
                    std::string(reinterpret_cast<const char*>(skeletonNifBytes.data()), skeletonNifBytes.size()));
                reader.parse(std::move(stream));

                std::map<const Nif::NiAVObject*, int> visited;
                for (std::size_t i = 0; i < nifFile.mRoots.size(); ++i)
                {
                    const Nif::Record* r = nifFile.mRoots[i];
                    const auto* avObj = dynamic_cast<const Nif::NiAVObject*>(r);
                    if (avObj)
                    {
                        collectBonesFromNode(avObj, -1, bones, visited);
                    }
                }
            }
            catch (const std::exception& e)
            {
                result.errorMessage = std::string("Failed to parse skeleton NIF: ") + e.what();
                return result;
            }
        }

        // 2. Parse KF
        Nif::NIFFile kfFile(VFS::Path::NormalizedView("animation.kf"));
        KfAnimation animation;
        try
        {
            Nif::Reader kfReader(kfFile, nullptr);
            auto stream = std::make_unique<std::istringstream>(
                std::string(reinterpret_cast<const char*>(kfBytes.data()), kfBytes.size()));
            kfReader.parse(std::move(stream));

            animation = loadKfAnimation(Nif::FileView(kfFile));
        }
        catch (const std::exception& e)
        {
            result.errorMessage = std::string("Failed to load KF: ") + e.what();
            return result;
        }

        return convertParsedKfToGltf(animation, bones, options);
    }

    KfConverter::ConversionResult KfConverter::convertParsedKfToGltf(
        const KfAnimation& animation,
        const std::vector<BoneInfo>& initialBones,
        const ConversionOptions& options)
    {
        ConversionResult result;

        std::vector<BoneInfo> bones = initialBones;
        // If no skeleton was provided, construct fallback bone list from KF controllers
        if (bones.empty())
        {
            for (const auto& [name, track] : animation.mTracks)
            {
                BoneInfo b;
                b.name = toLower(name);
                bones.push_back(b);
            }
        }

        if (bones.empty())
        {
            result.errorMessage = "No bone nodes found in skeleton or KF controllers";
            return result;
        }

        std::map<std::string, int> boneNameToIndex;
        for (std::size_t i = 0; i < bones.size(); ++i)
            boneNameToIndex[bones[i].name] = static_cast<int>(i);

        // 3. Partition text keys into animation clips
        std::vector<ClipGroup> clips;
        std::set<std::string> groupNames;

        for (const auto& [time, text] : animation.mTextKeys)
        {
            auto colonPos = text.find(':');
            if (colonPos != std::string::npos)
            {
                std::string group = toLower(text.substr(0, colonPos));
                while (!group.empty() && std::isspace(static_cast<unsigned char>(group.front())))
                    group.erase(group.begin());
                while (!group.empty() && std::isspace(static_cast<unsigned char>(group.back())))
                    group.pop_back();

                if (!group.empty())
                    groupNames.insert(group);
            }
        }

        for (const auto& group : groupNames)
        {
            float startTime = -1.0f;
            float stopTime = -1.0f;

            for (const auto& [time, text] : animation.mTextKeys)
            {
                std::string lowerText = toLower(text);
                if (lowerText.rfind(group + ":", 0) == 0)
                {
                    std::string action = lowerText.substr(group.size() + 1);
                    while (!action.empty() && std::isspace(static_cast<unsigned char>(action.front())))
                        action.erase(action.begin());

                    if (action == "start" && startTime < 0.0f)
                        startTime = time;
                    else if (action == "loop start" && startTime < 0.0f)
                        startTime = time;

                    if (action == "stop")
                        stopTime = time;
                    else if (action == "loop stop" && stopTime < 0.0f)
                        stopTime = time;
                }
            }

            if (startTime >= 0.0f && stopTime > startTime)
            {
                ClipGroup clip;
                clip.name = group;
                clip.startTime = startTime;
                clip.stopTime = stopTime;

                for (const auto& [time, text] : animation.mTextKeys)
                {
                    if (time >= startTime && time <= stopTime)
                    {
                        clip.events.emplace_back(text, time - startTime);
                    }
                }
                clips.push_back(clip);
            }
        }

        // If no group keys found, create default single clip across full range
        if (clips.empty())
        {
            float minT = 0.0f;
            float maxT = 0.0f;
            bool hasKeys = false;

            for (const auto& [name, track] : animation.mTracks)
            {
                const KfTrack* nifCtrl = &track;
                {
                    const auto& transKeys = nifCtrl->getTranslationKeys();
                    if (transKeys && !transKeys->mKeys.empty())
                    {
                        if (!hasKeys)
                        {
                            minT = transKeys->mKeys.front().first;
                            maxT = transKeys->mKeys.back().first;
                            hasKeys = true;
                        }
                        else
                        {
                            minT = (std::min)(minT, transKeys->mKeys.front().first);
                            maxT = (std::max)(maxT, transKeys->mKeys.back().first);
                        }
                    }

                    const auto& rotKeys = nifCtrl->getRotationKeys();
                    if (rotKeys && !rotKeys->mKeys.empty())
                    {
                        if (!hasKeys)
                        {
                            minT = rotKeys->mKeys.front().first;
                            maxT = rotKeys->mKeys.back().first;
                            hasKeys = true;
                        }
                        else
                        {
                            minT = (std::min)(minT, rotKeys->mKeys.front().first);
                            maxT = (std::max)(maxT, rotKeys->mKeys.back().first);
                        }
                    }
                }
            }

            ClipGroup defaultClip;
            defaultClip.name = options.clipPrefix.empty() ? "animation0" : options.clipPrefix;
            defaultClip.startTime = minT;
            defaultClip.stopTime = (std::max)(minT + 0.001f, maxT);
            for (const auto& [time, text] : animation.mTextKeys)
            {
                if (time >= defaultClip.startTime && time <= defaultClip.stopTime)
                    defaultClip.events.emplace_back(text, time - defaultClip.startTime);
            }
            clips.push_back(defaultClip);
        }

        // 4. Sample and build glTF / GLB buffer
        std::vector<std::uint8_t> binData;
        auto align4 = [](std::vector<std::uint8_t>& buf) {
            while (buf.size() % 4 != 0)
                buf.push_back(0);
        };

        struct AccessorDesc
        {
            std::size_t bufferView;
            std::size_t byteOffset;
            std::size_t count;
            std::string type;
            int componentType;
            std::vector<double> min;
            std::vector<double> max;
        };

        struct BufferViewDesc
        {
            std::size_t byteOffset;
            std::size_t byteLength;
        };

        std::vector<BufferViewDesc> bufferViews;
        std::vector<AccessorDesc> accessors;

        auto addAccessorFloat = [&](const std::vector<float>& values, std::string_view type, std::size_t compCount) -> std::size_t {
            align4(binData);
            std::size_t viewOffset = binData.size();
            std::size_t byteLen = values.size() * sizeof(float);

            const auto* bytePtr = reinterpret_cast<const std::uint8_t*>(values.data());
            binData.insert(binData.end(), bytePtr, bytePtr + byteLen);

            std::size_t viewIndex = bufferViews.size();
            bufferViews.push_back({ viewOffset, byteLen });

            std::size_t elemCount = values.size() / compCount;
            std::vector<double> minVals(compCount, 1e30);
            std::vector<double> maxVals(compCount, -1e30);

            for (std::size_t i = 0; i < elemCount; ++i)
            {
                for (std::size_t c = 0; c < compCount; ++c)
                {
                    double v = values[i * compCount + c];
                    minVals[c] = (std::min)(minVals[c], v);
                    maxVals[c] = (std::max)(maxVals[c], v);
                }
            }

            std::size_t accIndex = accessors.size();
            accessors.push_back({ viewIndex, 0, elemCount, std::string(type), 5126, minVals, maxVals });
            return accIndex;
        };

        nlohmann::ordered_json animationsJson = nlohmann::ordered_json::array();
        const int rateHz = (std::max)(1, options.resampleRateHz);
        const float dt = 1.0f / static_cast<float>(rateHz);

        for (const auto& clip : clips)
        {
            nlohmann::ordered_json animEntry;
            animEntry["name"] = clip.name;
            nlohmann::ordered_json channelsJson = nlohmann::ordered_json::array();
            nlohmann::ordered_json samplersJson = nlohmann::ordered_json::array();

            const float duration = clip.stopTime - clip.startTime;
            const int numSamples = (std::max)(2, static_cast<int>(std::ceil(duration * rateHz)) + 1);

            std::vector<float> timeSamples;
            timeSamples.reserve(numSamples);
            for (int s = 0; s < numSamples; ++s)
            {
                float t = (std::min)(duration, s * dt);
                timeSamples.push_back(t);
            }
            if (timeSamples.back() < duration)
                timeSamples.push_back(duration);

            std::size_t timeAccIdx = addAccessorFloat(timeSamples, "SCALAR", 1);

            for (std::size_t boneIdx = 0; boneIdx < bones.size(); ++boneIdx)
            {
                const auto& bone = bones[boneIdx];
                auto it = animation.mTracks.find(bone.name);
                if (it == animation.mTracks.end())
                    continue;

                const KfTrack& track = it->second;

                // Check Translations
                if (track.hasTranslation())
                {
                    std::vector<float> transSamples;
                    transSamples.reserve(timeSamples.size() * 3);
                    for (float tRel : timeSamples)
                    {
                        float tAbs = clip.startTime + tRel;
                        osg::Vec3f v = track.getTransform(tAbs).mTranslation.value_or(bone.restTranslation);
                        osg::Vec3f gltfV = toGltfTranslation(v, options.unitsPerMeter);
                        transSamples.push_back(gltfV.x());
                        transSamples.push_back(gltfV.y());
                        transSamples.push_back(gltfV.z());
                    }

                    std::size_t transAccIdx = addAccessorFloat(transSamples, "VEC3", 3);
                    std::size_t samplerIdx = samplersJson.size();

                    nlohmann::ordered_json sampler;
                    sampler["input"] = timeAccIdx;
                    sampler["output"] = transAccIdx;
                    sampler["interpolation"] = "LINEAR";
                    samplersJson.push_back(sampler);

                    nlohmann::ordered_json channel;
                    channel["sampler"] = samplerIdx;
                    channel["target"] = { { "node", boneIdx }, { "path", "translation" } };
                    channelsJson.push_back(channel);
                }

                // Check Rotations
                if (track.hasRotation())
                {
                    std::vector<float> rotSamples;
                    rotSamples.reserve(timeSamples.size() * 4);
                    for (float tRel : timeSamples)
                    {
                        float tAbs = clip.startTime + tRel;
                        osg::Quat q = track.getTransform(tAbs).mRotation.value_or(bone.restRotation);
                        osg::Quat gltfQ = toGltfRotation(q);
                        rotSamples.push_back(static_cast<float>(gltfQ.x()));
                        rotSamples.push_back(static_cast<float>(gltfQ.y()));
                        rotSamples.push_back(static_cast<float>(gltfQ.z()));
                        rotSamples.push_back(static_cast<float>(gltfQ.w()));
                    }

                    std::size_t rotAccIdx = addAccessorFloat(rotSamples, "VEC4", 4);
                    std::size_t samplerIdx = samplersJson.size();

                    nlohmann::ordered_json sampler;
                    sampler["input"] = timeAccIdx;
                    sampler["output"] = rotAccIdx;
                    sampler["interpolation"] = "LINEAR";
                    samplersJson.push_back(sampler);

                    nlohmann::ordered_json channel;
                    channel["sampler"] = samplerIdx;
                    channel["target"] = { { "node", boneIdx }, { "path", "rotation" } };
                    channelsJson.push_back(channel);
                }
            }

            animEntry["channels"] = channelsJson;
            animEntry["samplers"] = samplersJson;
            animationsJson.push_back(animEntry);
        }

        // 5. Build glTF JSON metadata
        nlohmann::ordered_json gltf;
        gltf["asset"] = { { "version", "2.0" }, { "generator", "OpenEden Morrowind Compatibility Pack" } };
        gltf["scene"] = 0;

        std::vector<int> rootNodes;
        for (std::size_t i = 0; i < bones.size(); ++i)
        {
            if (bones[i].parent == -1)
                rootNodes.push_back(static_cast<int>(i));
        }
        gltf["scenes"] = nlohmann::ordered_json::array({ { { "nodes", rootNodes } } });

        nlohmann::ordered_json nodesJson = nlohmann::ordered_json::array();
        std::vector<int> jointIndices;
        for (std::size_t i = 0; i < bones.size(); ++i)
        {
            const auto& b = bones[i];
            jointIndices.push_back(static_cast<int>(i));

            nlohmann::ordered_json node;
            node["name"] = b.name;

            osg::Vec3f gltfRestTrans = toGltfTranslation(b.restTranslation, options.unitsPerMeter);
            osg::Quat gltfRestRot = toGltfRotation(b.restRotation);

            node["translation"] = { gltfRestTrans.x(), gltfRestTrans.y(), gltfRestTrans.z() };
            node["rotation"] = { static_cast<float>(gltfRestRot.x()),
                static_cast<float>(gltfRestRot.y()),
                static_cast<float>(gltfRestRot.z()),
                static_cast<float>(gltfRestRot.w()) };

            if (!b.children.empty())
                node["children"] = b.children;

            nodesJson.push_back(node);
        }
        gltf["nodes"] = nodesJson;

        gltf["skins"] = nlohmann::ordered_json::array({ { { "joints", jointIndices } } });
        gltf["animations"] = animationsJson;

        nlohmann::ordered_json bufferViewsJson = nlohmann::ordered_json::array();
        for (const auto& bv : bufferViews)
        {
            bufferViewsJson.push_back({ { "buffer", 0 }, { "byteOffset", bv.byteOffset }, { "byteLength", bv.byteLength } });
        }
        gltf["bufferViews"] = bufferViewsJson;

        nlohmann::ordered_json accessorsJson = nlohmann::ordered_json::array();
        for (const auto& acc : accessors)
        {
            accessorsJson.push_back({ { "bufferView", acc.bufferView },
                { "byteOffset", acc.byteOffset },
                { "componentType", acc.componentType },
                { "count", acc.count },
                { "type", acc.type },
                { "min", acc.min },
                { "max", acc.max } });
        }
        gltf["accessors"] = accessorsJson;

        gltf["buffers"] = nlohmann::ordered_json::array({ { { "byteLength", binData.size() } } });

        std::string jsonStr = gltf.dump();
        while (jsonStr.size() % 4 != 0)
            jsonStr.push_back(' ');

        while (binData.size() % 4 != 0)
            binData.push_back(0);

        // 6. Build .glb binary container
        const std::uint32_t headerLen = 12;
        const std::uint32_t jsonChunkHeaderLen = 8;
        const std::uint32_t jsonChunkLen = static_cast<std::uint32_t>(jsonStr.size());
        const std::uint32_t binChunkHeaderLen = 8;
        const std::uint32_t binChunkLen = static_cast<std::uint32_t>(binData.size());
        const std::uint32_t totalLen = headerLen + jsonChunkHeaderLen + jsonChunkLen + binChunkHeaderLen + binChunkLen;

        result.glbData.resize(totalLen);
        std::uint8_t* ptr = result.glbData.data();

        // 12-byte header
        const std::uint32_t magic = 0x46546C67; // "glTF"
        const std::uint32_t version = 2;
        std::memcpy(ptr, &magic, 4);
        std::memcpy(ptr + 4, &version, 4);
        std::memcpy(ptr + 8, &totalLen, 4);
        ptr += 12;

        // JSON Chunk
        const std::uint32_t jsonChunkType = 0x4E4F534A; // "JSON"
        std::memcpy(ptr, &jsonChunkLen, 4);
        std::memcpy(ptr + 4, &jsonChunkType, 4);
        ptr += 8;
        std::memcpy(ptr, jsonStr.data(), jsonChunkLen);
        ptr += jsonChunkLen;

        // BIN Chunk
        const std::uint32_t binChunkType = 0x004E4942; // "BIN\0"
        std::memcpy(ptr, &binChunkLen, 4);
        std::memcpy(ptr + 4, &binChunkType, 4);
        ptr += 8;
        std::memcpy(ptr, binData.data(), binChunkLen);

        // 7. Build opas.clipsidecar JSON
        nlohmann::ordered_json sidecar;
        sidecar["format"] = "opas.clipsidecar";
        sidecar["version"] = 1;
        nlohmann::ordered_json sidecarClips = nlohmann::ordered_json::array();

        for (const auto& clip : clips)
        {
            nlohmann::ordered_json clipObj;
            clipObj["clip"] = clip.name;
            nlohmann::ordered_json eventsArr = nlohmann::ordered_json::array();
            for (const auto& [name, time] : clip.events)
            {
                eventsArr.push_back({ { "name", name }, { "timeS", time } });
            }
            clipObj["events"] = eventsArr;
            sidecarClips.push_back(clipObj);
        }
        sidecar["clips"] = sidecarClips;
        result.sidecarJson = sidecar.dump(2);
        result.success = true;

        return result;
    }
} // namespace compat_morrowind
