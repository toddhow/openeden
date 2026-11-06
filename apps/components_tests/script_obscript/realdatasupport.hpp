#ifndef OPENEDEN_COMPONENTS_TESTS_SCRIPT_OBSCRIPT_REALDATASUPPORT_H
#define OPENEDEN_COMPONENTS_TESTS_SCRIPT_OBSCRIPT_REALDATASUPPORT_H

// Task 0092 item 2: reading the player's own Fallout New Vegas masters for the gated checks. Read only; nothing read
// here is ever written to the repository or to committed output. The check that uses this is skipped, never failed,
// when the data is absent.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <zlib.h>

#include <openeden/script/obscript.hpp>

namespace RealData
{
    using openeden::script::FormId;
    using openeden::script::Value;
    using openeden::script::ValueKind;

    inline std::uint32_t le32(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
            | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    inline std::uint32_t le16(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8);
    }

    inline std::string lowered(std::string_view text)
    {
        std::string result(text);
        for (char& c : result)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return result;
    }

    struct Sub
    {
        std::string mType;
        std::string mData;
    };

    struct Record
    {
        std::string mType;
        std::uint32_t mFormId = 0;
        std::vector<Sub> mSubs;
    };

    // Walks the records of one master (24-byte headers, groups skipped into, zlib records inflated).
    template <class Visitor>
    void forEachRecord(const std::vector<std::uint8_t>& bytes, Visitor&& visit)
    {
        std::size_t position = 0;
        while (position + 24 <= bytes.size())
        {
            const std::uint8_t* header = bytes.data() + position;
            const std::string type(reinterpret_cast<const char*>(header), 4);
            if (type == "GRUP")
            {
                position += 24;
                continue;
            }
            const std::uint32_t size = le32(header + 4);
            const std::uint32_t flags = le32(header + 8);
            if (position + 24 + size > bytes.size())
            {
                return;
            }
            std::vector<std::uint8_t> data(bytes.begin() + static_cast<std::ptrdiff_t>(position + 24),
                bytes.begin() + static_cast<std::ptrdiff_t>(position + 24 + size));
            position += 24 + size;
            if ((flags & 0x40000) != 0 && type != "TES4")
            {
                if (data.size() < 4)
                {
                    continue;
                }
                std::vector<std::uint8_t> inflated(le32(data.data()));
                uLongf length = static_cast<uLongf>(inflated.size());
                if (uncompress(inflated.data(), &length, data.data() + 4, static_cast<uLong>(data.size() - 4)) != Z_OK)
                {
                    continue;
                }
                inflated.resize(length);
                data = std::move(inflated);
            }
            Record record;
            record.mType = type;
            record.mFormId = le32(header + 12);
            std::size_t at = 0;
            std::uint32_t extended = 0;
            while (at + 6 <= data.size())
            {
                std::string subType(reinterpret_cast<const char*>(data.data() + at), 4);
                std::size_t subSize = le16(data.data() + at + 4);
                if (subType == "XXXX" && at + 10 <= data.size())
                {
                    extended = le32(data.data() + at + 6);
                    at += 10;
                    continue;
                }
                if (extended != 0)
                {
                    subSize = extended;
                    extended = 0;
                }
                if (at + 6 + subSize > data.size())
                {
                    break;
                }
                record.mSubs.push_back(
                    { subType, std::string(reinterpret_cast<const char*>(data.data() + at + 6), subSize) });
                at += 6 + subSize;
            }
            visit(record);
        }
    }

    /// One script or fragment: text and compiled bytes, and where it came from.
    struct Unit
    {
        std::size_t mFile = 0;
        bool mIsScript = false;
        std::string mSource;
        std::string mCompiled;
    };

    struct Masters
    {
        std::vector<std::filesystem::path> mPaths;
        std::vector<std::vector<std::uint8_t>> mFiles;
    };

