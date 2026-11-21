#include "shaderwaitlabel.hpp"

#include <atomic>

namespace MWState
{
    namespace
    {
        std::atomic<int> sWaiters{ 0 };
        std::atomic<bool> sShown{ false };
    }

    void setShaderLabelShown(bool shown)
    {
        sShown = shown;
    }

    bool shaderLabelShown()
    {
        return sShown;
    }

    void shaderWait(bool begin)
    {
        if (begin)
        {
            ++sWaiters;
            return;
        }
        int now = sWaiters.load();
        while (now > 0 && !sWaiters.compare_exchange_weak(now, now - 1))
        {
        }
    }

    int shaderWaiters()
    {
        return sWaiters.load();
    }

    std::string loadingScreenText(const std::string& description, int waiters)
    {
        return waiters > 0 ? std::string(shaderWaitLabel) : description;
    }
}
