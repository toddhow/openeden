#include "daeanim.hpp"

#include <iostream>
#include <iterator>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include <pugixml.hpp>

#include <components/animation/constant.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/interpolate.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/textkeys.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/assimpscene.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

namespace vsgAdapters
{
    namespace
    {
        struct NodeKeys
        {
            std::map<float, vsg::vec3> mTranslations;
            std::map<float, vsg::quat> mRotations;
            std::map<float, float> mScales;
        };

        // Shifts the sampler times of every animation an <animation_clip> instances by the clip's start. COLLADA
        // exporters such as Blender's write each clip's samplers from time 0 and place the clip on the file's
        // timeline with start/end; the text keys (.txt) use that timeline. OpenMW 0.51 got the same result from OSG's
        // per-clip animations (OsgAnimationController::getTransformForNode subtracts the clip start). assimp ignores
        // <library_animation_clips>, so the offset is applied to the document before assimp reads it.
        // Returns false if the document has no clips and needs no change.
        bool applyClipStartTimes(pugi::xml_document& document, const std::string& filename)
        {
            const pugi::xml_node collada = document.child("COLLADA");
            std::map<std::string, double, std::less<>> clipStarts;
            for (const pugi::xml_node clip : collada.child("library_animation_clips").children("animation_clip"))
            {
                const double start = clip.attribute("start").as_double(0.0);
                for (const pugi::xml_node instance : clip.children("instance_animation"))
                {
                    std::string_view url = instance.attribute("url").as_string();
                    if (url.starts_with('#'))
                        url.remove_prefix(1);
                    if (!clipStarts.emplace(std::string(url), start).second)
                        Log(Debug::Warning) << "Animation \"" << url << "\" is used by more than one clip in "
                                            << filename << "; only the first clip's start time is applied";
                }
            }
            if (clipStarts.empty())
                return false;

            // A time source shared by several samplers is shifted once.
            std::set<pugi::xml_node> shiftedArrays;
            auto offsetSamplers = [&](const pugi::xml_node animation, double offset, const auto& self) -> void {
                for (const pugi::xml_node sampler : animation.children("sampler"))
                {
                    const pugi::xml_node input = sampler.find_child_by_attribute("input", "semantic", "INPUT");
                    std::string_view source = input.attribute("source").as_string();
                    if (source.starts_with('#'))
                        source.remove_prefix(1);
                    const pugi::xml_node array
                        = animation.find_child_by_attribute("source", "id", std::string(source).c_str())
                              .child("float_array");
                    if (!array || !shiftedArrays.insert(array).second)
                        continue;
                    // The C locale, so that assimp reads the numbers back whatever the global locale is.
                    std::istringstream values(array.text().as_string());
                    values.imbue(std::locale::classic());
                    std::ostringstream shifted;
                    shifted.imbue(std::locale::classic());
                    shifted.precision(17);
                    for (double value; values >> value;)
                        shifted << (value + offset) << ' ';
                    array.text().set(shifted.str().c_str());
                }
                for (const pugi::xml_node child : animation.children("animation"))
                    self(child, offset, self);
            };

            // Clips may instance nested animations (Blender 2.8 and later nests them in an action container).
            auto applyToAnimations = [&](const pugi::xml_node parent, const auto& self) -> void {
                for (const pugi::xml_node animation : parent.children("animation"))
                {
                    const auto start = clipStarts.find(std::string_view(animation.attribute("id").as_string()));
                    if (start != clipStarts.end())
                    {
                        if (start->second != 0.0)
                            offsetSamplers(animation, start->second, offsetSamplers);
                    }
                    else
                        self(animation, self);
                }
            };
            applyToAnimations(collada.child("library_animations"), applyToAnimations);
            return true;
        }

        template <class T>
        Anim::channel_ptr<T> makeChannel(const std::map<float, T>& keys)
        {
            if (keys.empty())
                return {};
            if (keys.size() == 1)
                return Anim::make_constant(keys.begin()->second);
            auto channel = Anim::make_channel<Anim::Linear<T>>();
            channel->keys = keys;
            return channel;
        }