    inline bool openMasters(Masters& masters)
    {
        const char* folderName = std::getenv("OPENEDEN_FNV_DATA");
        if (folderName == nullptr || !std::filesystem::exists(std::filesystem::path(folderName) / "FalloutNV.esm"))
        {
            return false;
        }
        for (const auto& entry : std::filesystem::directory_iterator(folderName))
        {
            if (entry.path().extension() == ".esm")
            {
                masters.mPaths.push_back(entry.path());
            }
        }
        std::sort(masters.mPaths.begin(), masters.mPaths.end());
        for (const auto& path : masters.mPaths)
        {
            std::ifstream in(path, std::ios::binary);
            masters.mFiles.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        return true;
    }

    /// Every script and every fragment that has both text and compiled bytes, in file order.
    inline std::vector<Unit> readUnits(const Masters& masters)
    {
        static const std::set<std::string> fragmentRecords = { "INFO", "QUST", "PACK", "TERM", "REFR", "PERK" };
        std::vector<Unit> units;
        for (std::size_t file = 0; file < masters.mFiles.size(); ++file)
        {
            forEachRecord(masters.mFiles[file], [&](const Record& record) {
                const bool isScript = record.mType == "SCPT";
                if (!isScript && !fragmentRecords.contains(record.mType))
                {
                    return;
                }
                // a record holds one group per script: SCHR starts it, SCDA and SCTX fill it
                Unit current;
                bool open = false;
                const auto close = [&]() {
                    if (open && !current.mSource.empty() && !current.mCompiled.empty())
                    {
                        units.push_back(current);
                    }
                    current = Unit();
                    open = false;
                };
                for (const Sub& sub : record.mSubs)
                {
                    if (sub.mType == "SCHR")
                    {
                        close();
                        open = true;
                        current.mFile = file;
                        current.mIsScript = isScript;
                    }
                    else if (open && sub.mType == "SCDA")
                    {
                        current.mCompiled = sub.mData;
                    }
                    else if (open && sub.mType == "SCTX")
                    {
                        current.mSource = sub.mData;
                    }
                }
                close();
            });
        }
        return units;
    }

    class DataWorld : public openeden::script::ObScriptWorld
    {
    public:
        bool resolveForm(std::string_view editorId, FormId& form) const override
        {
            const auto it = mForms.find(lowered(editorId));
            if (it == mForms.end())
            {
                return false;
            }
            form = it->second;
            return true;
        }
        bool findVariable(FormId owner, std::string_view name, ValueKind& kind) const override
        {
            const auto it = mVariables.find(owner);
            if (it == mVariables.end())
            {
                return false;
            }
            const auto variable = it->second.find(lowered(name));
            if (variable == it->second.end())
            {
                return false;
            }
            kind = variable->second;
            return true;
        }
        Value getVariable(FormId, std::string_view) const override { return {}; }
        void setVariable(FormId, std::string_view, Value) override {}
        ValueKind globalKind(std::string_view name) const override
        {
            const auto it = mGlobals.find(lowered(name));
            return it == mGlobals.end() ? ValueKind::String : it->second;
        }
        Value getGlobal(std::string_view) const override { return {}; }
        void setGlobal(std::string_view, Value) override {}
        void report(std::string_view) override {}

        std::unordered_map<std::string, FormId> mForms;
        std::unordered_map<std::string, ValueKind> mGlobals;
        std::unordered_map<FormId, std::unordered_map<std::string, ValueKind>> mVariables;
    };

    /// The world the compile checks run against: every editor id is a form ("player" is "playerref"), globals have a
    /// type, and a quest or placed reference has the script variables of its script (or its base object's).
    inline void buildWorld(const Masters& masters, DataWorld& world, std::set<std::string>& variableNames)
    {
        FormId nextId = 0x1000;
        std::map<std::pair<std::size_t, std::uint32_t>, std::unordered_map<std::string, ValueKind>> scriptVariables;
        struct Owner
        {
            std::size_t mFile;
            std::string mName;
            std::uint32_t mScript;
            std::uint32_t mBase;
        };
        std::vector<Owner> owners;
        std::map<std::pair<std::size_t, std::uint32_t>, std::uint32_t> baseScripts;
        std::size_t fileIndex = 0;
        for (const auto& file : masters.mFiles)
        {
            forEachRecord(file, [&](const Record& record) {
                std::string name;
                char type = 's';
                std::uint32_t scriptRef = 0;
                std::uint32_t baseRef = 0;
                ValueKind pendingKind = ValueKind::Integer;
                std::unordered_map<std::string, ValueKind> variables;
                for (const Sub& sub : record.mSubs)
                {
                    if (sub.mType == "SLSD" && sub.mData.size() >= 17)
                    {
                        pendingKind = sub.mData[16] == 0 ? ValueKind::Float : ValueKind::Integer;
                    }
                    else if (sub.mType == "SCVR")
                    {
                        const std::string variable = lowered(sub.mData.substr(0, sub.mData.find(char(0))));
                        variables[variable] = pendingKind;
                        variableNames.insert(variable);
                    }
                    else if (sub.mType == "SCRI" && sub.mData.size() == 4)
                    {
                        scriptRef = le32(reinterpret_cast<const std::uint8_t*>(sub.mData.data()));
                    }
                    else if (sub.mType == "NAME" && sub.mData.size() == 4
                        && (record.mType == "REFR" || record.mType == "ACHR" || record.mType == "ACRE"))
                    {
                        baseRef = le32(reinterpret_cast<const std::uint8_t*>(sub.mData.data()));
                    }
                    if (sub.mType == "EDID")
                    {
                        name = lowered(sub.mData.substr(0, sub.mData.find('\0')));
                    }
                    else if (sub.mType == "FNAM" && record.mType == "GLOB" && !sub.mData.empty())
                    {
                        type = sub.mData[0];
                    }
                }
                if (record.mType == "SCPT")
                {
                    scriptVariables[{ fileIndex, record.mFormId }] = std::move(variables);
                }
                if (scriptRef != 0)
                {
                    baseScripts[{ fileIndex, record.mFormId }] = scriptRef;
                }
                if (name.empty())
                {
                    return;
                }
                if (scriptRef != 0 || baseRef != 0)
                {
                    owners.push_back({ fileIndex, name, scriptRef, baseRef });
                }
                world.mForms.emplace(name, nextId++);
                if (record.mType == "GLOB")
                {
                    world.mGlobals[name] = type == 'f' ? ValueKind::Float : ValueKind::Integer;
                }
            });
            ++fileIndex;
        }
        {
            const auto playerRef = world.mForms.find("playerref");
            world.mForms.emplace("player", playerRef != world.mForms.end() ? playerRef->second : nextId++);
        }
        auto findIn = [](const auto& map, std::size_t file, std::uint32_t id) {
            auto it = map.find({ file, id });
            if (it == map.end())
            {
                it = map.find({ std::size_t(0), id & 0xFFFFFFu });
            }
            return it;
        };
        for (const Owner& owner : owners)
        {
            std::uint32_t script = owner.mScript;
            if (script == 0 && owner.mBase != 0)
            {
                const auto base = findIn(baseScripts, owner.mFile, owner.mBase);
                if (base == baseScripts.end())
                {
                    continue;
                }
                script = base->second;
            }
            const auto variables = findIn(scriptVariables, owner.mFile, script);
            const auto form = world.mForms.find(owner.mName);
            if (variables != scriptVariables.end() && form != world.mForms.end())
            {
                world.mVariables[form->second] = variables->second;
            }
        }
    }
}

#endif
