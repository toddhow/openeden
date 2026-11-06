// Task 0092 item 2: the gated real-data check of the derived function table. Skipped, never failed, when the Fallout
// New Vegas data is not there.
//
// In plain words: with the player's own copy of the New Vegas masters (read only), this pairs the command names in the
// script text with the compiled commands in the script bytes, builds the function table, and prints AGGREGATE numbers
// only: how many scripts could be paired, how many different commands got a name and a reliable argument shape, how
// sure the table is, and how many scripts then compile in strict mode (every command known, every argument checked)
// and in tolerant mode, with and without the table, and for the add-on masters with a table made from the base game
// alone. No script text, command name or opcode is printed. When OPENEDEN_OBSCRIPT_TABLE_OUT names a file, the table
// is written there (outside the repository: it is derived from the player's data and is never committed or shipped).
//
// Run it with tools/obscript_check/check.sh (second check), or set OPENEDEN_FNV_DATA.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <components/script_obscript/derive.hpp>
#include <components/script_obscript/functiontable.hpp>
#include <components/script_obscript/obscriptruntime.hpp>

#include "realdatasupport.hpp"

namespace
{
    using namespace RealData;
    using openeden::script::UnknownFunctionPolicy;

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

    // The SHAPE of one source line, so that failures can be counted by what kind of line they are without recording any
    // text: words become W (the language's own words stay as they are), numbers N, strings S, punctuation stays.
    std::string lineShape(const std::string& text, std::uint32_t lineNumber)
    {
        static const std::set<std::string> language
            = { "set", "to", "if", "elseif", "else", "endif", "while", "endwhile", "return", "begin", "end", "short",
                  "long", "int", "float", "ref", "scn", "scriptname", "messagebox" };
        std::size_t start = 0;
        for (std::uint32_t n = 1; n < lineNumber; ++n)
        {
            start = text.find('\n', start);
            if (start == std::string::npos)
            {
                return "(past the end)";
            }
            ++start;
        }
        const std::size_t end = std::min(text.find('\n', start), text.size());
        const std::string line = text.substr(start, end - start);
        std::string shape;
        std::size_t i = 0;
        while (i < line.size() && shape.size() < 60)
        {
            const char c = line[i];
            if (c == ';')
            {
                break;
            }
            if (c == ' ' || c == '\t' || c == ',' || c == '\r')
            {
                ++i;
                continue;
            }
            if (!shape.empty())
            {
                shape += ' ';
            }
            if (c == '"')
            {
                const std::size_t close = line.find('"', i + 1);
                shape += 'S';
                i = close == std::string::npos ? line.size() : close + 1;
            }
            else if (c >= '0' && c <= '9')
            {
                while (i < line.size() && ((line[i] >= '0' && line[i] <= '9') || line[i] == '.'))
                {
                    ++i;
                }
                shape += 'N';
            }
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
            {
                std::size_t j = i;
                while (j < line.size()
                    && ((line[j] >= '0' && line[j] <= '9') || (line[j] >= 'a' && line[j] <= 'z')
                        || (line[j] >= 'A' && line[j] <= 'Z') || line[j] == '_'))
                {
                    ++j;
                }
                const std::string word = lowered(line.substr(i, j - i));
                shape += language.contains(word) ? word : "W";
                i = j;
            }
            else
            {
                shape += c;
                ++i;
            }
        }
        return shape;
    }

    struct CompileCounts
    {
        std::size_t mScripts = 0;
        std::size_t mScriptsOk = 0;
        std::size_t mFragments = 0;
        std::size_t mFragmentsOk = 0;
        std::map<std::string, std::size_t> mFirstErrors;
        std::vector<bool> mOk; ///< One entry per unit, in order.
        std::map<std::string, std::size_t> mShapes; // first error kind and the shape of its line
    };

