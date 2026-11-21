#ifndef VSGOPENMW_MWSTATE_SHADERWAITLABEL_H
#define VSGOPENMW_MWSTATE_SHADERWAITLABEL_H

#include <string>

namespace MWState
{
    /*
     * Task 0363, stage f: while any thread waits for a shader the Slang runtime has to compile
     * (vsgUtil::SlangWaitSettings::mObserver), the loading screen says "Compiling shaders". The label is chosen where
     * the screen is drawn (MWState::LoadingScreen::run), not written into the loading work's description: the loading
     * writes that description again every frame (PreloadCell::run), so a label put there would be gone at once.
     *
     * Thread safe: any thread that makes a pipeline may wait. A wait outside a loading screen (in play) shows nothing.
     */
    inline constexpr const char* shaderWaitLabel = "#{OMWEngine:CompilingShaders}";

    /// A waiter starts (true) or stops (false). A stop without a start is ignored.
    void shaderWait(bool begin);

    /// How many threads wait now.
    int shaderWaiters();

    /// Set by the loading screen each frame it runs: true when the frame it just prepared says "Compiling shaders"
    /// (a test aid takes its screenshots from it).
    void setShaderLabelShown(bool shown);
    bool shaderLabelShown();

    /// What the loading screen shows: the label while a thread waits and the loading is on (`description` is not empty,
    /// the screen is being drawn), else the loading's own description. Pure.
    std::string loadingScreenText(const std::string& description, int waiters);
}

#endif
