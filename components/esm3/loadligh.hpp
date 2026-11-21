#ifndef OPENMW_ESM_LIGH_H
#define OPENMW_ESM_LIGH_H

#include <string>

#include "components/esm/defs.hpp"
#include "components/esm/refid.hpp"

namespace ESM
{

    class ESMReader;
    class ESMWriter;

    /*
     * Lights. Includes static light sources and also carryable candles
     * and torches.
     */

    struct Light
    {
        constexpr static RecNameInts sRecordId = REC_LIGH;

        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "Light"; }

        enum Flags
        {
            Dynamic = 0x001,
            Carry = 0x002, // Can be carried
            Negative = 0x004, // Negative light - i.e. darkness
            Flicker = 0x008,
            Fire = 0x010,
            OffDefault
            = 0x020, // Off by default - does not burn while placed in a cell, but can burn when equipped by an NPC
            FlickerSlow = 0x040,
            Pulse = 0x080,
            PulseSlow = 0x100,
            // OpenEden (task 0355 stage b), NOT a bit of the file format: a light made by a game pack's neutral world
            // (MWWorld::makeNeutralEsmLight) whose brightness is more than its 8 bit colour can hold. mData.mWeight
            // then holds the multiplier MWAnim::addLight applies to the colour. No file sets it.
            NeutralGain = 0x10000,
            // OpenEden (task 0355 stage h), NOT a bit of the file format: the light has its own flicker or pulse (a
            // game pack's period and amplitude, packed in mData.mTime, the seed in mData.mValue; see
            // components/mwanimation/lightflicker.hpp). No file sets it.
            NeutralFlicker = 0x20000,
            // OpenEden (task 0355 stage g), NOT bits of the file format either: set by MWWorld::makeNeutralEsmLight.
            // A spot light (the cone is mNeutralSpotFov degrees wide and points along the object's local +X axis),
            // and the game's shadow rule (casts a point light shadow / never does); neither bit means "as before".
            NeutralSpot = 0x40000,
            NeutralShadowCasts = 0x80000,
            NeutralShadowNever = 0x100000
        };

        struct LHDTstruct
        {
            float mWeight;
            int32_t mValue;
            int32_t mTime; // Duration
            int32_t mRadius;
            uint32_t mColor; // 4-byte rgba value
            int32_t mFlags;
        }; // Size = 24 bytes

        LHDTstruct mData;
        /// Task 0355 stage g: the full opening angle of a NeutralSpot light, in degrees. Not part of the record's file
        /// data: blank() sets 0 and load() leaves it 0.
        float mNeutralSpotFov = 0.f;
        /// Task 0362 item 2: how far (units) a NeutralFlicker light wanders from its place, the movement amplitude of
        /// the game's record, already made safe by MWWorld::neutralLightMovement. Not part of the file data: blank()
        /// sets 0 and load() leaves it 0.
        float mNeutralMovement = 0.f;

        uint32_t mRecordFlags;
        std::string mModel, mIcon, mName;
        ESM::RefId mId, mSound, mScript;

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();
        ///< Set record to default state (does not touch the ID).
    };
}
#endif
