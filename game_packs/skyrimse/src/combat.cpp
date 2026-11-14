#include <game_skyrimse/combat.hpp>

#include <algorithm>
#include <cmath>

namespace game_skyrimse
{
    namespace
    {
        using openeden::sdk::game::ClipSlot;

        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
        }

        // Clip names of Skyrim - Animations.bsa, meshes/actors/character/animations (read from the listing; events in
        // animationdata/defaultmale.txt: AttackRight has preHitFrame 0.233, weaponSwing 0.333, HitFrame 0.367, attackStop
        // 1.287 playback seconds). Case as in the sidecar's generator names, the file system of the VFS ignores case.
        constexpr SlotTable::Entry sHumanoid[] = {
            { ClipSlot::Attack, kWeaponAnim1HSword, "1HM_AttackRight" },
            { ClipSlot::AttackLeft, kWeaponAnim1HSword, "1HM_AttackLeft" },
            { ClipSlot::PowerAttack, kWeaponAnim1HSword, "1HM_AttackPower" },
            { ClipSlot::BlockHit, kWeaponAnim1HSword, "1HM_BlockHit" },
            { ClipSlot::Bash, kWeaponAnim1HSword, "1HM_BlockBash" },
            { ClipSlot::Recoil, kWeaponAnim1HSword, "1HM_RecoilRight" },
            { ClipSlot::Stagger, kWeaponAnim1HSword, "1HM_StaggerBackMedium" },
        };
    }

    SlotTable SlotTable::humanoid()
    {
        SlotTable table;
        table.mEntries = sHumanoid;
        table.mCount = std::size(sHumanoid);
        return table;
    }

    bool SkyrimCombat::setTestSwing(std::string_view editorId)
    {
        if (editorId.size() > kMaxCombatTestSwingName)
        {
            mTestSwing.clear();
            return false;
        }
        mTestSwing = lowerAscii(editorId);
        return true;
    }

    void SkyrimCombat::onActorAdded(const openeden::sdk::game::ActorInfo& info)
    {
        if (mTestSwing.empty() || !info.handle.valid() || lowerAscii(info.editorId) != mTestSwing)
            return;
        // The same placement again with a new session replaces the old handle.
        std::erase_if(mSwingers, [&](const openeden::sdk::game::ActorHandle& h) { return h.reference == info.handle.reference; });
        mSwingers.push_back(info.handle);
    }

    void SkyrimCombat::onActorRemoved(openeden::sdk::game::ActorHandle handle)
    {
        std::erase(mSwingers, handle);
    }

    void SkyrimCombat::onClipEvent(const openeden::sdk::game::ClipEvent& event)
    {
        (void)event;
        ++mRefusedEvents; // no rule acts on an event yet (S5-3 does)
        ++mEventsSeen;
    }

    void SkyrimCombat::onClipFinished(
        openeden::sdk::game::CombatSubject subject, std::uint32_t token, openeden::sdk::game::ClipEnd how)
    {
        (void)subject;
        (void)token;
        ++mClipsFinished;
        if (how == openeden::sdk::game::ClipEnd::Completed)
            ++mClipsCompleted;
    }

    void SkyrimCombat::onSweepResult(openeden::sdk::game::CombatSubject subject, std::uint32_t token,
        const openeden::sdk::game::SweepHit* hits, std::size_t count)
    {
        (void)subject;
        (void)token;
        (void)hits;
        (void)count;
        ++mRefusedSweepResults;
    }

    void SkyrimCombat::combatUpdate(float seconds, openeden::sdk::game::CombatRequestWriter& writer)
    {
        ++mUpdates;
        if (mSwingers.empty())
            return;
        mClock += (std::isfinite(seconds) && seconds > 0.f) ? seconds : 0.f;
        if (mClock < mNextSwing)
            return;
        mNextSwing = mClock + kTestSwingPeriod;
        const std::string_view clip = mSlots.clipFor(ClipSlot::Attack, kWeaponAnim1HSword);
        for (const openeden::sdk::game::ActorHandle& handle : mSwingers)
        {
            openeden::sdk::game::ClipRequest request;
            request.slot = ClipSlot::Attack;
            request.clip = clip;
            request.clipPerSex = false;
            request.blendSeconds = 0.1f;
            request.token = mNextToken++;
            if (writer.playClip(openeden::sdk::game::CombatSubject::of(handle), request))
                ++mSwingsRequested;
        }
    }
}
