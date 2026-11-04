#ifndef OPENEDEN_MISC_QUIETWINDOW_H
#define OPENEDEN_MISC_QUIETWINDOW_H

namespace Misc
{
    /// The variable that test and check runs set so that nothing they start appears on the desktop: the game's window
    /// (apps/openmw/vsgengine.cpp), Eden Studio's windows (apps/opencs/view/doc/quietwindows.hpp) and the message
    /// boxes of the crash catcher and of Debug::wrapApplication, which are not shown at all.
    inline constexpr const char* quietWindowVariable = "OPENEDEN_QUIET_WINDOW";

    /// Whether a value of OPENEDEN_QUIET_WINDOW asks for quiet: unset (null), empty or exactly "0" means off, anything
    /// else on.
    bool isQuietWindowValue(const char* value);

    /// Whether the environment asks for quiet (isQuietWindowValue of OPENEDEN_QUIET_WINDOW). It reads the environment
    /// on every call, so code that must not call into the C runtime later (a crash handler) reads it once beforehand.
    bool isQuietWindowRequested();
}

#endif