        // Text keys, one "group: key time" per line, as OpenMW 0.51 read them (RetrieveAnimationsVisitor).
        vsg::ref_ptr<Anim::Tags> readTextKeys(const VFS::Manager& vfs, const std::string& modelPath)
        {
            auto tags = vsg::ref_ptr{ new Anim::Tags };
            VFS::Path::Normalized path(modelPath);
            constexpr VFS::Path::ExtensionView txt("txt");
            path.changeExtension(txt);
            const Files::IStreamPtr file = vfs.find(path);
            if (!file)
            {
                Log(Debug::Warning) << "Text key file is not found: " << path;
                return tags;
            }
            std::string line;
            while (std::getline(*file, line))
                Anim::addTextKeyLine(*tags, line); // the one text key parser (task 0124)
            return tags;
        }
    }

    vsg::ref_ptr<vsg::Object> daeAnimation::read(std::istream& stream, vsg::ref_ptr<const vsg::Options> options) const
    {
        if (!vsg::compatibleExtension(options.get(), ".dae"))
            return {};

        std::string filename;
        options->getValue("filename", filename);
        try
        {
            std::string content(std::istreambuf_iterator<char>(stream), {});
            if (content.find("<library_animations") == std::string::npos)
                return {};
            if (content.find("<library_animation_clips") != std::string::npos)
            {
                pugi::xml_document document;
                const pugi::xml_parse_result parsed = document.load_buffer(content.data(), content.size());
                if (!parsed)
                    throw std::runtime_error(std::string("XML error: ") + parsed.description());
                if (applyClipStartTimes(document, filename))
                {
                    std::ostringstream shifted;
                    document.save(shifted, "", pugi::format_raw);
                    content = shifted.str();
                }
            }

            Assimp::Importer importer;
            std::string error;
            const aiScene* scene = Resource::readAssimpScene(importer, content, "dae", 0, error);
            if (scene == nullptr)
                throw std::runtime_error(error);
            // As 0.51's KeyframeManager, a model without animations is no animation source.
            if (scene->mNumAnimations == 0)
                return {};
            const bool rename = Resource::hasSkinnedMeshes(*scene);

            // Every animation in the file is merged into one timeline per node, which the text keys index.
            std::map<std::string, NodeKeys> nodes;
            for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
            {
                const aiAnimation& animation = *scene->mAnimations[a];
                const double ticksPerSecond = animation.mTicksPerSecond > 0 ? animation.mTicksPerSecond : 1.0;
                for (unsigned int c = 0; c < animation.mNumChannels; ++c)
                {
                    const aiNodeAnim& channel = *animation.mChannels[c];
                    std::string_view nodeName(channel.mNodeName.C_Str(), channel.mNodeName.length);
                    NodeKeys& keys
                        = nodes[rename ? Misc::StringUtils::underscoresToSpaces(nodeName) : std::string(nodeName)];
                    for (unsigned int k = 0; k < channel.mNumPositionKeys; ++k)
                    {
                        const aiVectorKey& key = channel.mPositionKeys[k];
                        keys.mTranslations[static_cast<float>(key.mTime / ticksPerSecond)]
                            = vsg::vec3(key.mValue.x, key.mValue.y, key.mValue.z);
                    }
                    for (unsigned int k = 0; k < channel.mNumRotationKeys; ++k)
                    {
                        const aiQuatKey& key = channel.mRotationKeys[k];
                        keys.mRotations[static_cast<float>(key.mTime / ticksPerSecond)]
                            = vsg::quat(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w);
                    }
                    // Anim::Transform controllers scale uniformly, as NIF keyframes do.
                    for (unsigned int k = 0; k < channel.mNumScalingKeys; ++k)
                    {
                        const aiVectorKey& key = channel.mScalingKeys[k];
                        keys.mScales[static_cast<float>(key.mTime / ticksPerSecond)] = key.mValue.x;
                    }
                }
            }

            auto controllers = vsg::ref_ptr{ new Anim::ControllerMap };
            controllers->tags = readTextKeys(mVfs, filename);
            for (const auto& [name, keys] : nodes)
            {
                auto ctrl = Anim::TransformController::create();
                ctrl->translate = makeChannel(keys.mTranslations);
                ctrl->rotate = makeChannel(keys.mRotations);
                ctrl->scale = makeChannel(keys.mScales);
                controllers->map.emplace(name, std::move(ctrl));
            }
            return controllers;
        }
        catch (std::exception& e)
        {
            std::cerr << "!daeAnimation::read(" << filename << "): " << e.what() << std::endl;
            return {};
        }
    }

    bool daeAnimation::getFeatures(Features& features) const
    {
        features.extensionFeatureMap[".dae"] = READ_ISTREAM;
        return true;
    }
}
