#include "opas/anim/SkeletonProfile.hpp"

#include "opas/format/FileReader.hpp"
#include "opas/format/FileWriter.hpp"

#include <optional>
#include <set>
#include <utility>

namespace opas::anim
{
    namespace
    {

        using format::detail::FileReader;
        using format::detail::Json;
        using format::detail::jsonString;

        constexpr std::string_view kFormatName = "opas.skeletonprofile";
        constexpr int kMinLegChain = 2;

        // A rule broken by a complete profile: where (JSON pointer) and why. One
        // definition of the rules serves the loader and validateSkeletonProfile.
        struct Problem
        {
            std::string pointer;
            std::string message;
        };

        // Names must be non-empty and free of control characters (U+0000-U+001F,
        // U+007F), as in the other formats (docs/formats/skeletonprofile.md).
        bool isValidName(std::string_view name)
        {
            if (name.empty())
            {
                return false;
            }
            for (const char c : name)
            {
                const auto u = static_cast<unsigned char>(c);
                if (u < 0x20 || u == 0x7f)
                {
                    return false;
                }
            }
            return true;
        }

        std::optional<Problem> findProblem(const SkeletonProfile& profile)
        {
            if (!isValidName(profile.name))
            {
                return Problem{ "/name", "must be non-empty, without control characters" };
            }
            std::set<std::string> sources;
            std::set<std::string> targets;
            for (std::size_t i = 0; i < profile.bones.size(); ++i)
            {
                const std::string at = "/bones/" + std::to_string(i);
                const BoneMapping& m = profile.bones[i];
                if (!isValidName(m.source))
                {
                    return Problem{ at + "/source", "must be non-empty, without control characters" };
                }
                if (!isValidName(m.target))
                {
                    return Problem{ at + "/target", "must be non-empty, without control characters" };
                }
                if (!sources.insert(m.source).second)
                {
                    return Problem{ at + "/source", "duplicate source bone \"" + m.source + "\"" };
                }
                if (!targets.insert(m.target).second)
                {
                    return Problem{ at + "/target", "duplicate target bone \"" + m.target + "\"" };
                }
            }
            if (!profile.legChain.empty() && profile.legChain.size() < static_cast<std::size_t>(kMinLegChain))
            {
                return Problem{ "/legChain", "must be empty or name at least 2 bones" };
            }
            std::set<std::string> chain;
            for (std::size_t i = 0; i < profile.legChain.size(); ++i)
            {
                const std::string at = "/legChain/" + std::to_string(i);
                const std::string& bone = profile.legChain[i];
                if (!targets.contains(bone))
                {
                    return Problem{ at, "must be a mapped target bone" };
                }
                if (!chain.insert(bone).second)
                {
                    return Problem{ at, "duplicate bone \"" + bone + "\"" };
                }
            }
            return std::nullopt;
        }

        class ProfileReader : public FileReader
        {
        public:
            using FileReader::FileReader;

            bool readMapping(const Json& value, const std::string& pointer, BoneMapping& out)
            {
                if (!value.is_object())
                {
                    return fail(value, pointer, "must be a bone mapping object");
                }
                return onlyKeys(value, pointer, { "source", "target" })
                    && stringField(value, pointer, "source", out.source)
                    && stringField(value, pointer, "target", out.target);
            }

            bool readDocument(const Json& root, SkeletonProfile& out)
            {
                if (!readHeader(root, kFormatName, { "format", "version", "name", "bones", "legChain" })
                    || !stringField(root, "", "name", out.name))
                {
                    return false;
                }
                const auto mapping
                    = [this](const Json& v, const std::string& p, BoneMapping& o) { return readMapping(v, p, o); };
                const auto bone
                    = [this](const Json& v, const std::string& p, std::string& o) { return readString(v, p, o); };
                if (!readArray(root, "bones", out.bones, mapping) || !readArray(root, "legChain", out.legChain, bone))
                {
                    return false;
                }
                if (const auto problem = findProblem(out))
                {
                    return failAt(root, problem->pointer, problem->message);
                }
                return true;
            }
        };

    } // namespace

    SkeletonProfileLoadResult loadSkeletonProfile(std::string_view text, std::string_view fileName)
    {
        SkeletonProfileLoadResult result;
        SkeletonProfile profile;
        const std::optional<format::LoadError> error = format::detail::parseAndRead<ProfileReader>(
            text, fileName, [&profile](ProfileReader& r, const Json& root) { return r.readDocument(root, profile); });
        if (error)
        {
            result.error = *error;
        }
        else
        {
            result.value = std::move(profile);
        }
        return result;
    }

    SkeletonProfileLoadResult loadSkeletonProfileFile(const std::filesystem::path& path)
    {
        SkeletonProfileLoadResult result;
        const std::optional<std::string> text = format::detail::readTextFile(path, result.error);
        if (!text)
        {
            return result;
        }
        return loadSkeletonProfile(*text, format::detail::utf8FileName(path));
    }

    std::string writeSkeletonProfile(const SkeletonProfile& profile)
    {
        std::string out = "{\n  \"format\": \"opas.skeletonprofile\",\n  \"version\": 1,\n";
        out += "  \"name\": " + jsonString(profile.name) + ",\n";
        out += "  \"bones\": " + format::detail::arrayBlock(profile.bones, [](const BoneMapping& m) {
            return "    {\"source\": " + jsonString(m.source) + ", \"target\": " + jsonString(m.target) + "}";
        }) + ",\n";
        out += "  \"legChain\": [";
        for (std::size_t i = 0; i < profile.legChain.size(); ++i)
        {
            out += (i > 0 ? ", " : "") + jsonString(profile.legChain[i]);
        }
        out += "]\n}\n";
        return out;
    }

    std::string validateSkeletonProfile(const SkeletonProfile& profile)
    {
        const std::optional<Problem> problem = findProblem(profile);
        return problem ? problem->pointer + ": " + problem->message : std::string{};
    }

    SkeletonProfile identityProfile(const rig::Skeleton& skeleton)
    {
        SkeletonProfile profile;
        profile.name = "identity";
        for (const rig::Bone& bone : skeleton.bones)
        {
            profile.bones.push_back(BoneMapping{ bone.name, bone.name });
        }
        return profile;
    }

} // namespace opas::anim
