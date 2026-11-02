#include "tileselection.hpp"

#include <algorithm>
#include <cmath>

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

    SelectionResult selectTiles(std::span<TileState> states, const SelectionCamera& camera, float threshold) noexcept
    {
        SelectionResult result;
        const double returnBelow = static_cast<double>(hysteresisShare) * threshold;
        for (TileState& state : states)
        {
            const double distance = distanceToTile(camera, state.mBounds);
            if (distance > camera.mViewDistance || !inViewCone(camera, state.mBounds))
            {
                state.mVisible = false;
                state.mPixels = 0;
                ++result.mSkipped;
                continue;
            }
            state.mVisible = true;
            const double pixels = pixelError(camera, state.mBounds);
            state.mPixels = static_cast<float>(pixels);
            const TileChoice before = state.mChoice;
            switch (before)
            {
                case TileChoice::Unset:
                    state.mChoice = pixels <= threshold ? TileChoice::Tile : TileChoice::Live;
                    break;
                case TileChoice::Tile:
                    if (pixels > threshold)
                        state.mChoice = TileChoice::Live;
                    break;
                case TileChoice::Live:
                    if (pixels < returnBelow)
                        state.mChoice = TileChoice::Tile;
                    break;
            }
            if (before != TileChoice::Unset && before != state.mChoice)
                ++result.mChanged;
            if (state.mChoice == TileChoice::Tile)
                ++result.mTiles;
            else
                ++result.mLive;
        }
        return result;
    }
}
