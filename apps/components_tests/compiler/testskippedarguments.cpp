// Task 0092 item 2: skipped arguments without warnings, as a parser option.
//
// In plain words: a command whose arguments are not known (a command declared from a table without a shape) is
// compiled so that its arguments are read and thrown away (the 'z' argument kind). Morrowind's compiler warns about
// each thrown away word ("Extra argument"); that stays the default. The ObScript runtime sets
// ParserOptions::mQuietSkippedArguments so that skipping on purpose is silent.

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/scanner.hpp>
#include <components/compiler/tokenloc.hpp>
#include <components/esm/refid.hpp>

namespace
{
    class SkipContext : public Compiler::Context
    {
    public:
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string&, const ESM::RefId&) const override
        {
            return { ' ', false };
        }
        bool isId(const ESM::RefId&) const override { return false; }
    };

    class CountingHandler : public Compiler::ErrorHandler
    {
    public:
        std::vector<std::string> mMessages;

    private:
        void report(const std::string& message, const Compiler::TokenLoc&, Type) override
        {
            mMessages.push_back(message);
        }
        void report(const std::string& message, Type) override { mMessages.push_back(message); }
    };

    std::size_t extraArgumentWarnings(bool quiet)
    {
        CountingHandler handler;
        SkipContext context;
        Compiler::Extensions extensions;
        extensions.registerFunction("skipper", 'l', "/zzzzzzzzzzzz", 0x20000, -1);
        context.setExtensions(&extensions);
        Compiler::ParserOptions options;
        options.mQuietSkippedArguments = quiet;
        context.setOptions(options);
        Compiler::FileParser parser(handler, context);
        try
        {
            std::istringstream input("Begin T\nskipper 1 2 3\nEnd\n");
            Compiler::Scanner scanner(handler, input, &extensions);
            scanner.scan(parser);
        }
        catch (const Compiler::SourceException&)
        {
            return 9999;
        }
        std::size_t count = 0;
        for (const std::string& message : handler.mMessages)
        {
            count += message == "Extra argument" ? 1 : 0;
        }
        return count;
    }

    TEST(CompilerSkippedArguments, the_option_is_off_by_default)
    {
        const Compiler::ParserOptions options;
        EXPECT_FALSE(options.mQuietSkippedArguments);
    }

    TEST(CompilerSkippedArguments, skipped_arguments_are_warned_about_by_default)
    {
        EXPECT_EQ(extraArgumentWarnings(false), 3u);
    }

    TEST(CompilerSkippedArguments, with_the_option_on_skipping_is_silent)
    {
        EXPECT_EQ(extraArgumentWarnings(true), 0u);
    }
}
