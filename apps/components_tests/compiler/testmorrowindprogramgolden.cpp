// Task 0092 stage b: Morrowind's compiled script programs must be byte-identical before and after the parser-options seam
// in components/compiler and the budget seam in components/interpreter.
//
// In plain words: the script compiler turns the text of a Morrowind script into a Program (four lists of numbers and
// strings). Task 0092 adds options to the compiler so that another game's scripts can use it. The one thing that must
// not change is what it makes for Morrowind. This test compiles a fixed set of scripts, writes everything the compiler
// produces for each (the instruction numbers, the literals, the local variable names, the error and warning messages
// with their places) as one canonical line of text, and compares the lines with a file that was recorded from the
// compiler BEFORE the seam was written (morrowind_programs.golden.txt, next to this file; it is golden data and is
// never edited by hand or regenerated: a change needs toddhow's approval).
//
// The scripts are generated here, not copied from any game: for every command the standard command table knows, one
// call built from the command's own argument signature (with the optional arguments left out and with all of them,
// and with an explicit reference where the command allows one), plus hand-written scripts for the language itself
// (declarations, set, if/elseif/else, while, return, messagebox, globals and member variables, strings, expressions,
// fragments without Begin, and malformed input).
//
// Every script is wrapped in "Begin ... End" except the fragments, which go through ScriptParser as dialogue results do.

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/locals.hpp>
#include <components/compiler/scanner.hpp>
#include <components/compiler/scriptparser.hpp>
#include <components/compiler/tokenloc.hpp>
#include <components/esm/refid.hpp>
#include <components/interpreter/program.hpp>

namespace
{
    class GoldenContext : public Compiler::Context
    {
    public:
        bool canDeclareLocals() const override { return true; }

        char getGlobalType(const std::string& name) const override
        {
            if (name == "globalshort")
                return 's';
            if (name == "globallong")
                return 'l';
            if (name == "globalfloat")
                return 'f';
            return ' ';
        }

        std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& id) const override
        {
            if (name == "membershort")
                return { 's', false };
            if (name == "memberlong")
                return { 'l', true };
            if (name == "memberfloat")
                return { 'f', false };
            return { ' ', false };
        }

