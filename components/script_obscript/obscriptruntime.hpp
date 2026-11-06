#ifndef OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_OBSCRIPTRUNTIME_H
#define OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_OBSCRIPTRUNTIME_H

#include <memory>

#include <openeden/script/obscript.hpp>

namespace ScriptObscript
{
    /// The ObScript runtime of the common compatibility pack (task 0092): implements openeden.script.obscript over
    /// components/compiler and components/interpreter.
    ///
    /// Ownership: the caller owns \a world and must keep it alive for as long as the runtime exists; the runtime owns
    /// everything else. Main thread only.
    std::unique_ptr<openeden::script::ObScriptContract> createRuntime(openeden::script::ObScriptWorld& world,
        openeden::script::UnknownFunctionPolicy policy = openeden::script::UnknownFunctionPolicy::Tolerant);

    /// Numbers that are not part of the contract, for tools and tests (the gated real-data check uses them). Works on
    /// a runtime made by createRuntime; returns false for any other implementation or an unknown script.
    ///  blocks is the number of Begin ... End blocks of  script (1 for a fragment);  unknownFunctions is the
    /// number of different unknown function names the runtime has accepted so far.
    bool inspect(const openeden::script::ObScriptContract& runtime, openeden::script::ScriptId script,
        std::size_t& blocks, std::size_t& unknownFunctions);
}

#endif
