#include "creativemotionstyle.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/settings/values.hpp>

#include <opas/style/Noise.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

namespace MWRender
{
    namespace
    {
        // Ground speed at which an actor counts as fully moving (task 0053): a bit under Morrowind's walk (about 1.5
        // m/s).
        constexpr float sFullMovingSpeedMps = 1.f;
        // Below this idle weight the idle life is switched off (the modifier reads a zero interval as "no idle").
        constexpr float sIdleOffWeight = 0.05f;
        // Posture weight while a weapon is drawn: a guarded stance slouches less.
        constexpr float sDrawnPostureWeight = 0.6f;
        constexpr std::string_view sStylesCategory = "CreativeMotion Styles";

        // Duration factors of transitionSeconds, see the task spec.
        constexpr float sToHit = 0.3f;
        constexpr float sToDeath = 0.5f;
        constexpr float sToAttack = 0.5f;
        constexpr float sFromAttack = 1.2f;
        constexpr float sStopping = 1.5f;
        constexpr float sWalkToRun = 0.8f;
        constexpr float sIdleTurn = 0.6f;
        constexpr float sStrafe = 0.8f;

        bool startsWith(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
        }

        bool endsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size()
                && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        bool isOneOf(std::string_view name, std::initializer_list<std::string_view> names)
        {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

        std::string lower(std::string_view text)
        {
            std::string result(text);
            Misc::StringUtils::lowerCaseInPlace(result);
            return result;
        }

        // The canonical bones of opas::style::StyleModifier and the Morrowind bones they stand for.
        struct BoneName
        {
            std::string_view mCanonical;
            std::string_view mMorrowind;
        };
        constexpr std::array<BoneName, 15> sBoneNames = { {
            { "Pelvis", "bip01 pelvis" },
            { "Spine", "bip01 spine" },
            { "Chest", "bip01 spine1" },
            { "Neck", "bip01 neck" },
            { "Head", "bip01 head" },
            { "LeftClavicle", "bip01 l clavicle" },
            { "RightClavicle", "bip01 r clavicle" },
            { "LeftUpperArm", "bip01 l upperarm" },
            { "LeftForearm", "bip01 l forearm" },
            { "RightUpperArm", "bip01 r upperarm" },
            { "RightForearm", "bip01 r forearm" },
            { "LeftThigh", "bip01 l thigh" },
            { "LeftShin", "bip01 l calf" },
            { "RightThigh", "bip01 r thigh" },
            { "RightShin", "bip01 r calf" },
        } };

        // Canonical forward is -Y (a positive pitch about X leans forward); Morrowind's is +Y: turn half way round Z.
        constexpr opas::math::Quat sCanonicalFromModel{ 0.0, 0.0, 1.0, 0.0 };
    }

    MotionKind classifyGroup(std::string_view groupName)
    {
        const std::string name = lower(groupName);
        if (startsWith(name, "swim"))
            return MotionKind::Swim;
        if (name == "idlesneak")
            return MotionKind::Sneak;
        if (name == "idleswim")
            return MotionKind::Swim;
        if (startsWith(name, "idle"))
        {
            const bool digitsOnly
                = std::all_of(name.begin() + 4, name.end(), [](char c) { return c >= '0' && c <= '9'; });
            return digitsOnly ? MotionKind::Idle : MotionKind::Ready;
        }
        if ((startsWith(name, "walk") || startsWith(name, "run"))
            && (endsWith(name, "left") || endsWith(name, "right")))
            return MotionKind::Strafe;
        if (startsWith(name, "walk"))
            return MotionKind::Walk;
        if (startsWith(name, "run"))
            return MotionKind::Run;
        if (startsWith(name, "sneak"))
            return MotionKind::Sneak;
        if (startsWith(name, "turn"))
            return MotionKind::Turn;
        if (startsWith(name, "jump"))
            return MotionKind::Jump;
        if (isOneOf(name,
                { "weapononehand", "weapontwohand", "weapontwowide", "handtohand", "bowandarrow", "crossbow",
                    "throwweapon", "spellcast" }))
            return MotionKind::Attack;
        if (startsWith(name, "hit") || name == "knockdown" || name == "knockout")
            return MotionKind::Hit;
        if (startsWith(name, "death"))
            return MotionKind::Death;
        if (name == "shield" || name == "torch")
            return MotionKind::Ready;
        return MotionKind::Other;
    }

