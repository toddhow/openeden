#include "derive.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "obscriptlimits.hpp"

namespace ScriptObscript
{
    namespace
    {
        // ---- the compiled bytes -------------------------------------------------------------------------------

        unsigned readU16(std::string_view bytes, std::size_t at)
        {
            return static_cast<unsigned>(static_cast<unsigned char>(bytes[at]))
                | (static_cast<unsigned>(static_cast<unsigned char>(bytes[at + 1])) << 8);
        }

        constexpr unsigned kReferencePrefix = 0x1C;
        constexpr unsigned kScriptName = 0x1D;
        constexpr unsigned kFirstCommand = 0x1000;

        struct Statement
        {
            unsigned mOpcode = 0;
            std::string_view mBody;
            bool mOnReference = false;
        };

        // Walk statements to the exact end. False when the bytes do not end where a statement ends.
        bool walkStatements(std::string_view bytes, std::vector<Statement>& out)
        {
            std::size_t at = 0;
            bool pending = false;
            while (at < bytes.size())
            {
                if (at + 4 > bytes.size())
                {
                    return false;
                }
                const unsigned opcode = readU16(bytes, at);
                if (opcode == kReferencePrefix)
                {
                    pending = true;
                    at += 4;
                    continue;
                }
                const unsigned length = readU16(bytes, at + 2);
                if (at + 4 + length > bytes.size())
                {
                    return false;
                }
                Statement statement;
                statement.mOpcode = opcode;
                statement.mBody = bytes.substr(at + 4, length);
                statement.mOnReference = pending;
                out.push_back(statement);
                pending = false;
                at += 4 + length;
            }
            return !pending;
        }

        struct CallRecord
        {
            unsigned mOpcode = 0;
            std::string_view mPayload;
            bool mOnReference = false;
            bool mInExpression = false;
        };

        // The number of arguments a call's payload states. A command statement's payload starts with the count; a call
        // inside an expression with no arguments has an empty payload. -1 when the payload cannot hold a count.
        int payloadCount(std::string_view payload)
        {
            if (payload.empty())
            {
                return 0;
            }
            if (payload.size() < 2)
            {
                return -1;
            }
            return static_cast<int>(readU16(payload, 0));
        }

        bool isOperatorCharacter(char c)
        {
            return c != 0 && std::string_view("+-*/%<>=!&|()~").find(c) != std::string_view::npos;
        }

        bool isTwoCharacterOperatorStart(char c)
        {
            return std::string_view("<>=!&|").find(c) != std::string_view::npos;
        }

        // Read the tokens of an expression and collect its calls. False when something is not understood.
        bool readExpression(std::string_view e, std::vector<CallRecord>& calls)
        {
            std::size_t i = 0;
            const std::size_t n = e.size();
            while (i < n)
            {
                const char c = e[i];
                if (c == ' ')
                {
                    if (i + 1 >= n)
                    {
                        return false;
                    }
                    const char d = e[i + 1];
                    if (d == 'r' || d == 's' || d == 'f' || d == 'G' || d == 'Z')
                    {
                        if (i + 4 > n)
                        {
                            return false;
                        }
                        i += 4;
                    }
                    else if (d == 'n')
                    {
                        if (i + 6 > n)
                        {
                            return false;
                        }
                        i += 6;
                    }
                    else if (d == 'z')
                    {
                        if (i + 10 > n)
                        {
                            return false;
                        }
                        i += 10;
                    }
                    else if (d == 'X')
                    {
                        if (i + 6 > n)
                        {
                            return false;
                        }
                        const unsigned length = readU16(e, i + 4);
                        if (i + 6 + length > n)
                        {
                            return false;
                        }
                        calls.push_back({ readU16(e, i + 2), e.substr(i + 6, length), false, true });
                        i += 6 + length;
                    }
                    else if ((d >= '0' && d <= '9') || d == '.')
                    {
                        std::size_t j = i + 1;
                        while (j < n && ((e[j] >= '0' && e[j] <= '9') || e[j] == '.'))
                        {
                            ++j;
                        }
                        i = j;
                    }
                    else if (isOperatorCharacter(d))
                    {
                        if (i + 2 < n && isTwoCharacterOperatorStart(d)
                            && (e[i + 2] == '=' || e[i + 2] == '&' || e[i + 2] == '|'))
                        {
                            i += 3;
                        }
                        else
                        {
                            i += 2;
                        }
                    }
                    else
                    {
                        return false;
                    }
                }
                else if (c == 'X')
                {
                    if (i + 5 > n)
                    {
                        return false;
                    }
                    const unsigned length = readU16(e, i + 3);
                    if (i + 5 + length > n)
                    {
                        return false;
                    }
                    calls.push_back({ readU16(e, i + 1), e.substr(i + 5, length), true, true });
                    i += 5 + length;
                }
                else if (c == 'r' || c == 's' || c == 'f' || c == 'G' || c == 'g')
                {
                    if (i + 3 > n)
                    {
                        return false;
                    }
                    i += 3;
                }
                else
                {
                    return false;
                }
            }
            return true;
        }

