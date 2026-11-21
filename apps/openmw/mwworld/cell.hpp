#ifndef OPENW_MWORLD_CELL
#define OPENW_MWORLD_CELL

#include <array>
#include <optional>

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec2.h>

#include <components/esm/refid.hpp>
#include <components/esmbridge/esmbridge.hpp>
#include <components/esmbridge/exteriorcelllocation.hpp>

#include <components/esm3/loadcell.hpp>

namespace ESM
{
    struct Cell;
}

namespace ESM4
{
    struct Cell;
}

namespace MWWorld
{
    class CellStore;

    class Cell : public ESM::CellVariant
    {
        struct MoodData
        {
            uint32_t mAmbiantColor;
            uint32_t mDirectionalColor;
            uint32_t mFogColor;
            float mFogDensity;
        };

    public:
        explicit Cell(const ESM4::Cell& cell);
        explicit Cell(const ESM::Cell& cell);
        /// Task 0125: an exterior record of a game pack's neutral world: the worldspace is the given one (its cell size
        /// comes from ESM::getCellSize) and the cell has no water.
        Cell(const ESM::Cell& cell, const ESM::RefId& worldspace);
        /// Task 0129: the same with the water the pack gives the square (a height in units), or none.
        Cell(const ESM::Cell& cell, const ESM::RefId& worldspace, std::optional<float> waterHeight);

        int getGridX() const { return mGridPos.x; }
        int getGridY() const { return mGridPos.y; }
        bool isExterior() const { return mIsExterior; }
        bool isQuasiExterior() const { return mIsQuasiExterior; }
        bool hasWater() const { return mHasWater; }
        bool noSleep() const { return mNoSleep; }
        const ESM::RefId& getRegion() const { return mRegion; }
        std::string_view getNameId() const { return mNameID; }
        std::string_view getDisplayName() const { return mDisplayname; }
        std::string_view getDescription() const { return mDescription; }
        const MoodData& getMood() const { return mMood; }
        float getWaterHeight() const { return mWaterHeight; }
        /// Task 0355 stage d: the linear fog (start, end, in units) a game pack's interior asks for, or none. Set by
        /// WorldModel when it builds a neutral cell; FogManager::configure reads it. Morrowind cells never have one.
        bool hasNeutralFog() const { return mHasNeutralFog; }
        float getNeutralFogStart() const { return mNeutralFogStart; }
        float getNeutralFogEnd() const { return mNeutralFogEnd; }
        /// Task 0355 (follow-up): the fog power and maximum (share = pow(linear, power) * max); 1 and 1 is the linear fog.
        float getNeutralFogPower() const { return mNeutralFogPower; }
        float getNeutralFogMax() const { return mNeutralFogMax; }
        void setNeutralFog(float start, float end, float power = 1.f, float max = 1.f)
        {
            mHasNeutralFog = true;
            mNeutralFogStart = start;
            mNeutralFogEnd = end;
            mNeutralFogPower = power;
            mNeutralFogMax = max;
        }
        /// Task 0362 item 1: the fog's FAR colour (0..1 floats) of a game pack's interior; getMood().mFogColor is the near
        /// colour. None when the pack gave none.
        bool hasNeutralFogFarColor() const { return mHasNeutralFogFar; }
        const std::array<float, 3>& getNeutralFogFarColor() const { return mNeutralFogFar; }
        void setNeutralFogFarColor(const std::array<float, 3>& colour)
        {
            mHasNeutralFogFar = true;
            mNeutralFogFar = colour;
        }
        const ESM::RefId& getId() const { return mId; }
        ESM::RefId getWorldSpace() const { return mIsExterior ? mParent : mId; }

        ESM::ExteriorCellLocation getExteriorCellLocation() const
        {
            return ESM::ExteriorCellLocation(mGridPos.x, mGridPos.y, getWorldSpace());
        }

    private:
        bool mIsExterior;
        bool mIsQuasiExterior;
        bool mHasWater;
        bool mNoSleep;

        vsg::ivec2 mGridPos;
        std::string mDisplayname; // How the game displays it
        std::string mNameID; // The name that will be used by the script and console commands
        ESM::RefId mRegion;
        ESM::RefId mId;
        ESM::RefId mParent;
        float mWaterHeight;
        std::string mDescription;
        MoodData mMood;
        bool mHasNeutralFog = false;
        float mNeutralFogStart = 0.f;
        float mNeutralFogEnd = 0.f;
        float mNeutralFogPower = 1.f;
        float mNeutralFogMax = 1.f;
        bool mHasNeutralFogFar = false;
        std::array<float, 3> mNeutralFogFar{ 0.f, 0.f, 0.f };
    };
}

#endif
