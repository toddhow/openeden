#include "obscriptcompile.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/scanner.hpp>
#include <components/compiler/scriptparser.hpp>
#include <components/compiler/tokenloc.hpp>
#include <components/esm/refid.hpp>
#include <components/misc/strings/lower.hpp>

#include "obscriptlimits.hpp"

namespace ScriptObscript
{
    using openeden::script::ErrorReport;
    using openeden::script::ScriptKind;
    using openeden::script::Severity;
    using openeden::script::UnknownFunctionPolicy;
    using openeden::script::ValueKind;

    namespace
    {
        // At most this many reports are kept for one script, so that a hostile script cannot make a million of them.
        constexpr std::size_t kMaxReports = 100;

        const std::unordered_set<std::string_view> kLanguageWords
            = { "set", "to", "if", "elseif", "else", "endif", "while", "endwhile", "return", "messagebox", "begin",
                  "end", "short", "long", "float", "int", "ref", "scriptname", "scn" };

        std::string lower(std::string_view text)
        {
            std::string result(text);
            Misc::StringUtils::lowerCaseInPlace(result);
            return result;
        }

        // ---- the first pass: lines and tokens -----------------------------------------------------------------

        struct Token
        {
            enum class Kind
            {
                Word,
                Number,
                Text,
                Punct,
            };

            Kind mKind = Kind::Punct;
            std::string mText;
            std::uint32_t mColumn = 0; // 1-based
        };

        struct Line
        {
            std::uint32_t mNumber = 0; // 1-based
            std::string_view mRaw;
            std::vector<Token> mTokens;
        };

        bool isWordCharacter(unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
        }

        bool isDigit(unsigned char c)
        {
            return c >= '0' && c <= '9';
        }

        // Collects reports for one script and applies the policy for warnings.
        class Reporter
        {
        public:
            Reporter(std::string script, bool strict)
                : mScript(std::move(script))
                , mStrict(strict)
            {
            }

            void error(std::uint32_t line, std::uint32_t column, std::string message)
            {
                add(Severity::Error, line, column, std::move(message));
            }

            // A warning is an error in strict mode.
            void warning(std::uint32_t line, std::uint32_t column, std::string message)
            {
                add(mStrict ? Severity::Error : Severity::Warning, line, column, std::move(message));
            }

            bool hasErrors() const { return mErrors > 0; }
            std::vector<ErrorReport> take() { return std::move(mReports); }

        private:
            void add(Severity severity, std::uint32_t line, std::uint32_t column, std::string message)
            {
                if (severity == Severity::Error)
                {
                    ++mErrors;
                }
                if (mReports.size() >= kMaxReports)
                {
                    return;
                }
                ErrorReport report;
                report.severity = severity;
                report.script = mScript;
                report.line = line;
                report.column = column;
                report.message = std::move(message);
                mReports.push_back(std::move(report));
            }

            std::string mScript;
            bool mStrict;
            std::vector<ErrorReport> mReports;
            std::size_t mErrors = 0;
        };

