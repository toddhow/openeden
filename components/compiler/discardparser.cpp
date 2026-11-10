#include "discardparser.hpp"

#include "context.hpp"
#include "scanner.hpp"

namespace Compiler
{
    DiscardParser::DiscardParser(ErrorHandler& errorHandler, const Context& context)
        : Parser(errorHandler, context)
        , mState(StartState)
    {
    }

    bool DiscardParser::parseInt(int value, const TokenLoc& loc, Scanner& scanner)
    {
        if (mState == StartState || mState == MinusState)
        {
            if (isEmpty())
                mTokenLoc = loc;

            start();
            return false;
        }

        if (mState == NameState)
        {
            scanner.putbackInt(value, loc);
            return false;
        }

        return Parser::parseInt(value, loc, scanner);
    }

    bool DiscardParser::parseFloat(float value, const TokenLoc& loc, Scanner& scanner)
    {
        if (mState == StartState || mState == MinusState)
        {
            if (isEmpty())
                mTokenLoc = loc;

            start();
            return false;
        }

        if (mState == NameState)
        {
            scanner.putbackFloat(value, loc);
            return false;
        }

        return Parser::parseFloat(value, loc, scanner);
    }

    bool DiscardParser::parseName(const std::string& name, const TokenLoc& loc, Scanner& scanner)
    {
        if (mState == StartState)
        {
            if (isEmpty())
                mTokenLoc = loc;

            start();

            // Task 0092: a name may be followed by ".member", which is part of the same argument.
            if (getContext().getOptions().mDottedSkippedArguments)
            {
                mState = NameState;
                return true;
            }
            return false;
        }

        if (mState == NameState)
        {
            scanner.putbackName(name, loc);
            return false;
        }

        if (mState == MemberState)
        {
            mState = DoneState;
            return false;
        }

        return Parser::parseName(name, loc, scanner);
    }

    bool DiscardParser::parseKeyword(int keyword, const TokenLoc& loc, Scanner& scanner)
    {
        if (getContext().getOptions().mDottedSkippedArguments)
        {
            // The name of another command (an extension keyword has a negative number) is an argument too, and
            // after a dot any word is the name of a member.
            if (mState == StartState && keyword < 0)
            {
                if (isEmpty())
                    mTokenLoc = loc;

                start();
                mState = NameState;
                return true;
            }

            if (mState == NameState)
            {
                scanner.putbackKeyword(keyword, loc);
                return false;
            }

            if (mState == MemberState)
            {
                mState = DoneState;
                return false;
            }
        }

        return Parser::parseKeyword(keyword, loc, scanner);
    }

    bool DiscardParser::parseSpecial(int code, const TokenLoc& loc, Scanner& scanner)
    {
        if (mState == NameState)
        {
            if (code == Scanner::S_member || code == Scanner::S_ref)
            {
                mState = MemberState;
                return true;
            }

            scanner.putbackSpecial(code, loc);
            return false;
        }

        if (code == Scanner::S_minus && mState == StartState)
        {
            // Task 0092: "Cmd - x" is a subtraction, not a negative argument; the minus goes back for the caller
            if (getContext().getOptions().mBinaryMinusAfterSkippedArguments && scanner.nameOrBracketFollows())
            {
                scanner.putbackSpecial(code, loc);
                return false;
            }

            if (isEmpty())
                mTokenLoc = loc;

            start();

            mState = MinusState;
            return true;
        }

        return Parser::parseSpecial(code, loc, scanner);
    }

    void DiscardParser::parseEOF(Scanner& scanner)
    {
        if (mState != NameState)
            Parser::parseEOF(scanner);
    }

    void DiscardParser::reset()
    {
        mState = StartState;
        mTokenLoc = TokenLoc();
        Parser::reset();
    }

    const TokenLoc& DiscardParser::getTokenLoc() const
    {
        return mTokenLoc;
    }
}
