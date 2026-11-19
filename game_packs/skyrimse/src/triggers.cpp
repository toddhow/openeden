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
                && volume.mBounds[2] > 0.0 && finite3(volume.mPosition) && std::isfinite(volume.mRotationZ)
                && std::isfinite(volume.mRotationX) && std::isfinite(volume.mRotationY);
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
            // world to the box's frame. The reference is turned by Rx(-ax) Ry(-ay) Rz(-az) (Z first: task 0151),
            // so the point is turned back by Rz(az) Ry(ay) Rx(ax), X first (half extents, see triggers.hpp). With no
            // turn about X and Y this is the plain Z rotation of task 0274. Task 0328: MQ105SetStage20 is turned
            // about X and Y.
            const double cx = std::cos(volume.mRotationX), sx = std::sin(volume.mRotationX);
            const double cy = std::cos(volume.mRotationY), sy = std::sin(volume.mRotationY);
            const double c = std::cos(volume.mRotationZ), s = std::sin(volume.mRotationZ);
            const double y1 = dy * cx - dz * sx, z1 = dy * sx + dz * cx; // Rx
            const double x2 = dx * cy + z1 * sy, z2 = -dx * sy + z1 * cy; // Ry
            const double lx = x2 * c - y1 * s, ly = x2 * s + y1 * c; // Rz
            return std::abs(lx) <= volume.mBounds[0] && std::abs(ly) <= volume.mBounds[1]
                && std::abs(z2) <= volume.mBounds[2];
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