    float transitionSeconds(MotionKind from, MotionKind to, float baseSeconds)
    {
        if (baseSeconds <= 0.f)
            return 0.f;
        if (to == MotionKind::Hit)
            return baseSeconds * sToHit;
        if (to == MotionKind::Death)
            return baseSeconds * sToDeath;
        if (to == MotionKind::Attack)
            return baseSeconds * sToAttack;
        if (from == MotionKind::Attack)
            return baseSeconds * sFromAttack;
        if ((from == MotionKind::Walk || from == MotionKind::Run || from == MotionKind::Sneak)
            && to == MotionKind::Idle)
            return baseSeconds * sStopping;
        if (from == MotionKind::Walk && to == MotionKind::Run)
            return baseSeconds * sWalkToRun;
        if ((from == MotionKind::Idle && to == MotionKind::Turn)
            || (from == MotionKind::Turn && to == MotionKind::Idle))
            return baseSeconds * sIdleTurn;
        if (from == MotionKind::Strafe || to == MotionKind::Strafe)
            return baseSeconds * sStrafe;
        return baseSeconds;
    }

    StyleWeights styleWeights(const ActorState& state)
    {
        StyleWeights weights;
        weights.mMoving = std::clamp(state.mSpeedMps / sFullMovingSpeedMps, 0.f, 1.f);
        weights.mIdle = (state.mTurning || state.mHit) ? 0.f : 1.f - weights.mMoving;
        weights.mPosture = state.mHit ? 0.f : (state.mWeaponDrawn ? sDrawnPostureWeight : 1.f);
        return weights;
    }

    void makeEffectiveProfile(const opas::style::StyleProfile& base, const StyleWeights& weights,
        std::uint64_t actorSeed, opas::style::StyleProfile& out)
    {
        out.name = base.name;
        out.gait = base.gait;
        out.timing = base.timing;
        out.gaze = base.gaze;

        const double posture = weights.mPosture;
        out.posture.slouchRad = base.posture.slouchRad * posture;
        out.posture.headPitchRad = base.posture.headPitchRad * posture;
        out.posture.headYawRad = base.posture.headYawRad * posture;
        out.posture.shoulderElevationRad = base.posture.shoulderElevationRad * posture;
        out.posture.spineLeanRad = base.posture.spineLeanRad * posture;

        out.idle = base.idle;
        if (weights.mIdle < sIdleOffWeight)
        {
            out.idle.breathingAmplitudeM = 0.0;
            out.idle.swayAmplitudeM = 0.0;
            out.idle.fidgetIntervalS = 0.0;
        }
        else
        {
            out.idle.breathingAmplitudeM = base.idle.breathingAmplitudeM * weights.mIdle;
            out.idle.swayAmplitudeM = base.idle.swayAmplitudeM * weights.mIdle;
        }
        out.idle.seed = base.idle.seed ^ opas::style::hash64(actorSeed);

        if (out.layers.size() != base.layers.size())
            out.layers = base.layers;
        for (std::size_t i = 0; i < base.layers.size(); ++i)
        {
            const opas::style::LayerConfig& source = base.layers[i];
            opas::style::LayerConfig& target = out.layers[i];
            target.name = source.name;
            target.bone = source.bone;
            const double scale = source.name == "limp" ? weights.mMoving : 1.0;
            target.weight = std::clamp(source.weight * scale, 0.0, 1.0);
        }
    }

    float cadenceFactor(const opas::style::StyleProfile& base, const StyleWeights& weights)
    {
        return 1.f + (static_cast<float>(base.gait.cadenceScale) - 1.f) * weights.mMoving;
    }

    StyleRules makeStyleRules(
        std::string_view defaultStyle, const std::vector<std::pair<std::string, std::string>>& entries)
    {
        StyleRules rules;
        rules.mDefaultStyle = std::string(defaultStyle);
        for (const auto& [key, style] : entries)
        {
            const std::string lowerKey = lower(key);
            if (startsWith(lowerKey, "actor.") && lowerKey.size() > 6)
                rules.mActors[lowerKey.substr(6)] = style;
            else if (startsWith(lowerKey, "race.") && lowerKey.size() > 5)
                rules.mRaces[lowerKey.substr(5)] = style;
        }
        return rules;
    }

    std::string selectStyleName(const StyleRules& rules, std::string_view actorId, std::string_view raceId)
    {
        if (const auto it = rules.mActors.find(lower(actorId)); it != rules.mActors.end())
            return it->second;
        if (!raceId.empty())
            if (const auto it = rules.mRaces.find(lower(raceId)); it != rules.mRaces.end())
                return it->second;
        return rules.mDefaultStyle;
    }

