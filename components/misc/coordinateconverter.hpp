#ifndef OPENMW_COMPONENTS_MISC_COORDINATECONVERTER_H
#define OPENMW_COMPONENTS_MISC_COORDINATECONVERTER_H

#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadpgrd.hpp>
#include <components/esm4/loadcell.hpp>
#include <components/esmbridge/esmbridge.hpp>
#include <components/misc/constants.hpp>

#include <vsg/maths/vec3.h>

namespace Misc
{
    /// \brief convert coordinates between world and local cell
    class CoordinateConverter
    {
    public:
        explicit CoordinateConverter(int cellX, int cellY)
            : mCellX(cellX)
            , mCellY(cellY)
        {
        }

        /// in-place conversion from local to world
        void toWorld(ESM::Pathgrid::Point& point) const
        {
            point.mX += mCellX;
            point.mY += mCellY;
        }

        /// in-place conversion from world to local
        void toLocal(ESM::Pathgrid::Point& point) const
        {
            point.mX -= mCellX;
            point.mY -= mCellY;
        }

        ESM::Pathgrid::Point toWorldPoint(ESM::Pathgrid::Point point) const
        {
            toWorld(point);
            return point;
        }

        ESM::Pathgrid::Point toLocalPoint(ESM::Pathgrid::Point point) const
        {
            toLocal(point);
            return point;
        }

        /// in-place conversion from local to world
        void toWorld(vsg::vec3& point) const
        {
            point.x += static_cast<float>(mCellX);
            point.y += static_cast<float>(mCellY);
        }

        /// in-place conversion from world to local
        void toLocal(vsg::vec3& point) const
        {
            point.x -= static_cast<float>(mCellX);
            point.y -= static_cast<float>(mCellY);
        }

        vsg::vec3 toWorldVec3(const vsg::vec3& point) const
        {
            vsg::vec3 result = point;
            toWorld(result);
            return result;
        }

        vsg::vec3 toLocalVec3(const vsg::vec3& point) const
        {
            vsg::vec3 result = point;
            toLocal(result);
            return result;
        }

    private:
        int mCellX;
        int mCellY;
    };

    template <class T>
    CoordinateConverter makeCoordinateConverterImpl(const T& cell)
    {
        if (cell.isExterior())
            return CoordinateConverter(cell.sSize * cell.getGridX(), cell.sSize * cell.getGridY());
        return CoordinateConverter(0, 0);
    }

    inline CoordinateConverter makeCoordinateConverter(const ESM::Cell& cell)
    {
        return makeCoordinateConverterImpl(cell);
    }

    inline CoordinateConverter makeCoordinateConverter(const ESM4::Cell& cell)
    {
        return makeCoordinateConverterImpl(cell);
    }

    inline CoordinateConverter makeCoordinateConverter(const ESM::CellVariant& cell)
    {
        return visit([](const auto& v) { return makeCoordinateConverterImpl(v); }, cell);
    }
}

#endif