    CompileCounts compileAll(const std::vector<const Unit*>& units, DataWorld& world, UnknownFunctionPolicy policy,
        const ScriptObscript::FunctionTable* table)
    {
        auto runtime = ScriptObscript::createRuntime(world, policy);
        if (table != nullptr)
        {
            std::vector<openeden::script::ErrorReport> problems;
            ScriptObscript::declareAll(*runtime, *table, 0.8, problems);
        }
        CompileCounts counts;
        for (const Unit* unit : units)
        {
            openeden::script::ScriptSource source;
            source.name = "script";
            source.text = unit->mSource;
            source.kind
                = unit->mIsScript ? openeden::script::ScriptKind::Script : openeden::script::ScriptKind::Fragment;
            const auto result = runtime->compile(source);
            counts.mOk.push_back(result.ok);
            (unit->mIsScript ? counts.mScripts : counts.mFragments) += 1;
            if (result.ok)
            {
                (unit->mIsScript ? counts.mScriptsOk : counts.mFragmentsOk) += 1;
                continue;
            }
            for (const auto& report : result.reports)
            {
                if (report.severity == openeden::script::Severity::Error)
                {
                    ++counts.mFirstErrors[messageKind(report.message)];
                    ++counts.mShapes[messageKind(report.message) + " | "
                        + (report.line == 0 ? std::string("(no line)") : lineShape(unit->mSource, report.line))];
                    break;
                }
            }
        }
        return counts;
    }

