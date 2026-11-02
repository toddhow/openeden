#ifndef OPENMW_COMPONENTS_LOD_CLUSTERFADE_H
#define OPENMW_COMPONENTS_LOD_CLUSTERFADE_H

// OpenEden world LOD (task 0059, stage d). The order in which an object cluster and the live objects of the same
// cells hand over to each other, as a pure state machine (no GPU, no clock): the game's MWRender::LodClusters runs one
// per drawn tile and the tests run it on its own.
//
// The rule that keeps the far field whole (no hole, no doubled coverage): the cluster fades in OVER the live objects
// (they stay until it is solid), and when the tile is no longer wanted the cluster stays SOLID until the live objects
// are back, and only then fades out over them. So at every step either the live objects or the cluster (or both,
// partly dithered) cover the cells.

namespace Lod
{
    enum class FadePhase
    {
        Hidden, // not drawn
        FadingIn, // dithering in over the live objects, which still draw the statics
        Solid, // fully in: the live objects leave out the statics of the cells it covers
        WaitingForLive, // no longer wanted but still solid, until the live objects draw the statics again
        FadingOut, // dithering out over the live objects, which draw the statics
    };

    struct TileFade
    {
        FadePhase mPhase = FadePhase::Hidden;
        float mFade = 0; // 0 to 1: the share of the dither the cluster keeps
    };

    /// Moves the state one frame. `wanted`: the selection wants this tile drawn. `liveReady`: the live objects draw the
    /// statics of all the tile's cells (or the tile has none to give back). `seconds` is the frame time; `fadeSeconds`
    /// the time from 0 to 1 (0 or less: no fade, at once). Rules:
    ///  - Hidden and wanted: FadingIn.
    ///  - FadingIn: the fade rises; at 1 and still wanted it is Solid; if no longer wanted it turns to FadingOut at the
    ///    current fade (the live objects never left).
    ///  - Solid: if no longer wanted it is WaitingForLive (the fade stays 1).
    ///  - WaitingForLive: wanted again means Solid; liveReady means FadingOut; else it waits.
    ///  - FadingOut: the fade falls; wanted again means FadingIn from the current fade; at 0 it is Hidden.
    void stepTileFade(TileFade& tile, bool wanted, bool liveReady, float seconds, float fadeSeconds);

    /// True while the live objects must leave out the statics of the tile's cells: only in Solid. (In WaitingForLive
    /// the tile still draws, but the live objects are being given their statics back.)
    bool tileCoversStatics(const TileFade& tile);

    /// True when the tile has anything to draw (every phase but Hidden).
    bool tileIsDrawn(const TileFade& tile);
}

#endif