        // The calls a statement holds. Nullopt-like: returns false when an expression was not understood.
        bool statementCalls(const Statement& statement, std::vector<CallRecord>& calls)
        {
            const std::string_view body = statement.mBody;
            if (statement.mOpcode >= kFirstCommand)
            {
                calls.push_back({ statement.mOpcode, body, statement.mOnReference, false });
                return true;
            }
            if (statement.mOpcode == 0x16 || statement.mOpcode == 0x18)
            {
                if (body.size() < 4)
                {
                    return false;
                }
                const unsigned length = readU16(body, 2);
                if (4 + length > body.size())
                {
                    return false;
                }
                return readExpression(body.substr(4, length), calls);
            }
            if (statement.mOpcode == 0x15)
            {
                // A target (a variable, with a reference before it for another script's variable), then the length of
                // the expression and the expression. The target's size varies, so every offset is tried.
                for (std::size_t k = 0; k + 2 <= body.size() && k < 10; ++k)
                {
                    if (readU16(body, k) != body.size() - k - 2)
                    {
                        continue;
                    }
                    std::vector<CallRecord> found;
                    if (readExpression(body.substr(k + 2), found))
                    {
                        calls.insert(calls.end(), found.begin(), found.end());
                        return true;
                    }
                }
                return false;
            }
            return true; // Begin, End, Else, EndIf, Return: no calls
        }

        // ---- the text -------------------------------------------------------------------------------------------

