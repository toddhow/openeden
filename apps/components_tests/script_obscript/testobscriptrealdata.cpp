// Task 0092 stage e: the gated real-data check. Skipped, never failed, when the Fallout New Vegas data is not there.
//
// In plain words: with the player's own copy of the New Vegas masters (read only), this compiles the source text of
// every script and script fragment in tolerant mode and prints AGGREGATE numbers only: how many compiled, why the
// others did not, how many different unknown function names there were, and how many scripts have the same number of
// Begin ... End blocks in the compiled bytes (SCDA) as in the recompiled source. Nothing from the masters is written
// anywhere: no script text, no name, no opcode. There is no function table yet (stage C of the research note), so the
// compile rate is a measurement and not a pass mark.
//
// Run it with tools/obscript_check/check.sh, or set OPENEDEN_FNV_DATA to the game's Data folder.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <zlib.h>

#include <components/script_obscript/obscriptruntime.hpp>

namespace
{
    using openeden::script::FormId;
    using openeden::script::ObScriptWorld;
    using openeden::script::Value;
    using openeden::script::ValueKind;

    std::uint32_t le32(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
            | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    std::uint32_t le16(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8);
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

    std::string lowered(std::string_view text)
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

    // The statement walk of note 0046: u16 opcode, u16 length, that many bytes; opcode 0x1C is four bytes with no
    // payload. Returns the number of Begin statements (opcode 0x10), or -1 when the walk does not end exactly at the
    // end.
    int countBeginStatements(const std::string& compiled)
    {
        std::size_t at = 0;
        int begins = 0;
        while (at + 4 <= compiled.size())
        {
            const std::uint32_t opcode = le16(reinterpret_cast<const std::uint8_t*>(compiled.data()) + at);
            const std::uint32_t length = le16(reinterpret_cast<const std::uint8_t*>(compiled.data()) + at + 2);
            if (opcode == 0x10)
            {
                ++begins;
            }
            at += opcode == 0x1C ? 4 : 4 + length;
        }
        return at == compiled.size() ? begins : -1;
    }

    // The invented FormIds and the globals the check needs: every editor id of the masters is a form.
    class DataWorld : public ObScriptWorld
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
        // The script variables of a quest or object, by the invented id of that quest or object.
        std::unordered_map<FormId, std::unordered_map<std::string, ValueKind>> mVariables;
    };

    std::string category(const std::vector<openeden::script::ErrorReport>& reports)
    {
        for (const auto& report : reports)
        {
            if (report.severity != openeden::script::Severity::Error)
            {
                continue;
            }
            const std::string& message = report.message;
            if (message.find("too large") != std::string::npos || message.find("too many") != std::string::npos
                || message.find("too long") != std::string::npos || message.find("nesting") != std::string::npos)
            {
                return "cap";
            }
            if (message.find("unknown function") != std::string::npos)
            {
                return "unknown function";
            }
            if (message.find("Unexpected") != std::string::npos || message.find("Syntax") != std::string::npos
                || message.find("syntax") != std::string::npos || message.find("end of file") != std::string::npos
                || message.find("outside") != std::string::npos || message.find("ScriptName") != std::string::npos
                || message.find("expected") != std::string::npos)
            {
                return "unknown construct";
            }
            if (message.find("variable") != std::string::npos || message.find("Parsing") != std::string::npos
                || message.find("type") != std::string::npos || message.find("Missing") != std::string::npos
                || message.find("expression") != std::string::npos)
            {
                return "type or variable error";
            }
            return "other";
        }
        return "other";
    }

    // The text of a message with anything quoted or numeric taken out, so that counting kinds reveals no script text.
    std::string messageKind(const std::string& message)
    {
        std::string kind;
        bool quoted = false;
        for (const char c : message)
        {
            if (c == '\'' || c == '"')
            {
                quoted = !quoted;
                kind += '*';
            }
            else if (!quoted && !(c >= '0' && c <= '9'))
            {
                kind += c;
            }
        }
        return kind;
    }

    TEST(ObScriptRealData, aggregate_compile_report_for_the_new_vegas_masters)
    {
        const char* folderName = std::getenv("OPENEDEN_FNV_DATA");
        if (folderName == nullptr || !std::filesystem::exists(std::filesystem::path(folderName) / "FalloutNV.esm"))
        {
            GTEST_SKIP() << "OPENEDEN_FNV_DATA does not name a Fallout New Vegas Data folder";
        }
        const std::filesystem::path folder(folderName);

        std::vector<std::filesystem::path> masters;
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            if (entry.path().extension() == ".esm")
            {
                masters.push_back(entry.path());
            }
        }
        std::sort(masters.begin(), masters.end());