    // Units that compiled in \a before and did not in \a after.
    std::size_t lost(const CompileCounts& before, const CompileCounts& after)
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < before.mOk.size() && i < after.mOk.size(); ++i)
        {
            count += before.mOk[i] && !after.mOk[i] ? 1 : 0;
        }
        return count;
    }

    void print(const std::string& label, const CompileCounts& counts)
    {
        std::cout << "OBSCRIPT-TABLE " << label << ": scripts " << counts.mScriptsOk << " of " << counts.mScripts
                  << ", fragments " << counts.mFragmentsOk << " of " << counts.mFragments << "\n";
        std::vector<std::pair<std::size_t, std::string>> ranked;
        for (const auto& [kind, count] : counts.mFirstErrors)
        {
            ranked.emplace_back(count, kind);
        }
        std::sort(ranked.rbegin(), ranked.rend());
        for (std::size_t i = 0; i < ranked.size() && i < 6; ++i)
        {
            std::cout << "OBSCRIPT-TABLE   first error kind (" << ranked[i].second << ") " << ranked[i].first << "\n";
        }
        if (std::getenv("OPENEDEN_OBSCRIPT_SHAPES") != nullptr)
        {
            std::vector<std::pair<std::size_t, std::string>> shapes;
            for (const auto& [shape, count] : counts.mShapes)
            {
                shapes.emplace_back(count, shape);
            }
            std::sort(shapes.rbegin(), shapes.rend());
            for (std::size_t i = 0; i < shapes.size() && i < 25; ++i)
            {
                std::cout << "OBSCRIPT-TABLE   failing line shape (" << shapes[i].second << ") " << shapes[i].first
                          << "\n";
            }
        }
    }

    // Statement-level command opcodes (the ones the first measurement counted), by the walk of note 0046.
    std::set<unsigned> statementOpcodes(const std::vector<Unit>& units, bool scriptsOnly)
    {
        std::set<unsigned> opcodes;
        for (const Unit& unit : units)
        {
            if (scriptsOnly && !unit.mIsScript)
            {
                continue;
            }
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(unit.mCompiled.data());
            std::size_t at = 0;
            while (at + 4 <= unit.mCompiled.size())
            {
                const unsigned opcode = le16(bytes + at);
                const unsigned length = le16(bytes + at + 2);
                if (opcode >= 0x1000)
                {
                    opcodes.insert(opcode);
                }
                at += opcode == 0x1C ? 4 : 4 + length;
            }
        }
        return opcodes;
    }

    TEST(ObScriptTableData, aggregate_report_for_the_new_vegas_masters)
    {
        Masters masters;
        if (!openMasters(masters))
        {
            GTEST_SKIP() << "OPENEDEN_FNV_DATA does not name a Fallout New Vegas Data folder";
        }
        const std::vector<Unit> units = readUnits(masters);
        DataWorld world;
        std::set<std::string> variableNames;
        buildWorld(masters, world, variableNames);

        ScriptObscript::DeriveEnvironment environment;
        for (const auto& [name, id] : world.mForms)
        {
            environment.mForms.insert(name);
        }
        environment.mVariables = variableNames;

        std::vector<ScriptObscript::SourceUnit> views;
        for (const Unit& unit : units)
        {
            views.push_back({ unit.mSource, unit.mCompiled });
        }
        ScriptObscript::DeriveStats stats;
        const ScriptObscript::FunctionTable table = ScriptObscript::deriveFunctionTable(views, environment, &stats);
        const std::string text = ScriptObscript::serialiseTable(table);
        // deterministic: the same data in the reverse order gives the same bytes
        std::vector<ScriptObscript::SourceUnit> reversed(views.rbegin(), views.rend());
        EXPECT_EQ(ScriptObscript::serialiseTable(ScriptObscript::deriveFunctionTable(reversed, environment)), text);

        std::cout << "OBSCRIPT-TABLE units " << stats.mUnits << ", aligned " << stats.mAligned
                  << " (skipped: line count " << stats.mSkippedLineCount << ", kind " << stats.mSkippedKind
                  << ", expression " << stats.mSkippedExpression << ", malformed " << stats.mSkippedMalformed << ")\n";
        std::cout << "OBSCRIPT-TABLE pairs " << stats.mPairs
                  << ", lines whose argument count differs between text and bytes " << stats.mLinesWithOtherCount
                  << "\n";
        std::cout << "OBSCRIPT-TABLE entries " << table.mEntries.size() << ", names with more than one opcode "
                  << stats.mNamesConflicting << ", reliable shapes (>= 0.9, not '*') " << stats.mReliableShapes
                  << ", variable arity '*' " << stats.mVariableArity << "\n";
        std::cout << "OBSCRIPT-TABLE opcodes seen (statements and calls inside expressions) " << stats.mOpcodesSeen
                  << ", named " << stats.mOpcodesNamed << ", with several names " << stats.mOpcodesWithSeveralNames
                  << ", unnamed (fn_0x...) " << (stats.mOpcodesSeen - stats.mOpcodesNamed) << "\n";

        // the statement-level count of the first measurement
        for (const bool scriptsOnly : { true, false })
        {
            const std::set<unsigned> statements = statementOpcodes(units, scriptsOnly);
            std::size_t statementsNamed = 0;
            std::size_t statementsReliable = 0;
            for (const unsigned opcode : statements)
            {
                bool named = false;
                bool reliable = false;
                for (const auto& entry : table.mEntries)
                {
                    if (entry.mOpcode == opcode)
                    {
                        named = true;
                        reliable = reliable || (entry.mSignature != "*" && entry.shapeConfidence() >= 0.9);
                    }
                }
                statementsNamed += named ? 1 : 0;
                statementsReliable += reliable ? 1 : 0;
            }
            std::cout << "OBSCRIPT-TABLE statement opcodes in "
                      << (scriptsOnly ? "the 3,715 scripts" : "scripts and fragments") << ": " << statements.size()
                      << ": named " << statementsNamed << ", with a reliable shape " << statementsReliable << "\n";
        }

        std::size_t opcodeSure = 0;
        std::size_t opcodeUnsure = 0;
        std::size_t shapeSure = 0;
        std::size_t shapeUnsure = 0;
        std::size_t oneOccurrence = 0;
        for (const auto& entry : table.mEntries)
        {
            (entry.opcodeConfidence() >= 0.9 ? opcodeSure : opcodeUnsure) += 1;
            (entry.shapeConfidence() >= 0.9 ? shapeSure : shapeUnsure) += 1;
            oneOccurrence += entry.mOccurrences == 1 ? 1 : 0;
        }
        std::cout << "OBSCRIPT-TABLE confidence: opcode >= 0.9 for " << opcodeSure << ", below for " << opcodeUnsure
                  << "; shape >= 0.9 for " << shapeSure << ", below for " << shapeUnsure << "; seen only once "
                  << oneOccurrence << "\n";

        // the table survives its text
        const auto parsed = ScriptObscript::parseTable(text, "table");
        EXPECT_TRUE(parsed.ok);
        EXPECT_EQ(ScriptObscript::serialiseTable(parsed.table), text);
        if (const char* out = std::getenv("OPENEDEN_OBSCRIPT_TABLE_OUT"))
        {
            std::ofstream file(out, std::ios::binary);
            file << text;
            std::cout << "OBSCRIPT-TABLE table written (" << text.size() << " bytes)\n";
        }

        // compiling, in sample
        std::vector<const Unit*> all;
        for (const Unit& unit : units)
        {
            all.push_back(&unit);
        }
        const CompileCounts tolerantPlain = compileAll(all, world, UnknownFunctionPolicy::Tolerant, nullptr);
        const CompileCounts tolerantTable = compileAll(all, world, UnknownFunctionPolicy::Tolerant, &table);
        print("tolerant, no table", tolerantPlain);
        print("tolerant, with the table", tolerantTable);
        std::cout << "OBSCRIPT-TABLE units that compile in tolerant mode without the table and not with it: "
                  << lost(tolerantPlain, tolerantTable) << " (gained: " << lost(tolerantTable, tolerantPlain) << ")\n";
        EXPECT_EQ(lost(tolerantPlain, tolerantTable), 0u);
        print("strict, no table", compileAll(all, world, UnknownFunctionPolicy::Strict, nullptr));
        print("strict, with the table", compileAll(all, world, UnknownFunctionPolicy::Strict, &table));

        // out of sample: a table from the base game only, used on the add-on masters
        std::size_t baseFile = masters.mPaths.size();
        for (std::size_t i = 0; i < masters.mPaths.size(); ++i)
        {
            if (masters.mPaths[i].filename() == "FalloutNV.esm")
            {
                baseFile = i;
            }
        }
        ASSERT_LT(baseFile, masters.mPaths.size());
        std::vector<ScriptObscript::SourceUnit> baseViews;
        std::vector<const Unit*> addOns;
        for (const Unit& unit : units)
        {
            if (unit.mFile == baseFile)
            {
                baseViews.push_back({ unit.mSource, unit.mCompiled });
            }
            else
            {
                addOns.push_back(&unit);
            }
        }
        ScriptObscript::DeriveStats baseStats;
        const ScriptObscript::FunctionTable baseTable
            = ScriptObscript::deriveFunctionTable(baseViews, environment, &baseStats);
        std::cout << "OBSCRIPT-TABLE base game alone: " << baseViews.size() << " units, " << baseTable.mEntries.size()
                  << " entries; add-on masters: " << addOns.size() << " units\n";
        const CompileCounts addOnPlain = compileAll(addOns, world, UnknownFunctionPolicy::Tolerant, nullptr);
        const CompileCounts addOnTable = compileAll(addOns, world, UnknownFunctionPolicy::Tolerant, &baseTable);
        print("add-ons, tolerant, no table", addOnPlain);
        print("add-ons, tolerant, base table", addOnTable);
        std::cout << "OBSCRIPT-TABLE add-on units that compile in tolerant mode without the table and not with it: "
                  << lost(addOnPlain, addOnTable) << " (gained: " << lost(addOnTable, addOnPlain) << ")\n";
        EXPECT_EQ(lost(addOnPlain, addOnTable), 0u);
        print("add-ons, strict, no table", compileAll(addOns, world, UnknownFunctionPolicy::Strict, nullptr));
        print("add-ons, strict, base table", compileAll(addOns, world, UnknownFunctionPolicy::Strict, &baseTable));

        EXPECT_GT(stats.mAligned, 0u);
    }
}
