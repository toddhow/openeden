#ifndef OPENEDEN_CRASHCATCHER_FREEZERESPONSE_H
#define OPENEDEN_CRASHCATCHER_FREEZERESPONSE_H

namespace Crash
{
    /// What the Windows crash monitor does about a freeze on one pass of its loop (CrashMonitor::run).
    enum class FreezeResponse
    {
        None,
        /// The application has just frozen: show the "has frozen" box, whose Abort button ends it.
        ShowBox,
        /// The application answers again: take the box down, if one is shown.
        HideBox,
        /// Write the freeze dump and end the application, as the box's Abort button does.
        Abort,
    };

    /// \a frozen: whether the application is frozen on this pass (CrashMonitor::isAppFrozen, whose threshold this does
    /// not change); \a wasFrozen: whether it was on the previous pass; \a quiet: OPENEDEN_QUIET_WINDOW is set. Quiet,
    /// a freeze is answered as if Abort had been clicked at once: no box can be shown, and a script's timeout often
    /// cannot end a native Windows program, so nothing else would end the run.
    constexpr FreezeResponse freezeResponse(bool frozen, bool wasFrozen, bool quiet)
    {
        if (frozen && !wasFrozen)
            return quiet ? FreezeResponse::Abort : FreezeResponse::ShowBox;
        if (!frozen && wasFrozen)
            return FreezeResponse::HideBox;
        return FreezeResponse::None;
    }
}

#endif