        // Split \a text into lines and tokens. Applies the identifier length and bracket nesting caps.
        std::vector<Line> lex(std::string_view text, Reporter& reporter)
        {
            std::vector<Line> lines;
            std::size_t start = 0;
            std::uint32_t number = 0;
            bool nestingReported = false;
            bool identifierReported = false;
            while (start <= text.size())
            {
                std::size_t end = text.find('\n', start);
                if (end == std::string_view::npos)
                {
                    end = text.size();
                }
                std::string_view raw = text.substr(start, end - start);
                if (!raw.empty() && raw.back() == '\r')
                {
                    raw.remove_suffix(1);
                }
                ++number;

                Line line;
                line.mNumber = number;
                line.mRaw = raw;

                std::size_t depth = 0;
                std::size_t i = 0;
                while (i < raw.size())
                {
                    const unsigned char c = static_cast<unsigned char>(raw[i]);
                    if (c == ';')
                    {
                        break; // a comment to the end of the line
                    }
                    if (c == ' ' || c == '\t' || c == ',')
                    {
                        ++i;
                        continue;
                    }

                    Token token;
                    token.mColumn = static_cast<std::uint32_t>(i + 1);
                    if (c == '"')
                    {
                        std::size_t close = raw.find('"', i + 1);
                        if (close == std::string_view::npos)
                        {
                            close = raw.size() - 1;
                        }
                        token.mKind = Token::Kind::Text;
                        token.mText = std::string(raw.substr(i, close - i + 1));
                        i = close + 1;
                    }
                    else if (isDigit(c))
                    {
                        std::size_t j = i;
                        while (j < raw.size() && isDigit(static_cast<unsigned char>(raw[j])))
                        {
                            ++j;
                        }
                        if (j + 1 < raw.size() && raw[j] == '.' && isDigit(static_cast<unsigned char>(raw[j + 1])))
                        {
                            ++j;
                            while (j < raw.size() && isDigit(static_cast<unsigned char>(raw[j])))
                            {
                                ++j;
                            }
                        }
                        // A word that starts with digits (3rd) is still a word.
                        if (j < raw.size() && isWordCharacter(static_cast<unsigned char>(raw[j])))
                        {
                            while (j < raw.size() && isWordCharacter(static_cast<unsigned char>(raw[j])))
                            {
                                ++j;
                            }
                            token.mKind = Token::Kind::Word;
                        }
                        else
                        {
                            token.mKind = Token::Kind::Number;
                        }
                        token.mText = std::string(raw.substr(i, j - i));
                        i = j;
                    }
                    else if (isWordCharacter(c))
                    {
                        std::size_t j = i;
                        while (j < raw.size() && isWordCharacter(static_cast<unsigned char>(raw[j])))
                        {
                            ++j;
                        }
                        token.mKind = Token::Kind::Word;
                        token.mText = std::string(raw.substr(i, j - i));
                        i = j;
                    }
                    else
                    {
                        token.mKind = Token::Kind::Punct;
                        std::size_t length = 1;
                        if (i + 1 < raw.size())
                        {
                            const std::string_view two = raw.substr(i, 2);
                            if (two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "&&" || two == "||"
                                || two == "->")
                            {
                                length = 2;
                            }
                        }
                        token.mText = std::string(raw.substr(i, length));
                        i += length;
                    }

                    if (token.mKind == Token::Kind::Word && token.mText.size() > Limits::kMaxIdentifierLength
                        && !identifierReported)
                    {
                        identifierReported = true;
                        reporter.error(number, token.mColumn,
                            std::format("identifier is too long ({} characters; the limit is {})", token.mText.size(),
                                Limits::kMaxIdentifierLength));
                    }
                    if (token.mKind == Token::Kind::Punct && token.mText == "(")
                    {
                        ++depth;
                        if (depth > Limits::kMaxNesting && !nestingReported)
                        {
                            nestingReported = true;
                            reporter.error(number, token.mColumn,
                                std::format("bracket nesting is too deep (the limit is {})", Limits::kMaxNesting));
                        }
                    }
                    else if (token.mKind == Token::Kind::Punct && token.mText == ")" && depth > 0)
                    {
                        --depth;
                    }
                    line.mTokens.push_back(std::move(token));
                }
                lines.push_back(std::move(line));
                start = end + 1;
            }
            return lines;
        }

        bool isWord(const Token& token, std::string_view lowerText)
        {
            return token.mKind == Token::Kind::Word && lower(token.mText) == lowerText;
        }

        // ---- structure of a script ----------------------------------------------------------------------------

        struct BlockSpan
        {
            std::string mEvent; // lower case
            std::uint32_t mBeginLine = 0;
            std::size_t mFirstBody = 0; // index into the lines
            std::size_t mEndLine = 0; // index of the End line
        };

        struct Structure
        {
            std::string mName;
            std::vector<BlockSpan> mBlocks;
        };

        char localTypeFor(const std::string& word)
        {
            if (word == "short")
            {
                return 's';
            }
            if (word == "long" || word == "int" || word == "ref")
            {
                return 'l';
            }
            if (word == "float")
            {
                return 'f';
            }
            return ' ';
        }

