#include "sandbox.hpp"

#include <opas/math/Transform.hpp>
#include <opas/rig/RigFile.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>
#include <variant>

namespace creativemotion_editor
{
    using namespace openeden::editor;

    namespace
    {
        constexpr double kPi = std::numbers::pi;
        constexpr int kCircleSegments = 24;
        constexpr int kHalfCircleSegments = 12;
        /// The ground grid: lines every metre out to this distance each way (the ground itself is much larger).
        constexpr int kGridHalfCells = 2;
        /// The push arrow: shaft length, head length and head half width, meters.
        constexpr double kArrowLengthM = 0.5;
        constexpr double kArrowHeadLengthM = 0.1;
        constexpr double kArrowHeadHalfWidthM = 0.04;

        const Colour kPending{ 1.0f, 0.15f, 0.15f, 1.0f };
        const Colour kApplied{ 0.2f, 1.0f, 0.2f, 1.0f };
        const Colour kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
        const Colour kGround{ 0.5f, 0.5f, 0.5f, 1.0f };

        Vec3 add(const Vec3& a, const Vec3& b)
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }
        Vec3 scaled(const Vec3& v, double s)
        {
            return { v.x * s, v.y * s, v.z * s };
        }
        Vec3 cross(const Vec3& a, const Vec3& b)
        {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        }
        double length(const Vec3& v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
        bool finite(const Vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        /// v rotated by the unit quaternion of \a pose.
        Vec3 rotate(const SandboxBodyPose& pose, const Vec3& v)
        {
            const Vec3 q{ pose.qx, pose.qy, pose.qz };
            const Vec3 t = scaled(cross(q, v), 2.0);
            return add(add(v, scaled(t, pose.qw)), cross(q, t));
        }

        template <class Seg>
        void drawBox(const opas::math::Vec3& h, const Seg& seg)
        {
            Vec3 c[8];
            for (int i = 0; i < 8; ++i)
                c[i] = { (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z };
            for (int i = 0; i < 8; ++i)
                for (int bit = 1; bit <= 4; bit <<= 1)
                    if ((i & bit) == 0)
                        seg(c[i], c[i | bit]);
        }

        template <class Seg>
        void drawSphere(double r, const Seg& seg)
        {
            for (int plane = 0; plane < 3; ++plane)
                for (int k = 0; k < kCircleSegments; ++k)
                {
                    const auto p = [&](int index) -> Vec3 {
                        const double a = 2 * kPi * index / kCircleSegments;
                        const double u = r * std::cos(a);
                        const double v = r * std::sin(a);
                        return plane == 0 ? Vec3{ u, v, 0 } : plane == 1 ? Vec3{ 0, u, v } : Vec3{ u, 0, v };
                    };
                    seg(p(k), p(k + 1));
                }
        }

        /// A capsule's axis is the body's local Z (opas::motor::buildRagdoll).
        template <class Seg>
        void drawCapsule(double h, double r, const Seg& seg)
        {
            for (const double z : { h, -h })
                for (int k = 0; k < kCircleSegments; ++k)
                {
                    const double a0 = 2 * kPi * k / kCircleSegments;
                    const double a1 = 2 * kPi * (k + 1) / kCircleSegments;
                    seg({ r * std::cos(a0), r * std::sin(a0), z }, { r * std::cos(a1), r * std::sin(a1), z });
                }
            for (const Vec3& side : { Vec3{ r, 0, 0 }, Vec3{ -r, 0, 0 }, Vec3{ 0, r, 0 }, Vec3{ 0, -r, 0 } })
                seg({ side.x, side.y, -h }, { side.x, side.y, h });
            for (const double sign : { 1.0, -1.0 })
                for (int plane = 0; plane < 2; ++plane)
                    for (int k = 0; k < kHalfCircleSegments; ++k)
                    {
                        const auto p = [&](int index) -> Vec3 {
                            const double a = kPi * index / kHalfCircleSegments;
                            const double u = r * std::cos(a);
                            const double z = sign * (h + r * std::sin(a));
                            return plane == 0 ? Vec3{ u, 0, z } : Vec3{ 0, u, z };
                        };
                        seg(p(k), p(k + 1));
                    }
        }

        /// An upper bound of how far a shape reaches from its centre, meters.
        double reach(const opas::rig::Shape& shape)
        {
            if (const auto* box = std::get_if<opas::rig::BoxShape>(&shape))
                return std::sqrt(box->halfExtentsM.x * box->halfExtentsM.x + box->halfExtentsM.y * box->halfExtentsM.y
                    + box->halfExtentsM.z * box->halfExtentsM.z);
            if (const auto* sphere = std::get_if<opas::rig::SphereShape>(&shape))
                return sphere->radiusM;
            if (const auto* capsule = std::get_if<opas::rig::CapsuleShape>(&shape))
                return capsule->halfHeightM + capsule->radiusM;
            return 0.0;
        }
    }

    double dropOffsetM(const opas::rig::Rig& rig)
    {
        if (rig.bodies.empty())
            return 0.0;
        const std::vector<opas::math::Transform> world = opas::rig::computeWorldRest(rig.skeleton);
        double lowest = 0.0;
        bool any = false;
        for (const opas::rig::Body& body : rig.bodies)
        {
            const auto bone = opas::rig::findBone(rig.skeleton, body.bone);
            if (!bone)
                continue;
            const opas::math::Transform t = world[*bone] * body.local;
            const double bottom = t.translation.z - reach(body.shape);
            lowest = any ? std::min(lowest, bottom) : bottom;
            any = true;
        }
        return any ? kDropClearanceM - lowest : 0.0;
    }

    struct SandboxController::Impl
    {
        explicit Impl(EditorContext& context)
            : mContext(context)
        {
        }

        struct LastPush
        {
            std::string mBone;
            Vec3 mImpulse;
            Vec3 mPoint;
        };

        SimulationSandbox* sandbox() const { return mContext.simulationSandbox(); }

        std::vector<std::string> bodyBones() const
        {
            std::vector<std::string> bones;
            if (mRig.has_value())
                for (const opas::rig::Body& body : mRig->bodies)
                    bones.push_back(body.bone);
            return bones;
        }

        std::string statusText() const
        {
            const SimulationSandbox* sb = sandbox();
            if (sb == nullptr || !sb->built())
                return "not built";
            const std::size_t pending = sb->pendingPushes();
            return "built: " + std::to_string(sb->bodyNames().size()) + " bodies, " + std::to_string(pending)
                + (pending == 1 ? " push waiting" : " pushes waiting");
        }

        /// Builds when nothing is built or a file was opened since. \return whether the sandbox is built.
        bool ensureBuilt()
        {
            SimulationSandbox* sb = sandbox();
            if (sb == nullptr)
            {
                mContext.log(LogLevel::Warning, "Push sandbox: this Eden Studio has no simulation sandbox");
                return false;
            }
            if (!mRig.has_value() || mProfilePath.empty())
            {
                mContext.log(LogLevel::Warning, "Push sandbox: open a rig and a profile first");
                return false;
            }
            if (!mNeedsBuild && sb->built())
                return true;
            std::string error;
            mLast.reset();
            if (!sb->build(mRigPath, mProfilePath, Vec3{ 0.0, 0.0, dropOffsetM(*mRig) }, 0.0, error))
            {
                mContext.log(LogLevel::Error, "Push sandbox: " + error);
                return false;
            }
            mNeedsBuild = false;
            return true;
        }

        EditorContext& mContext;
        std::filesystem::path mRigPath;
        std::filesystem::path mProfilePath;
        std::optional<opas::rig::Rig> mRig;
        bool mNeedsBuild = true;
        std::string mBone;
        Vec3 mDirection{ 1.0, 0.0, 0.0 };
        double mMagnitudeNs = kDefaultPushNs;
        std::optional<LastPush> mLast;
    };

    /// The panel's fields. Edits are commands so that Studio's form works as for every other source; a push itself is
    /// never a command.
    class SandboxController::Properties final : public PropertySource
    {
    public:
        explicit Properties(Impl& impl)
            : mImpl(impl)
        {
        }

        std::vector<PropertyDesc> describe() const override
        {
            PropertyDesc bone;
            bone.id = kSandboxBoneId;
            bone.label = "Bone";
            bone.type = PropertyType::Choice;
            bone.choices = mImpl.bodyBones();
            bone.tooltip = "The body to push. A joint selected in the viewport picks its body.";
            bone.group = "Push";

            PropertyDesc direction;
            direction.id = kSandboxDirectionId;
            direction.label = "Direction";
            direction.type = PropertyType::Vec3;
            direction.tooltip = "World direction (Z up); only its direction counts, not its length.";
            direction.group = "Push";

            PropertyDesc magnitude;
            magnitude.id = kSandboxMagnitudeId;
            magnitude.label = "Impulse";
            magnitude.type = PropertyType::Float;
            magnitude.unit = "N*s";
            magnitude.min = 0.0;
            magnitude.max = kMaxPushNs;
            magnitude.step = 5.0;
            magnitude.tooltip = "Sandbox > Push applies it at the body's centre just before the next step.";
            magnitude.group = "Push";

            PropertyDesc status;
            status.id = kSandboxStatusId;
            status.label = "Sandbox";
            status.type = PropertyType::String;
            status.readOnly = true;
            status.group = "Push";

            return { bone, direction, magnitude, status };
        }

        PropertyValue get(std::string_view id) const override
        {
            if (id == kSandboxBoneId)
                return mImpl.mBone;
            if (id == kSandboxDirectionId)
                return mImpl.mDirection;
            if (id == kSandboxMagnitudeId)
                return mImpl.mMagnitudeNs;
            if (id == kSandboxStatusId)
                return mImpl.statusText();
            return std::string();
        }

        std::unique_ptr<Command> makeSetCommand(std::string_view id, const PropertyValue& value) override
        {
            if (id == kSandboxBoneId)
            {
                const auto* text = std::get_if<std::string>(&value);
                const std::vector<std::string> bones = mImpl.bodyBones();
                if (text == nullptr || std::find(bones.begin(), bones.end(), *text) == bones.end())
                    return nullptr;
                return std::make_unique<SetCommand<std::string>>(mImpl.mBone, *text, "Set push bone");
            }
            if (id == kSandboxDirectionId)
            {
                const auto* vec = std::get_if<Vec3>(&value);
                if (vec == nullptr || !finite(*vec))
                    return nullptr;
                return std::make_unique<SetCommand<Vec3>>(mImpl.mDirection, *vec, "Set push direction");
            }
            if (id == kSandboxMagnitudeId)
            {
                const auto* number = std::get_if<double>(&value);
                if (number == nullptr || !std::isfinite(*number) || *number < 0.0 || *number > kMaxPushNs)
                    return nullptr;
                return std::make_unique<SetCommand<double>>(mImpl.mMagnitudeNs, *number, "Set push impulse");
            }
            return nullptr;
        }

    private:
        template <class T>
        class SetCommand final : public Command
        {
        public:
            SetCommand(T& target, T value, std::string label)
                : mTarget(target)
                , mOld(target)
                , mNew(std::move(value))
                , mLabel(std::move(label))
            {
            }
            std::string_view label() const override { return mLabel; }
            void redo() override { mTarget = mNew; }
            void undo() override { mTarget = mOld; }

        private:
            T& mTarget;
            T mOld;
            T mNew;
            std::string mLabel;
        };

        Impl& mImpl;
    };

    class SandboxController::SandboxGizmo final : public Gizmo
    {
    public:
        explicit SandboxGizmo(Impl& impl)
            : mImpl(impl)
        {
        }

        std::string_view id() const override { return "creativemotion.sandbox.gizmo"; }

        void draw(OverlaySink& sink) const override
        {
            const SimulationSandbox* sb = mImpl.sandbox();
            if (sb == nullptr || !sb->built() || !mImpl.mRig.has_value())
                return;

            for (int k = -kGridHalfCells; k <= kGridHalfCells; ++k)
            {
                sink.line(
                    { -double(kGridHalfCells), double(k), 0.0 }, { double(kGridHalfCells), double(k), 0.0 }, kGround);
                sink.line(
                    { double(k), -double(kGridHalfCells), 0.0 }, { double(k), double(kGridHalfCells), 0.0 }, kGround);
            }

            for (const opas::rig::Body& body : mImpl.mRig->bodies)
            {
                SandboxBodyPose pose;
                if (!sb->bodyPose(body.bone, pose))
                    continue;
                const auto seg = [&](const Vec3& a, const Vec3& b) {
                    sink.line(add(pose.positionM, rotate(pose, a)), add(pose.positionM, rotate(pose, b)), kWhite);
                };
                if (const auto* box = std::get_if<opas::rig::BoxShape>(&body.shape))
                    drawBox(box->halfExtentsM, seg);
                else if (const auto* sphere = std::get_if<opas::rig::SphereShape>(&body.shape))
                    drawSphere(sphere->radiusM, seg);
                else if (const auto* capsule = std::get_if<opas::rig::CapsuleShape>(&body.shape))
                    drawCapsule(capsule->halfHeightM, capsule->radiusM, seg);
            }

            if (mImpl.mLast.has_value())
                drawArrow(sink, *mImpl.mLast, sb->pendingPushes() > 0 ? kPending : kApplied);
        }

    private:
        static void drawArrow(OverlaySink& sink, const Impl::LastPush& push, const Colour& colour)
        {
            const double size = length(push.mImpulse);
            if (!(size > 0.0))
                return;
            const Vec3 dir = scaled(push.mImpulse, 1.0 / size);
            const Vec3 tip = add(push.mPoint, scaled(dir, kArrowLengthM));
            sink.line(push.mPoint, tip, colour);
            // Two head strokes in the plane spanned by the shaft and the axis least aligned with it.
            const Vec3 axis = std::abs(dir.x) < 0.9 ? Vec3{ 1.0, 0.0, 0.0 } : Vec3{ 0.0, 1.0, 0.0 };
            Vec3 side = cross(dir, axis);
            side = scaled(side, 1.0 / length(side));
            const Vec3 back = add(tip, scaled(dir, -kArrowHeadLengthM));
            sink.line(tip, add(back, scaled(side, kArrowHeadHalfWidthM)), colour);
            sink.line(tip, add(back, scaled(side, -kArrowHeadHalfWidthM)), colour);
        }

        Impl& mImpl;
    };

    SandboxController::SandboxController(EditorContext& context)
        : mImpl(std::make_unique<Impl>(context))
    {
    }

    SandboxController::~SandboxController() = default;

    void SandboxController::noteRigOpened(const std::filesystem::path& path)
    {
        mImpl->mNeedsBuild = true;
        mImpl->mLast.reset();
        opas::rig::RigLoadResult loaded = opas::rig::loadRigFile(path);
        if (!loaded.ok())
        {
            mImpl->mRig.reset();
            mImpl->mRigPath.clear();
            return;
        }
        mImpl->mRig = std::move(loaded.value);
        mImpl->mRigPath = path;
        const std::vector<std::string> bones = mImpl->bodyBones();
        if (std::find(bones.begin(), bones.end(), mImpl->mBone) == bones.end())
            mImpl->mBone = bones.empty() ? std::string() : bones.front();
    }

    void SandboxController::noteProfileOpened(const std::filesystem::path& path)
    {
        mImpl->mProfilePath = path;
        mImpl->mNeedsBuild = true;
        mImpl->mLast.reset();
    }

    void SandboxController::noteJointSelected(std::string_view bone)
    {
        const std::vector<std::string> bones = mImpl->bodyBones();
        if (std::find(bones.begin(), bones.end(), bone) != bones.end())
            mImpl->mBone = std::string(bone);
    }

    void SandboxController::noteStepped()
    {
        const SimulationSandbox* sb = mImpl->sandbox();
        if (sb != nullptr && sb->built())
            mImpl->mContext.requestRedraw();
    }

    bool SandboxController::available() const
    {
        return mImpl->sandbox() != nullptr && mImpl->mRig.has_value() && !mImpl->mProfilePath.empty();
    }

    PropertySource* SandboxController::properties()
    {
        if (mProperties == nullptr)
            mProperties = std::make_unique<Properties>(*mImpl);
        return mProperties.get();
    }

    Gizmo* SandboxController::gizmo()
    {
        if (mGizmo == nullptr)
            mGizmo = std::make_unique<SandboxGizmo>(*mImpl);
        return mGizmo.get();
    }

    bool SandboxController::push()
    {
        Impl& impl = *mImpl;
        const double size = length(impl.mDirection);
        if (!(size > 0.0) || !std::isfinite(size))
        {
            impl.mContext.log(LogLevel::Warning, "Push sandbox: the push direction is zero");
            return false;
        }
        if (!impl.ensureBuilt())
            return false;
        SimulationSandbox* sb = impl.sandbox();
        SandboxBodyPose pose;
        if (!sb->bodyPose(impl.mBone, pose))
        {
            impl.mContext.log(LogLevel::Warning, "Push sandbox: the sandbox has no body on '" + impl.mBone + "'");
            return false;
        }
        const Vec3 impulse = scaled(impl.mDirection, impl.mMagnitudeNs / size);
        if (!sb->push(impl.mBone, impulse, pose.positionM))
        {
            impl.mContext.log(LogLevel::Warning, "Push sandbox: the push was refused (too many waiting?)");
            return false;
        }
        impl.mLast = Impl::LastPush{ impl.mBone, impulse, pose.positionM };
        impl.mContext.requestRedraw();
        return true;
    }

    bool SandboxController::reset()
    {
        Impl& impl = *mImpl;
        SimulationSandbox* sb = impl.sandbox();
        if (sb == nullptr || impl.mNeedsBuild || !sb->built())
        {
            // ensureBuilt builds the ragdoll where it starts, which is what a reset does.
            const bool ok = impl.ensureBuilt();
            if (ok)
                impl.mContext.requestRedraw();
            return ok;
        }
        std::string error;
        impl.mLast.reset();
        if (!sb->reset(error))
        {
            impl.mContext.log(LogLevel::Error, "Push sandbox: " + error);
            return false;
        }
        impl.mContext.requestRedraw();
        return true;
    }
}
