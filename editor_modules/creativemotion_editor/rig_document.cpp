#include "documents.hpp"

#include "convert.hpp"
#include "property_commands.hpp"

#include <opas/rig/Rig.hpp>
#include <opas/rig/RigFile.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

namespace creativemotion_editor
{
    using namespace openeden::editor;

    namespace
    {
        constexpr double kPi = std::numbers::pi;
        /// The length of the joint limit drawings, in meters (task 0032).
        constexpr double kLimitLengthM = 0.1;
        constexpr double kTwistArcRadiusFactor = 0.25;
        constexpr int kCircleSegments = 24;
        constexpr int kHalfCircleSegments = 12;
        constexpr int kTwistArcSegments = 16;
        constexpr int kConeSpokeStep = 6; // spokes at rim points 0, 6, 12, 18
        // Merge IDs of rig properties: kMergeBase + the property's index, so edits of one property merge and no two
        // properties do.
        constexpr int kMergeBase = 0x2000;

        const Colour kGrey{ 0.8f, 0.8f, 0.8f, 1.0f };
        const Colour kOrange{ 1.0f, 0.6f, 0.1f, 1.0f };
        const Colour kCyan{ 0.1f, 0.9f, 0.9f, 1.0f };
        const Colour kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
        /// The selected joint's limits are drawn in this colour (API 1.2 selection, task 0091).
        const Colour kHighlight{ 1.0f, 1.0f, 0.2f, 1.0f };

        using V = opas::math::Vec3;

        /// One joint number the inspector edits.
        struct JointField
        {
            const char* mName;
            double opas::rig::Joint::*mMember;
        };

        const JointField kJointFields[] = {
            { "planeSwingMinRad", &opas::rig::Joint::planeSwingMinRad },
            { "planeSwingMaxRad", &opas::rig::Joint::planeSwingMaxRad },
            { "normalSwingMinRad", &opas::rig::Joint::normalSwingMinRad },
            { "normalSwingMaxRad", &opas::rig::Joint::normalSwingMaxRad },
            { "twistMinRad", &opas::rig::Joint::twistMinRad },
            { "twistMaxRad", &opas::rig::Joint::twistMaxRad },
        };
        constexpr int kJointFieldCount = 6;

        V add(V a, V b)
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }
        V scaled(V v, double s)
        {
            return { v.x * s, v.y * s, v.z * s };
        }

        void line(OverlaySink& sink, const V& a, const V& b, const Colour& colour)
        {
            sink.line(toEditor(a), toEditor(b), colour);
        }

        /// A rig's drawing: shapes and limits read the document's current rig.
        class RigShapesGizmo final : public Gizmo
        {
        public:
            explicit RigShapesGizmo(const opas::rig::Rig& rig)
                : mRig(rig)
            {
            }

            std::string_view id() const override { return "creativemotion.rig.shapes"; }

            void draw(OverlaySink& sink) const override
            {
                const std::vector<opas::math::Transform> world = opas::rig::computeWorldRest(mRig.skeleton);
                for (const opas::rig::Body& body : mRig.bodies)
                {
                    const auto bone = opas::rig::findBone(mRig.skeleton, body.bone);
                    if (!bone)
                        continue;
                    const opas::math::Transform t = world[*bone] * body.local;
                    const auto at = [&t](V p) { return opas::math::transformPoint(t, p); };
                    const auto seg = [&](V a, V b) { line(sink, at(a), at(b), kGrey); };
                    if (const auto* box = std::get_if<opas::rig::BoxShape>(&body.shape))
                        drawBox(box->halfExtentsM, seg);
                    else if (const auto* sphere = std::get_if<opas::rig::SphereShape>(&body.shape))
                        drawSphere(sphere->radiusM, seg);
                    else if (const auto* capsule = std::get_if<opas::rig::CapsuleShape>(&body.shape))
                        drawCapsule(capsule->halfHeightM, capsule->radiusM, seg);
                }
            }