        // One declaration line: "short name", "long name", "int name", "float name" or "ref name".
        void declareLocal(const Line& line, const std::string& word, CompiledScript& script, Reporter& reporter,
            bool& tooManyReported)
        {
            const char type = localTypeFor(word);
            const Token& first = line.mTokens[0];
            if (line.mTokens.size() < 2 || line.mTokens[1].mKind != Token::Kind::Word)
            {
                reporter.error(line.mNumber, first.mColumn, std::format("expected a variable name after '{}'", word));
                return;
            }
            const Token& name = line.mTokens[1];
            const std::string lowerName = lower(name.mText);
            if (script.mLocals.getType(lowerName) != ' ')
            {
                reporter.warning(line.mNumber, name.mColumn, "Local variable re-declaration");
                return;
            }
            const Compiler::Locals& declared = script.mLocals;
            const std::size_t count = declared.get('s').size() + declared.get('l').size() + declared.get('f').size();
            if (count >= Limits::kMaxLocals)
            {
                if (!tooManyReported)
                {
                    tooManyReported = true;
                    reporter.error(line.mNumber, name.mColumn,
                        std::format("the script has too many variables (the limit is {})", Limits::kMaxLocals));
                }
                return;
            }
            script.mLocals.declare(type, lowerName);
            if (word == "ref")
            {
                script.mRefLocals.insert(lowerName);
                script.mFormLocals.insert(lowerName);
            }
        }

        // Reads the ScriptName line, the declarations and the Begin ... End blocks of a whole script.
        void parseStructure(
            const std::vector<Line>& lines, CompiledScript& script, Structure& structure, Reporter& reporter)
        {
            std::size_t index = 0;
            while (index < lines.size() && lines[index].mTokens.empty())
            {
                ++index;
            }
            if (index == lines.size())
            {
                reporter.error(0, 0, "the script is empty");
                return;
            }

            const Line& header = lines[index];
            if (!(isWord(header.mTokens[0], "scriptname") || isWord(header.mTokens[0], "scn"))
                || header.mTokens.size() < 2 || header.mTokens[1].mKind != Token::Kind::Word)
            {
                reporter.error(header.mNumber, header.mTokens[0].mColumn,
                    "a script must start with a ScriptName line (ScriptName <name>)");
                return;
            }
            structure.mName = header.mTokens[1].mText;
            ++index;

            bool tooManyReported = false;
            for (; index < lines.size(); ++index)
            {
                const Line& line = lines[index];
                if (line.mTokens.empty())
                {
                    continue;
                }
                const Token& first = line.mTokens[0];
                const std::string word = first.mKind == Token::Kind::Word ? lower(first.mText) : std::string();

                if (localTypeFor(word) != ' ')
                {
                    declareLocal(line, word, script, reporter, tooManyReported);
                    continue;
                }

                if (word == "begin")
                {
                    if (line.mTokens.size() < 2 || line.mTokens[1].mKind != Token::Kind::Word)
                    {
                        reporter.error(line.mNumber, first.mColumn, "Begin needs an event name (Begin <event>)");
                        continue;
                    }
                    BlockSpan block;
                    block.mEvent = lower(line.mTokens[1].mText);
                    block.mBeginLine = line.mNumber;
                    block.mFirstBody = index + 1;
                    std::size_t end = index + 1;
                    while (end < lines.size() && !(!lines[end].mTokens.empty() && isWord(lines[end].mTokens[0], "end")))
                    {
                        ++end;
                    }
                    if (end == lines.size())
                    {
                        reporter.error(line.mNumber, first.mColumn,
                            std::format("unexpected end of file: the block begun on line {} has no End", line.mNumber));
                        return;
                    }
                    block.mEndLine = end;
                    if (structure.mBlocks.size() >= Limits::kMaxBlocks)
                    {
                        reporter.error(line.mNumber, first.mColumn,
                            std::format("the script has too many blocks (the limit is {})", Limits::kMaxBlocks));
                        return;
                    }
                    structure.mBlocks.push_back(std::move(block));
                    index = end;
                    continue;
                }

                reporter.error(line.mNumber, first.mColumn, "unexpected text outside a Begin ... End block");
            }
        }

