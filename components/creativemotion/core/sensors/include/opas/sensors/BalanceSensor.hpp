#pragma once

// The ragdoll's balance senses (M5a, docs/tasks/0017): centre of mass and its
// velocity, foot contact, the support area and the capture point, read from
// the physics world once per step.
//
// Ownership: a plain value owned by whoever runs the behaviors. It keeps
// body handles and buffers sized at creation, never a pointer to the world
// or the ragdoll; update() is handed both. update() allocates nothing.

#include "opas/math/Vec3.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"
#include "opas/rig/Profile.hpp"
#include "opas/rig/Rig.hpp"
#include "opas/sensors/Geometry.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace opas::sensors {

struct BalanceReading {
    // False when a body could not be read (a stale handle): then there is
    // no support and the other values are last valid ones, not current.
    bool valid = false;
    math::Vec3 centreOfMassM;
    math::Vec3 centreOfMassVelocityMps;
    math::Vec3 relativeVelocityMps; // v_CoM - v_ref (Stage C4d)
    // Foot points touching a surface (ground plane x, y), and their hull.
    std::vector<Point2> contacts;
    std::vector<Point2> supportArea; // counter-clockwise
    bool hasSupport = false;
    // Mean height of the surface under the contacts; 0 without support (the
    // capture point then uses the height above z = 0, but its margin is
    // +infinity, so nothing reads it as balanced).
    double supportHeightM = 0.0;
    Point2 capturePointM;
    // Signed distance from the capture point to the support area: negative
    // inside. +infinity without support.
    double captureMarginM = 0.0;
};

class BalanceSensor {
public:
    // The rig and profile the ragdoll was built from (validated), the built
    // ragdoll, and the magnitude of gravity (> 0). Fails, with a message in
    // *error, if the profile names no feet (profile version 2).
    static std::optional<BalanceSensor> create(const rig::Rig& rig, const rig::Profile& profile,
                                               const motor::Ragdoll& ragdoll, double gravityMps2,
                                               std::string* error = nullptr);

    // Reads the bodies and casts the foot rays. Call after a step (or from a
    // pre-step listener for the state the step starts from).
    void update(const physics::PhysicsWorld& world);
    void update(const physics::PhysicsWorld& world, math::Vec3 referenceVelocityMps);

    // Stage C4d: reference frame velocity for moving characters (e.g. walking/running).
    void setReferenceVelocity(math::Vec3 refVelocityMps);
    [[nodiscard]] math::Vec3 referenceVelocity() const;

    [[nodiscard]] const BalanceReading& reading() const { return reading_; }
    // The gravity magnitude it was created with, in m/s^2 (task 0019 uses it
    // for foot placement and gravity compensation).
    [[nodiscard]] double gravityMps2() const { return gravityMps2_; }

private:
    // A point on a non-box foot that may touch the ground: `local` in the
    // foot body's frame, lowered by `dropM` in world Z after posing (the
    // radius under a capsule end or a sphere centre).
    struct FootPoint {
        std::size_t body = 0; // index into bodies_
        math::Vec3 local;
        double dropM = 0.0;
    };

    // One foot body. A box foot keeps its half extents and tests whichever
    // four of its corners are lowest in the world (its bottom corners in any
    // orientation, so a foot rolled or upside down still finds the floor);
    // other shapes test footPoints_[firstPoint, firstPoint + pointCount).
    struct Foot {
        std::size_t body = 0; // index into bodies_
        bool isBox = false;
        math::Vec3 halfExtentsM; // box feet only
        std::size_t firstPoint = 0;
        std::size_t pointCount = 0;
    };

    BalanceSensor() = default;

    // Casts the ray under one foot point and records a contact if the surface
    // is close enough below it.
    void testPoint(const physics::PhysicsWorld& world, std::size_t body, math::Vec3 point,
                   double& surfaceHeightSum);
    // Support area, support height, capture point and margin from the
    // contacts found this update.
    void finishReading(double surfaceHeightSum);

    std::vector<physics::BodyId> bodies_;
    std::vector<double> massesKg_;
    double gravityMps2_ = 0.0;
    std::vector<Foot> feet_; // in profile order
    std::vector<FootPoint> footPoints_;
    std::vector<physics::BodyState> states_; // scratch, one per body
    std::vector<math::Vec3> positions_;      // scratch, one per body
    std::vector<math::Vec3> velocities_;     // scratch, one per body
    std::vector<Point2> contactScratch_;     // sorted in place by the hull
    BalanceReading reading_;
    math::Vec3 referenceVelocity_{};
};

} // namespace opas::sensors