        bool isWordStart(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool isWordChar(char c)
        {
            return isWordStart(c) || (c >= '0' && c <= '9');
        }

        char lowerChar(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        // Split one line into tokens: lower-cased words, numbers, quoted strings (with their quotes), operators. A
        // comment ends the line.
        std::vector<std::string> tokenise(std::string_view line)
        {
            std::vector<std::string> tokens;
            std::size_t i = 0;
            while (i < line.size())
            {
                const char c = line[i];
                if (c == ';')
                {
                    break;
                }
                if (c == ' ' || c == '\t' || c == '\r' || c == ',')
                {
                    ++i;
                    continue;
                }
                if (c == '"')
                {
                    std::size_t close = line.find('"', i + 1);
                    if (close == std::string_view::npos)
                    {
                        close = line.size() - 1;
                    }
                    tokens.emplace_back(line.substr(i, close - i + 1));
                    i = close + 1;
                }
                else if (c >= '0' && c <= '9')
                {
                    std::size_t j = i;
                    while (j < line.size() && line[j] >= '0' && line[j] <= '9')
                    {
                        ++j;
                    }
                    if (j + 1 < line.size() && line[j] == '.' && line[j + 1] >= '0' && line[j + 1] <= '9')
                    {
                        ++j;
                        while (j < line.size() && line[j] >= '0' && line[j] <= '9')
                        {
                            ++j;
                        }
                    }
                    if (j < line.size() && isWordChar(line[j]))
                    {
                        while (j < line.size() && isWordChar(line[j]))
                        {
                            ++j;
                        }
                    }
                    std::string token;
                    for (std::size_t k = i; k < j; ++k)
                    {
                        token += lowerChar(line[k]);
                    }
                    tokens.push_back(std::move(token));
                    i = j;
                }
                else if (isWordStart(c))
                {
                    std::size_t j = i;
                    std::string token;
                    while (j < line.size() && isWordChar(line[j]))
                    {
                        token += lowerChar(line[j]);
                        ++j;
                    }
                    tokens.push_back(std::move(token));
                    i = j;
                }
                else
                {
                    std::size_t length = 1;
                    if (i + 1 < line.size())
                    {
                        const std::string_view two = line.substr(i, 2);
                        if (two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "&&" || two == "||"
                            || two == "->")
                        {
                            length = 2;
                        }
                    }
                    tokens.emplace_back(line.substr(i, length));
                    i += length;
                }
            }
            return tokens;
        }

        const std::unordered_set<std::string_view> kLanguage = { "set", "to", "if", "elseif", "else", "endif", "return",
            "begin", "end", "short", "long", "int", "float", "ref", "scn", "scriptname" };

        bool isDeclaration(const std::vector<std::string>& tokens)
        {
            return tokens.size() == 2
                && (tokens[0] == "short" || tokens[0] == "long" || tokens[0] == "int" || tokens[0] == "float"
                    || tokens[0] == "ref")
                && isWordStart(tokens[1][0]);
        }

        bool isWord(const std::string& token)
        {
            return !token.empty() && isWordStart(token[0]);
        }

        bool isNumber(const std::string& token)
        {
            return !token.empty() && token[0] >= '0' && token[0] <= '9'
                && token.find_first_not_of("0123456789.") == std::string::npos;
        }

        bool isComparison(const std::string& token)
        {
            return token == "==" || token == "!=" || token == "<" || token == ">" || token == "<=" || token == ">=";
        }

        bool isOperatorToken(const std::string& token)
        {
            return isComparison(token) || token == "&&" || token == "||" || token == "+" || token == "-" || token == "*"
                || token == "/" || token == ")" || token == "(" || token == "%" || token == "~";
        }

        struct Locals
        {
            std::set<std::string> mAll;
            std::set<std::string> mRefs;
            std::set<std::string> mFloats;
        };

        enum class Class
        {
            Form,
            IntegerVariable,
            FloatVariable,
            IntegerNumber,
            FloatNumber,
            Text,
            Dotted,
            Word,
        };

        Class classify(const std::string& token, const Locals& locals, const DeriveEnvironment& environment)
        {
            if (token == ".")
            {
                return Class::Dotted;
            }
            if (!token.empty() && token[0] == '"')
            {
                return Class::Text;
            }
            if (isNumber(token))
            {
                return token.find('.') == std::string::npos ? Class::IntegerNumber : Class::FloatNumber;
            }
            if (locals.mRefs.contains(token))
            {
                return Class::Form;
            }
            if (locals.mFloats.contains(token))
            {
                return Class::FloatVariable;
            }
            if (locals.mAll.contains(token))
            {
                return Class::IntegerVariable;
            }
            if (environment.mForms.contains(token))
            {
                return Class::Form;
            }
            return Class::Word;
        }

        // The contract signature character a source word suggests.
        char kindOfClass(Class value)
        {
            switch (value)
            {
                case Class::Form:
                    return 'r';
                case Class::IntegerNumber:
                case Class::IntegerVariable:
                case Class::Dotted:
                    return 'l';
                case Class::FloatNumber:
                case Class::FloatVariable:
                    return 'f';
                case Class::Text:
                    return 'S';
                case Class::Word:
                    break;
            }
            return 'c';
        }

        // The arguments written after a command: words and numbers up to an operator; "name.variable" counts as one.
        // Each argument is its class.
        std::vector<Class> sourceArguments(const std::vector<std::string>& tokens, std::size_t from,
            const Locals& locals, const DeriveEnvironment& environment)
        {
            std::vector<Class> result;
            std::size_t i = from;
            while (i < tokens.size())
            {
                const std::string& token = tokens[i];
                if (isOperatorToken(token))
                {
                    break;
                }
                if (i + 2 < tokens.size() && (tokens[i + 1] == "." || tokens[i + 1] == "->"))
                {
                    result.push_back(Class::Dotted);
                    i += 3;
                    continue;
                }
                result.push_back(classify(token, locals, environment));
                ++i;
            }
            return result;
        }

        struct CallName
        {
            std::string mName;
            std::size_t mIndex = 0;
        };

        // Names that stand where a command can stand in an expression's tokens, in order.
        std::vector<CallName> callNames(
            const std::vector<std::string>& tokens, const Locals& locals, const DeriveEnvironment& environment)
        {
            std::vector<CallName> result;
            for (std::size_t i = 0; i < tokens.size(); ++i)
            {
                const std::string& token = tokens[i];
                if (!isWord(token) || kLanguage.contains(token))
                {
                    continue;
                }
                const std::string previous = i > 0 ? tokens[i - 1] : std::string();
                const std::string next = i + 1 < tokens.size() ? tokens[i + 1] : std::string();
                const bool afterDot = previous == "." || previous == "->";
                bool position = false;
                if (i == 0 || afterDot)
                {
                    position = true;
                }
                else
                {
                    position = previous == "(" || previous == "&&" || previous == "||" || isComparison(previous)
                        || previous == "+" || previous == "-" || previous == "*" || previous == "/";
                }
                if (!position)
                {
                    continue;
                }
                if (!afterDot && (next == "." || next == "->"))
                {
                    continue; // a reference before its dot
                }
                if (locals.mAll.contains(token))
                {
                    continue;
                }
                if (afterDot)
                {
                    if (environment.mVariables.contains(token))
                    {
                        continue;
                    }
                }
                else if (environment.mForms.contains(token) || environment.mVariables.contains(token))
                {
                    continue;
                }
                result.push_back({ token, i });
            }
            return result;
        }

        // ---- pairs and their votes ------------------------------------------------------------------------------

        // What the compiled bytes say about an argument list: the kinds of the leading arguments that carry a tag.
        // 'N' is a global variable (a number of unknown kind).
        std::string taggedKinds(std::string_view payload, int count)
        {
            std::string kinds;
            if (count <= 0 || payload.size() < 2)
            {
                return kinds;
            }
            std::size_t at = 2;
            for (int i = 0; i < count; ++i)
            {
                if (at >= payload.size())
                {
                    break;
                }
                const char tag = payload[at];
                std::size_t size = 0;
                char kind = 0;
                switch (tag)
                {
                    case 'r':
                        size = 3;
                        kind = 'r';
                        break;
                    case 'n':
                        size = 5;
                        kind = 'l';
                        break;
                    case 'z':
                        size = 9;
                        kind = 'f';
                        break;
                    case 's':
                        size = 3;
                        kind = 'l';
                        break;
                    case 'f':
                        size = 3;
                        kind = 'f';
                        break;
                    case 'G':
                        size = 3;
                        kind = 'N';
                        break;
                    default:
                        break;
                }
                if (size == 0 || at + size > payload.size())
                {
                    break;
                }
                kinds += kind;
                at += size;
            }
            return kinds;
        }

        struct Occurrence
        {
            unsigned mOpcode = 0;
            int mCompiledCount = -1;
            std::size_t mSourceCount = 0;
            std::string mKinds; ///< One vote per position, '?' for none; empty when the counts disagree.
            bool mOnReference = false;
            int mFloatVote = 0;
        };

        struct NameAggregate
        {
            std::vector<Occurrence> mOccurrences;
        };

        struct OpcodeUseAggregate
        {
            std::uint32_t mOccurrences = 0;
            std::uint32_t mMin = 0xFFFFFFFFu;
            std::uint32_t mMax = 0;
        };

        struct Unit
        {
            std::vector<std::pair<std::string, Occurrence>> mPairs;
            std::size_t mCountMismatches = 0;
        };

        enum class UnitResult
        {
            Aligned,
            LineCount,
            Kind,
            Expression,
        };

        Occurrence makeOccurrence(const CallRecord& call, const std::vector<Class>& arguments)
        {
            Occurrence occurrence;
            occurrence.mOpcode = call.mOpcode;
            occurrence.mOnReference = call.mOnReference;
            occurrence.mCompiledCount = payloadCount(call.mPayload);
            occurrence.mSourceCount = arguments.size();
            if (occurrence.mCompiledCount >= 0
                && static_cast<std::size_t>(occurrence.mCompiledCount) == arguments.size())
            {
                const std::string tagged = taggedKinds(call.mPayload, occurrence.mCompiledCount);
                for (std::size_t i = 0; i < arguments.size(); ++i)
                {
                    char vote = '?';
                    if (i < tagged.size())
                    {
                        vote = tagged[i] == 'N' ? '?' : tagged[i];
                    }
                    else
                    {
                        vote = kindOfClass(arguments[i]);
                    }
                    occurrence.mKinds += vote;
                }
            }
            return occurrence;
        }

        int floatVote(const std::vector<std::string>& tokens, std::size_t nameIndex, std::size_t argumentCount,
            const Locals& locals, bool isSet, const std::string& target)
        {
            const std::size_t after = nameIndex + 1 + argumentCount;
            if (isSet && nameIndex == 0 && after == tokens.size())
            {
                return locals.mFloats.contains(target) ? 1 : 0;
            }
            if (after + 1 < tokens.size() && isComparison(tokens[after]))
            {
                const std::string& other = tokens[after + 1];
                if (isNumber(other) && other.find('.') != std::string::npos)
                {
                    return 1;
                }
                if (locals.mFloats.contains(other))
                {
                    return 1;
                }
            }
            if (nameIndex >= 2 && isComparison(tokens[nameIndex - 1]))
            {
                const std::string& other = tokens[nameIndex - 2];
                if ((isNumber(other) && other.find('.') != std::string::npos) || locals.mFloats.contains(other))
                {
                    return 1;
                }
            }
            return 0;
        }

        UnitResult alignUnit(std::string_view source, const std::vector<Statement>& statements,
            const DeriveEnvironment& environment, Unit& unit)
        {
            // The text: lines as tokens, without blank lines, declarations and the script name.
            Locals locals;
            std::vector<std::vector<std::string>> lines;
            std::size_t start = 0;
            while (start <= source.size())
            {
                std::size_t end = source.find('\n', start);
                if (end == std::string_view::npos)
                {
                    end = source.size();
                }
                std::vector<std::string> tokens = tokenise(source.substr(start, end - start));
                start = end + 1;
                if (tokens.empty())
                {
                    continue;
                }
                if (isDeclaration(tokens))
                {
                    locals.mAll.insert(tokens[1]);
                    if (tokens[0] == "ref")
                    {
                        locals.mRefs.insert(tokens[1]);
                    }
                    if (tokens[0] == "float")
                    {
                        locals.mFloats.insert(tokens[1]);
                    }
                    continue;
                }
                if (tokens[0] == "scn" || tokens[0] == "scriptname")
                {
                    continue;
                }
                lines.push_back(std::move(tokens));
            }

            std::size_t firstStatement = 0;
            if (!statements.empty() && statements[0].mOpcode == kScriptName)
            {
                firstStatement = 1;
            }
            if (lines.size() != statements.size() - firstStatement)
            {
                return UnitResult::LineCount;
            }

            static const std::map<std::string, unsigned> keywords
                = { { "begin", 0x10 }, { "end", 0x11 }, { "set", 0x15 }, { "if", 0x16 }, { "else", 0x17 },
                      { "elseif", 0x18 }, { "endif", 0x19 }, { "return", 0x1E } };

            for (std::size_t n = 0; n < lines.size(); ++n)
            {
                const std::vector<std::string>& tokens = lines[n];
                const Statement& statement = statements[firstStatement + n];
                const auto keyword = keywords.find(tokens[0]);
                if (keyword != keywords.end())
                {
                    if (keyword->second != statement.mOpcode)
                    {
                        return UnitResult::Kind;
                    }
                    if (statement.mOpcode != 0x15 && statement.mOpcode != 0x16 && statement.mOpcode != 0x18)
                    {
                        continue;
                    }
                    std::vector<CallRecord> calls;
                    if (!statementCalls(statement, calls))
                    {
                        return UnitResult::Expression;
                    }
                    std::vector<std::string> expression;
                    std::string target;
                    if (statement.mOpcode == 0x15)
                    {
                        const auto to = std::find(tokens.begin(), tokens.end(), "to");
                        if (to == tokens.end())
                        {
                            return UnitResult::Kind;
                        }
                        expression.assign(to + 1, tokens.end());
                        if (tokens.size() > 1 && (tokens.size() < 3 || tokens[2] == "to"))
                        {
                            target = tokens[1];
                        }
                    }
                    else
                    {
                        expression.assign(tokens.begin() + 1, tokens.end());
                    }
                    const std::vector<CallName> names = callNames(expression, locals, environment);
                    if (names.size() != calls.size())
                    {
                        ++unit.mCountMismatches;
                        continue;
                    }
                    for (std::size_t c = 0; c < names.size(); ++c)
                    {
                        const std::vector<Class> arguments
                            = sourceArguments(expression, names[c].mIndex + 1, locals, environment);
                        Occurrence occurrence = makeOccurrence(calls[c], arguments);
                        occurrence.mFloatVote = floatVote(
                            expression, names[c].mIndex, arguments.size(), locals, statement.mOpcode == 0x15, target);
                        unit.mPairs.emplace_back(names[c].mName, std::move(occurrence));
                    }
                    continue;
                }

                // a command on a line of its own
                if (statement.mOpcode < kFirstCommand)
                {
                    return UnitResult::Kind;
                }
                std::string name = tokens[0];
                std::size_t from = 1;
                if (tokens.size() >= 3 && (tokens[1] == "." || tokens[1] == "->"))
                {
                    name = tokens[2];
                    from = 3;
                }
                if (!isWord(name))
                {
                    return UnitResult::Kind;
                }
                std::vector<CallRecord> calls;
                statementCalls(statement, calls);
                const std::vector<Class> arguments = sourceArguments(tokens, from, locals, environment);
                unit.mPairs.emplace_back(std::move(name), makeOccurrence(calls[0], arguments));
            }
            return UnitResult::Aligned;
        }

        // ---- building the table ---------------------------------------------------------------------------------

        char chooseKind(const std::map<char, std::size_t>& votes)
        {
            if (votes.empty())
            {
                return 'l';
            }
            // Only floats and integers: a float wins (a whole number can be written where a float is wanted).
            bool onlyNumbers = true;
            for (const auto& [kind, count] : votes)
            {
                onlyNumbers = onlyNumbers && (kind == 'f' || kind == 'l');
            }
            if (onlyNumbers && votes.contains('f'))
            {
                return 'f';
            }
            char best = 'l';
            std::size_t bestCount = 0;
            for (const auto& [kind, count] : votes) // ordered by character: ties go to the lower character
            {
                if (count > bestCount)
                {
                    best = kind;
                    bestCount = count;
                }
            }
            return best;
        }

        bool kindAgrees(char vote, char chosen)
        {
            return vote == '?' || vote == chosen || (chosen == 'f' && vote == 'l');
        }

        TableEntry buildEntry(const std::string& name, const NameAggregate& aggregate)
        {
            TableEntry entry;
            entry.mName = name;
            entry.mOccurrences = static_cast<std::uint32_t>(aggregate.mOccurrences.size());

            std::map<unsigned, std::size_t> opcodeVotes;
            for (const Occurrence& occurrence : aggregate.mOccurrences)
            {
                ++opcodeVotes[occurrence.mOpcode];
            }
            unsigned opcode = 0;
            std::size_t best = 0;
            for (const auto& [candidate, count] : opcodeVotes) // ordered: ties go to the lower opcode
            {
                if (count > best)
                {
                    opcode = candidate;
                    best = count;
                }
            }
            entry.mOpcode = opcode;
            entry.mOpcodeVotes = static_cast<std::uint32_t>(best);

            std::vector<const Occurrence*> chosen;
            int minCount = -1;
            int maxCount = 0;
            bool unreadable = false;
            int floatVotes = 0;
            for (const Occurrence& occurrence : aggregate.mOccurrences)
            {
                if (occurrence.mOpcode != opcode)
                {
                    continue;
                }
                chosen.push_back(&occurrence);
                entry.mExplicitUses += occurrence.mOnReference ? 1u : 0u;
                floatVotes += occurrence.mFloatVote;
                if (occurrence.mCompiledCount < 0)
                {
                    unreadable = true;
                    continue;
                }
                minCount = minCount < 0 ? occurrence.mCompiledCount : std::min(minCount, occurrence.mCompiledCount);
                maxCount = std::max(maxCount, occurrence.mCompiledCount);
            }
            entry.mReturns = floatVotes > 0 ? 'f' : 'l';

            if (unreadable || minCount < 0 || maxCount > static_cast<int>(DeriveLimits::kMaxArguments))
            {
                entry.mSignature = "*";
                return entry;
            }

            std::string kinds;
            for (int position = 0; position < maxCount; ++position)
            {
                std::map<char, std::size_t> votes;
                for (const Occurrence* occurrence : chosen)
                {
                    if (static_cast<std::size_t>(position) < occurrence->mKinds.size()
                        && occurrence->mKinds[static_cast<std::size_t>(position)] != '?')
                    {
                        ++votes[occurrence->mKinds[static_cast<std::size_t>(position)]];
                    }
                }
                kinds += chooseKind(votes);
            }

            std::uint32_t agreeing = 0;
            for (const Occurrence* occurrence : chosen)
            {
                bool agrees = occurrence->mCompiledCount >= 0
                    && static_cast<std::size_t>(occurrence->mCompiledCount) == occurrence->mSourceCount
                    && occurrence->mKinds.size() == occurrence->mSourceCount;
                for (std::size_t i = 0; agrees && i < occurrence->mKinds.size() && i < kinds.size(); ++i)
                {
                    agrees = kindAgrees(occurrence->mKinds[i], kinds[i]);
                }
                agreeing += agrees ? 1u : 0u;
            }
            entry.mShapeVotes = agreeing;

            if (static_cast<double>(agreeing)
                < DeriveLimits::kMinShapeAgreement * static_cast<double>(entry.mOccurrences))
            {
                entry.mSignature = "*";
                return entry;
            }
            const std::size_t required = static_cast<std::size_t>(minCount);
            entry.mSignature = kinds.substr(0, required) + "/" + kinds.substr(required);
            return entry;
        }
    }

    FunctionTable deriveFunctionTable(
        const std::vector<SourceUnit>& units, const DeriveEnvironment& environment, DeriveStats* stats)
    {
        DeriveStats local;
        std::map<std::string, NameAggregate> names;
        std::map<unsigned, OpcodeUseAggregate> opcodes;

        for (const SourceUnit& sourceUnit : units)
        {
            ++local.mUnits;
            std::vector<Statement> statements;
            if (!walkStatements(sourceUnit.mCompiled, statements))
            {
                ++local.mSkippedMalformed;
                continue;
            }

            // every command in the bytes counts for the opcode list, whatever happens to the pairing
            for (const Statement& statement : statements)
            {
                std::vector<CallRecord> calls;
                if (!statementCalls(statement, calls))
                {
                    continue;
                }
                for (const CallRecord& call : calls)
                {
                    OpcodeUseAggregate& use = opcodes[call.mOpcode];
                    ++use.mOccurrences;
                    const int count = payloadCount(call.mPayload);
                    if (count >= 0)
                    {
                        use.mMin = std::min(use.mMin, static_cast<std::uint32_t>(count));
                        use.mMax = std::max(use.mMax, static_cast<std::uint32_t>(count));
                    }
                }
            }

            Unit unit;
            switch (alignUnit(sourceUnit.mSource, statements, environment, unit))
            {
                case UnitResult::LineCount:
                    ++local.mSkippedLineCount;
                    continue;
                case UnitResult::Kind:
                    ++local.mSkippedKind;
                    continue;
                case UnitResult::Expression:
                    ++local.mSkippedExpression;
                    continue;
                case UnitResult::Aligned:
                    break;
            }
            ++local.mAligned;
            for (auto& [name, occurrence] : unit.mPairs)
            {
                if (name.size() > ScriptObscript::Limits::kMaxIdentifierLength)
                {
                    continue;
                }
                if (occurrence.mCompiledCount >= 0
                    && occurrence.mSourceCount != static_cast<std::size_t>(occurrence.mCompiledCount))
                {
                    ++local.mLinesWithOtherCount;
                }
                ++local.mPairs;
                names[name].mOccurrences.push_back(std::move(occurrence));
            }
        }

        FunctionTable table;
        std::map<unsigned, std::size_t> namesPerOpcode;
        std::set<std::string> conflicting;
        for (const auto& [name, aggregate] : names)
        {
            TableEntry entry = buildEntry(name, aggregate);
            if (entry.mOpcodeVotes != entry.mOccurrences)
            {
                conflicting.insert(name);
            }
            ++namesPerOpcode[entry.mOpcode];
            local.mReliableShapes += (entry.mSignature != "*" && entry.shapeConfidence() >= 0.9) ? 1 : 0;
            local.mVariableArity += entry.mSignature == "*" ? 1 : 0;
            table.mEntries.push_back(std::move(entry));
        }
        for (const auto& [opcode, use] : opcodes)
        {
            OpcodeUse record;
            record.mOpcode = opcode;
            record.mOccurrences = use.mOccurrences;
            record.mMinArguments = use.mMin == 0xFFFFFFFFu ? 0 : use.mMin;
            record.mMaxArguments = use.mMax;
            table.mOpcodes.push_back(record);
            ++local.mOpcodesSeen;
            const auto named = namesPerOpcode.find(opcode);
            if (named != namesPerOpcode.end())
            {
                ++local.mOpcodesNamed;
                local.mOpcodesWithSeveralNames += named->second > 1 ? 1 : 0;
            }
        }
        local.mNamesConflicting = conflicting.size();
        table.normalise();
        if (stats != nullptr)
        {
            *stats = local;
        }
        return table;
    }
}
