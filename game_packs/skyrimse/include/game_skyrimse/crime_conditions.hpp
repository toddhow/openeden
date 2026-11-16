#ifndef OPENEDEN_GAME_SKYRIMSE_CRIME_CONDITIONS_H
#define OPENEDEN_GAME_SKYRIMSE_CRIME_CONDITIONS_H

// Task 0258: what the crime rules tell a conversation's conditions. A small header of its own so that the dialogue
// code and the crime service can both name it without including each other.

#include <game_skyrimse/records.hpp>

#include <functional>

namespace game_skyrimse
{
    /// Task 0258: what the crime rules tell a conversation's conditions (GetCrimeGold, CanPayCrimeGold, GetAlarmed).
    /// `speakerBase` is the base of the speaker being asked about; `faction` 0 means the speaker's own crime faction.
    /// Every function may be empty: the condition then keeps its fresh-game answer (no crime gold, cannot pay, not
    /// alarmed). Main thread.
    struct CrimeConditionSource
    {
        std::function<float(FormId speakerBase, FormId faction)> mCrimeGold;
        std::function<bool(FormId speakerBase, FormId faction)> mCanPay;
        std::function<bool(FormId speakerBase)> mAlarmed;
    };
}

#endif
