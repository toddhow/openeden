#include "opas/sensors/BalanceSensor.hpp"

#include "opas/math/Quat.hpp"
#include "opas/sensors/CentreOfMass.hpp"

#include <cmath>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <variant>

namespace opas::sensors {
namespace {

// Foot contact by rays (toddhow's choice in docs/tasks/0017, question 1): a
// ray starts this far above each foot point (so it begins inside the foot,
// which it ignores) and reaches kRayReachBelowM below it.
constexpr double kRayStartAboveM = 0.01;
constexpr double kRayReachBelowM = 0.03; // the spec's 3 cm ray
// A foot point is in contact when the surface is at most this far below it
// (the spec's 2 cm; Jolt's penetration slop is also 2 cm, so a resting foot
// can sit up to that far into the floor and still counts).
constexpr double kContactDistanceM = 0.02;
// A box has eight corners; its bottom face (in the world) is the four lowest.
constexpr std::size_t kBoxCorners = 8;
constexpr std::size_t kBoxContactCorners = 4;

bool isFinite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

math::Vec3 toMath(physics::Vec3 v)
{
    return {v.x, v.y, v.z};
}

math::Quat toMath(physics::Quat q)
{
    return math::normalized(math::Quat{q.x, q.y, q.z, q.w});
}

physics::Vec3 toPhysics(math::Vec3 v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

} // namespace

std::optional<BalanceSensor> BalanceSensor::create(const rig::Rig& rig, const rig::Profile& profile,
                                                   const motor::Ragdoll& ragdoll,
                                                   double gravityMps2, std::string* error)
{
    const auto failWith = [error](std::string message) -> std::optional<BalanceSensor> {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return std::nullopt;
    };
    if (profile.feet.empty()) {
        return failWith("the profile names no feet (profile version 2, \"feet\")");
    }
    if (ragdoll.bodies.size() != rig.bodies.size()) {
        return failWith("the ragdoll was not built from this rig");
    }
    if (!(gravityMps2 > 0.0)) {
        return failWith("gravity must be > 0");
    }

    // Same count is not enough: masses and feet are read from the rig by
    // index, so every ragdoll body must sit on the bone the rig puts it on.
    if (!(ragdoll.skeleton == rig.skeleton)) {
        return failWith("the ragdoll was not built from this rig");
    }
    for (std::size_t b = 0; b < rig.bodies.size(); ++b) {
        if (rig::findBone(rig.skeleton, rig.bodies[b].bone) != ragdoll.bodies[b].bone) {
            return failWith("the ragdoll was not built from this rig");
        }
    }

    BalanceSensor sensor;
    sensor.gravityMps2_ = gravityMps2;
    for (std::size_t b = 0; b < rig.bodies.size(); ++b) {
        sensor.bodies_.push_back(ragdoll.bodies[b].id);
        sensor.massesKg_.push_back(rig.bodies[b].massKg);
    }
    std::size_t maxContacts = 0;
    for (const std::string& name : profile.feet) {
        const std::optional<std::size_t> body = rig::findBody(rig, name);
        if (!body) {
            return failWith("foot \"" + name + "\" has no body in the rig");
        }
        Foot foot;
        foot.body = *body;
        foot.firstPoint = sensor.footPoints_.size();
        const rig::Shape& shape = rig.bodies[*body].shape;
        if (const auto* box = std::get_if<rig::BoxShape>(&shape)) {
            foot.isBox = true;
            foot.halfExtentsM = box->halfExtentsM;
            maxContacts += kBoxContactCorners;
        } else if (const auto* capsule = std::get_if<rig::CapsuleShape>(&shape)) {
            for (const double sz : {-1.0, 1.0}) {
                sensor.footPoints_.push_back(
                    {*body, {0.0, 0.0, sz * capsule->halfHeightM}, capsule->radiusM});
            }
        } else {
            sensor.footPoints_.push_back(
                {*body, {0.0, 0.0, 0.0}, std::get<rig::SphereShape>(shape).radiusM});
        }
        foot.pointCount = sensor.footPoints_.size() - foot.firstPoint;
        maxContacts += foot.pointCount;
        sensor.feet_.push_back(foot);
    }
    // Every buffer update() uses, at its largest size, so it never allocates.
    sensor.states_.resize(sensor.bodies_.size());
    sensor.positions_.resize(sensor.bodies_.size());
    sensor.velocities_.resize(sensor.bodies_.size());
    sensor.contactScratch_.reserve(maxContacts);
    sensor.reading_.contacts.reserve(maxContacts);
    sensor.reading_.supportArea.reserve(maxContacts + 1);
    return sensor;
}

void BalanceSensor::update(const physics::PhysicsWorld& world)
{
    BalanceReading& r = reading_;
    // A body that could not be read (its handle went stale) would leave last
    // step's values behind; report the whole reading as invalid instead.
    r.valid = world.readBodies(bodies_, states_) == bodies_.size();
    if (!r.valid) {
        r.contacts.clear();
        r.supportArea.clear();
        r.hasSupport = false;
        r.captureMarginM = std::numeric_limits<double>::infinity();
        r.relativeVelocityMps = {};
        return;
    }

    // Centre of mass and its velocity: mass-weighted means over the bodies.
    for (std::size_t b = 0; b < states_.size(); ++b) {
        positions_[b] = toMath(states_[b].positionM);
        velocities_[b] = toMath(states_[b].linearVelocityMps);
    }
    r.centreOfMassM = centreOfMass(massesKg_, positions_);
    r.centreOfMassVelocityMps = centreOfMass(massesKg_, velocities_);

    // Foot contact.
    r.contacts.clear();
    double surfaceHeightSum = 0.0;
    for (const Foot& foot : feet_) {
        const physics::BodyState& s = states_[foot.body];
        const math::Quat rotation = toMath(s.rotation);
        const math::Vec3 position = toMath(s.positionM);
        if (foot.isBox) {
            // All eight corners in the world, then the four lowest (ties
            // broken by corner index, so the choice is deterministic).
            std::array<math::Vec3, kBoxCorners> corners;
            std::array<std::size_t, kBoxCorners> order{};
            const math::Vec3 e = foot.halfExtentsM;
            bool finite = true;
            for (std::size_t c = 0; c < kBoxCorners; ++c) {
                const math::Vec3 local{(c & 1U) != 0 ? e.x : -e.x, (c & 2U) != 0 ? e.y : -e.y,
                                       (c & 4U) != 0 ? e.z : -e.z};
                corners[c] = math::rotate(rotation, local) + position;
                order[c] = c;
                finite = finite && isFinite(corners[c]);
            }
            // An unstable state is never a contact, and sorting NaN heights
            // would break the sort's ordering.
            if (!finite) {
                continue;
            }
            std::sort(order.begin(), order.end(), [&corners](std::size_t a, std::size_t b) {
                return corners[a].z < corners[b].z || (corners[a].z == corners[b].z && a < b);
            });
            for (std::size_t i = 0; i < kBoxContactCorners; ++i) {
                testPoint(world, foot.body, corners[order[i]], surfaceHeightSum);
            }
        } else {
            for (std::size_t p = foot.firstPoint; p < foot.firstPoint + foot.pointCount; ++p) {
                const FootPoint& fp = footPoints_[p];
                testPoint(world, foot.body,
                          math::rotate(rotation, fp.local) + position -
                              math::Vec3{0.0, 0.0, fp.dropM},
                          surfaceHeightSum);
            }
        }
    }
    finishReading(surfaceHeightSum);
}

void BalanceSensor::testPoint(const physics::PhysicsWorld& world, std::size_t body,
                              math::Vec3 point, double& surfaceHeightSum)
{
    // A point that is not finite (an unstable state) is never a contact: the
    // hull would have to sort it, and NaN breaks the ordering.
    if (!isFinite(point)) {
        return;
    }
    const math::Vec3 origin = point + math::Vec3{0.0, 0.0, kRayStartAboveM};
    const auto hit = world.castRay(
        toPhysics(origin),
        physics::Vec3{0.0f, 0.0f, static_cast<float>(-(kRayStartAboveM + kRayReachBelowM))},
        bodies_[body]);
    if (hit && point.z - static_cast<double>(hit->pointM.z) <= kContactDistanceM) {
        reading_.contacts.push_back({point.x, point.y});
        surfaceHeightSum += hit->pointM.z;
    }
}

void BalanceSensor::finishReading(double surfaceHeightSum)
{
    BalanceReading& r = reading_;
    r.hasSupport = !r.contacts.empty();
    r.supportHeightM =
        r.hasSupport ? surfaceHeightSum / static_cast<double>(r.contacts.size()) : 0.0;
    contactScratch_.assign(r.contacts.begin(), r.contacts.end());
    convexHull(contactScratch_, r.supportArea);

    r.relativeVelocityMps = r.centreOfMassVelocityMps - referenceVelocity_;

    // Capture point and its margin to the support area.
    const Point2 com{r.centreOfMassM.x, r.centreOfMassM.y};
    const Point2 velocity{r.relativeVelocityMps.x, r.relativeVelocityMps.y};
    r.capturePointM =
        capturePoint(com, velocity, r.centreOfMassM.z - r.supportHeightM, gravityMps2_);
    r.captureMarginM = r.hasSupport ? signedDistance(r.capturePointM, r.supportArea)
                                    : std::numeric_limits<double>::infinity();
}

void BalanceSensor::setReferenceVelocity(math::Vec3 refVelocityMps)
{
    referenceVelocity_ = refVelocityMps;
}

math::Vec3 BalanceSensor::referenceVelocity() const
{
    return referenceVelocity_;
}

void BalanceSensor::update(const physics::PhysicsWorld& world, math::Vec3 referenceVelocityMps)
{
    setReferenceVelocity(referenceVelocityMps);
    update(world);
}

} // namespace opas::sensors
