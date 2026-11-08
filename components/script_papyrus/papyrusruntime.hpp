#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSRUNTIME_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSRUNTIME_H

#include <memory>

#include <openeden/script/papyrus.hpp>

namespace ScriptPapyrus
{
    /// The Papyrus runtime of the common compatibility pack (task 0093): implements openeden.script.papyrus.
    ///
    /// Ownership: the caller owns \a world and must keep it alive for as long as the runtime exists; the runtime owns
    /// everything else. Main thread only.
    ///
    /// Stage b of the task implements loadScript and link; the other operations report that they are not implemented
    /// yet (stages c to e).
    std::unique_ptr<openeden::script::PapyrusContract> createRuntime(openeden::script::PapyrusWorld& world,
        openeden::script::UnknownFunctionPolicy policy = openeden::script::UnknownFunctionPolicy::Tolerant);
}

#endif
