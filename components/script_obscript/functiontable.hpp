#ifndef OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_FUNCTIONTABLE_H
#define OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_FUNCTIONTABLE_H

// The ObScript function table (task 0092, item 2; decision 0014's answer on the table's source).
//
// In plain words: ObScript scripts call "commands" (functions) whose names, arguments and numbers live in the game's
// engine, not in the plugin files. This table lists each command the runtime knows: its name, the number of the
// compiled command (the opcode), what its arguments look like, and how sure we are of each fact. It is built on the
// player's own machine from the player's own data (derive.hpp pairs the command names in the script source with the
// opcodes in the compiled scripts) and kept in a file in the user's folder. Nothing in it is copied from any other
// project's tables, and no table is ever committed or shipped: the repository holds only the tool that makes it.
//
// A table can also carry hand-written entries ("seed" entries): each one must state where its facts come from.
//
// Text format (version 1, one record per line, fields separated by single spaces, names in lower case, numbers in
// hexadecimal where marked, "#" starts a comment line, the writer is canonical so that parse then write gives the same
// bytes):
//   openeden.obscript.functions 1
//   function <name> <opcode:hex> <signature> <returns:l|f> <occurrences> <opcodeVotes> <shapeVotes> <explicitUses>
//            <derived|seed> [<source words...>]
//   opcode <opcode:hex> <occurrences> <minArguments> <maxArguments>
// A signature uses the contract's characters (f l s r c S, and "/" before the optional ones; always present) or "*"
// for a command whose argument shape could not be established (the runtime then skips its arguments).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/script/obscript.hpp>

namespace ScriptObscript
{
    struct TableEntry
    {
        std::string mName; ///< Lower case.
        std::uint32_t mOpcode = 0; ///< The compiled command number; 0 for a seed entry that has none.
        std::string mSignature; ///< Contract characters with a "/", or "*".
        char mReturns = 'l'; ///< 'l' integer or 'f' float.
        std::uint32_t mOccurrences = 0; ///< Places the name was paired with a compiled command.
        std::uint32_t mOpcodeVotes = 0; ///< Of those, the places that agree with mOpcode.
        std::uint32_t mShapeVotes = 0; ///< Of those, the places whose arguments agree with mSignature.
        std::uint32_t mExplicitUses = 0; ///< Places where it was called on a reference ("ref.Command").
        bool mSeeded = false;
        std::string mSource; ///< A seed entry's source words, for example "READ <where>" or "RECALLED-VERIFY <where>".

        /// How sure the opcode is, 0 to 1 (1 for a seed entry: it has no counts).
        double opcodeConfidence() const;
        /// How sure the argument shape is, 0 to 1.
        double shapeConfidence() const;
    };

    /// A compiled command number the data uses and no name could be found for (or that has names, listed apart).
    struct OpcodeUse
    {
        std::uint32_t mOpcode = 0;
        std::uint32_t mOccurrences = 0;
        std::uint32_t mMinArguments = 0;
        std::uint32_t mMaxArguments = 0;
    };

    class FunctionTable
    {
    public:
        std::vector<TableEntry> mEntries; ///< Sorted by name, no duplicates.
        std::vector<OpcodeUse> mOpcodes; ///< Every opcode seen in the compiled data, sorted by opcode.

        const TableEntry* find(std::string_view lowerName) const;

        /// The name to print for a compiled command: its most used name, or "fn_0x1234" when it has none.
        std::string nameOfOpcode(std::uint32_t opcode) const;

        /// Sort the entries and opcodes into canonical order.
        void normalise();
    };

    /// "fn_0x" and the opcode in lower-case hexadecimal without leading zeros ("fn_0x1022").
    std::string unnamedOpcodeName(std::uint32_t opcode);

    /// Canonical text (see the top of this file).
    std::string serialiseTable(const FunctionTable& table);

    struct TableParseResult
    {
        bool ok = false;
        FunctionTable table;
        std::vector<openeden::script::ErrorReport> errors; ///< Located: "file:line:1: message".
    };

    /// Read a table. Strict: a bad header, a bad record, a duplicate name or a number out of range is an error with its
    /// line, and the first error ends the read (nothing partial is returned). Hostile input is capped.
    TableParseResult parseTable(std::string_view text, std::string_view fileName);

    /// Read hand-written entries (the same "function" records, each of which must be "seed" and must carry source words
    /// that start with READ or RECALLED-VERIFY) and add those whose name the table does not have. Returns the number
    /// added; problems are located errors. Entries the table has no room for are counted in one Warning at the end
    /// (a name the table already has is not reported: the derived entry wins by design).
    std::size_t mergeSeed(FunctionTable& table, std::string_view seedText, std::string_view fileName,
        std::vector<openeden::script::ErrorReport>& errors);

    /// Declare the table's entries to a runtime (ObScriptContract::declareFunction). Entries below \a minConfidence
    /// (opcode or shape) are declared with the "*" shape, which skips their arguments. Returns the number declared;
    /// problems (a name that cannot be registered) are reported and skipped.
    std::size_t declareAll(openeden::script::ObScriptContract& runtime, const FunctionTable& table,
        double minConfidence, std::vector<openeden::script::ErrorReport>& problems);

    namespace TableLimits
    {
        inline constexpr std::size_t kMaxBytes = 8u << 20;
        inline constexpr std::size_t kMaxEntries = 16384;
        inline constexpr std::size_t kMaxOpcodes = 16384;
        inline constexpr std::size_t kMaxLineBytes = 1024;
        inline constexpr std::size_t kMaxSignatureBytes = 40;
        inline constexpr std::size_t kMaxSeedErrors = 100; ///< Errors mergeSeed lists before it stops reading.
    }
}

#endif
