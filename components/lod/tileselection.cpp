#include "tileselection.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "residency.hpp"

namespace Lod
{
    namespace
    {
        double clampTo(double value, double low, double high)
        {
            return std::max(low, std::min(value, high));
        }

        // True when the bounding sphere of the tile touches the view cone: a cone around the forward vector whose
        // half angle reaches the corners of the screen. Conservative: it never hides a tile that could be on screen.
        bool inViewCone(const SelectionCamera& camera, const TileBounds& bounds)
        {
            const double half = bounds.mSide * 0.5;
            const double cx = bounds.mOriginX + half - camera.mX;
            const double cy = bounds.mOriginY + half - camera.mY;
            const double cz = (bounds.mMinHeight + bounds.mMaxHeight) * 0.5 - camera.mZ;
            const double halfHeight = (bounds.mMaxHeight - bounds.mMinHeight) * 0.5;
            const double radius = std::sqrt(2.0 * half * half + halfHeight * halfHeight);
            const double distance = std::sqrt(cx * cx + cy * cy + cz * cz);
            if (distance <= radius)
                return true; // the camera is inside the sphere
            const double tanY = std::tan(camera.mFovY * 0.5);
            const double tanCorner = tanY * std::sqrt(1.0 + camera.mAspect * camera.mAspect);
            const double halfAngle = std::atan(tanCorner);
            const double along = (cx * camera.mForwardX + cy * camera.mForwardY + cz * camera.mForwardZ) / distance;
            const double angle = std::acos(clampTo(along, -1.0, 1.0));
            // The sphere covers an angle of asin(radius / distance) around the direction of its centre.
            const double spread = std::asin(clampTo(radius / distance, 0.0, 1.0));
            return angle <= halfAngle + spread;
        }
    }