        DataWorld world;
        std::vector<std::vector<std::uint8_t>> files;
        for (const auto& path : masters)
        {
            std::ifstream in(path, std::ios::binary);
            files.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        // Pass 1: every editor id is a form; globals have a type.
        FormId nextId = 0x1000;
        // The variables of every script, and which quest or object has which script (both by file and raw id).
        std::map<std::pair<std::size_t, std::uint32_t>, std::unordered_map<std::string, ValueKind>> scriptVariables;
        struct Owner
        {
            std::size_t mFile;
            std::string mName;
            std::uint32_t mScript;
            std::uint32_t mBase; // a placed reference: the object it is made from, whose script it has
        };
        std::vector<Owner> owners;
        std::map<std::pair<std::size_t, std::uint32_t>, std::uint32_t> baseScripts; // record id -> its script id
        std::size_t fileIndex = 0;
        for (const auto& file : files)
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
                        variables[lowered(sub.mData.substr(0, sub.mData.find(char(0))))] = pendingKind;
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

        // An id in a DLC may name a record of the base game: try the same file, then the first master by its low bits.
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

        // Pass 2: compile every script and every fragment.
        auto runtime = ScriptObscript::createRuntime(world, openeden::script::UnknownFunctionPolicy::Tolerant);
        std::size_t scripts = 0;
        std::size_t scriptsOk = 0;
        std::size_t fragments = 0;
        std::size_t fragmentsOk = 0;
        std::size_t withoutSource = 0;
        std::size_t compiledBytes = 0;
        std::size_t structureCompared = 0;
        std::size_t structureMatches = 0;
        std::size_t unknownFunctions = 0;
        std::map<std::string, std::size_t> failures;
        std::map<std::string, std::size_t> kinds; // the first error of each failed script, by kind

        for (const auto& file : files)
        {
            forEachRecord(file, [&](const Record& record) {
                static const std::set<std::string> fragmentRecords = { "INFO", "QUST", "PACK", "TERM", "REFR", "PERK" };
                const bool isScript = record.mType == "SCPT";
                if (!isScript && !fragmentRecords.contains(record.mType))
                {
                    return;
                }
                std::string compiled;
                bool hasSource = false;
                std::vector<std::string> sources;
                for (const Sub& sub : record.mSubs)
                {
                    if (sub.mType == "SCDA")
                    {
                        compiled = sub.mData;
                    }
                    else if (sub.mType == "SCTX")
                    {
                        sources.push_back(sub.mData);
                        hasSource = true;
                    }
                }
                if (!hasSource)
                {
                    if (isScript)
                    {
                        ++withoutSource;
                    }
                    return;
                }
                for (const std::string& text : sources)
                {
                    openeden::script::ScriptSource source;
                    source.name = "script";
                    source.text = text;
                    source.kind
                        = isScript ? openeden::script::ScriptKind::Script : openeden::script::ScriptKind::Fragment;
                    const auto result = runtime->compile(source);
                    std::size_t blocks = 0;
                    ScriptObscript::inspect(*runtime, result.script, blocks, unknownFunctions);
                    if (isScript)
                    {
                        ++scripts;
                        scriptsOk += result.ok ? 1 : 0;
                    }
                    else
                    {
                        ++fragments;
                        fragmentsOk += result.ok ? 1 : 0;
                    }
                    if (!result.ok)
                    {
                        ++failures[category(result.reports)];
                        for (const auto& report : result.reports)
                        {
                            if (report.severity == openeden::script::Severity::Error)
                            {
                                ++kinds[messageKind(report.message)];
                                break;
                            }
                        }
                    }
                    else if (isScript && !compiled.empty())
                    {
                        compiledBytes += compiled.size();
                        const int begins = countBeginStatements(compiled);
                        if (begins >= 0)
                        {
                            ++structureCompared;
                            structureMatches += static_cast<std::size_t>(begins) == blocks ? 1 : 0;
                        }
                    }
                }
            });
        }

        // Aggregate numbers only.
        std::cout << "OBSCRIPT-CHECK masters " << masters.size() << "\n";
        std::cout << "OBSCRIPT-CHECK scripts compiled " << scriptsOk << " of " << scripts
                  << " (without source: " << withoutSource << ")\n";
        std::cout << "OBSCRIPT-CHECK fragments compiled " << fragmentsOk << " of " << fragments << "\n";
        for (const auto& [name, count] : failures)
        {
            std::cout << "OBSCRIPT-CHECK failures (" << name << ") " << count << "\n";
        }
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (const auto& [kind, count] : kinds)
        {
            ranked.emplace_back(count, kind);
        }
        std::sort(ranked.rbegin(), ranked.rend());
        for (std::size_t i = 0; i < ranked.size() && i < 12; ++i)
        {
            std::cout << "OBSCRIPT-CHECK first error kind (" << ranked[i].second << ") " << ranked[i].first << "\n";
        }
        std::cout << "OBSCRIPT-CHECK distinct unknown function names accepted " << unknownFunctions << "\n";
        std::cout
            << "OBSCRIPT-CHECK block structure (Begin statements in SCDA against blocks of the recompiled source) "
            << structureMatches << " of " << structureCompared << " compiled scripts match\n";
        std::cout << "OBSCRIPT-CHECK SCDA bytes seen " << compiledBytes << "\n";

        EXPECT_GT(scripts, 0u);
    }
}