        private:
            template <class Seg>
            static void drawBox(V h, const Seg& seg)
            {
                V c[8];
                for (int i = 0; i < 8; ++i)
                    c[i] = { (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z };
                for (int i = 0; i < 8; ++i)
                    for (int bit = 1; bit <= 4; bit <<= 1)
                        if ((i & bit) == 0)
                            seg(c[i], c[i | bit]);
            }

            template <class Seg>
            static void circle(double r, int plane, const Seg& seg)
            {
                // plane 0: XY, 1: YZ, 2: XZ
                for (int k = 0; k < kCircleSegments; ++k)
                {
                    const double a0 = 2 * kPi * k / kCircleSegments;
                    const double a1 = 2 * kPi * (k + 1) / kCircleSegments;
                    const auto p = [&](double a) -> V {
                        const double u = r * std::cos(a);
                        const double v = r * std::sin(a);
                        return plane == 0 ? V{ u, v, 0 } : plane == 1 ? V{ 0, u, v } : V{ u, 0, v };
                    };
                    seg(p(a0), p(a1));
                }
            }

            template <class Seg>
            static void drawSphere(double r, const Seg& seg)
            {
                circle(r, 0, seg);
                circle(r, 1, seg);
                circle(r, 2, seg);
            }

            template <class Seg>
            static void drawCapsule(double h, double r, const Seg& seg)
            {
                for (const double z : { h, -h })
                    for (int k = 0; k < kCircleSegments; ++k)
                    {
                        const double a0 = 2 * kPi * k / kCircleSegments;
                        const double a1 = 2 * kPi * (k + 1) / kCircleSegments;
                        seg({ r * std::cos(a0), r * std::sin(a0), z }, { r * std::cos(a1), r * std::sin(a1), z });
                    }
                for (const V& side : { V{ r, 0, 0 }, V{ -r, 0, 0 }, V{ 0, r, 0 }, V{ 0, -r, 0 } })
                    seg({ side.x, side.y, -h }, { side.x, side.y, h });
                for (const double sign : { 1.0, -1.0 })
                    for (int plane = 0; plane < 2; ++plane)
                        for (int k = 0; k < kHalfCircleSegments; ++k)
                        {
                            const double a0 = kPi * k / kHalfCircleSegments;
                            const double a1 = kPi * (k + 1) / kHalfCircleSegments;
                            const auto p = [&](double a) -> V {
                                const double u = r * std::cos(a);
                                const double z = sign * (h + r * std::sin(a));
                                return plane == 0 ? V{ u, 0, z } : V{ 0, u, z };
                            };
                            seg(p(a0), p(a1));
                        }
            }

            const opas::rig::Rig& mRig;
        };

        class RigLimitsGizmo final : public Gizmo
        {
        public:
            RigLimitsGizmo(const opas::rig::Rig& rig, const std::string& selectedJoint)
                : mRig(rig)
                , mSelected(selectedJoint)
            {
            }

            std::string_view id() const override { return "creativemotion.rig.limits"; }