        // ---- unknown names ------------------------------------------------------------------------------------

        struct Candidate
        {
            std::string mLower;
            std::string mSpelling;
            std::uint32_t mLine = 0;
            std::uint32_t mColumn = 0;
        };

        bool isOperatorToken(const Token& token)
        {
            if (token.mKind != Token::Kind::Punct)
            {
                return false;
            }
            static const std::unordered_set<std::string_view> operators
                = { "(", "+", "-", "*", "/", "==", "!=", "<", "<=", ">", ">=", "&&", "||" };
            return operators.contains(token.mText);
        }

        // Names that stand where a function call can stand and are not known: the first word of a statement, the word
        // after a dot, and a word after if, elseif, while, to, a bracket or an operator. Arguments of a call are not
        // such places, so a form name written as an argument is never taken for a function.
        void findCandidates(const Line& line, const CompileServices& services, const CompiledScript& script,
            std::vector<Candidate>& candidates)
        {
            const auto isKnown = [&](const std::string& name) {
                if (kLanguageWords.contains(name) || script.mLocals.getType(name) != ' '
                    || services.mFind(name) != nullptr)
                {
                    return true;
                }
                if (services.mWorld->globalKind(name) != ValueKind::String)
                {
                    return true;
                }
                openeden::script::FormId form = 0;
                return services.mWorld->resolveForm(name, form);
            };

            const std::vector<Token>& tokens = line.mTokens;
            for (std::size_t i = 0; i < tokens.size(); ++i)
            {
                const Token& token = tokens[i];
                if (token.mKind != Token::Kind::Word)
                {
                    continue;
                }
                const std::string name = lower(token.mText);
                bool position = false;
                bool afterDot = false;
                if (i == 0)
                {
                    position = !(tokens.size() > 1 && tokens[1].mKind == Token::Kind::Punct && tokens[1].mText == ".");
                }
                else
                {
                    const Token& previous = tokens[i - 1];
                    if (previous.mKind == Token::Kind::Punct && previous.mText == "." && i >= 2
                        && tokens[i - 2].mKind == Token::Kind::Word)
                    {
                        position = true;
                        afterDot = true;
                    }
                    else if (previous.mKind == Token::Kind::Word)
                    {
                        const std::string before = lower(previous.mText);
                        position = before == "if" || before == "elseif" || before == "while" || before == "to";
                    }
                    else
                    {
                        position = isOperatorToken(previous);
                    }
                }
                if (!position || isKnown(name))
                {
                    continue;
                }
                if (afterDot)
                {
                    // A variable of a quest or object, not a command.
                    openeden::script::FormId owner = 0;
                    openeden::script::ValueKind kind = ValueKind::Integer;
                    const std::string ownerName = lower(tokens[i - 2].mText);
                    if (services.mWorld->resolveForm(ownerName, owner)
                        && services.mWorld->findVariable(owner, name, kind))
                    {
                        continue;
                    }
                }
                Candidate candidate;
                candidate.mLower = name;
                candidate.mSpelling = token.mText;
                candidate.mLine = line.mNumber;
                candidate.mColumn = token.mColumn;
                candidates.push_back(std::move(candidate));
            }
        }

        // ---- the shared compiler ------------------------------------------------------------------------------

        class ScriptContext final : public Compiler::Context
        {
        public:
            ScriptContext(const CompileServices& services, const CompiledScript& script)
                : mServices(services)
                , mScript(script)
            {
            }

            bool canDeclareLocals() const override { return false; }

            char getGlobalType(const std::string& name) const override
            {
                switch (mServices.mWorld->globalKind(name))
                {
                    case ValueKind::Integer:
                    case ValueKind::Form:
                        return 'l';
                    case ValueKind::Float:
                        return 'f';
                    case ValueKind::String:
                        break;
                }
                return ' ';
            }

