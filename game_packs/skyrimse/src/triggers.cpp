#include <game_skyrimse/triggers.hpp>

#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        bool finite3(const double (&v)[3])
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
    }

    bool triggerSupported(const TriggerVolume& volume)
    {
        return volume.mType == kPrimitiveSphere && std::isfinite(volume.mBounds[0]) && volume.mBounds[0] > 0.0
            && finite3(volume.mPosition);
    }

    bool volumeContains(const TriggerVolume& volume, double x, double y, double z)
    {
        if (!triggerSupported(volume) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return false;
        const double dx = x - volume.mPosition[0], dy = y - volume.mPosition[1], dz = z - volume.mPosition[2];
        const double radius = volume.mBounds[0];
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }

    bool inViewCone(double eyeX, double eyeY, double eyeZ, float heading, float pitch, double targetX, double targetY,
        double targetZ, double halfAngle)
    {
        if (!std::isfinite(eyeX) || !std::isfinite(eyeY) || !std::isfinite(eyeZ) || !std::isfinite(targetX)
            || !std::isfinite(targetY) || !std::isfinite(targetZ) || !std::isfinite(heading) || !std::isfinite(pitch)
            || !std::isfinite(halfAngle))
            return false;
        const double dx = targetX - eyeX, dy = targetY - eyeY, dz = targetZ - eyeZ;
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length < 1e-6)
            return true;
        const double cp = std::cos(static_cast<double>(pitch));
        const double vx = std::sin(static_cast<double>(heading)) * cp;
        const double vy = std::cos(static_cast<double>(heading)) * cp;
        const double vz = std::sin(static_cast<double>(pitch));
        const double cosine = (vx * dx + vy * dy + vz * dz) / length;
        return cosine >= std::cos(halfAngle);
    }

    bool TriggerTracker::add(FormId reference, const TriggerVolume& volume)
    {
        if (!triggerSupported(volume))
        {
            ++mUnsupported;
            return false;
        }
        const auto it = mVolumes.find(reference);
        if (it != mVolumes.end())
        {
            it->second.mVolume = volume;
            return true;
        }
        if (mVolumes.size() >= kMaxTriggers)
            return false;
        Entry entry;
        entry.mVolume = volume;
        mVolumes.emplace(reference, entry);
        return true;
    }

    void TriggerTracker::update(const TriggerPlace& place, double x, double y, double z, std::vector<TriggerEdge>& edges)
    {
        if (!place.mKnown)
            return;
        for (auto& [reference, entry] : mVolumes) // a std::map: ascending reference order
        {
            const bool placeMatches = entry.mVolume.mInterior
                ? (place.mInterior && place.mCell == entry.mVolume.mCell)
                : !place.mInterior;
            const bool now = placeMatches && volumeContains(entry.mVolume, x, y, z);
            if (now == entry.mInside)
                continue;
            entry.mInside = now;
            TriggerEdge edge;
            edge.mReference = reference;
            edge.mEntered = now;
            edges.push_back(edge);
        }
    }

    bool TriggerTracker::inside(FormId reference) const
    {
        const auto it = mVolumes.find(reference);
        return it != mVolumes.end() && it->second.mInside;
    }

    void TriggerTracker::resetInside()
    {
        for (auto& item : mVolumes)
            item.second.mInside = false;
    }
}