            void draw(OverlaySink& sink) const override
            {
                const std::vector<opas::math::Transform> world = opas::rig::computeWorldRest(mRig.skeleton);
                for (const opas::rig::Joint& joint : mRig.joints)
                {
                    const auto bone = opas::rig::findBone(mRig.skeleton, joint.bone);
                    if (!bone)
                        continue;
                    const bool selected = joint.bone == mSelected;
                    const auto tinted = [selected](const Colour& colour) { return selected ? kHighlight : colour; };
                    const opas::math::Transform& w = world[*bone];
                    const V origin = w.translation;
                    const V x = joint.twistAxis;
                    const V z = joint.planeAxis;
                    const V y = opas::math::cross(z, x);
                    // Joint coordinates to model space.
                    const auto direction = [&](double dx, double dy, double dz) -> V {
                        return opas::math::rotate(w.rotation, add(add(scaled(x, dx), scaled(y, dy)), scaled(z, dz)));
                    };
                    const auto swingDir = [&](double p, double n) {
                        return direction(std::cos(p) * std::cos(n), std::cos(p) * std::sin(n), -std::sin(p));
                    };
                    const auto pointAt = [&](V dir) { return add(origin, scaled(dir, kLimitLengthM)); };

                    line(sink, origin, pointAt(direction(1, 0, 0)), tinted(kWhite));

                    if (joint.swing == opas::rig::SwingShape::Cone)
                    {
                        const double planeHalf = joint.planeSwingMaxRad;
                        const double normalHalf = joint.normalSwingMaxRad;
                        if (planeHalf != 0.0 || normalHalf != 0.0)
                        {
                            V rim[kCircleSegments];
                            for (int k = 0; k < kCircleSegments; ++k)
                            {
                                const double t = 2 * kPi * k / kCircleSegments;
                                rim[k] = pointAt(swingDir(planeHalf * std::cos(t), normalHalf * std::sin(t)));
                            }
                            for (int k = 0; k < kCircleSegments; ++k)
                                line(sink, rim[k], rim[(k + 1) % kCircleSegments], tinted(kOrange));
                            for (int k = 0; k < kCircleSegments; k += kConeSpokeStep)
                                line(sink, origin, rim[k], tinted(kOrange));
                        }
                    }
                    else
                    {
                        const V corner[4] = {
                            pointAt(swingDir(joint.planeSwingMinRad, joint.normalSwingMinRad)),
                            pointAt(swingDir(joint.planeSwingMaxRad, joint.normalSwingMinRad)),
                            pointAt(swingDir(joint.planeSwingMaxRad, joint.normalSwingMaxRad)),
                            pointAt(swingDir(joint.planeSwingMinRad, joint.normalSwingMaxRad)),
                        };
                        for (int i = 0; i < 4; ++i)
                            line(sink, corner[i], corner[(i + 1) % 4], tinted(kOrange));
                        for (int i = 0; i < 4; ++i)
                            line(sink, origin, corner[i], tinted(kOrange));
                    }

                    // The twist range as an arc around the twist axis.
                    V previous{};
                    for (int i = 0; i <= kTwistArcSegments; ++i)
                    {
                        const double a
                            = joint.twistMinRad + (joint.twistMaxRad - joint.twistMinRad) * i / kTwistArcSegments;
                        const V p = add(origin,
                            scaled(direction(0, std::cos(a), std::sin(a)), kTwistArcRadiusFactor * kLimitLengthM));
                        if (i > 0)
                            line(sink, previous, p, tinted(kCyan));
                        previous = p;
                    }
                }
            }

        private:
            const opas::rig::Rig& mRig;
            const std::string& mSelected;
        };

        class RigDocument final : public AssetDocument, public PropertySource, public JointSelectable
        {
        public:
            RigDocument(std::filesystem::path path, opas::rig::Rig rig)
                : mPath(std::move(path))
                , mAssetId(mPath.string())
                , mRig(std::move(rig))
                , mShapes(mRig)
                , mLimits(mRig, mSelectedJoint)
            {
            }

            std::vector<std::string> jointNames() const override
            {
                std::vector<std::string> names;
                for (const opas::rig::Joint& joint : mRig.joints)
                    names.push_back(joint.bone);
                return names;
            }
            void setSelectedJoint(const std::string& bone) override
            {
                const bool known = std::any_of(
                    mRig.joints.begin(), mRig.joints.end(), [&](const opas::rig::Joint& j) { return j.bone == bone; });
                mSelectedJoint = known ? bone : std::string();
            }
            const std::string& selectedJoint() const override { return mSelectedJoint; }

            std::string_view assetId() const override { return mAssetId; }
            PropertySource& properties() override { return *this; }
            std::vector<Gizmo*> gizmos() override { return { &mShapes, &mLimits }; }
            bool isDirty() const override { return mDirty; }

            bool save(std::string& error) override
            {
                const std::string text = opas::rig::writeRig(mRig);
                std::ofstream out(mPath, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    error = "cannot write '" + mPath.string() + "'";
                    return false;
                }
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.close();
                if (!out)
                {
                    error = "writing '" + mPath.string() + "' failed";
                    return false;
                }
                error.clear();
                mDirty = false;
                return true;
            }

