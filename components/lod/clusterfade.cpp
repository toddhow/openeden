#include "clusterfade.hpp"

#include <algorithm>
#include <cassert>

namespace Lod
{
    void stepTileFade(TileFade& tile, bool wanted, bool liveReady, float seconds, float fadeSeconds)
    {
        // The share of the whole fade this frame covers; no fade time means all of it at once.
        const float step = fadeSeconds > 0.f ? std::max(0.f, seconds) / fadeSeconds : 1.f;
        // A phase can hand straight on to the next within one frame (Hidden to FadingIn to Solid with no fade time),
        // never more than three times.
        for (int hops = 0; hops < 3; ++hops)
        {
            switch (tile.mPhase)
            {
                case FadePhase::Hidden:
                    tile.mFade = 0.f;
                    if (!wanted)
                        return;
                    tile.mPhase = FadePhase::FadingIn;
                    break;
                case FadePhase::FadingIn:
                    if (!wanted)
                    {
                        // The live objects never left (it was not solid), so no need to ask whether they are back.
                        tile.mPhase = FadePhase::FadingOut;
                        tile.mFade = std::max(0.f, tile.mFade - step);
                        if (tile.mFade <= 0.f)
                        {
                            tile.mFade = 0.f;
                            tile.mPhase = FadePhase::Hidden;
                        }
                        return;
                    }
                    tile.mFade = std::min(1.f, tile.mFade + step);
                    if (tile.mFade >= 1.f)
                        tile.mPhase = FadePhase::Solid;
                    return;
                case FadePhase::Solid:
                    tile.mFade = 1.f;
                    if (!wanted)
                        tile.mPhase = FadePhase::WaitingForLive;
                    return;
                case FadePhase::WaitingForLive:
                    tile.mFade = 1.f;
                    if (wanted)
                    {
                        tile.mPhase = FadePhase::Solid;
                        return;
                    }
                    if (!liveReady)
                        return;
                    tile.mPhase = FadePhase::FadingOut;
                    break;
                case FadePhase::FadingOut:
                    if (wanted)
                    {
                        tile.mPhase = FadePhase::FadingIn;
                        break;
                    }
                    if (!liveReady)
                    {
                        // The live objects lost the statics again (their chunk was rebuilt): come back up.
                        tile.mPhase = FadePhase::Reclaiming;
                        break;
                    }
                    tile.mFade = std::max(0.f, tile.mFade - step);
                    if (tile.mFade <= 0.f)
                    {
                        tile.mFade = 0.f;
                        tile.mPhase = FadePhase::Hidden;
                    }
                    return;
                case FadePhase::Reclaiming:
                    if (wanted)
                    {
                        tile.mPhase = FadePhase::FadingIn;
                        break;
                    }
                    if (liveReady)
                    {
                        tile.mPhase = FadePhase::FadingOut;
                        break;
                    }
                    tile.mFade = std::min(1.f, tile.mFade + step);
                    if (tile.mFade >= 1.f)
                        tile.mPhase = FadePhase::WaitingForLive;
                    return;
            }
        }
        // Every path above returns within three hops; reaching here would mean a loop between phases.
        assert(false && "stepTileFade did not settle");
    }

    bool tileCoversStatics(const TileFade& tile)
    {
        return tile.mPhase == FadePhase::Solid;
    }

    bool tileIsDrawn(const TileFade& tile)
    {
        return tile.mPhase != FadePhase::Hidden;
    }
}
