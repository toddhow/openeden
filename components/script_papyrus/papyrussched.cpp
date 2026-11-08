// Task 0093 stage d: the scheduler of the Papyrus runtime (stub: the tests of the stage are written against this).

#include "papyrusvm.hpp"

namespace ScriptPapyrus::Vm
{
    bool Runtime::completeLatent(std::uint64_t) noexcept
    {
        return false;
    }

    std::uint32_t Runtime::cancelWaiting(openeden::script::InstanceId) noexcept
    {
        return 0;
    }
}
