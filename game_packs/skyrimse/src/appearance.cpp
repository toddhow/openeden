#include "game_skyrimse/appearance.hpp"

#include <set>

namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ActorAppearance;
        using openeden::sdk::game::ActorPart;
        using openeden::sdk::game::ActorPartKind;

        bool fits(const std::string& path, const AppearanceLimits& limits)
        {
            return !path.empty() && path.size() <= limits.mMaxPathBytes;
        }

        // A head part, then the extra parts it names (HNAM), depth first, each part once.
        void addHeadPart(const ActorIndex& index, FormId id, std::uint32_t depth, const AppearanceLimits& limits,
            std::set<FormId>& seen, ActorAppearance& out)
        {
            if (out.parts.size() >= limits.mMaxParts || !seen.insert(id).second)
                return;
            const HeadPartRecord* head = index.headPart(id);
            if (head == nullptr)
                return;
            if (fits(head->mModel, limits))
            {
                ActorPart part;
                part.modelPath = head->mModel;
                part.kind = ActorPartKind::HeadPart;
                part.headPartType = head->mType;
                out.parts.push_back(std::move(part));
            }
            if (depth >= limits.mMaxHeadPartDepth)
                return;
            for (FormId extra : head->mExtraParts)
                addHeadPart(index, extra, depth + 1, limits, seen, out);
        }
    }

    bool makeActorAppearance(const ActorIndex& index, FormId npc, std::uint32_t variation, ActorAppearance& out,
        std::string& error, const AppearanceLimits& limits)
    {
        out = {};
        ActorIndex::Resolved resolved;
        switch (index.resolve(npc, kAppearanceLevel, variation, resolved))
        {
            case ActorIndex::ResolveStatus::Ok:
                break;
            case ActorIndex::ResolveStatus::UnknownNpc:
                error = "the form is not a known NPC_";
                return false;
            case ActorIndex::ResolveStatus::Cycle:
                error = "the NPC's template chain leads back to itself";
                return false;
            case ActorIndex::ResolveStatus::TooDeep:
                error = "the NPC's template chain is too deep";
                return false;
            case ActorIndex::ResolveStatus::BrokenLink:
                error = "the NPC's template chain names something that is not an NPC_ or a usable leveled list";
                return false;
        }
        // The sex comes with the traits (a templated NPC takes its template's), else the NPC's own.
        const ActorNpc* traits = index.npc(resolved.mTraitsFrom);
        const ActorNpc* own = index.npc(npc);
        const Sex sex = traits != nullptr ? traits->sex() : own->sex();
        const RaceRecord* race = index.race(resolved.mRace);
        if (race == nullptr)
        {
            error = "the NPC's race is not a known RACE";
            return false;
        }
        if (!fits(race->skeleton(sex), limits))
        {
            error = "the race has no skeleton model for this sex";
            return false;
        }
        out.skeletonPath = race->skeleton(sex);
        out.female = sex == Sex::Female;

        // The outfit first: its slots decide which skin parts are covered.
        std::vector<ActorPart> worn;
        std::uint32_t covered = 0;
        for (const ActorIndex::WornModel& model :
            index.wornModels(resolved.mDefaultOutfit, resolved.mRace, sex, kAppearanceLevel, variation))
        {
            if (!fits(model.mModel, limits))
                continue;
            ActorPart part;
            part.modelPath = model.mModel;
            part.kind = ActorPartKind::Armor;
            part.slotMask = model.mSlotMask;
            covered |= model.mSlotMask;
            worn.push_back(std::move(part));
        }

        // The skin: the NPC's own (WNAM) or the race's, every addon that fits the race and is not covered.
        const FormId skinId = resolved.mSkin != kNullFormId ? resolved.mSkin : race->mSkin;
        if (const ArmorExtra* skin = index.armor(skinId))
        {
            for (FormId armatureId : skin->mArmatures)
            {
                const ArmatureRecord* addon = index.armature(armatureId);
                if (addon == nullptr || !addon->appliesTo(resolved.mRace) || !fits(addon->model(sex), limits)
                    || (addon->mSlotMask & covered) != 0)
                    continue;
                ActorPart part;
                part.modelPath = addon->model(sex);
                part.kind = ActorPartKind::Skin;
                part.slotMask = addon->mSlotMask;
                out.parts.push_back(std::move(part));
            }
        }
        for (ActorPart& part : worn)
            out.parts.push_back(std::move(part));

        std::set<FormId> seen;
        for (FormId head : resolved.mHeadParts)
            addHeadPart(index, head, 0, limits, seen, out);

        if (out.parts.size() > limits.mMaxParts)
            out.parts.resize(limits.mMaxParts);
        error.clear();
        return true;
    }
}