            std::vector<PropertyDesc> describe() const override
            {
                std::vector<PropertyDesc> descs;
                const auto readOnly = [&](const std::string& id, const std::string& label, PropertyType type,
                                          const std::string& group) {
                    PropertyDesc d;
                    d.id = id;
                    d.label = label;
                    d.type = type;
                    d.readOnly = true;
                    d.group = group;
                    descs.push_back(std::move(d));
                };
                readOnly("rig.name", "Name", PropertyType::String, "Rig");
                readOnly("rig.bones", "Bones", PropertyType::Int, "Rig");
                readOnly("rig.bodies", "Bodies", PropertyType::Int, "Rig");
                readOnly("rig.joints", "Joints", PropertyType::Int, "Rig");
                for (const opas::rig::Joint& joint : mRig.joints)
                {
                    // With a joint selected the panel shows only that joint (API 1.2 selection).
                    if (!mSelectedJoint.empty() && joint.bone != mSelectedJoint)
                        continue;
                    readOnly("joint." + joint.bone + ".swing", "Swing", PropertyType::String, joint.bone);
                    for (const JointField& field : kJointFields)
                    {
                        PropertyDesc d;
                        d.id = "joint." + joint.bone + "." + field.mName;
                        d.label = field.mName;
                        d.type = PropertyType::Float;
                        d.unit = "rad";
                        d.min = -kPi;
                        d.max = kPi;
                        d.step = 0.01;
                        d.group = joint.bone;
                        d.tooltip
                            = "Limit of the joint on this bone, in radians from the rest pose (docs/formats/rig.md)";
                        descs.push_back(std::move(d));
                    }
                }
                return descs;
            }

            PropertyValue get(std::string_view id) const override
            {
                if (id == "rig.name")
                    return mRig.name;
                if (id == "rig.bones")
                    return static_cast<std::int64_t>(mRig.skeleton.bones.size());
                if (id == "rig.bodies")
                    return static_cast<std::int64_t>(mRig.bodies.size());
                if (id == "rig.joints")
                    return static_cast<std::int64_t>(mRig.joints.size());
                const Located found = locate(id);
                if (found.mJoint < 0)
                    return std::string();
                const opas::rig::Joint& joint = mRig.joints[found.mJoint];
                if (found.mField < 0)
                    return std::string(joint.swing == opas::rig::SwingShape::Cone ? "cone" : "pyramid");
                return joint.*kJointFields[found.mField].mMember;
            }

            std::unique_ptr<Command> makeSetCommand(std::string_view id, const PropertyValue& value) override
            {
                const double* number = std::get_if<double>(&value);
                if (number == nullptr || !std::isfinite(*number) || *number < -kPi || *number > kPi)
                    return nullptr;
                const Located found = locate(id);
                if (found.mJoint < 0 || found.mField < 0)
                    return nullptr;
                // The rig that results must be valid (a cone stays symmetric, a pyramid's ranges keep 0).
                opas::rig::Rig candidate = mRig;
                candidate.joints[found.mJoint].*kJointFields[found.mField].mMember = *number;
                if (!opas::rig::validateRig(candidate).empty())
                    return nullptr;
                return std::make_unique<SetNumberCommand>("Change " + std::string(id),
                    &(mRig.joints[found.mJoint].*kJointFields[found.mField].mMember), &mDirty, *number,
                    kMergeBase + found.mJoint * kJointFieldCount + found.mField);
            }

        private:
            struct Located
            {
                int mJoint = -1;
                int mField = -1; // -1: the read-only swing property
            };

            Located locate(std::string_view id) const
            {
                constexpr std::string_view prefix = "joint.";
                if (id.substr(0, prefix.size()) != prefix)
                    return {};
                for (std::size_t j = 0; j < mRig.joints.size(); ++j)
                {
                    const std::string head = "joint." + mRig.joints[j].bone + ".";
                    if (id.substr(0, head.size()) != head)
                        continue;
                    const std::string_view rest = id.substr(head.size());
                    if (rest == "swing")
                        return { static_cast<int>(j), -1 };
                    for (int f = 0; f < kJointFieldCount; ++f)
                        if (rest == kJointFields[f].mName)
                            return { static_cast<int>(j), f };
                }
                return {};
            }

            std::filesystem::path mPath;
            std::string mAssetId;
            opas::rig::Rig mRig;
            std::string mSelectedJoint; // before mLimits, which holds a reference to it
            RigShapesGizmo mShapes;
            RigLimitsGizmo mLimits;
            bool mDirty = false;
        };
    }

    std::unique_ptr<AssetDocument> openRig(const std::filesystem::path& path, std::string& error)
    {
        const opas::rig::RigLoadResult result = opas::rig::loadRigFile(path);
        if (!result.ok())
        {
            error = result.error.toString();
            if (error.empty())
                error = "cannot read '" + path.string() + "'";
            return nullptr;
        }
        error.clear();
        return std::make_unique<RigDocument>(path, *result.value);
    }
}
