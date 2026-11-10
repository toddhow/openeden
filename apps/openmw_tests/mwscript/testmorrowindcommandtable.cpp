// Task 0098, move 4, stage 2: characterization of Morrowind's script command set.
//
// In plain words: the game's script commands (AddItem, SetStrength, PlaySound, ...) exist twice. The compile side is a
// table that tells the script compiler each command's name, its arguments and its opcode number; the run side is a set
// of handlers that the interpreter runs for each opcode number. This test writes both down as plain text, from the
// real code, and compares the text with a recording made before the move of the command set out of the engine core
// (apps/openmw_tests/mwscript/data/morrowind_script_table.golden.txt). If a command is lost, renamed, renumbered or
// left without a handler, the text differs and the failing line is named.
//
// The recording is data: it can only change in a pull request that says why. To make a new one, run the tests with
// OPENEDEN_WRITE_SCRIPT_TABLE=<path> set; the test then writes the current text there and fails on purpose.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/compiler/literals.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/opcodes.hpp>

#include "apps/openmw/mwscript/extensions.hpp"

namespace
{
    struct Nothing0 : Interpreter::Opcode0
    {
        void execute(Interpreter::Runtime&) override {}
    };

    struct Nothing1 : Interpreter::Opcode1
    {
        void execute(Interpreter::Runtime&, unsigned int) override {}
    };

    // Is a handler installed for this code word? Installing a second handler for the same code throws
    // std::invalid_argument ("Duplicated interpreter instruction"), so a throw means "there already was one". When
    // nothing throws, the probe's own handler is now there, which does not disturb the probes of other codes.
    bool isInstalled(Interpreter::Interpreter& interpreter, Interpreter::Type_Code word)
    {
        const std::uint32_t code = static_cast<std::uint32_t>(word);
        try
        {
            if ((code >> 26) == 0x30)
                interpreter.installSegment3<Nothing1>(static_cast<int>((code >> 8) & 0x3ffff));
            else if ((code >> 26) == 0x32)
                interpreter.installSegment5<Nothing0>(static_cast<int>(code & 0x3ffffff));
            else
                throw std::logic_error("segment not used by commands");
        }
        catch (const std::invalid_argument&)
        {
            return true;
        }
        return false;
    }

    std::string hex(std::uint32_t value)
    {
        std::ostringstream out;
        out << "0x" << std::hex << value;
        return out.str();
    }

    // One text block for the compile table of one registration (consoleOnly or not): a line for every keyword.
    void dumpCompileTable(std::ostream& out, bool consoleOnly, std::vector<Interpreter::Type_Code>& words)
    {
        Compiler::Extensions extensions;
        Compiler::registerExtensions(extensions, consoleOnly);
        std::vector<std::string> keywords;
        extensions.listKeywords(keywords);
        std::sort(keywords.begin(), keywords.end());
        for (const std::string& keyword : keywords)
        {
            const int index = extensions.searchKeyword(keyword);
            Compiler::ScriptReturn returnType;
            Compiler::ScriptArgs arguments;
            bool explicitReference = true;
            const bool function = extensions.isFunction(index, returnType, arguments, explicitReference);
            if (!function)
            {
                explicitReference = true;
                if (!extensions.isInstruction(index, arguments, explicitReference))
                {
                    out << "? " << keyword << '\n';
                    continue;
                }
            }
            Compiler::Literals literals;
            std::vector<Interpreter::Type_Code> plain;
            std::vector<Interpreter::Type_Code> viaReference;
            if (function)
            {
                extensions.generateFunctionCode(index, plain, literals, "", 0);
                if (explicitReference)
                    extensions.generateFunctionCode(index, viaReference, literals, "x", 0);
            }
            else
            {
                extensions.generateInstructionCode(index, plain, literals, "", 0);
                if (explicitReference)
                    extensions.generateInstructionCode(index, viaReference, literals, "x", 0);
            }
            out << (function ? "F " : "I ") << keyword << ' ' << (function ? std::string(1, returnType) : "-") << ' '
                << arguments << ' ' << (explicitReference ? "explicit" : "implicit-only") << ' '
                << hex(static_cast<std::uint32_t>(plain.back()));
            words.push_back(plain.back());
            if (explicitReference)
            {
                out << ' ' << hex(static_cast<std::uint32_t>(viaReference.back()));
                words.push_back(viaReference.back());
            }
            out << '\n';
        }
    }