    StyleRules loadStyleRulesFromSettings()
    {
        std::vector<std::pair<std::string, std::string>> entries;
        // Defaults first, so that a user entry for the same key comes later and wins.
        for (const auto* settings : { &Settings::Manager::mDefaultSettings, &Settings::Manager::mUserSettings })
            for (const auto& [key, value] : *settings)
                if (key.first == sStylesCategory)
                    entries.emplace_back(key.second, value);
        return makeStyleRules(Settings::game().mCreativeMotionDefaultStyle.get(), entries);
    }

    StyleLibrary::StyleLibrary(Reader reader)
        : mReader(std::move(reader))
    {
    }

    const opas::style::StyleProfile* StyleLibrary::find(std::string_view name)
    {
        if (name.empty())
            return nullptr;
        const std::string key = lower(name);
        const std::lock_guard lock(mMutex);
        if (const auto it = mProfiles.find(key); it != mProfiles.end())
            return it->second.get();

        std::unique_ptr<opas::style::StyleProfile> profile;
        const std::string path = "styles/" + key + ".style.json";
        const std::optional<std::string> text = mReader ? mReader(path) : std::nullopt;
        if (!text)
            Log(Debug::Warning) << "CreativeMotion style '" << name << "' not found (" << path << ")";
        else
        {
            const opas::style::StyleLoadResult result = opas::style::loadStyleProfile(*text, path);
            if (result.ok())
                profile = std::make_unique<opas::style::StyleProfile>(*result.value);
            else
                Log(Debug::Warning) << "CreativeMotion style rejected: " << result.error.toString();
        }
        // A failure is remembered too (as a null), so that it is logged once.
        return mProfiles.emplace(key, std::move(profile)).first->second.get();
    }

    CreativeMotionStyleLayer::CreativeMotionStyleLayer(const opas::rig::Skeleton& skeleton)
    {
        // Model-space rest rotation of every bone (parents come first).
        std::vector<opas::math::Quat> modelRotation(skeleton.bones.size());
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            const opas::rig::Bone& bone = skeleton.bones[i];
            modelRotation[i]
                = bone.parent >= 0 ? modelRotation[bone.parent] * bone.localRest.rotation : bone.localRest.rotation;
        }

        for (const BoneName& names : sBoneNames)
        {
            const auto found = opas::rig::findBone(skeleton, names.mMorrowind);
            if (!found)
                continue;
            const int parent = skeleton.bones[*found].parent;
            const opas::math::Quat parentRotation = parent >= 0 ? modelRotation[parent] : opas::math::Quat{};
            mBound.push_back(
                { static_cast<int>(*found), opas::math::normalized(sCanonicalFromModel * parentRotation) });
            opas::rig::Bone canonical;
            canonical.name = std::string(names.mCanonical);
            mCanonical.bones.push_back(std::move(canonical));
        }
        mModifier = std::make_unique<opas::style::StyleModifier>(mCanonical);
    }

    void CreativeMotionStyleLayer::apply(
        std::span<opas::math::Transform> pose, const opas::style::StyleProfile& effective, double timeS) const
    {
        constexpr std::size_t sMaxBones = sBoneNames.size();
        if (mBound.empty())
            return;
        std::array<opas::math::Transform, sMaxBones> canonical;
        for (std::size_t k = 0; k < mBound.size(); ++k)
        {
            const opas::math::Transform& local = pose[mBound[k].mDriverIndex];
            const opas::math::Quat frame = mBound[k].mFrame;
            canonical[k].rotation = opas::math::normalized(frame * local.rotation * opas::math::conjugate(frame));
            canonical[k].translation = opas::math::rotate(frame, local.translation);
        }
        const std::array<opas::math::Transform, sMaxBones> before = canonical;
        mModifier->apply(std::span<opas::math::Transform>(canonical.data(), mBound.size()), effective, timeS);
        for (std::size_t k = 0; k < mBound.size(); ++k)
        {
            // A bone the style did not change keeps its exact input (no round trip through the frame).
            if (canonical[k] == before[k])
                continue;
            opas::math::Transform& local = pose[mBound[k].mDriverIndex];
            const opas::math::Quat inverse = opas::math::conjugate(mBound[k].mFrame);
            local.rotation = opas::math::normalized(inverse * canonical[k].rotation * mBound[k].mFrame);
            local.translation = opas::math::rotate(inverse, canonical[k].translation);
        }
    }

