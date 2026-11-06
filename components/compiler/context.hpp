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