        bool isId(const ESM::RefId& name) const override
        {
            return name == ESM::RefId::stringRefId("player") || name == ESM::RefId::stringRefId("someone")
                || name == ESM::RefId::stringRefId("othercript");
        }
    };

    class RecordingErrorHandler : public Compiler::ErrorHandler
    {
        std::vector<std::string> mMessages;

        void report(const std::string& message, const Compiler::TokenLoc& loc, Type type) override
        {
            std::ostringstream out;
            out << (type == ErrorMessage ? 'E' : 'W') << loc.mLine << ':' << loc.mColumn << ':' << message;
            mMessages.push_back(out.str());
        }

        void report(const std::string& message, Type type) override
        {
            mMessages.push_back(std::string(type == ErrorMessage ? "E:file:" : "W:file:") + message);
        }

    public:
        const std::vector<std::string>& messages() const { return mMessages; }
    };

    std::string escape(const std::string& text)
    {
        std::string result;
        for (const unsigned char c : text)
        {
            if (c >= 0x20 && c < 0x7f && c != '\\' && c != '|' && c != ';')
            {
                result += static_cast<char>(c);
            }
            else
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\x%02x", c);
                result += buffer;
            }
        }
        return result;
    }

    // One canonical line: everything the compiler produced for one script.
    std::string canonical(const std::string& name, const RecordingErrorHandler& handler,
        const Interpreter::Program& program, const Compiler::Locals& locals, const std::string& outcome)
    {
        std::ostringstream out;
        out << name << '|' << outcome << "|w" << handler.countWarnings() << "|e" << handler.countErrors() << "|msg";
        for (const std::string& message : handler.messages())
            out << ';' << escape(message);
        out << "|code";
        for (const auto code : program.mInstructions)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), " %08x", static_cast<unsigned>(code));
            out << buffer;
        }
        out << "|int";
        for (const auto value : program.mIntegers)
            out << ' ' << value;
        out << "|flt";
        for (const auto value : program.mFloats)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), " %08x", std::bit_cast<std::uint32_t>(value));
            out << buffer;
        }
        out << "|str";
        for (const std::string& value : program.mStrings)
            out << ';' << escape(value);
        out << "|loc";
        for (const char type : { 's', 'l', 'f' })
        {
            out << ' ' << type << ':';
            for (const std::string& variable : locals.get(type))
                out << escape(variable) << ',';
        }
        return out.str();
    }

    struct Compiled
    {
        std::string mLine;
    };

    // Compile a whole script (Begin ... End) the way ScriptManager::compile does.
    std::string compileFile(const std::string& name, const std::string& source, Compiler::Extensions& extensions)
    {
        RecordingErrorHandler handler;
        GoldenContext context;
        context.setExtensions(&extensions);
        Compiler::FileParser parser(handler, context);
        std::string outcome = "ok";
        try
        {
            std::istringstream input(source);
            Compiler::Scanner scanner(handler, input, context.getExtensions());
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
            outcome = "source-exception";
        }
        catch (const std::exception& e)
        {
            outcome = std::string("exception:") + e.what();
        }
        if (!handler.isGood() && outcome == "ok")
            outcome = "errors";
        return canonical(name, handler, parser.getProgram(), parser.getLocals(), outcome);
    }

    // Compile a fragment with no Begin, as DialogueManager::compile does.
    std::string compileFragment(const std::string& name, const std::string& source, Compiler::Extensions& extensions)
    {
        RecordingErrorHandler handler;
        GoldenContext context;
        context.setExtensions(&extensions);
        Compiler::Locals locals;
        Compiler::ScriptParser parser(handler, context, locals, false);
        std::string outcome = "ok";
        try
        {
            std::istringstream input(source);
            Compiler::Scanner scanner(handler, input, context.getExtensions());
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
            outcome = "source-exception";
        }
        catch (const std::exception& e)
        {
            outcome = std::string("exception:") + e.what();
        }
        if (!handler.isGood() && outcome == "ok")
            outcome = "errors";
        return canonical(name, handler, parser.getProgram(), locals, outcome);
    }

    // The text of one call of a command, built from its argument signature. \a withOptionals picks the arguments after
    // the slash. Characters that need a special argument ('j') are left out.
    std::string argumentsFor(const std::string& signature, bool withOptionals)
    {
        std::string text;
        bool optional = false;
        for (const char c : signature)
        {
            if (c == '/')
            {
                optional = true;
                continue;
            }
            if (optional && !withOptionals)
                break;
            switch (c)
            {
                case 'f':
                    text += " 1.5";
                    break;
                case 'c':
                    text += " \"abc\"";
                    break;
                case 'l':
                    text += " 3";
                    break;
                case 's':
                    text += " 2";
                    break;
                case 'S':
                    text += " \"Abc\"";
                    break;
                case 'x':
                    text += " \"x\"";
                    break;
                case 'X':
                    text += " 4";
                    break;
                case 'z':
                    text += " 5";
                    break;
                default:
                    break;
            }
        }
        return text;
    }

    std::string wrap(const std::string& body)
    {
        return "Begin Golden\n" + body + "\nEnd Golden\n";
    }

    std::vector<std::pair<std::string, std::string>> handWrittenScripts()
    {
        return {
            { "empty", "" },
            { "emptybody", wrap("") },
            { "locals", wrap("short a\nlong b\nfloat c\nSet a to 1\nSet b to a + 2\nSet c to b * 1.5") },
            { "redeclare", wrap("short a\nshort a\nlong a") },
            { "arith", wrap("short a\nfloat f\nSet a to ( 1 + 2 ) * 3 - 4 / 2\nSet f to a / 3.0 + 0.5") },
            { "compare", wrap("short a\nif ( a == 1 )\n Set a to 2\nelseif ( a != 3 )\n Set a to 4\nelse\n Set a to 5\nendif") },
            { "comparisons", wrap("short a\nif ( a < 1 || a > 2 )\nendif") },
            { "comparisons2", wrap("short a\nif ( a <= 1 )\nendif\nif ( a >= 1 )\nendif") },
            { "nestedif", wrap("short a\nif a == 1\n if a == 2\n  Set a to 3\n else\n  Set a to 4\n endif\nendif") },
            { "while", wrap("short a\nwhile ( a < 10 )\n Set a to a + 1\nendwhile") },
            { "whilereturn", wrap("short a\nwhile ( a < 10 )\n if ( a == 5 )\n  return\n endif\n Set a to a + 1\nendwhile") },
            { "return", wrap("return") },
            { "globals", wrap("Set globalshort to 1\nSet globallong to globalshort + 2\nSet globalfloat to 2.5") },
            { "members", wrap("Set someone.membershort to 1\nSet someone.memberlong to someone.memberlong + 1") },
            { "membersunknown", wrap("Set someone.nothing to 1") },
            { "unknownset", wrap("Set nothing to 1") },
            { "unknownname", wrap("nothing 1 2") },
            { "messagebox", wrap("short a\nMessageBox \"Hello %g\" a") },
            { "messageboxbuttons", wrap("MessageBox \"Pick\" \"One\" \"Two\" \"Three\"") },
            { "messageboxfloat", wrap("float f\nMessageBox \"x %.2f\" f") },
            { "comment", wrap("; a comment\nshort a ; trailing\nSet a to 1 ; more") },
            { "case", wrap("SHORT A\nSET a TO 1\nIf ( A == 1 )\nEndIf") },
            { "stringargs", wrap("MessageBox \"a\\\"b\"") },
            { "missingend", "Begin Golden\nshort a\nSet a to 1\n" },
            { "missingbegin", "short a\nSet a to 1\nEnd\n" },
            { "endifwithoutif", wrap("endif") },
            { "elsewithoutif", wrap("else") },
            { "unclosedif", wrap("if ( 1 )\n Set globalshort to 1") },
            { "unclosedparen", wrap("Set globalshort to ( 1 + 2") },
            { "typemix", wrap("short a\nSet a to \"text\"") },
            { "stray", wrap("1 2 3") },
            { "namedifferent", "Begin A\nreturn\nEnd B\n" },
            { "keywordname", "Begin Set\nreturn\nEnd\n" },
            { "junkafterend", "Begin A\nreturn\nEnd\nshort junk\nSet junk to 1" },
            { "longline", wrap("Set globallong to 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1") },
            { "neglit", wrap("Set globalshort to -5\nSet globalfloat to -1.25") },
            { "explicitmember", wrap("Set player.membershort to 3") },
            { "highdigits", wrap("Set globallong to 2147483647") },
            { "nonascii", wrap("MessageBox \"caf\xc3\xa9\"") },
            { "tabsandcommas", wrap("\tshort\ta,\nSet\ta\tto\t1") },
        };
    }

    std::vector<std::pair<std::string, std::string>> fragmentScripts()
    {
        return {
            { "frag_set", "Set globalshort to 1\n" },
            { "frag_if", "if ( globalshort == 1 )\n Set globalshort to 2\nendif\n" },
            { "frag_messagebox", "MessageBox \"hi\"\n" },
            { "frag_empty", "" },
            { "frag_unknown", "unknowncommand\n" },
            { "frag_return", "return\n" },
            { "frag_endextra", "Set globalshort to 1\nEnd\nSet globalshort to 2\n" },
        };
    }

    std::string buildDump()
    {
        Compiler::Extensions extensions;
        Compiler::registerExtensions(extensions, false);

        std::vector<std::string> keywords;
        extensions.listKeywords(keywords);
        std::sort(keywords.begin(), keywords.end());

        std::ostringstream dump;
        dump << "# keywords " << keywords.size() << '\n';

        for (const auto& [name, source] : handWrittenScripts())
            dump << compileFile(name, source, extensions) << '\n';
        for (const auto& [name, source] : fragmentScripts())
            dump << compileFragment(name, source, extensions) << '\n';

        for (const std::string& keyword : keywords)
        {
            const int index = extensions.searchKeyword(keyword);
            std::string arguments;
            char returnType = ' ';
            bool explicitOk = true;
            bool isFunction = extensions.isFunction(index, returnType, arguments, explicitOk);
            bool isInstruction = false;
            if (!isFunction)
            {
                explicitOk = true;
                isInstruction = extensions.isInstruction(index, arguments, explicitOk);
            }
            const bool hasOptionals = arguments.find('/') != std::string::npos;

            dump << "# " << keyword << ' ' << (isFunction ? 'F' : isInstruction ? 'I' : '?') << ' ' << returnType
                 << " sig=" << escape(arguments) << " explicit=" << (explicitOk ? 1 : 0) << '\n';

            std::vector<std::pair<std::string, bool>> variants = { { "", false } };
            if (hasOptionals)
                variants.emplace_back("all", true);

            for (const auto& [suffix, all] : variants)
            {
                const std::string call = keyword + argumentsFor(arguments, all);
                std::string implicitBody;
                std::string explicitBody;
                if (isFunction)
                {
                    implicitBody = "Set globallong to " + call;
                    explicitBody = "Set globallong to player->" + call;
                }
                else
                {
                    implicitBody = call;
                    explicitBody = "player->" + call;
                }
                dump << compileFile("cmd:" + keyword + ":implicit" + suffix, wrap(implicitBody), extensions) << '\n';
                if (explicitOk)
                    dump << compileFile("cmd:" + keyword + ":explicit" + suffix, wrap(explicitBody), extensions)
                         << '\n';
                if (isFunction)
                    dump << compileFile(
                        "cmd:" + keyword + ":cond" + suffix, wrap("if ( " + call + " == 1 )\nendif"), extensions)
                         << '\n';
            }
        }
        return dump.str();
    }

    std::string readGolden()
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / "morrowind_programs.golden.txt";
        std::ifstream in(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }

    std::vector<std::string> lines(const std::string& text)
    {
        std::vector<std::string> result;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
            result.push_back(line);
        return result;
    }

    TEST(CompilerMorrowindGolden, programs_are_byte_identical_to_the_recording_before_the_seam)
    {
        const std::string actual = buildDump();
        const std::string golden = readGolden();
        if (actual != golden)
        {
            // A diagnostic aid, not a way to regenerate the golden file: the recording was made once, from the
            // compiler before the seam.
            std::ofstream out(std::filesystem::temp_directory_path() / "morrowind_programs.actual.txt",
                std::ios::binary | std::ios::trunc);
            out << actual;
        }
        ASSERT_FALSE(golden.empty()) << "morrowind_programs.golden.txt is missing or empty";

        const std::vector<std::string> actualLines = lines(actual);
        const std::vector<std::string> goldenLines = lines(golden);

        // Report the first few differing scripts by name, not the whole dump.
        std::size_t differing = 0;
        std::string report;
        const std::size_t common = std::min(actualLines.size(), goldenLines.size());
        for (std::size_t i = 0; i < common; ++i)
        {
            if (actualLines[i] != goldenLines[i])
            {
                ++differing;
                if (differing <= 5)
                {
                    report += "line " + std::to_string(i + 1) + "\n  golden: " + goldenLines[i].substr(0, 400)
                        + "\n  actual: " + actualLines[i].substr(0, 400) + "\n";
                }
            }
        }
        EXPECT_EQ(actualLines.size(), goldenLines.size());
        EXPECT_EQ(differing, 0u) << report;
    }

    TEST(CompilerMorrowindGolden, the_dump_covers_the_whole_command_table)
    {
        Compiler::Extensions extensions;
        Compiler::registerExtensions(extensions, false);
        std::vector<std::string> keywords;
        extensions.listKeywords(keywords);
        EXPECT_GT(keywords.size(), 500u);
        const std::string actual = buildDump();
        for (const std::string& keyword : keywords)
            EXPECT_NE(actual.find("# " + keyword + " "), std::string::npos) << keyword;
    }

    TEST(CompilerMorrowindGolden, the_dump_is_deterministic)
    {
        EXPECT_EQ(buildDump(), buildDump());
    }
}