    // The whole recording: both compile tables, then, for each installOpcodes variant, which of the table's code
    // words have a handler, and which code words in the ranges the commands use have one that the table does not name.
    std::string dumpEverything()
    {
        std::ostringstream out;
        out << "# Morrowind script command set, compile table and installed handlers (task 0098 move 4)\n";
        for (const bool consoleOnly : { false, true })
        {
            out << "[compile table consoleOnly=" << (consoleOnly ? "true" : "false") << "]\n";
            std::vector<Interpreter::Type_Code> words;
            dumpCompileTable(out, consoleOnly, words);

            out << "[handlers consoleOnly=" << (consoleOnly ? "true" : "false") << "]\n";
            Interpreter::Interpreter interpreter;
            MWScript::installOpcodes(interpreter, consoleOnly);
            std::vector<std::uint32_t> missing;
            std::sort(words.begin(), words.end());
            words.erase(std::unique(words.begin(), words.end()), words.end());
            for (const Interpreter::Type_Code word : words)
                if (!isInstalled(interpreter, word))
                    missing.push_back(static_cast<std::uint32_t>(word));
            out << "table code words: " << words.size() << '\n';
            out << "table code words without a handler: " << missing.size() << '\n';
            for (const std::uint32_t word : missing)
                out << "  " << hex(word) << '\n';

            // Handlers the table does not name. Probing installs a handler where there was none, so this goes
            // last, and only the codes it found installed are listed.
            std::vector<std::uint32_t> tableCodes;
            for (const Interpreter::Type_Code word : words)
                tableCodes.push_back(static_cast<std::uint32_t>(word));
            std::vector<std::uint32_t> extra;
            for (std::uint32_t segment : { 0x30u, 0x32u })
            {
                std::vector<std::uint32_t> ranges;
                for (std::uint32_t code = 0; code < 0x400; ++code)
                    ranges.push_back(code);
                for (std::uint32_t code = 0x20000; code < 0x20400; ++code)
                    ranges.push_back(code);
                if (segment == 0x32)
                    for (std::uint32_t code = 0x2000000; code < 0x2000400; ++code)
                        ranges.push_back(code);
                for (const std::uint32_t code : ranges)
                {
                    const std::uint32_t word = segment == 0x30 ? (0xc0000000u | (code << 8)) : (0xc8000000u | code);
                    if (std::find(tableCodes.begin(), tableCodes.end(), word) != tableCodes.end())
                        continue;
                    if (isInstalled(interpreter, static_cast<Interpreter::Type_Code>(word)))
                        extra.push_back(word);
                }
            }
            out << "handlers the table does not name: " << extra.size() << '\n';
            for (const std::uint32_t word : extra)
                out << "  " << hex(word) << '\n';
        }
        return out.str();
    }

    std::vector<std::string> lines(const std::string& text)
    {
        std::vector<std::string> result;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);)
            result.push_back(line);
        return result;
    }

    TEST(MorrowindScriptTable, compile_table_and_handlers_match_the_recording)
    {
        const std::string actual = dumpEverything();
        if (const char* path = std::getenv("OPENEDEN_WRITE_SCRIPT_TABLE"))
        {
            std::ofstream(path, std::ios::binary) << actual;
            FAIL() << "wrote the recording to " << path;
        }

        const std::string path = std::string(reinterpret_cast<const char*>(OPENMW_PROJECT_SOURCE_DIR)) + "/apps/openmw_tests/mwscript/data/"
            "morrowind_script_table.golden.txt";
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good()) << "cannot open " << path;
        const std::string expected((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        const std::vector<std::string> a = lines(actual);
        const std::vector<std::string> e = lines(expected);
        for (std::size_t i = 0; i < std::max(a.size(), e.size()); ++i)
        {
            const std::string got = i < a.size() ? a[i] : "<missing>";
            const std::string want = i < e.size() ? e[i] : "<missing>";
            ASSERT_EQ(got, want) << "first difference at line " << i + 1;
        }
    }

    TEST(MorrowindScriptTable, the_table_is_not_empty_and_has_console_only_extras)
    {
        Compiler::Extensions plain;
        Compiler::registerExtensions(plain, false);
        Compiler::Extensions console;
        Compiler::registerExtensions(console, true);
        std::vector<std::string> a;
        std::vector<std::string> b;
        plain.listKeywords(a);
        console.listKeywords(b);
        EXPECT_EQ(a.size(), 512u);
        EXPECT_EQ(b.size(), 516u);
        EXPECT_NE(plain.searchKeyword("additem"), 0);
        EXPECT_EQ(plain.searchKeyword("user1"), 0);
        EXPECT_NE(console.searchKeyword("user1"), 0);
    }
}