            std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& id) const override
            {
                const std::string owner = lower(id.getRefIdString());
                openeden::script::FormId form = 0;
                openeden::script::ValueKind kind = ValueKind::Integer;
                if (mScript.mLocals.getType(owner) == ' ' && mServices.mWorld->resolveForm(owner, form)
                    && mServices.mWorld->findVariable(form, name, kind))
                {
                    return { kind == ValueKind::Float ? 'f' : 'l', false };
                }
                return { ' ', false };
            }

            bool isId(const ESM::RefId& name) const override
            {
                const std::string text = lower(name.getRefIdString());
                if (mScript.mLocals.getType(text) != ' ')
                {
                    return mScript.mRefLocals.contains(text);
                }
                openeden::script::FormId form = 0;
                return mServices.mWorld->resolveForm(text, form);
            }

        private:
            const CompileServices& mServices;
            const CompiledScript& mScript;
        };

        // Collects the compiler's messages as reports with 1-based positions.
        class CollectingHandler final : public Compiler::ErrorHandler
        {
        public:
            CollectingHandler(Reporter& reporter, bool tolerant)
                : mReporter(reporter)
                , mTolerant(tolerant)
            {
            }

            void setLineOffset(std::uint32_t offset) { mLineOffset = offset; }

        private:
            void report(const std::string& message, const Compiler::TokenLoc& loc, Type type) override
            {
                if (mTolerant && type == WarningMessage && message == "Extra argument")
                {
                    return; // the arguments of an unknown function are skipped on purpose
                }
                const std::uint32_t line = static_cast<std::uint32_t>(loc.mLine) + 1 + mLineOffset;
                const std::uint32_t column = static_cast<std::uint32_t>(loc.mColumn) + 1;
                if (type == ErrorMessage)
                {
                    mReporter.error(line, column, message);
                }
                else
                {
                    mReporter.warning(line, column, message);
                }
            }

            void report(const std::string& message, Type type) override
            {
                if (type == ErrorMessage)
                {
                    mReporter.error(0, 0, message);
                }
                else
                {
                    mReporter.warning(0, 0, message);
                }
            }

            Reporter& mReporter;
            bool mTolerant;
            std::uint32_t mLineOffset = 0;
        };

