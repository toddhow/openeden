#ifndef OPENEDEN_COMPONENTS_UI_RML_UISCALE_H
#define OPENEDEN_COMPONENTS_UI_RML_UISCALE_H

namespace UiRml
{
    /// The range `[GUI] scaling factor` is held to for the overlay (MyGUI's own range is wider; a UI at a quarter or
    /// eight times its design size is already unusable).
    inline constexpr float kMinGuiScale = 0.25f;
    inline constexpr float kMaxGuiScale = 8.f;

    /// The density-independent pixel ratio RmlUi's context gets for the game's GUI scaling factor (task 0111): the
    /// factor itself, held to [kMinGuiScale, kMaxGuiScale]; a value that is not finite or not positive becomes 1.
    /// Markup sizes are written in `dp`, so a ratio of 2 doubles every size, the font's included.
    float dpRatioFor(float guiScalingFactor);
}

#endif
