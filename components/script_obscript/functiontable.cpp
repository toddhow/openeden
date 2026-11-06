#include "functiontable.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <set>

#include "obscriptlimits.hpp"

namespace ScriptObscript
{
    using openeden::script::ErrorReport;
    using openeden::script::FunctionDescriptor;
    using openeden::script::Severity;
    using openeden::script::ValueKind;

    namespace
    {
        constexpr std::string_view kHeader = "openeden.obscript.functions";
        constexpr int kVersion = 1;

        std::string hexText(std::uint32_t value)
        {
            return std::format("{:x}", value);
        }

        ErrorReport makeError(std::string_view file, std::size_t line, std::string message)
        {
            ErrorReport report;
            report.severity = Severity::Error;
            report.script = std::string(file);
            report.line = static_cast<std::uint32_t>(line);
            report.column = line == 0 ? 0 : 1;
            report.message = std::move(message);
            return report;
        }

        // Fields separated by single spaces; an empty field (two spaces) is kept so that it can be refused.
        std::vector<std::string_view> splitFields(std::string_view line)
        {
            std::vector<std::string_view> fields;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t space = line.find(' ', start);
                if (space == std::string_view::npos)
                {
                    fields.push_back(line.substr(start));
                    break;
                }
                fields.push_back(line.substr(start, space - start));
                start = space + 1;
            }
            return fields;
        }