    std::string describeStyle(const opas::style::StyleProfile& base, bool enabled, const StyleWeights& weights)
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
            "style %s [%s]  moving=%.2f idle=%.2f posture=%.2f cadence=%.2f slouch=%.3f", base.name.c_str(),
            enabled ? "on" : "off", weights.mMoving, weights.mIdle, weights.mPosture, cadenceFactor(base, weights),
            base.posture.slouchRad);
        std::string text = buffer;
        text += " layers:";
        if (base.layers.empty())
            text += " none";
        for (const opas::style::LayerConfig& layer : base.layers)
        {
            std::snprintf(buffer, sizeof(buffer), " %s=%.2f", layer.name.c_str(), layer.weight);
            text += buffer;
        }
        return text;
    }

    namespace
    {
        // The speed is smoothed at this rate (per second), as the style layer's speed was in task 0053.
        constexpr float sIntentSmoothingRate = 6.f;
        // A let-go actor counts as stopping while its smoothed speed is above this (metres per second).
        constexpr float sStoppedSpeedMps = 0.2f;
        // An actor that wants to move counts as starting until it has reached this share of the speed it wants.
        constexpr float sStartedShare = 0.9f;
        constexpr std::string_view sTransitionsCategory = "CreativeMotion Transitions";

        bool isMoving(MotionKind kind)
        {
            return kind == MotionKind::Walk || kind == MotionKind::Run || kind == MotionKind::Sneak;
        }

        float finiteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }

        // A finite number in [minimum, maximum] (above minimum when exclusive); false (out unchanged) for anything
        // else. The range is checked on the double, before the cast, so no value can overflow the float.
        bool parseNumber(const std::string& text, double minimum, bool exclusive, double maximum, float& out)
        {
            double value = 0.0;
            const char* const begin = text.data();
            const char* const end = begin + text.size();
            const auto [last, error] = std::from_chars(begin, end, value);
            if (text.empty() || error != std::errc() || last != end || !std::isfinite(value))
                return false;
            if (value < minimum || (exclusive && value == minimum) || value > maximum)
                return false;
            out = static_cast<float>(value);
            return true;
        }

        std::string located(const std::string& key, const std::string& text, std::string_view why)
        {
            return "[" + std::string(sTransitionsCategory) + "] " + key + " = '" + text + "': " + std::string(why);
        }

        struct Factor
        {
            std::string_view mKey;
            float TransitionTimes::*mField;
        };
        constexpr Factor sFactors[] = { { "stopping", &TransitionTimes::mStopping },
            { "flip", &TransitionTimes::mFlip }, { "starting", &TransitionTimes::mStarting },
            { "fast turn", &TransitionTimes::mFastTurn }, { "slow turn", &TransitionTimes::mSlowTurn } };
    }

    TransitionTimes makeTransitionTimes(
        const std::vector<std::pair<std::string, std::string>>& entries, std::vector<std::string>* problems)
    {
        TransitionTimes times;
        for (const auto& [key, text] : entries)
        {
            const Factor* factor = nullptr;
            for (const Factor& candidate : sFactors)
            {
                if (candidate.mKey == key)
                    factor = &candidate;
            }
            if (factor != nullptr)
            {
                const bool read = parseNumber(text, 0.0, false, sMaxTransitionFactor, times.*(factor->mField));
                if (!read && problems != nullptr)
                    problems->push_back(located(key, text, "not a number from 0 to 4; the default is used"));
            }
            else if (key == "fast turn rate")
            {
                const bool read = parseNumber(text, 0.0, true, sMaxFastTurnRate, times.mFastTurnRate);
                if (!read && problems != nullptr)
                    problems->push_back(located(key, text, "not a number above 0 and at most 50; the default is used"));
            }
            else if (problems != nullptr)
            {
                problems->push_back(located(key, text,
                    "unknown key (stopping, flip, starting, fast turn, slow turn, fast turn rate); ignored"));
            }
        }
        return times;
    }

    TransitionTimes loadTransitionTimesFromSettings(std::vector<std::string>* problems)
    {
        std::vector<std::pair<std::string, std::string>> entries;
        // Defaults first, so that a user entry for the same key comes later and wins.
        for (const auto* settings : { &Settings::Manager::mDefaultSettings, &Settings::Manager::mUserSettings })
            for (const auto& [key, value] : *settings)
                if (key.first == sTransitionsCategory)
                    entries.emplace_back(key.second, value);
        return makeTransitionTimes(entries, problems);
    }

    float transitionSeconds(
        MotionKind from, MotionKind to, float baseSeconds, const TransitionIntent& intent, const TransitionTimes& times)
    {
        if (baseSeconds <= 0.f)
            return 0.f;
        if (isMoving(from) && to == MotionKind::Idle)
            return baseSeconds * (intent.mStopping ? times.mStopping : times.mFlip);
        if (from == MotionKind::Idle && isMoving(to) && intent.mStarting)
            return baseSeconds * times.mStarting;
        const bool turnIn = from == MotionKind::Idle && to == MotionKind::Turn;
        const bool turnOut = from == MotionKind::Turn && to == MotionKind::Idle;
        const bool turnInPlace = turnIn || turnOut;
        if (turnInPlace)
        {
            const bool fast = std::abs(intent.mTurnRate) >= times.mFastTurnRate;
            return baseSeconds * (fast ? times.mFastTurn : times.mSlowTurn);
        }
        return transitionSeconds(from, to, baseSeconds);
    }

    ActorIntent IntentTracker::update(const IntentInput& input)
    {
        const float measured = std::max(finiteOr(input.mMeasuredSpeedMps, 0.f), 0.f);
        const float intended = std::max(finiteOr(input.mIntendedSpeedMps, 0.f), 0.f);

        ActorIntent intent;
        intent.mTargetSpeedMps = input.mWantsToMove ? intended : 0.f;
        if (std::isfinite(input.mDt) && input.mDt > 0.f)
        {
            const float before = mSmoothed;
            mSmoothed = before + (measured - before) * std::min(1.f, input.mDt * sIntentSmoothingRate);
            intent.mAccelerationMps2 = (mSmoothed - before) / input.mDt;
            intent.mTurnRate = finiteOr(input.mYawDeltaRad, 0.f) / input.mDt;
        }
        intent.mSmoothedSpeedMps = mSmoothed;
        intent.mStopping = !input.mWantsToMove && mSmoothed > sStoppedSpeedMps;
        // Not slowing down: in the frame the movement keys are pressed the measured speed may still be 0 (the
        // animation has not moved the actor yet), so an increase is not required, only that the speed does not fall.
        intent.mStarting = input.mWantsToMove && intent.mTargetSpeedMps > 0.f
            && mSmoothed < sStartedShare * intent.mTargetSpeedMps && intent.mAccelerationMps2 >= 0.f;
        if (input.mStanceChanged)
            mStanceHold = sStanceChangeHoldS;
        else if (std::isfinite(input.mDt) && input.mDt > 0.f)
            mStanceHold = std::max(0.f, mStanceHold - input.mDt);
        intent.mStanceChanging = mStanceHold > 0.f;
        return intent;
    }

    CreativeMotionPreset parseCreativeMotionPreset(std::string_view text, bool* recognised)
    {
        const std::string name = lower(text);
        const bool known = name == "off" || name == "natural";
        if (recognised != nullptr)
            *recognised = known;
        return name == "natural" ? CreativeMotionPreset::Natural : CreativeMotionPreset::Off;
    }

    CreativeMotionFeatures resolveCreativeMotionFeatures(CreativeMotionPreset preset, FeatureSetting animation,
        FeatureSetting transitionRules, FeatureSetting styleLayer)
    {
        const bool natural = preset == CreativeMotionPreset::Natural;
        const auto choose = [natural](FeatureSetting setting) {
            return setting.mExplicit ? setting.mValue : (natural || setting.mValue);
        };
        CreativeMotionFeatures features;
        features.mAnimation = choose(animation);
        features.mTransitionRules = choose(transitionRules);
        features.mStyleLayer = choose(styleLayer);
        features.mIntentTransitions = natural && features.mTransitionRules;
        return features;
    }

    CreativeMotionFeatures loadCreativeMotionFeaturesFromSettings(bool* recognised)
    {
        const auto feature = [](const char* key, bool value) {
            FeatureSetting setting;
            setting.mValue = value;
            const auto& user = Settings::Manager::mUserSettings;
            setting.mExplicit = user.find(std::make_pair(std::string("Game"), std::string(key))) != user.end();
            return setting;
        };
        const CreativeMotionPreset preset
            = parseCreativeMotionPreset(Settings::game().mCreativeMotionPreset.get(), recognised);
        return resolveCreativeMotionFeatures(preset,
            feature("creativemotion animation", Settings::game().mCreativeMotionAnimation),
            feature("creativemotion transition rules", Settings::game().mCreativeMotionTransitionRules),
            feature("creativemotion style layer", Settings::game().mCreativeMotionStyleLayer));
    }
}
