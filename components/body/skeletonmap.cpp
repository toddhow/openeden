#include "skeletonmap.hpp"

// nlohmann/json stays inside this file (core/format/CLAUDE.md): the reader is derived from CreativeMotion's internal
// FileReader, reached through the PRIVATE opas_format_json link, and no JSON type appears in a public header.
#include <opas/format/FileReader.hpp>

#include <edenarchive/hash.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <set>
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

        constexpr std::string_view sFormatName = "openeden.skeletonmap";
        constexpr int sNewestVersion = 1;
        constexpr std::size_t sDigestHexSize = 64;
        // Counts that a valid file cannot exceed, used by the pre-scan before anything is parsed: the root, the source
        // and proportions objects, up to the canonical role count plus a spare of roles, and per extra entry the entry
        // itself and its "suggested" object. The values are generous, not tight: the semantic caps are the reader's and
        // the validator's.
        constexpr std::size_t sMaxObjectsInFile = 3 + 128 + 2 * SkeletonMapLimits::sMaxBones + 16;
        constexpr std::size_t sMaxArraysInFile
            = 8 + 128 + 3; // role alias lists, the roles, missingRoles and extra lists
        // Commas: an extra entry has at most 12 members (11 commas) plus one separating comma; a role has up to 16
        // aliases.
        constexpr std::size_t sMaxCommasInFile
            = SkeletonMapLimits::sMaxBones * 13 + 128 * (SkeletonMapLimits::sMaxAliasesPerRole + 3) + 64;

        template <typename E>
        struct EnumName
        {
            std::string_view mName;
            E mValue;
        };

        constexpr std::array<EnumName<MapAction>, 5> sActions = { {
            { "secondary", MapAction::Secondary },
            { "fold", MapAction::Fold },
            { "attach", MapAction::Attach },
            { "drop", MapAction::Drop },
            { "ask", MapAction::Ask },
        } };
        constexpr std::array<EnumName<FoldRule>, 3> sRules = { {
            { "nearest-mapped-ancestor", FoldRule::NearestMappedAncestor },
            { "named", FoldRule::Named },
            { "distribute", FoldRule::Distribute },
        } };
        constexpr std::array<EnumName<SecondaryKind>, 8> sKinds = { {
            { "breast", SecondaryKind::Breast },
            { "belly", SecondaryKind::Belly },
            { "butt", SecondaryKind::Butt },
            { "cloth", SecondaryKind::Cloth },
            { "hair", SecondaryKind::Hair },
            { "tail", SecondaryKind::Tail },
            { "twist", SecondaryKind::Twist },
            { "other", SecondaryKind::Other },
        } };
        constexpr std::array<EnumName<MapConfidence>, 3> sConfidences = { {
            { "low", MapConfidence::Low },
            { "medium", MapConfidence::Medium },
            { "high", MapConfidence::High },
        } };
        constexpr std::array<EnumName<ProportionsMode>, 2> sModes = { {
            { "tier1", ProportionsMode::Tier1 },
            { "inverse-bind", ProportionsMode::InverseBind },
        } };

        template <typename E, std::size_t N>
        const char* nameOf(const std::array<EnumName<E>, N>& names, E value)
        {
            for (const EnumName<E>& entry : names)
            {
                if (entry.mValue == value)
                {
                    return entry.mName.data();
                }
            }
            return "";
        }

        // Appends the UTF-8 bytes of a code point (a lone surrogate keeps its three-byte form: it only has to compare
        // equal to itself).
        void appendUtf8(std::string& out, std::uint32_t cp)
        {
            if (cp < 0x80)
            {
                out += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xc0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            }
            else
            {
                out += static_cast<char>(0xe0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            }
        }

        int hexValue(char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        }

        // ---- Pre-scan: size, nesting depth, the number of objects, arrays and values, and keys that appear twice in
        // one object are checked on the text BEFORE any JSON value is built, so a hostile file cannot make the parser
        // recurse or allocate, and nlohmann cannot silently keep the last of two equal keys (review fix 12). A key's
        // escapes are decoded (\uXXXX to UTF-8, a surrogate pair as two separate code units) so "accepted"
        // equals "accepted"; malformed escapes are left to the parser, which refuses them.
        std::optional<LoadError> prescan(std::string_view text, std::string_view fileName)
        {
            if (text.size() > SkeletonMapLimits::sMaxFileBytes)
            {
                return LoadError{ std::string(fileName), 1, "",
                    "the file is larger than " + std::to_string(SkeletonMapLimits::sMaxFileBytes) + " bytes" };
            }
            struct Level
            {
                bool mObject = false;
                bool mExpectKey = false; // an object waiting for a key (after '{' or ',')
                std::set<std::string> mKeys;
            };
            std::vector<Level> levels;
            levels.reserve(SkeletonMapLimits::sMaxNesting + 1);
            std::size_t commas = 0;
            std::size_t arrays = 0;
            std::size_t objects = 0;
            std::size_t line = 1;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (c == '\n')
                {
                    ++line;
                }
                if (c == '"')
                {
                    const bool isKey = !levels.empty() && levels.back().mObject && levels.back().mExpectKey;
                    std::string decoded;
                    std::size_t j = i + 1;
                    for (; j < text.size() && text[j] != '"'; ++j)
                    {
                        if (text[j] == '\n')
                        {
                            ++line;
                        }
                        if (text[j] != '\\' || j + 1 >= text.size())
                        {
                            decoded += text[j];
                            continue;
                        }
                        const char e = text[++j];
                        if (e == 'u' && j + 4 < text.size())
                        {
                            std::uint32_t cp = 0;
                            bool ok = true;
                            for (std::size_t k = 1; k <= 4; ++k)
                            {
                                const int h = hexValue(text[j + k]);
                                ok = ok && h >= 0;
                                cp = cp * 16 + static_cast<std::uint32_t>(h < 0 ? 0 : h);
                            }
                            if (ok)
                            {
                                appendUtf8(decoded, cp);
                                j += 4;
                                continue;
                            }
                        }
                        switch (e)
                        {
                            case 'n':
                                decoded += '\n';
                                break;
                            case 't':
                                decoded += '\t';
                                break;
                            case 'r':
                                decoded += '\r';
                                break;
                            case 'b':
                                decoded += '\b';
                                break;
                            case 'f':
                                decoded += '\f';
                                break;
                            default:
                                decoded += e; // \" \\ \/ and anything the parser will refuse
                                break;
                        }
                    }
                    if (isKey)
                    {
                        levels.back().mExpectKey = false;
                        if (!levels.back().mKeys.insert(decoded).second)
                        {
                            return LoadError{ std::string(fileName), line, "",
                                "the key \"" + decoded.substr(0, 60) + "\" appears twice in one object" };
                        }
                    }
                    i = j;
                    continue;
                }
                if (c == '[' || c == '{')
                {
                    if (c == '[')
                    {
                        ++arrays;
                    }
                    else
                    {
                        ++objects;
                    }
                    if (arrays > sMaxArraysInFile)
                    {
                        return LoadError{ std::string(fileName), line, "", "the file holds too many arrays" };
                    }
                    if (objects > sMaxObjectsInFile)
                    {
                        return LoadError{ std::string(fileName), line, "", "the file holds too many objects" };
                    }
                    if (levels.size() + 1 > SkeletonMapLimits::sMaxNesting)
                    {
                        return LoadError{ std::string(fileName), line, "",
                            "nesting is deeper than " + std::to_string(SkeletonMapLimits::sMaxNesting) + " levels" };
                    }
                    Level level;
                    level.mObject = c == '{';
                    level.mExpectKey = c == '{';
                    levels.push_back(std::move(level));
                }
                else if (c == ']' || c == '}')
                {
                    if (!levels.empty())
                    {
                        levels.pop_back();
                    }
                }
                else if (c == ',')
                {
                    ++commas;
                    if (commas > sMaxCommasInFile)
                    {
                        return LoadError{ std::string(fileName), line, "", "the file holds too many values" };
                    }
                    if (!levels.empty() && levels.back().mObject)
                    {
                        levels.back().mExpectKey = true;
                    }
                }
            }
            return std::nullopt;
        }

        std::string at(const std::string& pointer, std::string_view key)
        {
            return pointer + "/" + std::string(key);
        }

        std::string at(const std::string& pointer, std::size_t index)
        {
            return pointer + "/" + std::to_string(index);
        }

        bool hasControlCharacter(std::string_view text)
        {
            for (const char c : text)
            {
                const auto u = static_cast<unsigned char>(c);
                if (u < 0x20 || u == 0x7f)
                {
                    return true;
                }
            }
            return false;
        }

        bool isDigest(std::string_view text)
        {
            if (text.size() != sDigestHexSize)
            {
                return false;
            }
            return std::all_of(
                text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
        }

        // ---- Reader ------------------------------------------------------------------------------------------------

        class MapReader : public FileReader
        {
        public:
            using FileReader::FileReader;

            bool readDocument(const Json& root, SkeletonMap& out)
            {
                if (!readHeader(root, sFormatName,
                        { "format", "version", "name", "source", "profile", "roles", "missingRoles", "extra",
                            "proportions" },
                        sNewestVersion))
                {
                    return false;
                }
                if (!textField(root, "", "name", SkeletonMapLimits::sMaxTextBytes, out.mName))
                {
                    return false;
                }
                const Json* source = objectField(root, "", "source", "an object");
                if (source == nullptr || !readSource(*source, "/source", out.mSource))
                {
                    return false;
                }
                if (!textField(root, "", "profile", SkeletonMapLimits::sMaxTextBytes, out.mProfile))
                {
                    return false;
                }
                const Json* roles = listField(root, "roles", SkeletonMapLimits::sMaxBones);
                if (roles == nullptr)
                {
                    return false;
                }
                out.mRoles.resize(roles->size());
                for (std::size_t i = 0; i < roles->size(); ++i)
                {
                    if (!readRole((*roles)[i], at("/roles", i), out.mRoles[i]))
                    {
                        return false;
                    }
                }
                const auto missing = root.find("missingRoles");
                if (missing != root.end())
                {
                    if (!missing->is_array())
                    {
                        return fail(*missing, "/missingRoles", "must be an array");
                    }
                    if (missing->size() > canonicalRoles().size())
                    {
                        return fail(*missing, "/missingRoles",
                            "has more than " + std::to_string(canonicalRoles().size()) + " entries");
                    }
                    out.mMissingRoles.resize(missing->size());
                    for (std::size_t i = 0; i < missing->size(); ++i)
                    {
                        if (!readText((*missing)[i], at("/missingRoles", i), SkeletonMapLimits::sMaxNameBytes,
                                out.mMissingRoles[i]))
                        {
                            return false;
                        }
                    }
                }
                const Json* extras = listField(root, "extra", SkeletonMapLimits::sMaxBones);
                if (extras == nullptr)
                {
                    return false;
                }
                out.mExtra.resize(extras->size());
                for (std::size_t i = 0; i < extras->size(); ++i)
                {
                    if (!readExtra((*extras)[i], at("/extra", i), out.mExtra[i]))
                    {
                        return false;
                    }
                }
                const auto proportions = root.find("proportions");
                if (proportions != root.end())
                {
                    if (!proportions->is_object())
                    {
                        return fail(*proportions, "/proportions", "must be an object");
                    }
                    if (!readProportions(*proportions, "/proportions", out))
                    {
                        return false;
                    }
                }
                return true;
            }

        private:
            bool readText(const Json& value, const std::string& pointer, std::size_t maxBytes, std::string& out)
            {
                if (!value.is_string() || value.get_ref<const std::string&>() == kNonFiniteValue)
                {
                    return fail(value, pointer, "must be a string");
                }
                const std::string& text = value.get_ref<const std::string&>();
                if (text.empty())
                {
                    return fail(value, pointer, "must not be empty");
                }
                if (text.size() > maxBytes)
                {
                    return fail(value, pointer, "is longer than " + std::to_string(maxBytes) + " bytes");
                }
                if (hasControlCharacter(text))
                {
                    return fail(value, pointer, "must not contain control characters");
                }
                if (!isWellFormedUtf8(text))
                {
                    return fail(value, pointer, "is not well-formed UTF-8");
                }
                out = text;
                return true;
            }

            bool textField(const Json& object, const std::string& pointer, std::string_view key, std::size_t maxBytes,
                std::string& out)
            {
                const Json* value = member(object, pointer, key);
                return value != nullptr && readText(*value, at(pointer, key), maxBytes, out);
            }

            bool optionalText(const Json& object, const std::string& pointer, std::string_view key,
                std::size_t maxBytes, std::string& out)
            {
                const auto found = object.find(key);
                return found == object.end() || readText(*found, at(pointer, key), maxBytes, out);
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

            // A required array member with at most `limit` elements.
            const Json* listField(const Json& root, std::string_view key, std::size_t limit)
            {
                const Json* list = member(root, "", key);
                if (list == nullptr)
                {
                    return nullptr;
                }
                const std::string pointer = "/" + std::string(key);
                if (!list->is_array())
                {
                    fail(*list, pointer, "must be an array");
                    return nullptr;
                }
                if (list->size() > limit)
                {
                    fail(*list, pointer, "has more than " + std::to_string(limit) + " entries");
                    return nullptr;
                }
                return list;
            }

            bool readSource(const Json& object, const std::string& pointer, MapSource& out)
            {
                if (!onlyKeys(object, pointer, { "boneCount", "digest" }))
                {
                    return false;
                }
                const Json* count = member(object, pointer, "boneCount");
                if (count == nullptr)
                {
                    return false;
                }
                if (!count->is_number_integer())
                {
                    return fail(*count, at(pointer, "boneCount"), "must be an integer");
                }
                const std::int64_t value = count->is_number_unsigned()
                    ? static_cast<std::int64_t>(std::min<std::uint64_t>(count->get<std::uint64_t>(),
                        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
                    : count->get<std::int64_t>();
                if (value < 0 || value > static_cast<std::int64_t>(SkeletonMapLimits::sMaxBones))
                {
                    return fail(*count, at(pointer, "boneCount"),
                        "must be between 0 and " + std::to_string(SkeletonMapLimits::sMaxBones));
                }
                out.mBoneCount = static_cast<std::uint32_t>(value);
                const Json* digest = member(object, pointer, "digest");
                if (digest == nullptr)
                {
                    return false;
                }
                if (!digest->is_string() || !isDigest(digest->get_ref<const std::string&>()))
                {
                    return fail(*digest, at(pointer, "digest"), "must be 64 lower-case hexadecimal digits");
                }
                out.mDigest = digest->get_ref<const std::string&>();
                return true;
            }

            bool readRole(const Json& object, const std::string& pointer, MapRole& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer, { "role", "bone", "aliases" }))
                {
                    return false;
                }
                if (!textField(object, pointer, "role", SkeletonMapLimits::sMaxNameBytes, out.mRole)
                    || !textField(object, pointer, "bone", SkeletonMapLimits::sMaxNameBytes, out.mBone))
                {
                    return false;
                }
                const auto aliases = object.find("aliases");
                if (aliases == object.end())
                {
                    return true;
                }
                const std::string aliasPointer = at(pointer, "aliases");
                if (!aliases->is_array())
                {
                    return fail(*aliases, aliasPointer, "must be an array");
                }
                if (aliases->size() > SkeletonMapLimits::sMaxAliasesPerRole)
                {
                    return fail(*aliases, aliasPointer,
                        "has more than " + std::to_string(SkeletonMapLimits::sMaxAliasesPerRole) + " entries");
                }
                out.mAliases.resize(aliases->size());
                for (std::size_t i = 0; i < aliases->size(); ++i)
                {
                    if (!readText(
                            (*aliases)[i], at(aliasPointer, i), SkeletonMapLimits::sMaxNameBytes, out.mAliases[i]))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool readSuggestion(const Json& object, const std::string& pointer, MapSuggestion& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer, { "heuristic", "confidence" }))
                {
                    return false;
                }
                if (!textField(object, pointer, "heuristic", SkeletonMapLimits::sMaxNameBytes, out.mHeuristic))
                {
                    return false;
                }
                const Json* confidence = member(object, pointer, "confidence");
                return confidence != nullptr
                    && readEnum(*confidence, at(pointer, "confidence"), sConfidences, out.mConfidence);
            }

            bool readExtra(const Json& object, const std::string& pointer, MapExtra& out)
            {
                if (!object.is_object())
                {
                    return fail(object, pointer, "must be an object");
                }
                if (!onlyKeys(object, pointer,
                        { "bone", "action", "parent", "kind", "useClipTrack", "rule", "to", "toSecond", "attachPoint",
                            "reason", "suggested", "accepted" }))
                {
                    return false;
                }
                if (!textField(object, pointer, "bone", SkeletonMapLimits::sMaxNameBytes, out.mBone))
                {
                    return false;
                }
                const Json* action = member(object, pointer, "action");
                if (action == nullptr || !readEnum(*action, at(pointer, "action"), sActions, out.mAction))
                {
                    return false;
                }
                if (!optionalText(object, pointer, "parent", SkeletonMapLimits::sMaxNameBytes, out.mParent)
                    || !optionalText(object, pointer, "to", SkeletonMapLimits::sMaxNameBytes, out.mTo)
                    || !optionalText(object, pointer, "toSecond", SkeletonMapLimits::sMaxNameBytes, out.mToSecond)
                    || !optionalText(object, pointer, "attachPoint", SkeletonMapLimits::sMaxNameBytes, out.mAttachPoint)
                    || !optionalText(object, pointer, "reason", SkeletonMapLimits::sMaxTextBytes, out.mReason))
                {
                    return false;
                }
                if (const auto kind = object.find("kind"); kind != object.end())
                {
                    SecondaryKind value = SecondaryKind::Other;
                    if (!readEnum(*kind, at(pointer, "kind"), sKinds, value))
                    {
                        return false;
                    }
                    out.mKind = value;
                }
                if (const auto rule = object.find("rule"); rule != object.end())
                {
                    FoldRule value = FoldRule::NearestMappedAncestor;
                    if (!readEnum(*rule, at(pointer, "rule"), sRules, value))
                    {
                        return false;
                    }
                    out.mRule = value;
                }
                if (const auto clip = object.find("useClipTrack"); clip != object.end())
                {
                    if (!readBool(*clip, at(pointer, "useClipTrack"), out.mUseClipTrack))
                    {
                        return false;
                    }
                }
                if (const auto suggested = object.find("suggested"); suggested != object.end())
                {
                    MapSuggestion value;
                    if (!readSuggestion(*suggested, at(pointer, "suggested"), value))
                    {
                        return false;
                    }
                    out.mSuggested = value;
                }
                const Json* accepted = member(object, pointer, "accepted");
                return accepted != nullptr && readBool(*accepted, at(pointer, "accepted"), out.mAccepted);
            }

            bool readProportions(const Json& object, const std::string& pointer, SkeletonMap& out)
            {
                if (!onlyKeys(object, pointer, { "mode", "note" }))
                {
                    return false;
                }
                const Json* mode = member(object, pointer, "mode");
                if (mode == nullptr || !readEnum(*mode, at(pointer, "mode"), sModes, out.mProportions))
                {
                    return false;
                }
                return optionalText(object, pointer, "note", SkeletonMapLimits::sMaxTextBytes, out.mProportionsNote);
            }
        };

        // ---- Writer ------------------------------------------------------------------------------------------------
        // A small tree laid out by the canonical rules of bodyjson.hpp: an array of scalars is one line, an object
        // whose values are all scalars or such arrays is one line, everything else is expanded.

        struct Node
        {
            enum class Kind
            {
                Scalar,
                FlatArray,
                Array,
                Object,
            };
            Kind mKind = Kind::Scalar;
            std::string mText;
            std::vector<Node> mItems;
            std::vector<std::string> mKeys;
        };

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
            return std::all_of(node.mItems.begin(), node.mItems.end(),
                [](const Node& item) { return item.mKind == Node::Kind::Scalar; });
        }

        bool isLeafObject(const Node& node)
        {
            if (node.mKind != Node::Kind::Object)
            {
                return false;
            }
            return std::all_of(node.mItems.begin(), node.mItems.end(),
                [](const Node& item) { return item.mKind == Node::Kind::Scalar || isFlatArray(item); });
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

        // ---- Roles -------------------------------------------------------------------------------------------------

        struct RoleStorage
        {
            std::deque<std::string> mStrings; // views point into these; a deque never moves its elements
            std::vector<RoleInfo> mRoles;

            std::string_view keep(std::string text)
            {
                mStrings.push_back(std::move(text));
                return mStrings.back();
            }

            void add(std::string name, bool optional, std::string parent)
            {
                const std::string_view n = keep(std::move(name));
                const std::string_view p = keep(std::move(parent));
                mRoles.push_back({ n, optional, p });
            }
        };

        const RoleStorage& roleStorage()
        {
            static const RoleStorage storage = [] {
                RoleStorage s;
                // The 17 core roles, in the order of the profile humanoid-to-eden.
                s.add("Pelvis", false, "");
                s.add("Spine", false, "Pelvis");
                s.add("Chest", false, "Spine");
                s.add("Neck", false, "Chest");
                s.add("Head", false, "Neck");
                s.add("LeftUpperArm", false, "Chest");
                s.add("LeftForearm", false, "LeftUpperArm");
                s.add("LeftHand", false, "LeftForearm");
                s.add("RightUpperArm", false, "Chest");
                s.add("RightForearm", false, "RightUpperArm");
                s.add("RightHand", false, "RightForearm");
                s.add("LeftThigh", false, "Pelvis");
                s.add("LeftShin", false, "LeftThigh");
                s.add("LeftFoot", false, "LeftShin");
                s.add("RightThigh", false, "Pelvis");
                s.add("RightShin", false, "RightThigh");
                s.add("RightFoot", false, "RightShin");
                // The optional roles (toddhow, answer 5).
                s.add("LeftClavicle", true, "Chest");
                s.add("RightClavicle", true, "Chest");
                s.add("Spine2", true, "Chest");
                s.add("LeftToes", true, "LeftFoot");
                s.add("RightToes", true, "RightFoot");
                for (const char* side : { "Left", "Right" })
                {
                    for (const char* finger : { "Thumb", "Index", "Middle", "Ring", "Pinky" })
                    {
                        for (int joint = 1; joint <= 3; ++joint)
                        {
                            const std::string name = std::string(side) + finger + std::to_string(joint);
                            const std::string parent = joint == 1
                                ? std::string(side) + "Hand"
                                : std::string(side) + finger + std::to_string(joint - 1);
                            s.add(name, true, parent);
                        }
                    }
                }
                for (int i = 1; i <= 8; ++i)
                {
                    s.add("Tail" + std::to_string(i), true, i == 1 ? "Pelvis" : "Tail" + std::to_string(i - 1));
                }
                return s;
            }();
            return storage;
        }

        // ---- Validator ---------------------------------------------------------------------------------------------

        constexpr std::size_t sMaxFindingsPerRule = 64;

        class Findings
        {
        public:
            void error(std::string id, std::string pointer, std::string message)
            {
                add(std::move(id), MapSeverity::Error, std::move(pointer), std::move(message));
            }

            void warning(std::string id, std::string pointer, std::string message)
            {
                add(std::move(id), MapSeverity::Warning, std::move(pointer), std::move(message));
            }

            MapReport take()
            {
                MapReport report;
                report.mFindings = std::move(mFindings);
                std::sort(
                    report.mFindings.begin(), report.mFindings.end(), [](const MapFinding& a, const MapFinding& b) {
                        if (a.mSeverity != b.mSeverity)
                        {
                            return a.mSeverity == MapSeverity::Error;
                        }
                        if (a.mPointer != b.mPointer)
                        {
                            return a.mPointer < b.mPointer;
                        }
                        if (a.mId != b.mId)
                        {
                            return a.mId < b.mId;
                        }
                        return a.mMessage < b.mMessage;
                    });
                return report;
            }

        private:
            void add(std::string id, MapSeverity severity, std::string pointer, std::string message)
            {
                std::size_t& seen = mCount[id];
                if (seen >= sMaxFindingsPerRule)
                {
                    return;
                }
                ++seen;
                mFindings.push_back({ std::move(id), severity, std::move(pointer), std::move(message) });
            }

            std::vector<MapFinding> mFindings;
            std::map<std::string, std::size_t> mCount;
        };

        std::string shown(std::string_view name)
        {
            std::string out = "'";
            for (const char c : name.substr(0, 60))
            {
                const auto u = static_cast<unsigned char>(c);
                out += (u < 0x20 || u == 0x7f) ? '?' : c;
            }
            if (name.size() > 60)
            {
                out += "...";
            }
            out += "'";
            return out;
        }

        // A text field: non-empty (when required), at most maxBytes, no control characters.
        void checkText(
            Findings& findings, const std::string& pointer, std::string_view text, std::size_t maxBytes, bool required)
        {
            if (text.empty())
            {
                if (required)
                {
                    findings.error("BODY-MAP-016", pointer, "must not be empty");
                }
                return;
            }
            if (text.size() > maxBytes)
            {
                findings.error("BODY-MAP-016", pointer, "is longer than " + std::to_string(maxBytes) + " bytes");
            }
            if (hasControlCharacter(text))
            {
                findings.error("BODY-MAP-016", pointer, "must not contain control characters");
            }
            else if (!isWellFormedUtf8(text))
            {
                findings.error("BODY-MAP-016", pointer, "is not well-formed UTF-8");
            }
        }

        bool forbidden(
            Findings& findings, const std::string& pointer, const char* field, bool present, const char* actionName)
        {
            if (present)
            {
                findings.error("BODY-MAP-009", at(pointer, field),
                    std::string("is not allowed for the action \"") + actionName + "\"");
            }
            return present;
        }

        void requireField(
            Findings& findings, const std::string& pointer, const char* field, bool present, const char* actionName)
        {
            if (!present)
            {
                findings.error("BODY-MAP-009", at(pointer, field),
                    std::string("is required for the action \"") + actionName + "\"");
            }
        }
    }

    // ---- Public: names -------------------------------------------------------------------------------------------

    const char* toString(MapAction value)
    {
        return nameOf(sActions, value);
    }

    const char* toString(FoldRule value)
    {
        return nameOf(sRules, value);
    }

    const char* toString(SecondaryKind value)
    {
        return nameOf(sKinds, value);
    }

    const char* toString(MapConfidence value)
    {
        return nameOf(sConfidences, value);
    }

    const char* toString(ProportionsMode value)
    {
        return nameOf(sModes, value);
    }

    const std::vector<RoleInfo>& canonicalRoles()
    {
        return roleStorage().mRoles;
    }

    const RoleInfo* findRole(std::string_view name)
    {
        for (const RoleInfo& role : roleStorage().mRoles)
        {
            if (role.mName == name)
            {
                return &role;
            }
        }
        return nullptr;
    }

    std::string normalisedBoneName(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        bool pendingSeparator = false;
        for (const char c : name)
        {
            if (c == ' ' || c == '_' || c == '-' || c == '.' || c == ':')
            {
                pendingSeparator = !out.empty();
                continue;
            }
            if (pendingSeparator)
            {
                out += '_';
                pendingSeparator = false;
            }
            out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        return out;
    }

    std::string engineBoneKey(std::string_view name)
    {
        // Misc::StringUtils::lowerCase: A to Z lower-cased through a table, every other byte unchanged.
        std::string out(name);
        for (char& c : out)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return out;
    }

    bool isWellFormedUtf8(std::string_view text)
    {
        std::size_t i = 0;
        while (i < text.size())
        {
            const auto lead = static_cast<unsigned char>(text[i]);
            if (lead < 0x80)
            {
                ++i;
                continue;
            }
            std::size_t extra = 0;
            std::uint32_t cp = 0;
            if (lead >= 0xc2 && lead <= 0xdf)
            {
                extra = 1;
                cp = lead & 0x1fu;
            }
            else if (lead >= 0xe0 && lead <= 0xef)
            {
                extra = 2;
                cp = lead & 0x0fu;
            }
            else if (lead >= 0xf0 && lead <= 0xf4)
            {
                extra = 3;
                cp = lead & 0x07u;
            }
            else
            {
                return false; // a continuation byte, an overlong lead (C0, C1) or above U+10FFFF (F5 to FF)
            }
            if (text.size() - i <= extra)
            {
                return false; // cut off
            }
            for (std::size_t k = 1; k <= extra; ++k)
            {
                const auto next = static_cast<unsigned char>(text[i + k]);
                if ((next & 0xc0u) != 0x80u)
                {
                    return false;
                }
                cp = (cp << 6) | (next & 0x3fu);
            }
            if ((extra == 2 && cp < 0x800) || (extra == 3 && (cp < 0x10000 || cp > 0x10ffff))
                || (cp >= 0xd800 && cp <= 0xdfff))
            {
                return false; // overlong, out of range or a surrogate
            }
            i += extra + 1;
        }
        return true;
    }

    std::string skeletonMapDigest(const SkeletonMap& map)
    {
        const std::string text = writeSkeletonMap(map);
        EdenArchive::Hasher hasher;
        const std::string domain = "openeden.skeletonmap.map-digest.v1";
        hasher.update(domain.data(), domain.size());
        hasher.update(text.data(), text.size());
        return EdenArchive::toHex(hasher.finalize());
    }

    // ---- Public: the digest --------------------------------------------------------------------------------------

    std::string skeletonDigest(const std::vector<RestBone>& skeleton)
    {
        struct Record
        {
            std::string mName;
            std::string mParent;
            std::string mBytes; // the whole record, length-prefixed fields then the rest values as float bits
        };
        const auto putUint32 = [](std::string& out, std::uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8)
            {
                out += static_cast<char>((value >> shift) & 0xff);
            }
        };
        const auto putField = [&putUint32](std::string& out, const std::string& field) {
            putUint32(out, static_cast<std::uint32_t>(field.size()));
            out += field;
        };
        const auto putFloat = [&putUint32](std::string& out, double value) {
            if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
            {
                // Never cast: a double beyond the float range is undefined behaviour to convert. A marker no finite
                // float has (a quiet NaN pattern) keeps the digest defined; BODY-MAP-025 refuses such a skeleton.
                putUint32(out, 0x7fc0beefu);
                return;
            }
            float f = static_cast<float>(value);
            if (f == 0.0f)
            {
                f = 0.0f; // negative zero and zero hash alike
            }
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            if (f == 0.0f)
            {
                bits = 0;
            }
            putUint32(out, bits);
        };
        std::vector<Record> records;
        records.reserve(skeleton.size());
        for (const RestBone& bone : skeleton)
        {
            Record record;
            record.mName = bone.mName;
            if (bone.mParent < 0)
            {
                record.mParent.clear();
            }
            else if (static_cast<std::size_t>(bone.mParent) < skeleton.size())
            {
                record.mParent = skeleton[static_cast<std::size_t>(bone.mParent)].mName;
            }
            else
            {
                record.mParent = std::string("\x01invalid-parent");
            }
            putField(record.mBytes, record.mName);
            putField(record.mBytes, record.mParent);
            for (const double v : bone.mTranslation)
            {
                putFloat(record.mBytes, v);
            }
            for (const double v : bone.mRotation)
            {
                putFloat(record.mBytes, v);
            }
            records.push_back(std::move(record));
        }
        std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
            if (a.mName != b.mName)
            {
                return a.mName < b.mName;
            }
            if (a.mParent != b.mParent)
            {
                return a.mParent < b.mParent;
            }
            return a.mBytes < b.mBytes;
        });
        EdenArchive::Hasher hasher;
        const std::string domain = "openeden.skeletonmap.digest.v1";
        hasher.update(domain.data(), domain.size());
        std::string count;
        putUint32(count, static_cast<std::uint32_t>(records.size()));
        hasher.update(count.data(), count.size());
        for (const Record& record : records)
        {
            hasher.update(record.mBytes.data(), record.mBytes.size());
        }
        return EdenArchive::toHex(hasher.finalize());
    }

    // ---- Public: reading and writing ---------------------------------------------------------------------------

    SkeletonMapResult readSkeletonMap(std::string_view text, std::string_view fileName)
    {
        SkeletonMapResult result;
        try
        {
            if (const std::optional<LoadError> tooBig = prescan(text, fileName))
            {
                result.mError = *tooBig;
                return result;
            }
            SkeletonMap map;
            const std::optional<LoadError> error = opas::format::detail::parseAndRead<MapReader>(
                text, fileName, [&map](MapReader& reader, const Json& root) { return reader.readDocument(root, map); });
            if (error)
            {
                result.mError = *error;
            }
            else
            {
                result.mValue = std::move(map);
            }
        }
        catch (const std::exception& e)
        {
            // Out of memory and the like: the contract is that nothing is thrown.
            result.mValue.reset();
            result.mError = LoadError{ std::string(fileName), 1, "", std::string("cannot read the map: ") + e.what() };
        }
        return result;
    }

    SkeletonMapResult readSkeletonMapFile(const std::filesystem::path& path)
    {
        SkeletonMapResult result;
        const std::string fileName = path.generic_string();
        try
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                result.mError = LoadError{ fileName, 0, "", "cannot open the file" };
                return result;
            }
            std::string text;
            text.resize(SkeletonMapLimits::sMaxFileBytes + 1);
            stream.read(text.data(), static_cast<std::streamsize>(text.size()));
            text.resize(static_cast<std::size_t>(stream.gcount()));
            if (stream.bad())
            {
                result.mError = LoadError{ fileName, 0, "", "cannot read the file" };
                return result;
            }
            return readSkeletonMap(text, fileName);
        }
        catch (const std::exception& e)
        {
            result.mValue.reset();
            result.mError = LoadError{ fileName, 0, "", std::string("cannot read the file: ") + e.what() };
            return result;
        }
    }

    std::string writeSkeletonMap(const SkeletonMap& map)
    {
        Node root = object();
        add(root, "format", str(sFormatName));
        add(root, "version", integer(sNewestVersion));
        add(root, "name", str(map.mName));
        Node source = object();
        add(source, "boneCount", integer(map.mSource.mBoneCount));
        add(source, "digest", str(map.mSource.mDigest));
        add(root, "source", std::move(source));
        add(root, "profile", str(map.mProfile));

        Node roles = array();
        for (const MapRole& role : map.mRoles)
        {
            Node entry = object();
            add(entry, "role", str(role.mRole));
            add(entry, "bone", str(role.mBone));
            if (!role.mAliases.empty())
            {
                add(entry, "aliases", flatStrings(role.mAliases));
            }
            roles.mItems.push_back(std::move(entry));
        }
        add(root, "roles", std::move(roles));
        if (!map.mMissingRoles.empty())
        {
            add(root, "missingRoles", flatStrings(map.mMissingRoles));
        }

        Node extras = array();
        for (const MapExtra& e : map.mExtra)
        {
            Node entry = object();
            add(entry, "bone", str(e.mBone));
            add(entry, "action", str(toString(e.mAction)));
            if (!e.mParent.empty())
            {
                add(entry, "parent", str(e.mParent));
            }
            if (e.mKind)
            {
                add(entry, "kind", str(toString(*e.mKind)));
            }
            if (e.mUseClipTrack)
            {
                add(entry, "useClipTrack", boolean(true));
            }
            if (e.mRule)
            {
                add(entry, "rule", str(toString(*e.mRule)));
            }
            if (!e.mTo.empty())
            {
                add(entry, "to", str(e.mTo));
            }
            if (!e.mToSecond.empty())
            {
                add(entry, "toSecond", str(e.mToSecond));
            }
            if (!e.mAttachPoint.empty())
            {
                add(entry, "attachPoint", str(e.mAttachPoint));
            }
            if (!e.mReason.empty())
            {
                add(entry, "reason", str(e.mReason));
            }
            if (e.mSuggested)
            {
                Node suggested = object();
                add(suggested, "heuristic", str(e.mSuggested->mHeuristic));
                add(suggested, "confidence", str(toString(e.mSuggested->mConfidence)));
                add(entry, "suggested", std::move(suggested));
            }
            add(entry, "accepted", boolean(e.mAccepted));
            extras.mItems.push_back(std::move(entry));
        }
        add(root, "extra", std::move(extras));

        Node proportions = object();
        add(proportions, "mode", str(toString(map.mProportions)));
        if (!map.mProportionsNote.empty())
        {
            add(proportions, "note", str(map.mProportionsNote));
        }
        add(root, "proportions", std::move(proportions));

        std::string out;
        render(root, 0, out);
        out += "\n";
        return out;
    }

    // ---- Public: the report --------------------------------------------------------------------------------------

    bool MapReport::hasErrors() const
    {
        return std::any_of(
            mFindings.begin(), mFindings.end(), [](const MapFinding& f) { return f.mSeverity == MapSeverity::Error; });
    }

    std::size_t MapReport::count(std::string_view id) const
    {
        return static_cast<std::size_t>(
            std::count_if(mFindings.begin(), mFindings.end(), [id](const MapFinding& f) { return f.mId == id; }));
    }

    MapReport validateSourceSkeleton(const std::vector<RestBone>& skeleton)
    {
        Findings findings;
        if (skeleton.size() > SkeletonMapLimits::sMaxBones)
        {
            findings.error("BODY-MAP-020", "/skeleton",
                "has " + std::to_string(skeleton.size()) + " bones, more than "
                    + std::to_string(SkeletonMapLimits::sMaxBones));
        }
        std::set<std::string> names;
        std::set<std::string> normalised;
        std::vector<std::size_t> depth(skeleton.size(), 0);
        for (std::size_t i = 0; i < skeleton.size(); ++i)
        {
            const RestBone& bone = skeleton[i];
            const std::string pointer = at("/skeleton", i);
            if (bone.mName.empty() || bone.mName.size() > SkeletonMapLimits::sMaxNameBytes
                || hasControlCharacter(bone.mName) || !isWellFormedUtf8(bone.mName))
            {
                findings.error("BODY-MAP-020", at(pointer, "name"),
                    "must be 1 to 128 bytes of well-formed UTF-8 without control characters");
            }
            for (std::size_t k = 0; k < bone.mTranslation.size(); ++k)
            {
                const double v = bone.mTranslation[k];
                if (!std::isfinite(v) || std::abs(v) > SkeletonMapLimits::sMaxRestTranslation)
                {
                    findings.error(
                        "BODY-MAP-025", at(at(pointer, "translation"), k), "must be finite and at most 1e4 in size");
                }
            }
            double lengthSquared = 0;
            bool rotationFinite = true;
            for (const double v : bone.mRotation)
            {
                rotationFinite = rotationFinite && std::isfinite(v) && std::abs(v) <= 2.0;
                lengthSquared += rotationFinite ? v * v : 0.0;
            }
            if (!rotationFinite
                || std::abs(std::sqrt(lengthSquared) - 1.0) > SkeletonMapLimits::sRestRotationLengthTolerance)
            {
                findings.error("BODY-MAP-025", at(pointer, "rotation"), "must be a finite unit quaternion");
            }
            if (!names.insert(bone.mName).second)
            {
                findings.error("BODY-MAP-022", at(pointer, "name"), "the name " + shown(bone.mName) + " is used twice");
            }
            else if (!normalised.insert(normalisedBoneName(bone.mName)).second)
            {
                findings.error("BODY-MAP-022", at(pointer, "name"),
                    "the name " + shown(bone.mName) + " is the same as another bone's after normalisation");
            }
            if (bone.mParent < -1 || bone.mParent >= static_cast<int>(i))
            {
                findings.error("BODY-MAP-021", at(pointer, "parent"),
                    "the parent index " + std::to_string(bone.mParent) + " must be -1 or the index of an earlier bone");
            }
            else if (bone.mParent >= 0)
            {
                depth[i] = depth[static_cast<std::size_t>(bone.mParent)] + 1;
            }
            if (depth[i] >= SkeletonMapLimits::sMaxDepth)
            {
                findings.error("BODY-MAP-023", pointer,
                    "the hierarchy is deeper than " + std::to_string(SkeletonMapLimits::sMaxDepth) + " levels");
            }
        }
        return findings.take();
    }

    MapReport validateSkeletonMap(
        const SkeletonMap& map, const std::vector<RestBone>* source, const std::vector<BoneWeightMass>* masses)
    {
        Findings findings;

        checkText(findings, "/name", map.mName, SkeletonMapLimits::sMaxTextBytes, true);
        checkText(findings, "/profile", map.mProfile, SkeletonMapLimits::sMaxTextBytes, true);
        checkText(findings, "/proportions/note", map.mProportionsNote, SkeletonMapLimits::sMaxTextBytes, false);

        if (map.mRoles.size() > SkeletonMapLimits::sMaxBones)
        {
            findings.error(
                "BODY-MAP-014", "/roles", "has more than " + std::to_string(SkeletonMapLimits::sMaxBones) + " entries");
        }
        if (map.mExtra.size() > SkeletonMapLimits::sMaxBones)
        {
            findings.error(
                "BODY-MAP-014", "/extra", "has more than " + std::to_string(SkeletonMapLimits::sMaxBones) + " entries");
        }
        if (map.mRoles.size() + map.mExtra.size() > SkeletonMapLimits::sMaxBones
            && map.mExtra.size() <= SkeletonMapLimits::sMaxBones && map.mRoles.size() <= SkeletonMapLimits::sMaxBones)
        {
            findings.error("BODY-MAP-014", "/extra",
                "roles and extra entries together have more than " + std::to_string(SkeletonMapLimits::sMaxBones)
                    + " bones");
        }

        // Roles.
        std::map<std::string, std::string> roleBone; // role name -> source bone
        std::set<std::string> roleNames;
        for (std::size_t i = 0; i < map.mRoles.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            const MapRole& role = map.mRoles[i];
            const std::string pointer = at("/roles", i);
            checkText(findings, at(pointer, "role"), role.mRole, SkeletonMapLimits::sMaxNameBytes, true);
            checkText(findings, at(pointer, "bone"), role.mBone, SkeletonMapLimits::sMaxNameBytes, true);
            if (findRole(role.mRole) == nullptr)
            {
                findings.error(
                    "BODY-MAP-001", at(pointer, "role"), "the role " + shown(role.mRole) + " is not a canonical role");
            }
            if (!roleNames.insert(role.mRole).second)
            {
                findings.error(
                    "BODY-MAP-002", at(pointer, "role"), "the role " + shown(role.mRole) + " is listed twice");
            }
            roleBone.emplace(role.mRole, role.mBone);
            if (role.mAliases.size() > SkeletonMapLimits::sMaxAliasesPerRole)
            {
                findings.error("BODY-MAP-014", at(pointer, "aliases"),
                    "has more than " + std::to_string(SkeletonMapLimits::sMaxAliasesPerRole) + " entries");
            }
        }
        if (roleNames.count("Pelvis") == 0)
        {
            findings.error("BODY-MAP-019", "/roles", "the map names no Pelvis role");
        }
        // Every core role must be in the map (review fix 2): a missing one would leave its bone to a fold that freezes
        // the joint. Pelvis already has its own rule.
        for (const RoleInfo& info : canonicalRoles())
        {
            if (!info.mOptional && info.mName != "Pelvis" && roleNames.count(std::string(info.mName)) == 0)
            {
                findings.error("BODY-MAP-024", "/roles",
                    "the core role " + std::string(info.mName) + " has no bone; find it and list it under roles");
            }
        }
        for (std::size_t i = 0; i < map.mMissingRoles.size() && i < canonicalRoles().size(); ++i)
        {
            const std::string pointer = at("/missingRoles", i);
            checkText(findings, pointer, map.mMissingRoles[i], SkeletonMapLimits::sMaxNameBytes, true);
            if (findRole(map.mMissingRoles[i]) == nullptr)
            {
                findings.error(
                    "BODY-MAP-001", pointer, "the role " + shown(map.mMissingRoles[i]) + " is not a canonical role");
            }
            findings.error("BODY-MAP-024", pointer,
                "the suggestion found no bone for the role " + shown(map.mMissingRoles[i])
                    + "; list it under roles and remove it here");
        }

        // Every bone name of the map: role bones, then extras. A name used twice is 003, two that normalise equal 004.
        std::map<std::string, std::string> firstUse; // exact name -> pointer of the first use
        std::map<std::string, std::string> firstNormalised; // normalised name -> exact name
        const auto noteName = [&](const std::string& name, const std::string& pointer) {
            if (name.empty())
            {
                return;
            }
            const auto [it, inserted] = firstUse.emplace(name, pointer);
            if (!inserted)
            {
                findings.error(
                    "BODY-MAP-003", pointer, "the bone " + shown(name) + " is already used at " + it->second);
                return;
            }
            const auto [nit, nInserted] = firstNormalised.emplace(normalisedBoneName(name), name);
            if (!nInserted)
            {
                findings.error("BODY-MAP-004", pointer,
                    "the bone " + shown(name) + " is the same as " + shown(nit->second) + " after normalisation");
            }
        };
        for (std::size_t i = 0; i < map.mRoles.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            noteName(map.mRoles[i].mBone, at(at("/roles", i), "bone"));
        }
        for (std::size_t i = 0; i < map.mExtra.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            checkText(
                findings, at(at("/extra", i), "bone"), map.mExtra[i].mBone, SkeletonMapLimits::sMaxNameBytes, true);
            noteName(map.mExtra[i].mBone, at(at("/extra", i), "bone"));
        }

        // Aliases: not a bone of the map, not listed twice (also across roles), exactly or after normalisation: the
        // engine looks names up by engineBoneKey, so an alias "breast_l_01" next to an extra bone "Breast_L_01" would
        // be one bone with two rules (review fix 1).
        std::set<std::string> seenAliases;
        for (std::size_t i = 0; i < map.mRoles.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            const MapRole& role = map.mRoles[i];
            for (std::size_t a = 0; a < role.mAliases.size() && a < SkeletonMapLimits::sMaxAliasesPerRole + 1; ++a)
            {
                const std::string pointer = at(at(at("/roles", i), "aliases"), a);
                checkText(findings, pointer, role.mAliases[a], SkeletonMapLimits::sMaxNameBytes, true);
                const std::string normalised = normalisedBoneName(role.mAliases[a]);
                if (firstUse.count(role.mAliases[a]) != 0 || firstNormalised.count(normalised) != 0)
                {
                    findings.error("BODY-MAP-017", pointer,
                        "the alias " + shown(role.mAliases[a]) + " is also a bone of the map (or the same after "
                            + "normalisation)");
                }
                if (!seenAliases.insert(normalised).second)
                {
                    findings.error("BODY-MAP-017", pointer,
                        "the alias " + shown(role.mAliases[a]) + " is listed twice (or the same as another alias after "
                            + "normalisation)");
                }
            }
        }

        // A bone named exactly like a canonical role it is not: parents and fold targets resolve role names first, so
        // that bone cannot be named as one (review fix 10, a known limit of v1, warned about).
        for (std::size_t i = 0; i < map.mRoles.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            const MapRole& role = map.mRoles[i];
            if (findRole(role.mBone) != nullptr && role.mBone != role.mRole)
            {
                findings.warning("BODY-MAP-026", at(at("/roles", i), "bone"),
                    "the bone " + shown(role.mBone) + " is named like the role " + shown(role.mBone)
                        + "; a parent or fold target of that name means the role");
            }
        }
        for (std::size_t i = 0; i < map.mExtra.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            if (findRole(map.mExtra[i].mBone) != nullptr)
            {
                findings.warning("BODY-MAP-026", at(at("/extra", i), "bone"),
                    "the bone " + shown(map.mExtra[i].mBone)
                        + " is named like a role; a parent or fold target of that name means the role, not this bone");
            }
        }

        // Extra entries.
        std::map<std::string, const MapExtra*> extraByBone;
        for (const MapExtra& e : map.mExtra)
        {
            extraByBone.emplace(e.mBone, &e);
        }
        std::set<std::string> roleBoneNames;
        for (const auto& [role, bone] : roleBone)
        {
            roleBoneNames.insert(bone);
        }
        // A "mapped" name: a role name, a role bone or a secondary bone. resolved: the bone name it stands for.
        const auto resolveMapped = [&](const std::string& name, bool allowSecondary) -> std::optional<std::string> {
            if (const auto role = roleBone.find(name); role != roleBone.end())
            {
                return role->second;
            }
            if (roleBoneNames.count(name) != 0)
            {
                return name;
            }
            if (allowSecondary)
            {
                const auto e = extraByBone.find(name);
                if (e != extraByBone.end() && e->second->mAction == MapAction::Secondary)
                {
                    return name;
                }
            }
            return std::nullopt;
        };

        // The weight each bone carries, summed over duplicates (the list is the caller's and may be long).
        std::map<std::string, double> massOf;
        if (masses != nullptr)
        {
            for (const BoneWeightMass& m : *masses)
            {
                massOf[m.mBone] += m.mMass;
            }
        }

        for (std::size_t i = 0; i < map.mExtra.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            const MapExtra& e = map.mExtra[i];
            const std::string pointer = at("/extra", i);
            const char* action = toString(e.mAction);
            if (!e.mAccepted)
            {
                findings.error(
                    "BODY-MAP-007", at(pointer, "accepted"), "the entry for " + shown(e.mBone) + " is not accepted");
            }
            if (e.mAction == MapAction::Ask)
            {
                findings.error("BODY-MAP-008", at(pointer, "action"),
                    "the entry for " + shown(e.mBone) + " has no decision yet (\"ask\")");
            }
            checkText(findings, at(pointer, "parent"), e.mParent, SkeletonMapLimits::sMaxNameBytes, false);
            checkText(findings, at(pointer, "to"), e.mTo, SkeletonMapLimits::sMaxNameBytes, false);
            checkText(findings, at(pointer, "toSecond"), e.mToSecond, SkeletonMapLimits::sMaxNameBytes, false);
            checkText(findings, at(pointer, "attachPoint"), e.mAttachPoint, SkeletonMapLimits::sMaxNameBytes, false);
            checkText(findings, at(pointer, "reason"), e.mReason, SkeletonMapLimits::sMaxTextBytes, false);

            switch (e.mAction)
            {
                case MapAction::Secondary:
                    requireField(findings, pointer, "parent", !e.mParent.empty(), action);
                    requireField(findings, pointer, "kind", e.mKind.has_value(), action);
                    forbidden(findings, pointer, "rule", e.mRule.has_value(), action);
                    forbidden(findings, pointer, "to", !e.mTo.empty(), action);
                    forbidden(findings, pointer, "toSecond", !e.mToSecond.empty(), action);
                    forbidden(findings, pointer, "attachPoint", !e.mAttachPoint.empty(), action);
                    forbidden(findings, pointer, "reason", !e.mReason.empty(), action);
                    break;
                case MapAction::Fold:
                {
                    forbidden(findings, pointer, "parent", !e.mParent.empty(), action);
                    forbidden(findings, pointer, "kind", e.mKind.has_value(), action);
                    forbidden(findings, pointer, "useClipTrack", e.mUseClipTrack, action);
                    forbidden(findings, pointer, "attachPoint", !e.mAttachPoint.empty(), action);
                    forbidden(findings, pointer, "reason", !e.mReason.empty(), action);
                    const FoldRule rule = e.mRule.value_or(FoldRule::NearestMappedAncestor);
                    if (rule == FoldRule::NearestMappedAncestor)
                    {
                        forbidden(
                            findings, pointer, "to", !e.mTo.empty(), "fold with the rule nearest-mapped-ancestor");
                        forbidden(findings, pointer, "toSecond", !e.mToSecond.empty(),
                            "fold with the rule nearest-mapped-ancestor");
                    }
                    else if (rule == FoldRule::Named)
                    {
                        requireField(findings, pointer, "to", !e.mTo.empty(), "fold with the rule named");
                        forbidden(findings, pointer, "toSecond", !e.mToSecond.empty(), "fold with the rule named");
                    }
                    else
                    {
                        requireField(findings, pointer, "to", !e.mTo.empty(), "fold with the rule distribute");
                        requireField(
                            findings, pointer, "toSecond", !e.mToSecond.empty(), "fold with the rule distribute");
                    }
                    break;
                }
                case MapAction::Attach:
                    requireField(findings, pointer, "parent", !e.mParent.empty(), action);
                    requireField(findings, pointer, "attachPoint", !e.mAttachPoint.empty(), action);
                    forbidden(findings, pointer, "kind", e.mKind.has_value(), action);
                    forbidden(findings, pointer, "useClipTrack", e.mUseClipTrack, action);
                    forbidden(findings, pointer, "rule", e.mRule.has_value(), action);
                    forbidden(findings, pointer, "to", !e.mTo.empty(), action);
                    forbidden(findings, pointer, "toSecond", !e.mToSecond.empty(), action);
                    forbidden(findings, pointer, "reason", !e.mReason.empty(), action);
                    break;
                case MapAction::Drop:
                    forbidden(findings, pointer, "parent", !e.mParent.empty(), action);
                    forbidden(findings, pointer, "kind", e.mKind.has_value(), action);
                    forbidden(findings, pointer, "useClipTrack", e.mUseClipTrack, action);
                    forbidden(findings, pointer, "rule", e.mRule.has_value(), action);
                    forbidden(findings, pointer, "to", !e.mTo.empty(), action);
                    forbidden(findings, pointer, "toSecond", !e.mToSecond.empty(), action);
                    forbidden(findings, pointer, "attachPoint", !e.mAttachPoint.empty(), action);
                    if (e.mReason.empty())
                    {
                        findings.warning("BODY-MAP-018", at(pointer, "reason"),
                            "the drop of " + shown(e.mBone) + " gives no reason");
                    }
                    break;
                case MapAction::Ask:
                    forbidden(findings, pointer, "parent", !e.mParent.empty(), action);
                    forbidden(findings, pointer, "kind", e.mKind.has_value(), action);
                    forbidden(findings, pointer, "useClipTrack", e.mUseClipTrack, action);
                    forbidden(findings, pointer, "rule", e.mRule.has_value(), action);
                    forbidden(findings, pointer, "to", !e.mTo.empty(), action);
                    forbidden(findings, pointer, "toSecond", !e.mToSecond.empty(), action);
                    forbidden(findings, pointer, "attachPoint", !e.mAttachPoint.empty(), action);
                    forbidden(findings, pointer, "reason", !e.mReason.empty(), action);
                    break;
            }

            // Parents and targets must be mapped.
            if ((e.mAction == MapAction::Secondary || e.mAction == MapAction::Attach) && !e.mParent.empty())
            {
                if (!resolveMapped(e.mParent, true))
                {
                    findings.error("BODY-MAP-010", at(pointer, "parent"),
                        "the parent " + shown(e.mParent) + " is neither a role, a role bone nor a secondary bone");
                }
            }
            if (e.mAction == MapAction::Fold)
            {
                std::optional<std::string> first;
                std::optional<std::string> second;
                if (!e.mTo.empty())
                {
                    first = resolveMapped(e.mTo, true);
                    if (!first)
                    {
                        findings.error("BODY-MAP-010", at(pointer, "to"),
                            "the target " + shown(e.mTo) + " is neither a role, a role bone nor a secondary bone");
                    }
                }
                if (!e.mToSecond.empty())
                {
                    second = resolveMapped(e.mToSecond, true);
                    if (!second)
                    {
                        findings.error("BODY-MAP-010", at(pointer, "toSecond"),
                            "the target " + shown(e.mToSecond)
                                + " is neither a role, a role bone nor a secondary bone");
                    }
                }
                if (first && second && *first == *second
                    && e.mRule.value_or(FoldRule::NearestMappedAncestor) == FoldRule::Distribute)
                {
                    findings.error(
                        "BODY-MAP-011", at(pointer, "toSecond"), "a distribute fold needs two different targets");
                }
            }

            // Drops that lose weight.
            if (masses != nullptr && e.mAction == MapAction::Drop)
            {
                const auto carried = massOf.find(e.mBone);
                if (carried != massOf.end() && carried->second >= sMapWeightMassThreshold)
                {
                    findings.warning("BODY-MAP-015", pointer,
                        "the drop of " + shown(e.mBone) + " loses " + std::to_string(carried->second * 100.0)
                            + " percent of the skin weight");
                }
            }
        }

        // Parent chains of secondary and attach entries: cycles and depth.
        for (std::size_t i = 0; i < map.mExtra.size() && i < SkeletonMapLimits::sMaxBones; ++i)
        {
            const MapExtra& start = map.mExtra[i];
            if ((start.mAction != MapAction::Secondary && start.mAction != MapAction::Attach) || start.mParent.empty())
            {
                continue;
            }
            std::set<std::string> visited;
            visited.insert(start.mBone);
            std::string current = start.mParent;
            std::size_t depth = 1;
            while (true)
            {
                if (roleBone.count(current) != 0 || roleBoneNames.count(current) != 0)
                {
                    break; // reached a role: fine
                }
                const auto next = extraByBone.find(current);
                if (next == extraByBone.end() || next->second->mAction != MapAction::Secondary)
                {
                    break; // an unmapped parent is BODY-MAP-010, reported above
                }
                if (!visited.insert(current).second)
                {
                    findings.error("BODY-MAP-012", at(at("/extra", i), "parent"),
                        "the parents of " + shown(start.mBone) + " form a cycle");
                    break;
                }
                if (++depth > SkeletonMapLimits::sMaxDepth)
                {
                    findings.error("BODY-MAP-012", at(at("/extra", i), "parent"),
                        "the parent chain of " + shown(start.mBone) + " is deeper than "
                            + std::to_string(SkeletonMapLimits::sMaxDepth) + " levels");
                    break;
                }
                current = next->second->mParent;
                if (current.empty())
                {
                    break;
                }
            }
        }

        // Against the source skeleton.
        if (source != nullptr)
        {
            // By engineBoneKey: the engine finds bones whatever their case (review fix 1).
            std::map<std::string, std::string> sourceNames; // key -> the source's own spelling
            for (const RestBone& bone : *source)
            {
                sourceNames.emplace(engineBoneKey(bone.mName), bone.mName);
            }
            std::set<std::string> usedKeys;
            for (const auto& [name, pointer] : firstUse)
            {
                usedKeys.insert(engineBoneKey(name));
            }
            for (std::size_t i = 0; i < map.mRoles.size() && i < SkeletonMapLimits::sMaxBones; ++i)
            {
                if (!map.mRoles[i].mBone.empty() && sourceNames.count(engineBoneKey(map.mRoles[i].mBone)) == 0)
                {
                    findings.error("BODY-MAP-005", at(at("/roles", i), "bone"),
                        "the bone " + shown(map.mRoles[i].mBone) + " is not in the source skeleton");
                }
            }
            for (std::size_t i = 0; i < map.mExtra.size() && i < SkeletonMapLimits::sMaxBones; ++i)
            {
                if (!map.mExtra[i].mBone.empty() && sourceNames.count(engineBoneKey(map.mExtra[i].mBone)) == 0)
                {
                    findings.error("BODY-MAP-005", at(at("/extra", i), "bone"),
                        "the bone " + shown(map.mExtra[i].mBone) + " is not in the source skeleton");
                }
            }
            for (const auto& [key, name] : sourceNames)
            {
                if (usedKeys.count(key) == 0)
                {
                    findings.error("BODY-MAP-006", "/extra",
                        "the source bone " + shown(name) + " is neither a role bone nor listed in extra");
                }
            }
            if (map.mSource.mBoneCount != source->size())
            {
                findings.error("BODY-MAP-013", "/source/boneCount",
                    "the map was made for " + std::to_string(map.mSource.mBoneCount) + " bones, the skeleton has "
                        + std::to_string(source->size()));
            }
            if (map.mSource.mDigest != skeletonDigest(*source))
            {
                findings.error(
                    "BODY-MAP-013", "/source/digest", "the map was made for a different skeleton (digest differs)");
            }
        }
        return findings.take();
    }
}
