// Task 0128 (milestone M-S1): locks in a neutral session. The pure decision and the session's lock state. Written from
// the task spec before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include "apps/openmw/mwworld/neutrallock.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::NeutralLockOutcome;
    using MWWorld::NeutralLockState;

    WorldObject lockedObject(WorldId key)
    {
        WorldObject o;
        o.id = 77;
        o.kind = ObjectKind::Door;
        LockComponent lock;
        lock.locked = true;
        lock.level = 25;
        lock.key = key;
        o.lock = lock;
        return o;
    }

    bool never(WorldId)
    {
        return false;
    }

    TEST(NeutralLockDecision, an_object_with_no_lock_is_not_locked)
    {
        WorldObject o;
        EXPECT_EQ(MWWorld::decideNeutralLock(o, false, false, never), NeutralLockOutcome::NotLocked);
    }

    TEST(NeutralLockDecision, a_lock_component_that_is_not_set_is_not_locked)
    {
        WorldObject o = lockedObject(9);
        o.lock->locked = false;
        EXPECT_EQ(MWWorld::decideNeutralLock(o, false, false, never), NeutralLockOutcome::NotLocked);
    }

    TEST(NeutralLockDecision, a_locked_object_with_no_key_held_stays_locked)
    {
        EXPECT_EQ(MWWorld::decideNeutralLock(lockedObject(9), false, false, never), NeutralLockOutcome::Locked);
    }

    TEST(NeutralLockDecision, the_matching_key_unlocks)
    {
        WorldId asked = kNoWorldId;
        const auto outcome = MWWorld::decideNeutralLock(lockedObject(9), false, false, [&](WorldId key) {
            asked = key;
            return key == 9;
        });
        EXPECT_EQ(outcome, NeutralLockOutcome::UnlockedByKey);
        EXPECT_EQ(asked, 9u);
    }

    TEST(NeutralLockDecision, another_key_does_not_unlock)
    {
        const auto outcome
            = MWWorld::decideNeutralLock(lockedObject(9), false, false, [](WorldId key) { return key == 10; });
        EXPECT_EQ(outcome, NeutralLockOutcome::Locked);
    }

    TEST(NeutralLockDecision, a_lock_that_names_no_key_cannot_be_opened_by_any_key)
    {
        bool asked = false;
        const auto outcome = MWWorld::decideNeutralLock(lockedObject(kNoWorldId), false, false, [&](WorldId) {
            asked = true;
            return true;
        });
        EXPECT_EQ(outcome, NeutralLockOutcome::Locked);
        EXPECT_FALSE(asked);
    }

    TEST(NeutralLockDecision, an_earlier_unlock_stays_open_without_asking_for_the_key)
    {
        bool asked = false;
        const auto outcome = MWWorld::decideNeutralLock(lockedObject(9), false, true, [&](WorldId) {
            asked = true;
            return false;
        });
        EXPECT_EQ(outcome, NeutralLockOutcome::Open);
        EXPECT_FALSE(asked);
    }

    // toddhow, 2026-11-12: a lock of level 0 that names no key is not a lock (the Angarvunde draugr chests); a level 0
    // lock with a key still needs the key.
    TEST(NeutralLockDecision, level_zero_with_no_key_is_not_locked)
    {
        WorldObject o = lockedObject(kNoWorldId);
        o.lock->level = 0;
        EXPECT_EQ(MWWorld::decideNeutralLock(o, false, false, never), NeutralLockOutcome::NotLocked);
        o = lockedObject(9);
        o.lock->level = 0;
        EXPECT_EQ(MWWorld::decideNeutralLock(o, false, false, never), NeutralLockOutcome::Locked);
    }

    TEST(NeutralLockDecision, the_developer_aid_opens_everything)
    {
        EXPECT_EQ(MWWorld::decideNeutralLock(lockedObject(kNoWorldId), true, false, never), NeutralLockOutcome::Open);
    }

    TEST(NeutralLockDecision, the_developer_aid_does_not_make_an_unlocked_object_locked)
    {
        WorldObject o;
        EXPECT_EQ(MWWorld::decideNeutralLock(o, true, false, never), NeutralLockOutcome::NotLocked);
    }

    TEST(NeutralLockDecision, a_null_key_check_means_no_key)
    {
        EXPECT_EQ(MWWorld::decideNeutralLock(lockedObject(9), false, false, {}), NeutralLockOutcome::Locked);
    }

    TEST(NeutralLockState, keys_are_per_game_and_per_form)
    {
        NeutralLockState s;
        EXPECT_FALSE(s.hasKey("skyrimse", 9));
        s.giveKey("skyrimse", 9);
        EXPECT_TRUE(s.hasKey("skyrimse", 9));
        EXPECT_FALSE(s.hasKey("skyrimse", 10));
        EXPECT_FALSE(s.hasKey("other", 9));
        EXPECT_EQ(s.keyCount(), 1u);
    }

    TEST(NeutralLockState, unlocks_are_per_game_and_per_placement_and_do_not_give_keys)
    {
        NeutralLockState s;
        EXPECT_FALSE(s.isUnlocked("skyrimse", 77));
        s.unlock("skyrimse", 77);
        EXPECT_TRUE(s.isUnlocked("skyrimse", 77));
        EXPECT_FALSE(s.isUnlocked("skyrimse", 78));
        EXPECT_FALSE(s.isUnlocked("other", 77));
        EXPECT_FALSE(s.hasKey("skyrimse", 77));
        EXPECT_EQ(s.unlockedCount(), 1u);
    }

    TEST(NeutralLockState, clear_forgets_keys_and_unlocks)
    {
        NeutralLockState s;
        s.giveKey("g", 1);
        s.unlock("g", 2);
        s.clear();
        EXPECT_FALSE(s.hasKey("g", 1));
        EXPECT_FALSE(s.isUnlocked("g", 2));
    }

    TEST(NeutralLockState, keys_from_text_reads_hexadecimal_ids)
    {
        NeutralLockState s;
        EXPECT_EQ(s.giveKeysFromText("skyrimse:1A,skyrimse:0x2b,other:ff"), 3u);
        EXPECT_TRUE(s.hasKey("skyrimse", 0x1A));
        EXPECT_TRUE(s.hasKey("skyrimse", 0x2B));
        EXPECT_TRUE(s.hasKey("other", 0xFF));
    }

    TEST(NeutralLockState, keys_from_text_skips_malformed_entries)
    {
        NeutralLockState s;
        EXPECT_EQ(s.giveKeysFromText("novalue,:12,g:,g:zz,g:12x,g:5,,g:0x"), 1u);
        EXPECT_TRUE(s.hasKey("g", 5));
        EXPECT_EQ(s.keyCount(), 1u);
    }

    TEST(NeutralLockState, keys_from_text_refuses_the_null_id_and_an_empty_text)
    {
        NeutralLockState s;
        EXPECT_EQ(s.giveKeysFromText(""), 0u);
        EXPECT_EQ(s.giveKeysFromText("g:0"), 0u);
        EXPECT_EQ(s.keyCount(), 0u);
    }

    TEST(NeutralLockState, a_new_game_forgets_the_old_session_and_reads_the_keys_again)
    {
        MWWorld::neutralLockState().giveKey("g", 1);
        MWWorld::neutralLockState().unlock("g", 2);
        EXPECT_EQ(MWWorld::resetNeutralLockState("g:7,g:8"), 2u);
        EXPECT_FALSE(MWWorld::neutralLockState().hasKey("g", 1));
        EXPECT_FALSE(MWWorld::neutralLockState().isUnlocked("g", 2));
        EXPECT_TRUE(MWWorld::neutralLockState().hasKey("g", 7));
        EXPECT_TRUE(MWWorld::neutralLockState().hasKey("g", 8));
        EXPECT_EQ(MWWorld::resetNeutralLockState(""), 0u);
        EXPECT_EQ(MWWorld::neutralLockState().keyCount(), 0u);
    }

    TEST(NeutralLockState, the_session_state_is_one_object)
    {
        EXPECT_EQ(&MWWorld::neutralLockState(), &MWWorld::neutralLockState());
    }

    TEST(NeutralLockMessages, a_refusal_and_an_unlock_have_their_own_texts_and_the_rest_none)
    {
        EXPECT_EQ(MWWorld::neutralLockMessageKey(NeutralLockOutcome::Locked), "NeutralDoorLocked");
        EXPECT_EQ(MWWorld::neutralLockMessageKey(NeutralLockOutcome::UnlockedByKey), "NeutralLockUnlocked");
        EXPECT_TRUE(MWWorld::neutralLockMessageKey(NeutralLockOutcome::Open).empty());
        EXPECT_TRUE(MWWorld::neutralLockMessageKey(NeutralLockOutcome::NotLocked).empty());
    }
}