        // Compile one piece of text (a block body plus its End, or a whole fragment) with the shared parser.
        void compileText(const std::string& text, bool withEnd, std::uint32_t lineOffset,
            const Compiler::Extensions& extensions, const CompileServices& services, CompiledScript& script,
            Reporter& reporter, CompiledBlock& block, bool& failed)
        {
            CollectingHandler handler(reporter, services.mPolicy == UnknownFunctionPolicy::Tolerant);
            handler.setLineOffset(lineOffset);
            if (services.mPolicy == UnknownFunctionPolicy::Strict)
            {
                handler.setWarningsMode(2); // warnings are errors
            }

            ScriptContext context(services, script);
            context.setExtensions(&extensions);
            Compiler::ParserOptions options;
            options.mDotExplicitReference = true;
            options.mReferenceLocals = true;
            options.mLogicalOperators = true;
            context.setOptions(options);

            try
            {
                Compiler::ScriptParser parser(handler, context, script.mLocals, withEnd);
                // No parser.reset(): it would clear the locals declared above, and each block has a parser of its own.
                std::istringstream input(text);
                Compiler::Scanner scanner(handler, input, &extensions);
                scanner.setLogicalOperators(options.mLogicalOperators);
                scanner.scan(parser);
                block.mProgram = parser.getProgram();
            }
            catch (const Compiler::SourceException&)
            {
                failed = true; // the compiler has reported it
            }
            catch (const std::exception& e)
            {
                failed = true;
                reporter.error(lineOffset + 1, 0, std::format("the expression could not be compiled ({})", e.what()));
            }
            if (!handler.isGood())
            {
                failed = true;
            }
        }
    }

    bool isValidFunctionName(std::string_view name)
    {
        if (name.empty() || name.size() > Limits::kMaxIdentifierLength)
        {
            return false;
        }
        for (const char c : name)
        {
            if (!isWordCharacter(static_cast<unsigned char>(c)))
            {
                return false;
            }
        }
        if (isDigit(static_cast<unsigned char>(name.front())))
        {
            return false;
        }
        return !kLanguageWords.contains(lower(name));
    }

    std::string translateSignature(
        std::string_view signature, std::size_t& required, std::size_t& total, std::string& error)
    {
        std::string result;
        required = 0;
        total = 0;
        bool optional = false;
        for (const char c : signature)
        {
            if (c == '/')
            {
                if (optional)
                {
                    error = "a signature may contain only one '/'";
                    return {};
                }
                optional = true;
                result += '/';
                continue;
            }
            char translated = 0;
            switch (c)
            {
                case 'f':
                    translated = 'f';
                    break;
                case 'l':
                case 's':
                    translated = 'l';
                    break;
                case 'r':
                case 'c':
                    translated = 'c';
                    break;
                case 'S':
                    translated = 'S';
                    break;
                default:
                    error = std::format("'{}' is not a signature character (use f, l, s, r, c, S and /)", c);
                    return {};
            }
            result += translated;
            ++total;
            if (!optional)
            {
                ++required;
            }
        }
        if (total > Limits::kMaxFunctionArguments)
        {
            error = std::format("a function may have at most {} arguments", Limits::kMaxFunctionArguments);
            return {};
        }
        if (!optional)
        {
            result += '/';
        }
        return result;
    }

    CompileOutput compileScript(const openeden::script::ScriptSource& source, const CompileServices& services) noexcept
    {
        CompileOutput output;
        try
        {
            const bool strict = services.mPolicy == UnknownFunctionPolicy::Strict;
            Reporter reporter(source.name, strict);
            output.mScript.mName = source.name;
            output.mScript.mKind = source.kind;

            auto finish = [&]() {
                output.mReports = reporter.take();
                output.mOk = std::none_of(output.mReports.begin(), output.mReports.end(),
                    [](const ErrorReport& report) { return report.severity == Severity::Error; });
            };

            if (source.text.size() > Limits::kMaxSourceBytes)
            {
                reporter.error(0, 0,
                    std::format("the script is too large ({} bytes; the limit is {})", source.text.size(),
                        Limits::kMaxSourceBytes));
                finish();
                return output;
            }
            if (static_cast<std::size_t>(std::count(source.text.begin(), source.text.end(), '\n')) + 1
                > Limits::kMaxLines)
            {
                reporter.error(0, 0, std::format("the script has too many lines (the limit is {})", Limits::kMaxLines));
                finish();
                return output;
            }

            std::vector<Line> lines = lex(source.text, reporter);
            if (reporter.hasErrors())
            {
                finish();
                return output;
            }

            Structure structure;
            if (source.kind == ScriptKind::Script)
            {
                parseStructure(lines, output.mScript, structure, reporter);
                if (reporter.hasErrors())
                {
                    finish();
                    return output;
                }
            }

            // A fragment may declare locals too (result scripts do): the declaration lines are read here and
            // left blank, so that the line numbers stay the same. A newline ends the text, as the parsers expect.
            std::string fragmentText;
            if (source.kind == ScriptKind::Fragment)
            {
                bool tooManyReported = false;
                for (const Line& line : lines)
                {
                    const std::string word = !line.mTokens.empty() && line.mTokens[0].mKind == Token::Kind::Word
                        ? lower(line.mTokens[0].mText)
                        : std::string();
                    if (localTypeFor(word) != ' ')
                    {
                        declareLocal(line, word, output.mScript, reporter, tooManyReported);
                    }
                    else
                    {
                        fragmentText.append(line.mRaw);
                    }
                    fragmentText += '\n';
                }
                if (reporter.hasErrors())
                {
                    finish();
                    return output;
                }
            }

            // A declaration inside a block (the masters have some) declares a local of the whole script. The line is
            // left blank in the text given to the compiler, so that the line numbers stay the same.
            std::set<std::size_t> declarationLines;
            if (source.kind == ScriptKind::Script)
            {
                bool tooManyReported = false;
                for (const BlockSpan& block : structure.mBlocks)
                {
                    for (std::size_t i = block.mFirstBody; i < block.mEndLine; ++i)
                    {
                        const Line& line = lines[i];
                        const std::string word = !line.mTokens.empty() && line.mTokens[0].mKind == Token::Kind::Word
                            ? lower(line.mTokens[0].mText)
                            : std::string();
                        if (localTypeFor(word) != ' ')
                        {
                            declareLocal(line, word, output.mScript, reporter, tooManyReported);
                            declarationLines.insert(i);
                        }
                    }
                }
                if (reporter.hasErrors())
                {
                    finish();
                    return output;
                }
            }

            // Names that are not known: an error in strict mode, an ignored function in tolerant mode.
            std::vector<Candidate> candidates;
            if (source.kind == ScriptKind::Script)
            {
                for (const BlockSpan& block : structure.mBlocks)
                {
                    for (std::size_t i = block.mFirstBody; i < block.mEndLine; ++i)
                    {
                        findCandidates(lines[i], services, output.mScript, candidates);
                    }
                }
            }
            else
            {
                for (const Line& line : lines)
                {
                    findCandidates(line, services, output.mScript, candidates);
                }
            }
            std::set<std::string> seen;
            for (const Candidate& candidate : candidates)
            {
                if (!seen.insert(candidate.mLower).second || services.mFind(candidate.mLower) != nullptr)
                {
                    continue;
                }
                if (strict)
                {
                    reporter.error(
                        candidate.mLine, candidate.mColumn, std::format("unknown function '{}'", candidate.mSpelling));
                }
                else if (services.mAddUnknown(candidate.mLower, candidate.mSpelling) == nullptr)
                {
                    reporter.error(candidate.mLine, candidate.mColumn,
                        std::format(
                            "too many different unknown functions (the limit is {})", Limits::kMaxUnknownFunctions));
                    break;
                }
            }
            if (reporter.hasErrors())
            {
                finish();
                return output;
            }

            // The command table: every registered function and every accepted unknown name.
            Compiler::Extensions extensions;
            services.mForEachFunction([&extensions](const FunctionEntry& entry) {
                extensions.registerFunction(entry.mName, entry.mReturns == ValueKind::Float ? 'f' : 'l',
                    entry.mCompilerSignature, entry.mCode, entry.mAllowsExplicit ? entry.mCode + 1 : -1);
            });

            bool failed = false;
            if (source.kind == ScriptKind::Script)
            {
                for (const BlockSpan& span : structure.mBlocks)
                {
                    std::string text;
                    for (std::size_t i = span.mFirstBody; i < span.mEndLine; ++i)
                    {
                        if (!declarationLines.contains(i))
                        {
                            text.append(lines[i].mRaw);
                        }
                        text += '\n';
                    }
                    text += "End\n";
                    CompiledBlock block;
                    block.mEvent = span.mEvent;
                    compileText(
                        text, true, span.mBeginLine, extensions, services, output.mScript, reporter, block, failed);
                    output.mScript.mBlocks.push_back(std::move(block));
                }
            }
            else
            {
                CompiledBlock block;
                compileText(fragmentText, false, 0, extensions, services, output.mScript, reporter, block, failed);
                output.mScript.mBlocks.push_back(std::move(block));
            }
            finish();
            if (failed)
            {
                output.mOk = false;
                if (std::none_of(output.mReports.begin(), output.mReports.end(),
                        [](const ErrorReport& report) { return report.severity == Severity::Error; }))
                {
                    ErrorReport report;
                    report.script = source.name;
                    report.message = "the script could not be compiled";
                    output.mReports.push_back(std::move(report));
                }
            }
        }
        catch (const std::exception& e)
        {
            output.mOk = false;
            ErrorReport report;
            report.script = source.name;
            report.message = std::format("the script could not be compiled ({})", e.what());
            output.mReports.push_back(std::move(report));
        }
        catch (...)
        {
            output.mOk = false;
            ErrorReport report;
            report.script = source.name;
            report.message = "the script could not be compiled (unknown error)";
            output.mReports.push_back(std::move(report));
        }
        return output;
    }
}
