#include "skyrimheldgear.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/mwanimation/play.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/settings/values.hpp>

namespace MWRender::SkyrimHeldGear
{
    namespace
    {
        std::string lowered(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        // A weapon or shield file as a static model: read, flattened, built. Null (with `error`) on any failure.
        vsg::ref_ptr<vsg::Node> buildStatic(const VFS::Manager& vfs, const SkyrimModel::BuildContext& context,
            const std::string& model, std::string& error)
        {
            try
            {
                const VFS::Path::Normalized path
                    = Misc::ResourceHelpers::correctMeshPath(VFS::Path::toNormalized(model));
                Files::IStreamPtr stream = vfs.get(path);
                if (!stream)
                {
                    error = "'" + model + "' is not in the VFS";
                    return {};
                }
                const NifScene::Scene scene = NifScene::readScene(std::move(stream), path, NifScene::SceneOptions{});
                NifScene::Flat::Limits limits;
                limits.mSurfaceMaps = Settings::game().mNeutralWorldMaterials;
                limits.mSkinnedAtRest = true;
                const NifScene::Flat::FlatModel flat = NifScene::Flat::flatten(scene, limits);
                SkyrimModel::BuildContext modelContext = context;
                modelContext.mFilename = model;
                SkyrimModel::BuildResult built = SkyrimModel::buildNode(flat, modelContext);
                if (!built.mNode)
                    error = "'" + model + "' draws nothing";
                return built.mNode;
            }
            catch (const std::exception& e)
            {
                error = "'" + model + "': " + e.what();
                return {};
            }
        }

        vsg::ref_ptr<vsg::Switch> makeSwitch(const vsg::ref_ptr<vsg::Node>& child, bool shown)
        {
            auto sw = vsg::Switch::create();
            sw->addChild(shown, child);
            return sw;
        }
    }

    std::string_view sheathNodeFor(std::uint8_t weaponType)
    {
        switch (weaponType)
        {
            case 1:
                return "WeaponSword";
            case 2:
                return "WeaponDagger";
            case 3:
                return "WeaponAxe";
            case 4:
                return "WeaponMace";
            case 5:
            case 6:
                return "WeaponBack";
            default:
                return {};
        }
    }

    bool DrawClock::update(float now) noexcept
    {
        if (now < mLast) // a restarted clock: keep what is left of the hold
            mUntil = std::min(mUntil - mLast + now, now + kDrawnHoldSeconds);
        mLast = now;
        const int forced = mForced.load(std::memory_order_acquire);
        if (forced >= 0)
        {
            mPending.store(false, std::memory_order_relaxed); // a combat clip does not draw it while it is forced
            mUntil = -1.0e30f;
            return forced == 1;
        }
        if (mPending.exchange(false, std::memory_order_acq_rel))
            mUntil = now + kDrawnHoldSeconds;
        return now < mUntil;
    }

    Controller::Controller(vsg::ref_ptr<vsg::Switch> sheathed, vsg::ref_ptr<vsg::Switch> drawn)
        : mSheathed(std::move(sheathed))
        , mDrawn(std::move(drawn))
    {
        hints.autoPlay = true;
    }

    void Controller::run(vsg::Object&, float time) const
    {
        const bool drawn = mClock.update(time);
        if (mApplied && drawn == mShownDrawn.load(std::memory_order_relaxed))
            return;
        mApplied = true;
        mShownDrawn.store(drawn, std::memory_order_relaxed);
        if (mSheathed)
            mSheathed->setAllChildren(!drawn);
        if (mDrawn)
            mDrawn->setAllChildren(drawn);
    }

    void attach(MWAnim::ObjectAnimation& anim, const openeden::sdk::game::ActorAppearance& appearance,
        const VFS::Manager& vfs, const SkyrimModel::BuildContext& context, Report& report)
    {
        report = {};
        if (appearance.heldWeaponModel.empty() && appearance.heldShieldModel.empty())
            return;
        if (!anim.animation)
        {
            report.mFailure = "the actor has no skeleton";
            return;
        }
        std::unordered_map<std::string, vsg::ref_ptr<Anim::Transform>> nodes;
        anim.animation->bones.forEach([&nodes](const std::string& name, Anim::Bone& bone) {
            if (!bone.path.empty())
                nodes.emplace(lowered(name), bone.path.back());
        });
        const auto find = [&nodes](std::string_view name) -> vsg::ref_ptr<Anim::Transform> {
            const auto it = nodes.find(lowered(name));
            return it == nodes.end() ? vsg::ref_ptr<Anim::Transform>() : it->second;
        };
        const auto note = [&report](const std::string& what) {
            if (report.mFailure.empty())
                report.mFailure = what;
        };

        if (!appearance.heldShieldModel.empty())
        {
            std::string error;
            const auto hand = find(kShieldNode);
            if (!hand)
                note("the skeleton has no SHIELD node");
            else if (const auto model = buildStatic(vfs, context, appearance.heldShieldModel, error))
            {
                hand->addChild(model);
                report.mShieldAttached = true;
            }
            else
                note(error);
        }
        if (!appearance.heldWeaponModel.empty())
        {
            std::string error;
            const std::string_view sheathName = sheathNodeFor(appearance.heldWeaponType);
            const auto sheath = sheathName.empty() ? vsg::ref_ptr<Anim::Transform>() : find(sheathName);
            const auto hand = find(kHandNode);
            if (!sheath || !hand)
                note("the skeleton lacks the weapon nodes " + std::string(sheathName) + " or WEAPON");
            else if (const auto model = buildStatic(vfs, context, appearance.heldWeaponModel, error))
            {
                // One shared model, two places; a switch shows one of them.
                auto atSheath = makeSwitch(model, true);
                auto inHand = makeSwitch(model, false);
                sheath->addChild(atSheath);
                hand->addChild(inHand);
                auto controller = vsg::ref_ptr<Controller>(new Controller(atSheath, inHand));
                anim.addController(controller, anim.transform());
                anim.keepAlive.push_back(controller);
                report.mWeaponAttached = true;
                report.mSheathNode = std::string(sheathName);
            }
            else
                note(error);
        }
    }

    void setDrawn(const std::vector<vsg::ref_ptr<vsg::Object>>& keepAlive, bool drawn)
    {
        for (const vsg::ref_ptr<vsg::Object>& kept : keepAlive)
            if (auto* controller = dynamic_cast<Controller*>(kept.get()))
            {
                controller->force(drawn);
                return;
            }
    }

    void noteCombat(const std::vector<vsg::ref_ptr<vsg::Object>>& keepAlive)
    {
        for (const vsg::ref_ptr<vsg::Object>& kept : keepAlive)
            if (auto* controller = dynamic_cast<Controller*>(kept.get()))
            {
                controller->note();
                return;
            }
    }
}
