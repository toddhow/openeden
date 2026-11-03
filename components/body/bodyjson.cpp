#include "bodyjson.hpp"

// nlohmann/json stays inside this file: CreativeMotion's internal reader headers are reached through the PRIVATE
// opas_format_json link (core/format/CLAUDE.md), and no JSON type appears in a public header of this library.
#include <opas/format/FileReader.hpp>

#include <opas/anim/SkeletonProfile.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Body
{
    namespace
    {
        using opas::format::LoadError;
        using opas::format::detail::FileReader;
        using opas::format::detail::Json;
        using opas::format::detail::jsonString;
        using opas::format::detail::kNonFiniteValue;
        using opas::format::detail::number;

        constexpr std::string_view sFormatName = "openeden.body";
        constexpr int sNewestVersion = 1;
        constexpr std::size_t sSmallListCount = 2; // [begin, end] and [bone, weight]

        template <typename E>
        struct EnumName
        {
            std::string_view mName;
            E mValue;
        };

        constexpr std::array<EnumName<Origin>, 3> sOrigins = { {
            { "authored", Origin::Authored },
            { "derived", Origin::Derived },
            { "missing", Origin::Missing },
        } };
        constexpr std::array<EnumName<MeshFormat>, 3> sFormats = { {
            { "nif", MeshFormat::Nif },
            { "gltf", MeshFormat::Gltf },
            { "summary", MeshFormat::Summary },
        } };
        constexpr std::array<EnumName<MeshRole>, 5> sRoles = { {
            { "skin", MeshRole::Skin },
            { "head", MeshRole::Head },
            { "hair", MeshRole::Hair },
            { "garmentBase", MeshRole::GarmentBase },
            { "other", MeshRole::Other },
        } };
        constexpr std::array<EnumName<AgeClass>, 3> sAges = { {
            { "unspecified", AgeClass::Unspecified },
            { "minor", AgeClass::Minor },
            { "adult", AgeClass::Adult },
        } };
        constexpr std::array<EnumName<ShapeKind>, 3> sShapes = { {
            { "sphere", ShapeKind::Sphere },
            { "capsule", ShapeKind::Capsule },
            { "box", ShapeKind::Box },
        } };

        // ---- Pre-scan: nesting depth and element count are checked on the text BEFORE any JSON value is built, so a
        // hostile file cannot make the parser recurse deeply or allocate for millions of values.
        std::optional<LoadError> prescan(std::string_view text, std::string_view fileName)
        {
            if (text.size() > Limits::sMaxFileBytes)
            {
                return LoadError{ std::string(fileName), 1, "",
                    "the file is larger than " + std::to_string(Limits::sMaxFileBytes) + " bytes" };
            }
            // Commas are a cheap upper bound of the number of values; the slack covers object members and lists.
            constexpr std::size_t sMaxCommas = Limits::sMaxTotalElements + Limits::sMaxArrayElements / 4;
            std::size_t depth = 0;
            std::size_t commas = 0;
            std::size_t arrays = 0;
            std::size_t objects = 0;
            std::size_t line = 1;
            bool inString = false;
            bool escaped = false;
            for (const char c : text)
            {
                if (c == '\n')
                {
                    ++line;
                }
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }
                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '[' || c == '{')
                {
                    ++(c == '[' ? arrays : objects);
                    if (arrays > Limits::sMaxArraysInFile || objects > Limits::sMaxObjectsInFile)
                    {
                        return LoadError{ std::string(fileName), line, "",
                            "the file holds too many arrays or objects" };
                    }
                    ++depth;
                    if (depth > Limits::sMaxNesting)
                    {
                        return LoadError{ std::string(fileName), line, "",
                            "nesting is deeper than " + std::to_string(Limits::sMaxNesting) + " levels" };
                    }
                }
                else if (c == ']' || c == '}')
                {
                    if (depth > 0)
                    {
                        --depth;
                    }
                }
                else if (c == ',')
                {
                    ++commas;
                    if (commas > sMaxCommas)
                    {
                        return LoadError{ std::string(fileName), line, "", "the file holds too many values" };
                    }
                }
            }
            return std::nullopt;
        }

        // ---- Reader ------------------------------------------------------------------------------------------------

        std::string at(const std::string& pointer, std::string_view key)
        {
            return pointer + "/" + std::string(key);
        }

        std::string at(const std::string& pointer, std::size_t index)
        {
            return pointer + "/" + std::to_string(index);
        }

        class BodyReader : public FileReader
        {
        public:
            using FileReader::FileReader;

            bool readDocument(const Json& root, BodyPackage& out)
            {
                if (!readHeader(root, sFormatName,
                        { "format", "version", "identity", "targets", "binding", "meshes", "morphSet", "proxy",
                            "physicsProfile", "measurements", "regionMap", "landmarks", "softTissue", "provenance" },
                        sNewestVersion))
                {
                    return false;
                }
                const Json* identity = objectField(root, "", "identity", "an object");
                if (identity == nullptr || !readIdentity(*identity, "/identity", out.mIdentity))
                {
                    return false;
                }
                if (!readObjects(root, "", "targets", Limits::sMaxListEntries, false, out.mTargets,
                        [this](const Json& v, const std::string& p, Target& o) { return readTarget(v, p, o); }))
                {
                    return false;
                }
                const Json* binding = objectField(root, "", "binding", "an object");
                if (binding == nullptr || !readBinding(*binding, "/binding", out.mBinding))
                {
                    return false;
                }
                if (!readObjects(root, "", "meshes", Limits::sMaxMeshes, true, out.mMeshes,
                        [this](const Json& v, const std::string& p, MeshEntry& o) { return readMesh(v, p, o); }))
                {
                    return false;
                }
                return readOptionalParts(root, out);
            }

        private:
            std::size_t mTotalElements = 0;

            // ---- generic helpers
            bool readText(const Json& value, const std::string& pointer, std::string& out)
            {
                if (!value.is_string() || value.get_ref<const std::string&>() == kNonFiniteValue)
                {
                    return fail(value, pointer, "must be a string");
                }
                const std::string& text = value.get_ref<const std::string&>();
                if (text.size() > Limits::sMaxStringBytes)
                {
                    return fail(value, pointer, "is longer than " + std::to_string(Limits::sMaxStringBytes) + " bytes");
                }
                out = text;
                return true;
            }

            bool textField(const Json& object, const std::string& pointer, std::string_view key, std::string& out)
            {
                const Json* value = member(object, pointer, key);
                return value != nullptr && readText(*value, at(pointer, key), out);
            }

            bool optionalTextField(
                const Json& object, const std::string& pointer, std::string_view key, std::optional<std::string>& out)
            {
                const auto found = object.find(key);
                if (found == object.end())
                {
                    return true;
                }
                std::string text;
                if (!readText(*found, at(pointer, key), text))
                {
                    return false;
                }
                out = std::move(text);
                return true;
            }

            bool readBool(const Json& value, const std::string& pointer, bool& out)
            {
                if (!value.is_boolean())
                {
                    return fail(value, pointer, "must be true or false");
                }
                out = value.get<bool>();
                return true;
            }

            std::size_t mTotalRanges = 0;

            bool readUint(const Json& value, const std::string& pointer, std::uint32_t& out)
            {
                if (!value.is_number_integer())
                {
                    return fail(value, pointer, "must be an integer");
                }
                if (value.is_number_unsigned())
                {
                    const auto number64 = value.get<std::uint64_t>();
                    if (number64 > std::numeric_limits<std::uint32_t>::max())
                    {
                        return fail(value, pointer, "is out of range");
                    }
                    out = static_cast<std::uint32_t>(number64);
                    return true;
                }
                const auto signed64 = value.get<std::int64_t>();
                if (signed64 < 0)
                {
                    return fail(value, pointer, "must not be negative");
                }
                out = static_cast<std::uint32_t>(signed64);
                return true;
            }

            bool uintField(const Json& object, const std::string& pointer, std::string_view key, std::uint32_t& out)
            {
                const Json* value = member(object, pointer, key);
                return value != nullptr && readUint(*value, at(pointer, key), out);
            }

            bool intField(const Json& object, const std::string& pointer, std::string_view key, int& out)
            {
                const Json* value = member(object, pointer, key);
                return value != nullptr && readInt(*value, at(pointer, key), out);
            }

            template <typename E, std::size_t N>
            bool readEnum(
                const Json& value, const std::string& pointer, const std::array<EnumName<E>, N>& names, E& out)
            {
                if (value.is_string())
                {
                    const std::string& text = value.get_ref<const std::string&>();
                    for (const EnumName<E>& entry : names)
                    {
                        if (text == entry.mName)
                        {
                            out = entry.mValue;
                            return true;
                        }
                    }
                }
                std::string expected;
                for (const EnumName<E>& entry : names)
                {
                    expected += (expected.empty() ? "\"" : ", \"") + std::string(entry.mName) + "\"";
                }
                return fail(value, pointer, "must be one of " + expected);
            }

            template <typename E, std::size_t N>
            bool enumField(const Json& object, const std::string& pointer, std::string_view key,
                const std::array<EnumName<E>, N>& names, E& out)
            {
                const Json* value = member(object, pointer, key);
                return value != nullptr && readEnum(*value, at(pointer, key), names, out);
            }

            // Finds a list member. `list` stays null when the member is optional and absent. Size is capped.
            bool readList(const Json& object, const std::string& pointer, std::string_view key, std::size_t limit,
                bool required, const Json*& list)
            {
                list = nullptr;
                const auto found = object.find(key);
                if (found == object.end())
                {
                    return !required || member(object, pointer, key) != nullptr;
                }
                if (!found->is_array())
                {
                    return fail(*found, at(pointer, key), "must be an array");
                }
                if (found->size() > limit)
                {
                    return fail(*found, at(pointer, key), "has more than " + std::to_string(limit) + " entries");
                }
                list = &*found;
                return true;
            }

            bool countElements(const Json& list, const std::string& pointer)
            {
                if (list.size() > Limits::sMaxArrayElements)
                {
                    return fail(
                        list, pointer, "has more than " + std::to_string(Limits::sMaxArrayElements) + " entries");
                }
                mTotalElements += list.size();
                if (mTotalElements > Limits::sMaxTotalElements)
                {
                    return fail(list, pointer, "the file holds too many numbers");
                }
                return true;
            }

            template <typename Item, typename ReadItem>
            bool readObjects(const Json& object, const std::string& pointer, std::string_view key, std::size_t limit,
                bool required, std::vector<Item>& out, ReadItem readItem)
            {
                const Json* list = nullptr;
                if (!readList(object, pointer, key, limit, required, list))
                {
                    return false;
                }
                if (list == nullptr)
                {
                    return true;
                }
                const std::string base = at(pointer, key);
                out.reserve(list->size());
                for (std::size_t i = 0; i < list->size(); ++i)
                {
                    Item item{};
                    if (!readItem((*list)[i], at(base, i), item))
                    {
                        return false;
                    }
                    out.push_back(std::move(item));
                }
                return true;
            }

            bool readStringList(const Json& object, const std::string& pointer, std::string_view key, std::size_t limit,
                bool required, std::vector<std::string>& out)
            {
                const Json* list = nullptr;
                if (!readList(object, pointer, key, limit, required, list))
                {
                    return false;
                }
                if (list == nullptr)
                {
                    return true;
                }
                const std::string base = at(pointer, key);
                for (std::size_t i = 0; i < list->size(); ++i)
                {
                    std::string text;
                    if (!readText((*list)[i], at(base, i), text))
                    {
                        return false;
                    }
                    out.push_back(std::move(text));
                }
                return true;
            }

            template <std::size_t N>
            bool readFixed(const Json& value, const std::string& pointer, std::array<double, N>& out)
            {
                if (!value.is_array() || value.size() != N)
                {
                    return fail(value, pointer, "must be an array of " + std::to_string(N) + " numbers");
                }
                for (std::size_t i = 0; i < N; ++i)
                {
                    if (!readNumber(value[i], at(pointer, i), out[i]))
                    {
                        return false;
                    }
                }
                return true;
            }

            template <std::size_t N>
            bool fixedField(
                const Json& object, const std::string& pointer, std::string_view key, std::array<double, N>& out)
            {
                const Json* value = member(object, pointer, key);
                return value != nullptr && readFixed(*value, at(pointer, key), out);
            }

            bool readUintList(
                const Json& object, const std::string& pointer, std::string_view key, std::vector<std::uint32_t>& out)
            {
                const Json* list = nullptr;
                if (!readList(object, pointer, key, Limits::sMaxArrayElements, false, list))
                {
                    return false;
                }
                if (list == nullptr)
                {
                    return true;
                }
                const std::string base = at(pointer, key);
                if (!countElements(*list, base))
                {
                    return false;
                }
                out.resize(list->size());
                for (std::size_t i = 0; i < list->size(); ++i)
                {
                    if (!readUint((*list)[i], at(base, i), out[i]))
                    {
                        return false;
                    }
                }
                return true;
            }

            // ---- identity, targets, binding
            bool readIdentity(const Json& object, const std::string& pointer, Identity& out)
            {
                if (!onlyKeys(object, pointer,
                        { "id", "name", "version", "author", "license", "contractVersion", "ageClass" }))
                {
                    return false;
                }
                const Json* id = member(object, pointer, "id");
                if (id == nullptr || !readText(*id, at(pointer, "id"), out.mId))
                {
                    return false;
                }
                if (!isValidPackageId(out.mId))
                {
                    return fail(
                        *id, at(pointer, "id"), "is not a valid id (1 to 64 characters of a-z, 0-9, '_', '.' and '-')");
                }
                if (!textField(object, pointer, "name", out.mName)
                    || !textField(object, pointer, "version", out.mVersion)
                    || !textField(object, pointer, "author", out.mAuthor)
                    || !textField(object, pointer, "license", out.mLicense))
                {
                    return false;
                }
                const Json* contract = objectField(object, pointer, "contractVersion", "an object");
                if (contract == nullptr)
                {
                    return false;
                }
                const std::string contractPointer = at(pointer, "contractVersion");
                if (!onlyKeys(*contract, contractPointer, { "major", "minor" })
                    || !intField(*contract, contractPointer, "major", out.mContractVersion.mMajor)
                    || !intField(*contract, contractPointer, "minor", out.mContractVersion.mMinor))
                {
                    return false;
                }
                const auto age = object.find("ageClass");
                return age == object.end() || readEnum(*age, at(pointer, "ageClass"), sAges, out.mAgeClass);
            }

            bool readTarget(const Json& object, const std::string& pointer, Target& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer, { "race", "sex", "creatureType", "priority" })
                    || !optionalTextField(object, pointer, "race", out.mRace)
                    || !optionalTextField(object, pointer, "sex", out.mSex)
                    || !optionalTextField(object, pointer, "creatureType", out.mCreatureType))
                {
                    return false;
                }
                const auto priority = object.find("priority");
                return priority == object.end() || readInt(*priority, at(pointer, "priority"), out.mPriority);
            }

            // The inline profile is re-dumped as indented text and read by CreativeMotion's own loader, so there is one
            // set of profile rules. Its error is mapped back: the line of the sub-object plus the loader's line, and
            // the pointer below the sub-object's pointer.
            bool readInlineProfile(const Json& object, const std::string& pointer, opas::anim::SkeletonProfile& out)
            {
                const auto bones = object.find("bones");
                if (bones != object.end() && bones->is_array() && bones->size() > Limits::sMaxBones)
                {
                    return fail(*bones, at(pointer, "bones"),
                        "has more than " + std::to_string(Limits::sMaxBones) + " entries");
                }
                const auto chain = object.find("legChain");
                if (chain != object.end() && chain->is_array() && chain->size() > Limits::sMaxBones)
                {
                    return fail(*chain, at(pointer, "legChain"),
                        "has more than " + std::to_string(Limits::sMaxBones) + " entries");
                }
                const std::string text = object.dump(2, ' ', false, Json::error_handler_t::replace);
                const opas::anim::SkeletonProfileLoadResult loaded = opas::anim::loadSkeletonProfile(text, "inline");
                if (loaded.value)
                {
                    out = *loaded.value;
                    return true;
                }
                const std::size_t start = object.start_pos();
                const std::size_t firstLine = start == std::string::npos ? 1 : lineAt(start);
                const std::size_t innerLine = loaded.error.line == 0 ? 1 : loaded.error.line;
                return failAtLine(firstLine + innerLine - 1, pointer + loaded.error.field,
                    "skeleton profile: " + loaded.error.message);
            }

            bool readBinding(const Json& object, const std::string& pointer, Binding& out)
            {
                if (!onlyKeys(object, pointer, { "skeleton", "rootBone", "unmappedBones", "restSkeleton" }))
                {
                    return false;
                }
                const Json* skeleton = member(object, pointer, "skeleton");
                if (skeleton == nullptr)
                {
                    return false;
                }
                const std::string skeletonPointer = at(pointer, "skeleton");
                if (skeleton->is_object())
                {
                    opas::anim::SkeletonProfile profile;
                    if (!readInlineProfile(*skeleton, skeletonPointer, profile))
                    {
                        return false;
                    }
                    out.mSkeletonProfile = std::move(profile);
                }
                else
                {
                    std::string path;
                    if (!skeleton->is_string())
                    {
                        return fail(*skeleton, skeletonPointer, "must be a path string or an inline profile object");
                    }
                    if (!readText(*skeleton, skeletonPointer, path))
                    {
                        return false;
                    }
                    out.mSkeletonPath = std::move(path);
                }
                if (!textField(object, pointer, "rootBone", out.mRootBone)
                    || !readStringList(object, pointer, "unmappedBones", Limits::sMaxBones, false, out.mUnmappedBones))
                {
                    return false;
                }
                std::vector<RestBone> rest;
                const Json* restList = nullptr;
                if (!readList(object, pointer, "restSkeleton", Limits::sMaxBones, false, restList))
                {
                    return false;
                }
                if (restList != nullptr)
                {
                    if (!readObjects(object, pointer, "restSkeleton", Limits::sMaxBones, false, rest,
                            [this](const Json& v, const std::string& p, RestBone& o) { return readRestBone(v, p, o); }))
                    {
                        return false;
                    }
                    out.mRestSkeleton = std::move(rest);
                }
                return true;
            }

            bool readRestBone(const Json& object, const std::string& pointer, RestBone& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                return onlyKeys(object, pointer, { "name", "parent", "translation", "rotation" })
                    && textField(object, pointer, "name", out.mName) && intField(object, pointer, "parent", out.mParent)
                    && fixedField(object, pointer, "translation", out.mTranslation)
                    && fixedField(object, pointer, "rotation", out.mRotation);
            }

            // ---- meshes
            bool readMesh(const Json& object, const std::string& pointer, MeshEntry& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer,
                        { "id", "path", "meshFormat", "lod", "role", "partType", "attachBone", "summary" }))
                {
                    return false;
                }
                const Json* id = member(object, pointer, "id");
                if (id == nullptr || !readText(*id, at(pointer, "id"), out.mId))
                {
                    return false;
                }
                if (!isValidPackageId(out.mId))
                {
                    return fail(
                        *id, at(pointer, "id"), "is not a valid id (1 to 64 characters of a-z, 0-9, '_', '.' and '-')");
                }
                const Json* path = member(object, pointer, "path");
                if (path == nullptr || !readText(*path, at(pointer, "path"), out.mPath))
                {
                    return false;
                }
                if (!isSafePackagePath(out.mPath))
                {
                    return fail(*path, at(pointer, "path"),
                        "is not a safe relative path (no empty, '.' or '..' parts, no ':' or '\\', at most 512 bytes)");
                }
                if (!enumField(object, pointer, "meshFormat", sFormats, out.mFormat)
                    || !intField(object, pointer, "lod", out.mLod)
                    || !enumField(object, pointer, "role", sRoles, out.mRole)
                    || !optionalTextField(object, pointer, "partType", out.mPartType)
                    || !optionalTextField(object, pointer, "attachBone", out.mAttachBone))
                {
                    return false;
                }
                const auto summary = object.find("summary");
                if (summary != object.end())
                {
                    MeshSummary value;
                    if (!summary->is_object())
                    {
                        return fail(*summary, at(pointer, "summary"), "must be an object");
                    }
                    if (!readSummary(*summary, at(pointer, "summary"), value))
                    {
                        return false;
                    }
                    out.mSummary = std::move(value);
                }
                return true;
            }

            bool readSummary(const Json& object, const std::string& pointer, MeshSummary& out)
            {
                if (!onlyKeys(object, pointer,
                        { "vertexCount", "triangleCount", "boundsMin", "boundsMax", "boneNames", "influenceOffsets",
                            "influences", "triangleIndices", "uvSetCount" })
                    || !uintField(object, pointer, "vertexCount", out.mVertexCount)
                    || !uintField(object, pointer, "triangleCount", out.mTriangleCount)
                    || !fixedField(object, pointer, "boundsMin", out.mBoundsMin)
                    || !fixedField(object, pointer, "boundsMax", out.mBoundsMax)
                    || !readStringList(object, pointer, "boneNames", Limits::sMaxBones, false, out.mBoneNames)
                    || !readUintList(object, pointer, "influenceOffsets", out.mInfluenceOffsets)
                    || !readUintList(object, pointer, "triangleIndices", out.mTriangleIndices))
                {
                    return false;
                }
                const Json* influences = nullptr;
                if (!readList(object, pointer, "influences", Limits::sMaxArrayElements, false, influences))
                {
                    return false;
                }
                if (influences != nullptr)
                {
                    const std::string base = at(pointer, "influences");
                    if (!countElements(*influences, base))
                    {
                        return false;
                    }
                    out.mInfluences.resize(influences->size());
                    for (std::size_t i = 0; i < influences->size(); ++i)
                    {
                        const Json& pair = (*influences)[i];
                        const std::string pairPointer = at(base, i);
                        if (!pair.is_array() || pair.size() != sSmallListCount)
                        {
                            return fail(pair, pairPointer, "must be an array [bone, weight]");
                        }
                        if (!readUint(pair[0], at(pairPointer, std::size_t{ 0 }), out.mInfluences[i].mBone)
                            || !readNumber(pair[1], at(pairPointer, std::size_t{ 1 }), out.mInfluences[i].mWeight))
                        {
                            return false;
                        }
                    }
                }
                const auto uvSets = object.find("uvSetCount");
                return uvSets == object.end() || readUint(*uvSets, at(pointer, "uvSetCount"), out.mUvSetCount);
            }

            // ---- optional parts
            bool readOrigin(const Json& object, const std::string& pointer, Origin& out)
            {
                return enumField(object, pointer, "origin", sOrigins, out);
            }

            bool readOptionalBool(const Json& object, const std::string& pointer, std::string_view key, bool& out)
            {
                const auto found = object.find(key);
                return found == object.end() || readBool(*found, at(pointer, key), out);
            }

            bool readOptionalParts(const Json& root, BodyPackage& out)
            {
                return readPart(root, "morphSet", out.mMorphSet,
                           [this](const Json& v, const std::string& p, MorphSet& o) { return readMorphSet(v, p, o); })
                    && readPart(root, "proxy", out.mProxy,
                        [this](const Json& v, const std::string& p, Proxy& o) { return readProxy(v, p, o); })
                    && readPart(root, "physicsProfile", out.mPhysicsProfile,
                        [this](const Json& v, const std::string& p, PhysicsProfile& o) { return readPhysics(v, p, o); })
                    && readPart(root, "measurements", out.mMeasurements,
                        [this](
                            const Json& v, const std::string& p, Measurements& o) { return readMeasurements(v, p, o); })
                    && readPart(root, "regionMap", out.mRegionMap,
                        [this](const Json& v, const std::string& p, RegionMap& o) { return readRegionMap(v, p, o); })
                    && readPart(root, "landmarks", out.mLandmarks,
                        [this](const Json& v, const std::string& p, Landmarks& o) { return readLandmarks(v, p, o); })
                    && readPart(root, "softTissue", out.mSoftTissue,
                        [this](const Json& v, const std::string& p, SoftTissue& o) { return readSoftTissue(v, p, o); })
                    && readObjects(root, "", "provenance", Limits::sMaxListEntries, false, out.mProvenance,
                        [this](const Json& v, const std::string& p, ProvenanceRecord& o) {
                            return readProvenance(v, p, o);
                        });
            }

            template <typename Part, typename ReadPart>
            bool readPart(const Json& root, std::string_view key, std::optional<Part>& out, ReadPart readBody)
            {
                const auto found = root.find(key);
                if (found == root.end())
                {
                    return true;
                }
                const std::string pointer = at("", key);
                if (!found->is_object())
                {
                    return fail(*found, pointer, "must be an object");
                }
                Part part{};
                if (!readBody(*found, pointer, part))
                {
                    return false;
                }
                out = std::move(part);
                return true;
            }

            bool readMorphSet(const Json& object, const std::string& pointer, MorphSet& out)
            {
                return onlyKeys(object, pointer, { "origin", "morphs" }) && readOrigin(object, pointer, out.mOrigin)
                    && readObjects(object, pointer, "morphs", Limits::sMaxListEntries, false, out.mMorphs,
                        [this](const Json& v, const std::string& p, Morph& o) { return readMorph(v, p, o); });
            }

            bool readMorph(const Json& object, const std::string& pointer, Morph& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer, { "id", "range", "default", "targets" })
                    || !textField(object, pointer, "id", out.mId))
                {
                    return false;
                }
                std::array<double, 2> range{};
                if (!fixedField(object, pointer, "range", range))
                {
                    return false;
                }
                out.mRangeMin = range[0];
                out.mRangeMax = range[1];
                const Json* defaultValue = member(object, pointer, "default");
                if (defaultValue == nullptr || !readNumber(*defaultValue, at(pointer, "default"), out.mDefault))
                {
                    return false;
                }
                return readObjects(object, pointer, "targets", Limits::sMaxListEntries, false, out.mTargets,
                    [this](const Json& v, const std::string& p, MorphTarget& o) { return readMorphTarget(v, p, o); });
            }

            bool readMorphTarget(const Json& object, const std::string& pointer, MorphTarget& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer, { "mesh", "vertices", "deltas" })
                    || !textField(object, pointer, "mesh", out.mMesh)
                    || !readUintList(object, pointer, "vertices", out.mVertices))
                {
                    return false;
                }
                const Json* deltas = nullptr;
                if (!readList(object, pointer, "deltas", Limits::sMaxArrayElements, false, deltas))
                {
                    return false;
                }
                if (deltas == nullptr)
                {
                    return true;
                }
                const std::string base = at(pointer, "deltas");
                if (!countElements(*deltas, base))
                {
                    return false;
                }
                out.mDeltas.resize(deltas->size());
                for (std::size_t i = 0; i < deltas->size(); ++i)
                {
                    if (!readFixed((*deltas)[i], at(base, i), out.mDeltas[i]))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool readProxy(const Json& object, const std::string& pointer, Proxy& out)
            {
                return onlyKeys(object, pointer, { "origin", "capsules" }) && readOrigin(object, pointer, out.mOrigin)
                    && readObjects(object, pointer, "capsules", Limits::sMaxListEntries, false, out.mCapsules,
                        [this](const Json& v, const std::string& p, ProxyCapsule& o) { return readCapsule(v, p, o); });
            }

            bool readCapsule(const Json& object, const std::string& pointer, ProxyCapsule& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                const Json* radius = nullptr;
                return onlyKeys(object, pointer, { "bone", "from", "to", "radius" })
                    && textField(object, pointer, "bone", out.mBone) && fixedField(object, pointer, "from", out.mFrom)
                    && fixedField(object, pointer, "to", out.mTo)
                    && (radius = member(object, pointer, "radius")) != nullptr
                    && readNumber(*radius, at(pointer, "radius"), out.mRadius);
            }

            bool readPhysics(const Json& object, const std::string& pointer, PhysicsProfile& out)
            {
                if (!onlyKeys(object, pointer,
                        { "origin", "rig", "profile", "shapes", "masses", "jointLimits", "restrictedTier" })
                    || !readOrigin(object, pointer, out.mOrigin)
                    || !optionalTextField(object, pointer, "rig", out.mRigPath)
                    || !optionalTextField(object, pointer, "profile", out.mProfilePath)
                    || !readObjects(object, pointer, "shapes", Limits::sMaxListEntries, false, out.mShapes,
                        [this](const Json& v, const std::string& p, PhysicsShape& o) { return readShape(v, p, o); })
                    || !readObjects(object, pointer, "masses", Limits::sMaxListEntries, false, out.mMasses,
                        [this](const Json& v, const std::string& p, PhysicsMass& o) { return readMass(v, p, o); })
                    || !readObjects(object, pointer, "jointLimits", Limits::sMaxListEntries, false, out.mJointLimits,
                        [this](const Json& v, const std::string& p, JointLimit& o) { return readJointLimit(v, p, o); }))
                {
                    return false;
                }
                return readOptionalBool(object, pointer, "restrictedTier", out.mRestrictedTier);
            }

            bool readShape(const Json& object, const std::string& pointer, PhysicsShape& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                return onlyKeys(object, pointer, { "bone", "kind", "size" })
                    && textField(object, pointer, "bone", out.mBone)
                    && enumField(object, pointer, "kind", sShapes, out.mKind)
                    && fixedField(object, pointer, "size", out.mSize);
            }

            bool readMass(const Json& object, const std::string& pointer, PhysicsMass& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                const Json* kilograms = nullptr;
                return onlyKeys(object, pointer, { "bone", "kilograms" })
                    && textField(object, pointer, "bone", out.mBone)
                    && (kilograms = member(object, pointer, "kilograms")) != nullptr
                    && readNumber(*kilograms, at(pointer, "kilograms"), out.mKilograms);
            }

            bool numberField(const Json& object, const std::string& pointer, std::string_view key, double& out)
            {
                const Json* value = member(object, pointer, key);
                return value != nullptr && readNumber(*value, at(pointer, key), out);
            }

            bool readJointLimit(const Json& object, const std::string& pointer, JointLimit& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                return onlyKeys(object, pointer, { "bone", "min", "max", "rest" })
                    && textField(object, pointer, "bone", out.mBone) && numberField(object, pointer, "min", out.mMin)
                    && numberField(object, pointer, "max", out.mMax) && numberField(object, pointer, "rest", out.mRest);
            }

            bool readMeasurements(const Json& object, const std::string& pointer, Measurements& out)
            {
                return onlyKeys(object, pointer, { "origin", "entries" }) && readOrigin(object, pointer, out.mOrigin)
                    && readObjects(object, pointer, "entries", Limits::sMaxListEntries, false, out.mEntries,
                        [this](const Json& v, const std::string& p, Measurement& o) {
                            if (!v.is_object())
                            {
                                return fail(v, p, "must be an object");
                            }
                            return onlyKeys(v, p, { "name", "value" }) && textField(v, p, "name", o.mName)
                                && numberField(v, p, "value", o.mValue);
                        });
            }

            bool readRegionMap(const Json& object, const std::string& pointer, RegionMap& out)
            {
                return onlyKeys(object, pointer, { "origin", "mesh", "regions" })
                    && readOrigin(object, pointer, out.mOrigin) && textField(object, pointer, "mesh", out.mMesh)
                    && readObjects(object, pointer, "regions", Limits::sMaxListEntries, false, out.mRegions,
                        [this](const Json& v, const std::string& p, Region& o) { return readRegion(v, p, o); });
            }

            bool readRegion(const Json& object, const std::string& pointer, Region& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer, { "id", "ranges" }) || !textField(object, pointer, "id", out.mId))
                {
                    return false;
                }
                const Json* ranges = nullptr;
                if (!readList(object, pointer, "ranges", Limits::sMaxListEntries, false, ranges))
                {
                    return false;
                }
                if (ranges == nullptr)
                {
                    return true;
                }
                const std::string base = at(pointer, "ranges");
                if (!countElements(*ranges, base))
                {
                    return false;
                }
                mTotalRanges += ranges->size();
                if (mTotalRanges > Limits::sMaxRegionRanges)
                {
                    return fail(*ranges, base, "the file holds too many region ranges");
                }
                out.mRanges.resize(ranges->size());
                for (std::size_t i = 0; i < ranges->size(); ++i)
                {
                    const Json& pair = (*ranges)[i];
                    const std::string pairPointer = at(base, i);
                    if (!pair.is_array() || pair.size() != sSmallListCount)
                    {
                        return fail(pair, pairPointer, "must be an array [begin, end]");
                    }
                    if (!readUint(pair[0], at(pairPointer, std::size_t{ 0 }), out.mRanges[i].mBegin)
                        || !readUint(pair[1], at(pairPointer, std::size_t{ 1 }), out.mRanges[i].mEnd))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool readLandmarks(const Json& object, const std::string& pointer, Landmarks& out)
            {
                return onlyKeys(object, pointer, { "origin", "entries" }) && readOrigin(object, pointer, out.mOrigin)
                    && readObjects(object, pointer, "entries", Limits::sMaxListEntries, false, out.mEntries,
                        [this](const Json& v, const std::string& p, Landmark& o) {
                            if (!v.is_object())
                            {
                                return fail(v, p, "must be an object");
                            }
                            return onlyKeys(v, p, { "name", "bone", "offset" }) && textField(v, p, "name", o.mName)
                                && textField(v, p, "bone", o.mBone) && fixedField(v, p, "offset", o.mOffset);
                        });
            }

            bool readSoftTissue(const Json& object, const std::string& pointer, SoftTissue& out)
            {
                if (!onlyKeys(object, pointer, { "origin", "nodes", "restrictedTier" })
                    || !readOrigin(object, pointer, out.mOrigin)
                    || !readObjects(object, pointer, "nodes", Limits::sMaxListEntries, false, out.mNodes,
                        [this](const Json& v, const std::string& p, SoftTissueNode& o) {
                            if (!v.is_object())
                            {
                                return fail(v, p, "must be an object");
                            }
                            return onlyKeys(v, p, { "id", "bone", "mass", "stiffness", "damping" })
                                && textField(v, p, "id", o.mId) && textField(v, p, "bone", o.mBone)
                                && numberField(v, p, "mass", o.mMass) && numberField(v, p, "stiffness", o.mStiffness)
                                && numberField(v, p, "damping", o.mDamping);
                        }))
                {
                    return false;
                }
                return readOptionalBool(object, pointer, "restrictedTier", out.mRestrictedTier);
            }

            bool readProvenance(const Json& object, const std::string& pointer, ProvenanceRecord& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                return onlyKeys(object, pointer, { "part", "sourceDigest", "toolId", "toolVersion", "parameters" })
                    && textField(object, pointer, "part", out.mPart)
                    && textField(object, pointer, "sourceDigest", out.mSourceDigest)
                    && textField(object, pointer, "toolId", out.mToolId)
                    && textField(object, pointer, "toolVersion", out.mToolVersion)
                    && textField(object, pointer, "parameters", out.mParameters);
            }
        };

        // ---- Writer ------------------------------------------------------------------------------------------------
        // A small tree is built and then laid out by the canonical rules of bodyjson.hpp: an array of scalars is one
        // line, an object whose values are all scalars or such arrays is one line, everything else is expanded.

        struct Node
        {
            enum class Kind
            {
                Scalar,
                FlatArray, // already-rendered "[a, b, c]"
                Array,
                Object,
            };
            Kind mKind = Kind::Scalar;
            std::string mText; // Scalar and FlatArray
            std::vector<Node> mItems; // Array items, Object values
            std::vector<std::string> mKeys; // Object keys, parallel to mItems
        };

        // Shortest text that reads back to the same double (opas number()), except that a whole number below 1e15
        // is written in full: std::to_chars would write 1000000 as "1e+06", and the format says whole numbers have no
        // exponent.
        constexpr double sMaxWholeNumber = 1e15;

        std::string canonicalNumber(double value)
        {
            if (std::isfinite(value) && std::abs(value) < sMaxWholeNumber && value == std::floor(value))
            {
                return value == 0 ? "0" : std::to_string(static_cast<std::int64_t>(value));
            }
            return number(value);
        }

        Node scalar(std::string text)
        {
            Node node;
            node.mText = std::move(text);
            return node;
        }

        Node str(std::string_view text)
        {
            return scalar(jsonString(text));
        }

        Node num(double value)
        {
            return scalar(canonicalNumber(value));
        }

        Node integer(std::int64_t value)
        {
            return scalar(std::to_string(value));
        }

        Node boolean(bool value)
        {
            return scalar(value ? "true" : "false");
        }

        Node array()
        {
            Node node;
            node.mKind = Node::Kind::Array;
            return node;
        }

        Node object()
        {
            Node node;
            node.mKind = Node::Kind::Object;
            return node;
        }

        void add(Node& target, std::string key, Node value)
        {
            target.mKeys.push_back(std::move(key));
            target.mItems.push_back(std::move(value));
        }

        Node flat(const double* values, std::size_t count)
        {
            Node node;
            node.mKind = Node::Kind::FlatArray;
            node.mText = "[";
            for (std::size_t i = 0; i < count; ++i)
            {
                node.mText += (i > 0 ? ", " : "") + canonicalNumber(values[i]);
            }
            node.mText += "]";
            return node;
        }

        template <std::size_t N>
        Node flat(const std::array<double, N>& values)
        {
            return flat(values.data(), N);
        }

        Node flatInts(const std::uint32_t* values, std::size_t count)
        {
            Node node;
            node.mKind = Node::Kind::FlatArray;
            node.mText = "[";
            for (std::size_t i = 0; i < count; ++i)
            {
                node.mText += (i > 0 ? ", " : "") + std::to_string(values[i]);
            }
            node.mText += "]";
            return node;
        }

        Node flatStrings(const std::vector<std::string>& values)
        {
            Node node;
            node.mKind = Node::Kind::FlatArray;
            node.mText = "[";
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                node.mText += (i > 0 ? ", " : "") + jsonString(values[i]);
            }
            node.mText += "]";
            return node;
        }

        bool isFlatArray(const Node& node)
        {
            if (node.mKind == Node::Kind::FlatArray)
            {
                return true;
            }
            if (node.mKind != Node::Kind::Array)
            {
                return false;
            }
            for (const Node& item : node.mItems)
            {
                if (item.mKind != Node::Kind::Scalar)
                {
                    return false;
                }
            }
            return true;
        }

        bool isLeafObject(const Node& node)
        {
            if (node.mKind != Node::Kind::Object)
            {
                return false;
            }
            for (const Node& item : node.mItems)
            {
                if (item.mKind != Node::Kind::Scalar && !isFlatArray(item))
                {
                    return false;
                }
            }
            return true;
        }

        void render(const Node& node, std::size_t indent, std::string& out)
        {
            switch (node.mKind)
            {
                case Node::Kind::Scalar:
                case Node::Kind::FlatArray:
                    out += node.mText;
                    return;
                case Node::Kind::Array:
                {
                    if (node.mItems.empty())
                    {
                        out += "[]";
                        return;
                    }
                    if (isFlatArray(node))
                    {
                        out += "[";
                        for (std::size_t i = 0; i < node.mItems.size(); ++i)
                        {
                            out += (i > 0 ? ", " : "") + node.mItems[i].mText;
                        }
                        out += "]";
                        return;
                    }
                    out += "[\n";
                    for (std::size_t i = 0; i < node.mItems.size(); ++i)
                    {
                        out.append(indent + 2, ' ');
                        render(node.mItems[i], indent + 2, out);
                        out += i + 1 < node.mItems.size() ? ",\n" : "\n";
                    }
                    out.append(indent, ' ');
                    out += "]";
                    return;
                }
                case Node::Kind::Object:
                {
                    if (node.mItems.empty())
                    {
                        out += "{}";
                        return;
                    }
                    if (isLeafObject(node))
                    {
                        out += "{";
                        for (std::size_t i = 0; i < node.mItems.size(); ++i)
                        {
                            out += (i > 0 ? ", " : "") + jsonString(node.mKeys[i]) + ": ";
                            render(node.mItems[i], 0, out);
                        }
                        out += "}";
                        return;
                    }
                    out += "{\n";
                    for (std::size_t i = 0; i < node.mItems.size(); ++i)
                    {
                        out.append(indent + 2, ' ');
                        out += jsonString(node.mKeys[i]) + ": ";
                        render(node.mItems[i], indent + 2, out);
                        out += i + 1 < node.mItems.size() ? ",\n" : "\n";
                    }
                    out.append(indent, ' ');
                    out += "}";
                    return;
                }
            }
        }

        Node profileNode(const opas::anim::SkeletonProfile& profile)
        {
            Node node = object();
            add(node, "format", str("opas.skeletonprofile"));
            add(node, "version", integer(1));
            add(node, "name", str(profile.name));
            Node bones = array();
            for (const opas::anim::BoneMapping& mapping : profile.bones)
            {
                Node entry = object();
                add(entry, "source", str(mapping.source));
                add(entry, "target", str(mapping.target));
                bones.mItems.push_back(std::move(entry));
            }
            add(node, "bones", std::move(bones));
            add(node, "legChain", flatStrings(profile.legChain));
            return node;
        }

        Node summaryNode(const MeshSummary& summary)
        {
            Node node = object();
            add(node, "vertexCount", integer(summary.mVertexCount));
            add(node, "triangleCount", integer(summary.mTriangleCount));
            add(node, "boundsMin", flat(summary.mBoundsMin));
            add(node, "boundsMax", flat(summary.mBoundsMax));
            add(node, "boneNames", flatStrings(summary.mBoneNames));
            add(node, "influenceOffsets", flatInts(summary.mInfluenceOffsets.data(), summary.mInfluenceOffsets.size()));
            Node influences = array();
            influences.mItems.reserve(summary.mInfluences.size());
            for (const MeshSummary::Influence& influence : summary.mInfluences)
            {
                Node pair;
                pair.mKind = Node::Kind::FlatArray;
                pair.mText = "[" + std::to_string(influence.mBone) + ", " + canonicalNumber(influence.mWeight) + "]";
                influences.mItems.push_back(std::move(pair));
            }
            add(node, "influences", std::move(influences));
            add(node, "triangleIndices", flatInts(summary.mTriangleIndices.data(), summary.mTriangleIndices.size()));
            add(node, "uvSetCount", integer(summary.mUvSetCount));
            return node;
        }

        Node meshNode(const MeshEntry& mesh)
        {
            Node node = object();
            add(node, "id", str(mesh.mId));
            add(node, "path", str(mesh.mPath));
            add(node, "meshFormat", str(toString(mesh.mFormat)));
            add(node, "lod", integer(mesh.mLod));
            add(node, "role", str(toString(mesh.mRole)));
            if (mesh.mPartType)
            {
                add(node, "partType", str(*mesh.mPartType));
            }
            if (mesh.mAttachBone)
            {
                add(node, "attachBone", str(*mesh.mAttachBone));
            }
            if (mesh.mSummary)
            {
                add(node, "summary", summaryNode(*mesh.mSummary));
            }
            return node;
        }

        Node bindingNode(const Binding& binding)
        {
            Node node = object();
            if (binding.mSkeletonProfile)
            {
                add(node, "skeleton", profileNode(*binding.mSkeletonProfile));
            }
            else
            {
                add(node, "skeleton", str(binding.mSkeletonPath ? *binding.mSkeletonPath : std::string()));
            }
            add(node, "rootBone", str(binding.mRootBone));
            add(node, "unmappedBones", flatStrings(binding.mUnmappedBones));
            if (binding.mRestSkeleton)
            {
                Node rest = array();
                for (const RestBone& bone : *binding.mRestSkeleton)
                {
                    Node entry = object();
                    add(entry, "name", str(bone.mName));
                    add(entry, "parent", integer(bone.mParent));
                    add(entry, "translation", flat(bone.mTranslation));
                    add(entry, "rotation", flat(bone.mRotation));
                    rest.mItems.push_back(std::move(entry));
                }
                add(node, "restSkeleton", std::move(rest));
            }
            return node;
        }

        Node originNode(Origin origin)
        {
            return str(toString(origin));
        }

        Node morphSetNode(const MorphSet& set)
        {
            Node node = object();
            add(node, "origin", originNode(set.mOrigin));
            Node morphs = array();
            for (const Morph& morph : set.mMorphs)
            {
                Node entry = object();
                add(entry, "id", str(morph.mId));
                const std::array<double, 2> range = { morph.mRangeMin, morph.mRangeMax };
                add(entry, "range", flat(range));
                add(entry, "default", num(morph.mDefault));
                Node targets = array();
                for (const MorphTarget& target : morph.mTargets)
                {
                    Node targetNode = object();
                    add(targetNode, "mesh", str(target.mMesh));
                    add(targetNode, "vertices", flatInts(target.mVertices.data(), target.mVertices.size()));
                    Node deltas = array();
                    for (const std::array<double, 3>& delta : target.mDeltas)
                    {
                        deltas.mItems.push_back(flat(delta));
                    }
                    add(targetNode, "deltas", std::move(deltas));
                    targets.mItems.push_back(std::move(targetNode));
                }
                add(entry, "targets", std::move(targets));
                morphs.mItems.push_back(std::move(entry));
            }
            add(node, "morphs", std::move(morphs));
            return node;
        }

        Node proxyNode(const Proxy& proxy)
        {
            Node node = object();
            add(node, "origin", originNode(proxy.mOrigin));
            Node capsules = array();
            for (const ProxyCapsule& capsule : proxy.mCapsules)
            {
                Node entry = object();
                add(entry, "bone", str(capsule.mBone));
                add(entry, "from", flat(capsule.mFrom));
                add(entry, "to", flat(capsule.mTo));
                add(entry, "radius", num(capsule.mRadius));
                capsules.mItems.push_back(std::move(entry));
            }
            add(node, "capsules", std::move(capsules));
            return node;
        }

        Node physicsNode(const PhysicsProfile& physics)
        {
            Node node = object();
            add(node, "origin", originNode(physics.mOrigin));
            if (physics.mRigPath)
            {
                add(node, "rig", str(*physics.mRigPath));
            }
            if (physics.mProfilePath)
            {
                add(node, "profile", str(*physics.mProfilePath));
            }
            Node shapes = array();
            for (const PhysicsShape& shape : physics.mShapes)
            {
                Node entry = object();
                add(entry, "bone", str(shape.mBone));
                add(entry, "kind", str(toString(shape.mKind)));
                add(entry, "size", flat(shape.mSize));
                shapes.mItems.push_back(std::move(entry));
            }
            add(node, "shapes", std::move(shapes));
            Node masses = array();
            for (const PhysicsMass& mass : physics.mMasses)
            {
                Node entry = object();
                add(entry, "bone", str(mass.mBone));
                add(entry, "kilograms", num(mass.mKilograms));
                masses.mItems.push_back(std::move(entry));
            }
            add(node, "masses", std::move(masses));
            Node limits = array();
            for (const JointLimit& limit : physics.mJointLimits)
            {
                Node entry = object();
                add(entry, "bone", str(limit.mBone));
                add(entry, "min", num(limit.mMin));
                add(entry, "max", num(limit.mMax));
                add(entry, "rest", num(limit.mRest));
                limits.mItems.push_back(std::move(entry));
            }
            add(node, "jointLimits", std::move(limits));
            add(node, "restrictedTier", boolean(physics.mRestrictedTier));
            return node;
        }

        Node measurementsNode(const Measurements& measurements)
        {
            Node node = object();
            add(node, "origin", originNode(measurements.mOrigin));
            Node entries = array();
            for (const Measurement& measurement : measurements.mEntries)
            {
                Node entry = object();
                add(entry, "name", str(measurement.mName));
                add(entry, "value", num(measurement.mValue));
                entries.mItems.push_back(std::move(entry));
            }
            add(node, "entries", std::move(entries));
            return node;
        }

        Node regionMapNode(const RegionMap& map)
        {
            Node node = object();
            add(node, "origin", originNode(map.mOrigin));
            add(node, "mesh", str(map.mMesh));
            Node regions = array();
            for (const Region& region : map.mRegions)
            {
                Node entry = object();
                add(entry, "id", str(region.mId));
                Node ranges = array();
                for (const TriangleRange& range : region.mRanges)
                {
                    const std::array<std::uint32_t, 2> pair = { range.mBegin, range.mEnd };
                    ranges.mItems.push_back(flatInts(pair.data(), pair.size()));
                }
                add(entry, "ranges", std::move(ranges));
                regions.mItems.push_back(std::move(entry));
            }
            add(node, "regions", std::move(regions));
            return node;
        }

        Node landmarksNode(const Landmarks& landmarks)
        {
            Node node = object();
            add(node, "origin", originNode(landmarks.mOrigin));
            Node entries = array();
            for (const Landmark& landmark : landmarks.mEntries)
            {
                Node entry = object();
                add(entry, "name", str(landmark.mName));
                add(entry, "bone", str(landmark.mBone));
                add(entry, "offset", flat(landmark.mOffset));
                entries.mItems.push_back(std::move(entry));
            }
            add(node, "entries", std::move(entries));
            return node;
        }

        Node softTissueNode(const SoftTissue& soft)
        {
            Node node = object();
            add(node, "origin", originNode(soft.mOrigin));
            Node nodes = array();
            for (const SoftTissueNode& item : soft.mNodes)
            {
                Node entry = object();
                add(entry, "id", str(item.mId));
                add(entry, "bone", str(item.mBone));
                add(entry, "mass", num(item.mMass));
                add(entry, "stiffness", num(item.mStiffness));
                add(entry, "damping", num(item.mDamping));
                nodes.mItems.push_back(std::move(entry));
            }
            add(node, "nodes", std::move(nodes));
            add(node, "restrictedTier", boolean(soft.mRestrictedTier));
            return node;
        }
    }

    BodyLoadResult readBodyPackage(std::string_view text, std::string_view fileName)
    {
        BodyLoadResult result;
        try
        {
            if (const std::optional<LoadError> tooBig = prescan(text, fileName))
            {
                result.mError = *tooBig;
                return result;
            }
            BodyPackage package;
            const std::optional<LoadError> error = opas::format::detail::parseAndRead<BodyReader>(text, fileName,
                [&package](BodyReader& reader, const Json& root) { return reader.readDocument(root, package); });
            if (error)
            {
                result.mError = *error;
            }
            else
            {
                result.mValue = std::move(package);
            }
        }
        catch (const std::exception& e)
        {
            // Out of memory and the like: the contract is that nothing is thrown.
            result.mValue.reset();
            result.mError
                = LoadError{ std::string(fileName), 1, "", std::string("cannot read the package: ") + e.what() };
        }
        return result;
    }

    BodyLoadResult readBodyPackageFile(const std::filesystem::path& path)
    {
        BodyLoadResult result;
        const std::string fileName = opas::format::detail::utf8FileName(path);
        try
        {
            std::error_code code;
            const std::uintmax_t size = std::filesystem::file_size(path, code);
            if (code)
            {
                result.mError = LoadError{ fileName, 0, "", "cannot open the file: " + code.message() };
                return result;
            }
            if (size > Limits::sMaxFileBytes)
            {
                result.mError = LoadError{ fileName, 0, "",
                    "the file is larger than " + std::to_string(Limits::sMaxFileBytes) + " bytes" };
                return result;
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                result.mError = LoadError{ fileName, 0, "", "cannot open the file" };
                return result;
            }
            std::string text(static_cast<std::size_t>(size), '\0');
            stream.read(text.data(), static_cast<std::streamsize>(text.size()));
            if (stream.gcount() != static_cast<std::streamsize>(text.size()))
            {
                result.mError = LoadError{ fileName, 0, "", "cannot read the file" };
                return result;
            }
            return readBodyPackage(text, fileName);
        }
        catch (const std::exception& e)
        {
            result.mValue.reset();
            result.mError = LoadError{ fileName, 0, "", std::string("cannot read the file: ") + e.what() };
            return result;
        }
    }

    std::string writeBodyPackage(const BodyPackage& package)
    {
        Node root = object();
        add(root, "format", str(sFormatName));
        add(root, "version", integer(sNewestVersion));

        Node identity = object();
        add(identity, "id", str(package.mIdentity.mId));
        add(identity, "name", str(package.mIdentity.mName));
        add(identity, "version", str(package.mIdentity.mVersion));
        add(identity, "author", str(package.mIdentity.mAuthor));
        add(identity, "license", str(package.mIdentity.mLicense));
        Node contract = object();
        add(contract, "major", integer(package.mIdentity.mContractVersion.mMajor));
        add(contract, "minor", integer(package.mIdentity.mContractVersion.mMinor));
        add(identity, "contractVersion", std::move(contract));
        add(identity, "ageClass", str(toString(package.mIdentity.mAgeClass)));
        add(root, "identity", std::move(identity));

        if (!package.mTargets.empty())
        {
            Node targets = array();
            for (const Target& target : package.mTargets)
            {
                Node entry = object();
                if (target.mRace)
                {
                    add(entry, "race", str(*target.mRace));
                }
                if (target.mSex)
                {
                    add(entry, "sex", str(*target.mSex));
                }
                if (target.mCreatureType)
                {
                    add(entry, "creatureType", str(*target.mCreatureType));
                }
                add(entry, "priority", integer(target.mPriority));
                targets.mItems.push_back(std::move(entry));
            }
            add(root, "targets", std::move(targets));
        }

        add(root, "binding", bindingNode(package.mBinding));
        Node meshes = array();
        for (const MeshEntry& mesh : package.mMeshes)
        {
            meshes.mItems.push_back(meshNode(mesh));
        }
        add(root, "meshes", std::move(meshes));

        if (package.mMorphSet)
        {
            add(root, "morphSet", morphSetNode(*package.mMorphSet));
        }
        if (package.mProxy)
        {
            add(root, "proxy", proxyNode(*package.mProxy));
        }
        if (package.mPhysicsProfile)
        {
            add(root, "physicsProfile", physicsNode(*package.mPhysicsProfile));
        }
        if (package.mMeasurements)
        {
            add(root, "measurements", measurementsNode(*package.mMeasurements));
        }
        if (package.mRegionMap)
        {
            add(root, "regionMap", regionMapNode(*package.mRegionMap));
        }
        if (package.mLandmarks)
        {
            add(root, "landmarks", landmarksNode(*package.mLandmarks));
        }
        if (package.mSoftTissue)
        {
            add(root, "softTissue", softTissueNode(*package.mSoftTissue));
        }
        if (!package.mProvenance.empty())
        {
            Node records = array();
            for (const ProvenanceRecord& record : package.mProvenance)
            {
                Node entry = object();
                add(entry, "part", str(record.mPart));
                add(entry, "sourceDigest", str(record.mSourceDigest));
                add(entry, "toolId", str(record.mToolId));
                add(entry, "toolVersion", str(record.mToolVersion));
                add(entry, "parameters", str(record.mParameters));
                records.mItems.push_back(std::move(entry));
            }
            add(root, "provenance", std::move(records));
        }

        std::string out;
        render(root, 0, out);
        out += "\n";
        return out;
    }
}