        bool parseDecimal(std::string_view text, std::uint32_t& value)
        {
            if (text.empty() || text.size() > 10)
            {
                return false;
            }
            std::uint64_t result = 0;
            for (const char c : text)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
                result = result * 10 + static_cast<std::uint64_t>(c - '0');
            }
            if (result > 0xFFFFFFFFull)
            {
                return false;
            }
            value = static_cast<std::uint32_t>(result);
            return true;
        }

        bool parseHex(std::string_view text, std::uint32_t& value)
        {
            if (text.empty() || text.size() > 8)
            {
                return false;
            }
            std::uint32_t result = 0;
            for (const char c : text)
            {
                std::uint32_t digit = 0;
                if (c >= '0' && c <= '9')
                {
                    digit = static_cast<std::uint32_t>(c - '0');
                }
                else if (c >= 'a' && c <= 'f')
                {
                    digit = static_cast<std::uint32_t>(c - 'a' + 10);
                }
                else
                {
                    return false;
                }
                result = (result << 4) | digit;
            }
            value = result;
            return true;
        }

        bool validName(std::string_view name)
        {
            if (name.empty() || name.size() > Limits::kMaxIdentifierLength)
            {
                return false;
            }
            for (const char c : name)
            {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                {
                    return false;
                }
            }
            return true;
        }

        // "*", or the contract's characters with exactly one "/".
        bool validSignature(std::string_view signature)
        {
            if (signature == "*")
            {
                return true;
            }
            if (signature.empty() || signature.size() > TableLimits::kMaxSignatureBytes)
            {
                return false;
            }
            std::size_t slashes = 0;
            for (const char c : signature)
            {
                if (c == '/')
                {
                    ++slashes;
                }
                else if (std::string_view("flsrcS").find(c) == std::string_view::npos)
                {
                    return false;
                }
            }
            return slashes == 1;
        }

        // A "function" record: function name opcode signature returns occurrences opcodeVotes shapeVotes
        // explicitUses origin [source words]. seedFile is true when reading a hand-written file.
        bool readFunction(
            const std::vector<std::string_view>& fields, bool seedFile, TableEntry& entry, std::string& error)
        {
            if (fields.size() < 10)
            {
                error = "a function record needs at least 10 fields";
                return false;
            }
            for (const std::string_view field : fields)
            {
                if (field.empty())
                {
                    error = "empty field in a record (two spaces in a row)";
                    return false;
                }
            }
            if (!validName(fields[1]))
            {
                error = "not a valid function name (lower-case letters, digits and underscores)";
                return false;
            }
            entry.mName = std::string(fields[1]);
            if (!parseHex(fields[2], entry.mOpcode))
            {
                error = "the opcode is not hexadecimal";
                return false;
            }
            if (!validSignature(fields[3]))
            {
                error = "not a valid signature (f l s r c S with one '/', or '*')";
                return false;
            }
            entry.mSignature = std::string(fields[3]);
            if (fields[4] != "l" && fields[4] != "f")
            {
                error = "returns must be l or f";
                return false;
            }
            entry.mReturns = fields[4][0];
            if (!parseDecimal(fields[5], entry.mOccurrences) || !parseDecimal(fields[6], entry.mOpcodeVotes)
                || !parseDecimal(fields[7], entry.mShapeVotes) || !parseDecimal(fields[8], entry.mExplicitUses))
            {
                error = "the counts are not numbers";
                return false;
            }
            const std::string_view origin = fields[9];
            if (origin != "derived" && origin != "seed")
            {
                error = "the origin must be derived or seed";
                return false;
            }
            if (entry.mOpcodeVotes > entry.mOccurrences || entry.mShapeVotes > entry.mOccurrences
                || entry.mExplicitUses > entry.mOccurrences)
            {
                error = "the votes cannot be more than the occurrences";
                return false;
            }
            if (origin == "derived")
            {
                if (seedFile)
                {
                    error = "a seed file holds seed entries only";
                    return false;
                }
                if (fields.size() != 10)
                {
                    error = "a derived record has 10 fields";
                    return false;
                }
                return true;
            }
            entry.mSeeded = true;
            if (fields.size() < 11)
            {
                error = "a seed entry needs a source (READ or RECALLED-VERIFY, then where it comes from)";
                return false;
            }
            std::string source;
            for (std::size_t i = 10; i < fields.size(); ++i)
            {
                if (i > 10)
                {
                    source += ' ';
                }
                source += fields[i];
            }
            if (source.rfind("READ", 0) != 0 && source.rfind("RECALLED-VERIFY", 0) != 0)
            {
                error = "the source of a seed entry must start with READ or RECALLED-VERIFY";
                return false;
            }
            entry.mSource = std::move(source);
            return true;
        }
    }

    double TableEntry::opcodeConfidence() const
    {
        if (mSeeded)
        {
            return 1.0;
        }
        return mOccurrences == 0 ? 0.0 : static_cast<double>(mOpcodeVotes) / static_cast<double>(mOccurrences);
    }

    double TableEntry::shapeConfidence() const
    {
        if (mSeeded)
        {
            return 1.0;
        }
        return mOccurrences == 0 ? 0.0 : static_cast<double>(mShapeVotes) / static_cast<double>(mOccurrences);
    }

    const TableEntry* FunctionTable::find(std::string_view lowerName) const
    {
        const auto it = std::lower_bound(mEntries.begin(), mEntries.end(), lowerName,
            [](const TableEntry& entry, std::string_view name) { return entry.mName < name; });
        if (it == mEntries.end() || it->mName != lowerName)
        {
            return nullptr;
        }
        return &*it;
    }

    std::string FunctionTable::nameOfOpcode(std::uint32_t opcode) const
    {
        const TableEntry* best = nullptr;
        for (const TableEntry& entry : mEntries) // sorted by name: a tie goes to the lower name
        {
            if (entry.mOpcode == opcode && !entry.mSeeded
                && (best == nullptr || entry.mOpcodeVotes > best->mOpcodeVotes))
            {
                best = &entry;
            }
        }
        return best != nullptr ? best->mName : unnamedOpcodeName(opcode);
    }

    void FunctionTable::normalise()
    {
        std::sort(mEntries.begin(), mEntries.end(),
            [](const TableEntry& left, const TableEntry& right) { return left.mName < right.mName; });
        std::sort(mOpcodes.begin(), mOpcodes.end(),
            [](const OpcodeUse& left, const OpcodeUse& right) { return left.mOpcode < right.mOpcode; });
    }

    std::string unnamedOpcodeName(std::uint32_t opcode)
    {
        return "fn_0x" + hexText(opcode);
    }

    std::string serialiseTable(const FunctionTable& table)
    {
        std::string text = std::format("{} {}\n", kHeader, kVersion);
        for (const TableEntry& entry : table.mEntries)
        {
            text += std::format("function {} {} {} {} {} {} {} {} {}", entry.mName, hexText(entry.mOpcode),
                entry.mSignature, entry.mReturns, entry.mOccurrences, entry.mOpcodeVotes, entry.mShapeVotes,
                entry.mExplicitUses, entry.mSeeded ? "seed" : "derived");
            if (entry.mSeeded)
            {
                text += ' ';
                text += entry.mSource;
            }
            text += '\n';
        }
        for (const OpcodeUse& use : table.mOpcodes)
        {
            text += std::format(
                "opcode {} {} {} {}\n", hexText(use.mOpcode), use.mOccurrences, use.mMinArguments, use.mMaxArguments);
        }
        return text;
    }

    TableParseResult parseTable(std::string_view text, std::string_view fileName)
    {
        TableParseResult result;
        const auto fail = [&](std::size_t line, std::string message) {
            result.ok = false;
            result.table = FunctionTable();
            result.errors.push_back(makeError(fileName, line, std::move(message)));
            return result;
        };
        if (text.size() > TableLimits::kMaxBytes)
        {
            return fail(0,
                std::format("the table is too large ({} bytes; the limit is {})", text.size(), TableLimits::kMaxBytes));
        }

        FunctionTable table;
        std::set<std::string> names;
        std::set<std::uint32_t> opcodes;
        bool headerSeen = false;
        std::size_t lineNumber = 0;
        std::size_t start = 0;
        while (start <= text.size())
        {
            std::size_t end = text.find('\n', start);
            if (end == std::string_view::npos)
            {
                end = text.size();
            }
            std::string_view line = text.substr(start, end - start);
            start = end + 1;
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            if (line.size() > TableLimits::kMaxLineBytes)
            {
                return fail(lineNumber,
                    std::format("the line is too long (the limit is {} bytes)", TableLimits::kMaxLineBytes));
            }
            if (line.empty() || line.front() == '#')
            {
                continue;
            }
            const std::vector<std::string_view> fields = splitFields(line);
            if (!headerSeen)
            {
                if (fields.size() != 2 || fields[0] != kHeader)
                {
                    return fail(lineNumber, "the table must start with the header 'openeden.obscript.functions 1'");
                }
                if (fields[1] != std::to_string(kVersion))
                {
                    return fail(lineNumber,
                        std::format(
                            "unsupported table version '{}' (this reader reads version {})", fields[1], kVersion));
                }
                headerSeen = true;
                continue;
            }
            if (fields[0] == "function")
            {
                TableEntry entry;
                std::string error;
                if (!readFunction(fields, false, entry, error))
                {
                    return fail(lineNumber, error);
                }
                if (!names.insert(entry.mName).second)
                {
                    return fail(lineNumber, std::format("duplicate function '{}'", entry.mName));
                }
                if (table.mEntries.size() >= TableLimits::kMaxEntries)
                {
                    return fail(
                        lineNumber, std::format("too many functions (the limit is {})", TableLimits::kMaxEntries));
                }
                table.mEntries.push_back(std::move(entry));
            }
            else if (fields[0] == "opcode")
            {
                if (fields.size() != 5)
                {
                    return fail(lineNumber, "an opcode record has 5 fields");
                }
                OpcodeUse use;
                if (!parseHex(fields[1], use.mOpcode))
                {
                    return fail(lineNumber, "the opcode is not hexadecimal");
                }
                if (!parseDecimal(fields[2], use.mOccurrences))
                {
                    return fail(lineNumber, "the occurrences are not a number");
                }
                if (!parseDecimal(fields[3], use.mMinArguments) || !parseDecimal(fields[4], use.mMaxArguments))
                {
                    return fail(lineNumber, "the argument counts are not numbers");
                }
                if (use.mMinArguments > use.mMaxArguments)
                {
                    return fail(lineNumber, "the smallest number of arguments is larger than the largest");
                }
                if (!opcodes.insert(use.mOpcode).second)
                {
                    return fail(lineNumber, std::format("duplicate opcode {}", hexText(use.mOpcode)));
                }
                if (table.mOpcodes.size() >= TableLimits::kMaxOpcodes)
                {
                    return fail(
                        lineNumber, std::format("too many opcodes (the limit is {})", TableLimits::kMaxOpcodes));
                }
                table.mOpcodes.push_back(use);
            }
            else
            {
                return fail(lineNumber, std::format("unknown record '{}'", std::string(fields[0]).substr(0, 32)));
            }
        }
        if (!headerSeen)
        {
            return fail(1, "the table must start with the header 'openeden.obscript.functions 1'");
        }
        table.normalise();
        result.ok = true;
        result.table = std::move(table);
        return result;
    }

    std::size_t mergeSeed(
        FunctionTable& table, std::string_view seedText, std::string_view fileName, std::vector<ErrorReport>& errors)
    {
        std::size_t added = 0;
        std::size_t lineNumber = 0;
        std::size_t start = 0;
        std::set<std::string> seen;
        while (start <= seedText.size())
        {
            std::size_t end = seedText.find('\n', start);
            if (end == std::string_view::npos)
            {
                end = seedText.size();
            }
            std::string_view line = seedText.substr(start, end - start);
            start = end + 1;
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            if (line.empty() || line.front() == '#')
            {
                continue;
            }
            if (line.size() > TableLimits::kMaxLineBytes)
            {
                errors.push_back(makeError(fileName, lineNumber, "the line is too long"));
                continue;
            }
            const std::vector<std::string_view> fields = splitFields(line);
            if (fields[0] != "function")
            {
                errors.push_back(makeError(fileName, lineNumber, "a seed file holds function records only"));
                continue;
            }
            TableEntry entry;
            std::string error;
            if (!readFunction(fields, true, entry, error))
            {
                errors.push_back(makeError(fileName, lineNumber, error));
                continue;
            }
            if (!seen.insert(entry.mName).second)
            {
                errors.push_back(
                    makeError(fileName, lineNumber, std::format("duplicate function '{}' in the seed", entry.mName)));
                continue;
            }
            if (table.find(entry.mName) != nullptr || table.mEntries.size() >= TableLimits::kMaxEntries)
            {
                continue; // a derived entry wins
            }
            // keep the entries sorted, so that find() stays valid while the seed is read
            const auto where = std::lower_bound(table.mEntries.begin(), table.mEntries.end(), entry.mName,
                [](const TableEntry& existing, const std::string& name) { return existing.mName < name; });
            table.mEntries.insert(where, std::move(entry));
            ++added;
        }
        table.normalise();
        return added;
    }

    std::size_t declareAll(openeden::script::ObScriptContract& runtime, const FunctionTable& table,
        double minConfidence, std::vector<ErrorReport>& problems)
    {
        std::size_t declared = 0;
        for (const TableEntry& entry : table.mEntries)
        {
            FunctionDescriptor descriptor;
            descriptor.name = entry.mName;
            descriptor.signature = entry.mSignature;
            if (entry.mSignature != "*"
                && (entry.opcodeConfidence() < minConfidence || entry.shapeConfidence() < minConfidence))
            {
                descriptor.signature = "*";
            }
            descriptor.returns = entry.mReturns == 'f' ? ValueKind::Float : ValueKind::Integer;
            ErrorReport error;
            if (runtime.declareFunction(descriptor, error))
            {
                ++declared;
            }
            else
            {
                problems.push_back(std::move(error));
            }
        }
        return declared;
    }
}