    double distanceToTile(const SelectionCamera& camera, const TileBounds& bounds) noexcept
    {
        const double side = bounds.mSide;
        const double dx = camera.mX - clampTo(camera.mX, bounds.mOriginX, bounds.mOriginX + side);
        const double dy = camera.mY - clampTo(camera.mY, bounds.mOriginY, bounds.mOriginY + side);
        const double dz = camera.mZ - clampTo(camera.mZ, bounds.mMinHeight, bounds.mMaxHeight);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    double pixelError(const SelectionCamera& camera, const TileBounds& bounds) noexcept
    {
        const double distance = std::max(distanceToTile(camera, bounds), 1.0);
        return bounds.mGeometricError * camera.mViewportHeight / (2.0 * distance * std::tan(camera.mFovY * 0.5));
    }

    namespace
    {
        // The choice of one tile that is not covered: visibility, then threshold with hysteresis.
        void evaluateTile(TileState& state, const SelectionCamera& camera, float threshold, double returnBelow,
            SelectionResult& result) noexcept
        {
            const double distance = distanceToTile(camera, state.mBounds);
            const bool inRange = distance <= camera.mViewDistance;
            const bool inCone = inRange && inViewCone(camera, state.mBounds);
            // A tile out of the view cone is still chosen when the caller asks for it (mOffViewScale): with a scaled-up
            // threshold, so that what is held for the unseen ground is coarse.
            // Only a tile with no choice yet (or one just uncovered): a choice made in view stays when the camera looks
            // away, so turning back and forth does not build coarse tiles over fine ones and back.
            const bool heldOffView = inRange && !inCone && camera.mOffViewScale > 0
                && (state.mChoice == TileChoice::Unset || state.mChoice == TileChoice::Covered);
            if (!inCone && !heldOffView)
            {
                state.mVisible = false;
                state.mPixels = 0;
                ++result.mSkipped;
                return;
            }
            state.mVisible = inCone;
            const double pixels = pixelError(camera, state.mBounds);
            state.mPixels = inCone ? static_cast<float>(pixels) : 0.f;
            if (heldOffView)
            {
                threshold = static_cast<float>(threshold * camera.mOffViewScale);
                returnBelow *= camera.mOffViewScale;
            }
            const TileChoice before = state.mChoice;
            // Nothing else draws a finest tile's square past the live terrain's reach: it is drawn whatever the error.
            const bool forced = camera.mLiveRange > 0 && !state.mHasChildren
                && !squareWithinRange(camera.mX, camera.mY, state.mBounds.mOriginX, state.mBounds.mOriginY,
                    state.mBounds.mSide, camera.mLiveRange);
            switch (before)
            {
                case TileChoice::Unset:
                case TileChoice::Covered:
                    state.mChoice = forced || pixels <= threshold ? TileChoice::Tile : TileChoice::Live;
                    break;
                case TileChoice::Tile:
                    if (!forced && pixels > threshold)
                        state.mChoice = TileChoice::Live;
                    break;
                case TileChoice::Live:
                    if (forced || pixels < returnBelow)
                        state.mChoice = TileChoice::Tile;
                    break;
            }
            if (before != TileChoice::Unset && before != TileChoice::Covered && before != state.mChoice)
                ++result.mChanged;
            if (heldOffView)
                ++result.mSkipped; // chosen, but not in view: counted as not visible
            else if (state.mChoice == TileChoice::Tile)
                ++result.mTiles;
            else
                ++result.mLive;
        }
    }

    SelectionResult selectTiles(std::span<TileState> states, const SelectionCamera& camera, float threshold) noexcept
    {
        SelectionResult result;
        const double returnBelow = static_cast<double>(hysteresisShare) * threshold;
        for (TileState& state : states)
            evaluateTile(state, camera, threshold, returnBelow, result);
        return result;
    }

    std::vector<TileId> orderTilesForTree(std::vector<TileId> ids)
    {
        std::sort(ids.begin(), ids.end(), [](const TileId& lhs, const TileId& rhs) {
            if (lhs.mLevel != rhs.mLevel)
                return lhs.mLevel > rhs.mLevel;
            return packTileKey(lhs).value_or(0) < packTileKey(rhs).value_or(0);
        });
        return ids;
    }

    std::vector<std::int32_t> nearestAncestors(std::span<const TileId> ordered)
    {
        std::map<std::uint64_t, std::int32_t> indexOf;
        for (std::size_t i = 0; i < ordered.size(); ++i)
            indexOf.emplace(packTileKey(ordered[i]).value_or(0), static_cast<std::int32_t>(i));
        std::vector<std::int32_t> result(ordered.size(), -1);
        for (std::size_t i = 0; i < ordered.size(); ++i)
        {
            TileId current = ordered[i];
            while (true)
            {
                const std::optional<TileId> parent = parentTile(current);
                if (!parent)
                    break;
                const auto found = indexOf.find(packTileKey(*parent).value_or(0));
                if (found != indexOf.end() && found->second < static_cast<std::int32_t>(i))
                {
                    result[i] = found->second;
                    break;
                }
                current = *parent;
            }
        }
        return result;
    }

    SelectionResult selectTileTree(std::span<TileState> states, std::span<const std::int32_t> parents,
        const SelectionCamera& camera, float threshold) noexcept
    {
        SelectionResult result;
        const double returnBelow = static_cast<double>(hysteresisShare) * threshold;
        for (std::size_t i = 0; i < states.size(); ++i)
        {
            TileState& state = states[i];
            const std::int32_t parent = i < parents.size() ? parents[i] : -1;
            if (parent >= 0 && static_cast<std::size_t>(parent) < i)
            {
                const TileChoice parentChoice = states[static_cast<std::size_t>(parent)].mChoice;
                if (parentChoice == TileChoice::Tile || parentChoice == TileChoice::Covered)
                {
                    state.mChoice = TileChoice::Covered;
                    state.mVisible = false;
                    state.mPixels = 0;
                    ++result.mCovered;
                    continue;
                }
            }
            evaluateTile(state, camera, threshold, returnBelow, result);
        }
        return result;
    }
}
