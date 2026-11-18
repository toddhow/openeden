#include <game_skyrimse/triggers.hpp>

#include <cmath>

namespace game_skyrimse
{
    bool acceptsBoxTriggers(bool mainQuest, bool college, bool daedric, bool dragonborn)
    {
        return mainQuest || college || daedric || dragonborn;
    }

    namespace
    {
        bool finite3(const double (&v)[3])
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
    }

    bool triggerSupported(const TriggerVolume& volume)
    {
        if (volume.mType == kPrimitiveBox) // task 0274: only where the pack asked for boxes
            return volume.mAcceptBox && finite3(volume.mBounds) && volume.mBounds[0] > 0.0 && volume.mBounds[1] > 0.0
                && volume.mBounds[2] > 0.0 && finite3(volume.mPosition) && std::isfinite(volume.mRotationZ);
        return volume.mType == kPrimitiveSphere && std::isfinite(volume.mBounds[0]) && volume.mBounds[0] > 0.0
            && finite3(volume.mPosition);
    }

    bool volumeContains(const TriggerVolume& volume, double x, double y, double z)
    {
        if (!triggerSupported(volume) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return false;
        const double dx = x - volume.mPosition[0], dy = y - volume.mPosition[1], dz = z - volume.mPosition[2];
        if (volume.mType == kPrimitiveBox)
        {
            // world to the box's frame: the reference is turned clockwise by its rotation (the data's convention), so
            // the point is turned back by the same angle the other way (half extents, see triggers.hpp)
            const double c = std::cos(volume.mRotationZ), s = std::sin(volume.mRotationZ);
            const double lx = dx * c - dy * s, ly = dx * s + dy * c;
            return std::abs(lx) <= volume.mBounds[0] && std::abs(ly) <= volume.mBounds[1]
                && std::abs(dz) <= volume.mBounds[2];
        }
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

    void TriggerTracker::update(
        const TriggerPlace& place, double x, double y, double z, std::vector<TriggerEdge>& edges)
    {
        if (!place.mKnown)
            return;
        for (auto& [reference, entry] : mVolumes) // a std::map: ascending reference order
        {
            const bool placeMatches
                = entry.mVolume.mInterior ? (place.mInterior && place.mCell == entry.mVolume.mCell) : !place.mInterior;
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

    void TriggerTracker::updateSubject(
        FormId subject, const TriggerPlace& place, double x, double y, double z, std::vector<TriggerEdge>& edges)
    {
        if (!place.mKnown || subject == kNullFormId)
            return;
        auto found = mSubjects.find(subject);
        if (found == mSubjects.end())
        {
            if (mSubjects.size() >= kMaxSubjects)
                return;
            found = mSubjects.emplace(subject, std::set<FormId>()).first;
        }
        std::set<FormId>& inside = found->second;
        for (const auto& [reference, entry] : mVolumes) // ascending reference order
        {
            const bool placeMatches
                = entry.mVolume.mInterior ? (place.mInterior && place.mCell == entry.mVolume.mCell) : !place.mInterior;
            const bool now = placeMatches && volumeContains(entry.mVolume, x, y, z);
            const bool was = inside.count(reference) != 0;
            if (now == was)
                continue;
            if (now)
                inside.insert(reference);
            else
                inside.erase(reference);
            TriggerEdge edge;
            edge.mReference = reference;
            edge.mEntered = now;
            edge.mSubject = subject;
            edges.push_back(edge);
        }
    }

    void TriggerTracker::forgetSubject(FormId subject)
    {
        mSubjects.erase(subject);
    }

    bool TriggerTracker::subjectInside(FormId subject, FormId reference) const
    {
        const auto found = mSubjects.find(subject);
        return found != mSubjects.end() && found->second.count(reference) != 0;
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
