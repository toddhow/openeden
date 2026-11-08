#ifndef OPENEDEN_COMPONENTS_TESTS_PAPYRUS_PAPYRUSTESTUTIL_H
#define OPENEDEN_COMPONENTS_TESTS_PAPYRUS_PAPYRUSTESTUTIL_H

// Task 0093: helpers shared by the Papyrus tests (generated scripts only; no game asset).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <components/script_papyrus/pexfile.hpp>

#include "pexassembler.hpp"

namespace PapyrusTest
{
    using Bytes = std::vector<std::uint8_t>;

    inline ScriptPapyrus::Pex::ParseResult parseBytes(const Bytes& bytes, bool strict = false)
    {
        ScriptPapyrus::Pex::ParseOptions options;
        options.mStrictSizeConvention = strict;
        return ScriptPapyrus::Pex::parse(bytes, "test.pex", options);
    }

    inline std::string messages(const ScriptPapyrus::Pex::ParseResult& result)
    {
        std::string text;
        for (const auto& report : result.mReports)
        {
            text += report.format() + "\n";
        }
        return text;
    }

    inline bool contains(const std::string& text, std::string_view part)
    {
        return text.find(part) != std::string::npos;
    }

    /// The function with this name in any state of the first object, compared without regard to case.
    inline const ScriptPapyrus::Pex::Function* findFunction(
        const ScriptPapyrus::Pex::File& file, std::string_view name, std::string_view state = {})
    {
        if (file.mObjects.empty())
        {
            return nullptr;
        }
        for (const auto& s : file.mObjects[0].mStates)
        {
            if (!state.empty() && ScriptPapyrus::Pex::lowered(file.str(s.mName)) != ScriptPapyrus::Pex::lowered(state))
            {
                continue;
            }
            for (const auto& f : s.mFunctions)
            {
                if (ScriptPapyrus::Pex::lowered(file.str(f.mName)) == ScriptPapyrus::Pex::lowered(name))
                {
                    return &f;
                }
            }
        }
        return nullptr;
    }

    /// A script that uses most of the format: parent, doc, user flag, variables of every value type, an auto property,
    /// a full property with both accessors, two states, a global function with a parameter, and (Fallout 4) a struct.
    inline PexAsm::ScriptBuilder richScript(PexAsm::Target target)
    {
        using namespace PexAsm;
        ScriptBuilder script("RichQuest");
        script.parent("Quest").doc("a documented script").userFlag("Hidden", 0);
        script.var("counter", "Int", Int(5))
            .var("label", "String", Str("hi"))
            .var("ratio", "Float", Flt(1.5f))
            .var("on", "Bool", Bool(true))
            .var("target", "Actor");
        script.autoProperty("Total", "Int", Int(7));
        script.fullProperty("Computed", "Int");
        script.getter().local("::t", "Int").assign(Id("::t"), Id("counter")).ret(Id("::t"));
        script.setter().assign(Id("counter"), Id("aValue")).ret(Nil());
        if (target == Target::Fallout4)
        {
            script.structDef("Point").member("x", "Float", Flt(2.0f)).member("y", "Float");
        }
        script.function("OnInit")
            .local("::t", "Int")
            .local("::s", "String")
            .assign(Id("::t"), Int(1))
            .iadd(Id("::t"), Id("::t"), Int(2))
            .op(Op::StrCat, { Id("::s"), Str("a"), Str("b") })
            .jmpf(Bool(false), 2)
            .callMethod("GetValue", Id("self"), Id("::t"), { Int(1), Flt(2.0f) })
            .ret(Nil());
        script.function("Helper", "Int").global().param("a", "Int").ret(Id("a"));
        script.state("Busy").function("OnInit").ret(Nil());
        return script;
    }

    inline PexAsm::Options optionsFor(PexAsm::Target target)
    {
        PexAsm::Options options;
        options.mTarget = target;
        return options;
    }
}

#endif
