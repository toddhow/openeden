#ifndef OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_DERIVE_H
#define OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_DERIVE_H

// Deriving the ObScript function table from the player's own data (task 0092, item 2).
//
// In plain words: every script of the game's master files exists twice, as the text the designers wrote (SCTX) and as
// compiled bytes (SCDA). The two list the same statements in the same order, so the n-th command written in the text is
// the n-th command in the bytes. Pairing them gives each command name its compiled number (opcode), how many arguments
// it took, and what kind each argument was, as often as the data uses the command. Nothing is read from any other
// source: no engine file, no other project's tables, no web page.
//
// What the compiled bytes look like (MEASURED on the Fallout New Vegas masters, see the 0092 stage log; this is the
// derivation's own reading of the format, written from the data):
//  - statements: u16 opcode, u16 length, then length bytes; opcode 0x1C is four bytes with no payload and says that
//    the next command is called on a reference; opcode 0x1D (the script name) is skipped;
//  - control statements: 0x10 Begin, 0x11 End, 0x15 Set, 0x16 If, 0x17 Else, 0x18 ElseIf, 0x19 EndIf, 0x1E Return;
//    every opcode from 0x1000 up is a command;
//  - a command's payload is a u16 argument count and the arguments, each tagged: 'r' + u16 (form), 'n' + i32 (integer),
//    'z' + f64 (float), 's' + u16 (integer variable), 'f' + u16 (float variable), 'G' + u16 (global variable); an
//    argument without a tag (a word the engine knows, a string) ends what can be read from the payload;
//  - If, ElseIf and Set carry an expression (u16 length and tokens): ' ' + 'r'/'s'/'f'/'G'/'Z' + u16, ' ' + 'n'...
//    digits, ' ' + an operator, ' ' + 'X' + u16 opcode + u16 length + payload (a call), or 'X' directly after a
//    reference ('r' u16) for "ref.Command".
// A statement the reading does not understand makes the whole script be skipped (counted, never guessed).

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "functiontable.hpp"

namespace ScriptObscript
{
    /// One script or fragment: its text and its compiled bytes.
    struct SourceUnit
    {
        std::string_view mSource;
        std::string_view mCompiled;
    };

    /// The names the data defines apart from commands, so that they are not taken for commands.
    struct DeriveEnvironment
    {
        std::set<std::string> mForms; ///< Lower-case names of forms (editor ids, "player").
        std::set<std::string> mVariables; ///< Lower-case names of script variables of any script.
    };

    /// What happened, for the report (numbers only).
    struct DeriveStats
    {
        std::size_t mUnits = 0;
        std::size_t mAligned = 0; ///< Scripts whose statements matched the text line by line.
        std::size_t mSkippedLineCount = 0; ///< Different number of text lines and compiled statements.
        std::size_t mSkippedKind = 0; ///< A line and its statement did not agree on what they are.
        std::size_t mSkippedExpression = 0; ///< An expression the reading did not understand.
        std::size_t mSkippedMalformed = 0; ///< Compiled bytes that do not walk to their end.
        std::size_t mLinesWithOtherCount = 0; ///< Lines whose command count differs between text and bytes.
        std::size_t mPairs = 0; ///< Command names paired with a compiled command.
        std::size_t mNamesConflicting = 0; ///< Names seen with more than one opcode.
        std::size_t mOpcodesSeen = 0; ///< Different opcodes in all the compiled data (aligned or not).
        std::size_t mOpcodesNamed = 0; ///< Of those, the ones that got at least one name.
        std::size_t mOpcodesWithSeveralNames = 0; ///< Opcodes with more than one name (aliases or noise).
        std::size_t mReliableShapes = 0; ///< Entries whose shape confidence is at least 0.9 and not "*".
        std::size_t mVariableArity = 0; ///< Entries given the "*" shape.
    };

    /// Pair names with opcodes over all \a units. Deterministic: the result does not depend on the order of the units.
    FunctionTable deriveFunctionTable(
        const std::vector<SourceUnit>& units, const DeriveEnvironment& environment, DeriveStats* stats = nullptr);

    namespace DeriveLimits
    {
        inline constexpr std::size_t kMaxArguments = 16; ///< More arguments than this gives the "*" shape.
        inline constexpr double kMinShapeAgreement = 0.8; ///< Below this a shape is not trusted.
    }
}

#endif
