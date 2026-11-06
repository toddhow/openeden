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
}

#endif
