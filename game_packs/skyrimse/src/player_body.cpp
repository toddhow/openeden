#include "game_skyrimse/player_body.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ActorAppearance;
        using openeden::sdk::game::ActorPart;
        using openeden::sdk::game::ActorPartKind;

        constexpr std::uint32_t kShieldSlot = 0x200; // BOD2 slot 39, as appearance.cpp and combat_stats.hpp
        constexpr std::size_t kMaxWorn = 64; // the inventory model's own cap on equipped items (kMaxEquipped)

        bool fits(const std::string& path, const AppearanceLimits& limits)
        {
            return !path.empty() && path.size() <= limits.mMaxPathBytes;
        }

        // The first addon of an armour that fits the race and has a model for the sex (an outfit's rule).
        const ArmatureRecord* addonFor(const ActorIndex& index, const ArmorExtra& armor, FormId race, Sex sex)
        {
            for (const FormId armatureId : armor.mArmatures)
            {
                const ArmatureRecord* addon = index.armature(armatureId);
                if (addon != nullptr && addon->appliesTo(race) && !addon->model(sex).empty())
                    return addon;
            }
            return nullptr;
        }

        struct Piece
        {
            std::string mModel;
            std::uint32_t mSlotMask = 0;
        };

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
            for (const FormId extra : head->mExtraParts)
                addHeadPart(index, extra, depth + 1, limits, seen, out);
        }
    }

    bool makePlayerAppearance(const ActorIndex& index, const PlayerBodyRequest& request, ActorAppearance& out,
        std::string& error, const AppearanceLimits& limits)
    {
        out = {};
        const RaceRecord* race = index.race(request.mRace);
        if (race == nullptr)
        {
            error = "the player's race is not a known RACE";
            return false;
        }
        const Sex sex = request.mFemale ? Sex::Female : Sex::Male;
        if (!fits(race->skeleton(sex), limits))
        {
            error = "the player's race has no skeleton model for this sex";
            return false;
        }
        out.skeletonPath = race->skeleton(sex);
        out.female = request.mFemale;
        {
            const float scale = race->mHeight[sex == Sex::Male ? 0 : 1];
            out.heightScale = std::isfinite(scale) ? std::clamp(scale, kMinHeightScale, kMaxHeightScale) : 1.0f;
        }

        // What he wears: the equipped pieces, sorted by form id so that the order of the list is not the order of the
        // picture, then the start clothes whose slots no equipped piece takes. A shield is held, not worn, when the
        // pack is told to show held gear (as an NPC's, task 0220).
        std::vector<FormId> equipped = request.mWornArmour;
        std::sort(equipped.begin(), equipped.end());
        equipped.erase(std::unique(equipped.begin(), equipped.end()), equipped.end());
        if (equipped.size() > kMaxWorn)
            equipped.resize(kMaxWorn);

        std::vector<Piece> worn;
        std::uint32_t covered = 0;
        const ArmatureRecord* shieldAddon = nullptr;
        for (const FormId id : equipped)
        {
            const ArmorExtra* armor = index.armor(id);
            if (armor == nullptr)
                continue;
            const ArmatureRecord* addon = addonFor(index, *armor, request.mRace, sex);
            if (addon == nullptr || !fits(addon->model(sex), limits))
                continue;
            if (limits.mHeldGear && (armor->mSlotMask & kShieldSlot) != 0)
            {
                if (shieldAddon == nullptr)
                    shieldAddon = addon;
                continue;
            }
            worn.push_back({ addon->model(sex), addon->mSlotMask });
            covered |= addon->mSlotMask;
        }
        if (const OutfitRecord* outfit = index.outfit(request.mStartOutfit))
        {
            std::vector<Piece> start;
            std::uint32_t startCovered = 0;
            for (const FormId id : outfit->mItems)
            {
                const ArmorExtra* armor = index.armor(id);
                if (armor == nullptr)
                    continue;
                const ArmatureRecord* addon = addonFor(index, *armor, request.mRace, sex);
                if (addon == nullptr || !fits(addon->model(sex), limits) || (addon->mSlotMask & covered) != 0)
                    continue;
                start.push_back({ addon->model(sex), addon->mSlotMask });
                startCovered |= addon->mSlotMask;
            }
            // start clothes first (the order of an outfit), then the equipped pieces over them
            worn.insert(worn.begin(), start.begin(), start.end());
            covered |= startCovered;
        }

        // The skin: the race's, every addon that fits and is not covered.
        if (const ArmorExtra* skin = index.armor(race->mSkin))
        {
            for (const FormId armatureId : skin->mArmatures)
            {
                const ArmatureRecord* addon = index.armature(armatureId);
                if (addon == nullptr || !addon->appliesTo(request.mRace) || (addon->mSlotMask & covered) != 0
                    || !fits(addon->model(sex), limits))
                    continue;
                ActorPart part;
                part.modelPath = addon->model(sex);
                part.kind = ActorPartKind::Skin;
                part.slotMask = addon->mSlotMask;
                out.parts.push_back(std::move(part));
            }
        }
        for (const Piece& piece : worn)
        {
            ActorPart part;
            part.modelPath = piece.mModel;
            part.kind = ActorPartKind::Armor;
            part.slotMask = piece.mSlotMask;
            out.parts.push_back(std::move(part));
        }

        // The head: the race's default head parts for the sex, in the race's own order (INDX).
        {
            std::vector<RaceHeadPart> defaults;
            for (const RaceHeadPart& h : race->mHeadParts)
                if (h.mSex == sex)
                    defaults.push_back(h);
            std::stable_sort(defaults.begin(), defaults.end(),
                [](const RaceHeadPart& a, const RaceHeadPart& b) { return a.mIndex < b.mIndex; });
            std::set<FormId> seen;
            for (const RaceHeadPart& h : defaults)
                addHeadPart(index, h.mHeadPart, 0, limits, seen, out);
        }

        // What he holds (the rule of an NPC's, task 0220: melee types 1 to 6 with a model; a two-handed weapon gives no
        // shield).
        if (limits.mHeldGear)
        {
            bool twoHanded = false;
            if (request.mWeapon != kNullFormId)
            {
                const WeaponLook* look = index.weaponLook(request.mWeapon);
                if (look != nullptr && look->mHasData && look->mHasDnam && look->mAnimType >= 1 && look->mAnimType <= 6
                    && fits(look->mModel, limits))
                {
                    out.heldWeaponModel = look->mModel;
                    out.heldWeaponType = look->mAnimType;
                    twoHanded = look->mAnimType == 5 || look->mAnimType == 6;
                }
            }
            if (shieldAddon != nullptr && !twoHanded)
                out.heldShieldModel = shieldAddon->model(sex);
        }

        if (out.parts.size() > limits.mMaxParts)
            out.parts.resize(limits.mMaxParts);
        error.clear();
        return true;
    }
}
