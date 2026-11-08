#ifndef COMPILER_CONTEXT_H_INCLUDED
#define COMPILER_CONTEXT_H_INCLUDED

#include <string>
namespace ESM
{
    class RefId;
}

namespace Compiler
{
    class Extensions;

    /// Options that change how the parsers read a script. The defaults are Morrowind's behaviour: a caller that wants
    /// another game's dialect sets them (task 0092). Every option is off in a default-constructed object.
    struct ParserOptions
    {
        /// "ref.Command" is a command called on another reference, as "ref->Command" is. Off: the dot only means a
        /// variable of another script ("ref.variable"), as in Morrowind.
        bool mDotExplicitReference = false;

        /// A local variable that Context::isId accepts (a variable that holds a reference) may stand before "->" or
        /// "." in an expression, as the name of an object may. Off: a local is always just a value, as in Morrowind.
        bool mReferenceLocals = false;

        /// "&&" and "||" join conditions: "a && b" is true when both are not zero, "a || b" when one is. "&&" binds
        /// tighter than "||", and both bind looser than the comparisons. Both sides are always evaluated. The caller
        /// must also call Scanner::setLogicalOperators(true), because the scanner does not see the context. Off:
        /// the characters are not an operator, as in Morrowind.
        bool mLogicalOperators = false;

        /// A name that Context::isId accepts, and a local variable that it accepts, may be used as a VALUE: "Set x to
        /// SomeRef", "If ( x == Player )". The value is the form id, asked for by name at run time
        /// (Generator::fetchForm, Interpreter::Context::getFormId). Forms may be compared and stored but not used in
        /// arithmetic; a name that is not a local, a global or an id is a warning and counts as zero. Off: a name that
        /// is an id may only be followed by "->" or ".", as in Morrowind.
        bool mFormValues = false;

        /// Arguments that a command table says to skip (the 'z' argument kind) give no "Extra argument" warning. Off:
        /// each skipped token is a warning, as in Morrowind. The skipped arguments of a command declared without a
        /// shape are not a mistake, so the ObScript runtime asks for this.
        bool mQuietSkippedArguments = false;

        /// A member name after "." may begin with digits ("Quest.2ndStage"): the scanner would otherwise read ".2" as
        /// a number and fail on the letters. The caller must also call Scanner::setDigitLeadingMembers(true), because
        /// the scanner does not see the context. Off: as in Morrowind, where this is a syntax error.
        bool mDigitLeadingMembers = false;

        /// A skipped argument (the 'z' argument kind) may be a dotted name ("Quest.var"), a keyword (the name of
        /// another command) or a name after the dot that is spelled like a keyword. Off: the dot ends the arguments
        /// with an error, as in Morrowind.
        bool mDottedSkippedArguments = false;

        /// "Set Quest.name to value" where the context does not know the variable is a warning and the line is
        /// skipped. Off: an error, as in Morrowind.
        bool mUnknownMembersAreWarnings = false;

        /// The most typed calls that may stand inside the arguments of typed calls ("Set a to Foo Foo Foo 1" is
        /// three deep). A call chain parses recursively, so hostile input could otherwise overflow the stack. 0: no
        /// limit, as in Morrowind.
        std::size_t mMaxCallDepth = 0;
    };

    class Context
    {
        const Extensions* mExtensions;
        ParserOptions mOptions;

    public:
        Context()
            : mExtensions(nullptr)
        {
        }

        virtual ~Context() = default;

        virtual bool canDeclareLocals() const = 0;
        ///< Is the compiler allowed to declare local variables?

        void setExtensions(const Extensions* extensions = nullptr) { mExtensions = extensions; }

        const Extensions* getExtensions() const { return mExtensions; }

        void setOptions(const ParserOptions& options) { mOptions = options; }

        const ParserOptions& getOptions() const { return mOptions; }

        virtual char getGlobalType(const std::string& name) const = 0;
        ///< 'l: long, 's': short, 'f': float, ' ': does not exist.

        virtual std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& id) const = 0;
        ///< Return type of member variable \a name in script \a id or in script of reference of
        /// \a id
        /// \return first: 'l: long, 's': short, 'f': float, ' ': does not exist.
        /// second: true: script of reference

        virtual bool isId(const ESM::RefId& name) const = 0;
        ///< Does \a name match an ID, that can be referenced?
    };
}

#endif
