#include "landmanager.hpp"

#include "../mwbase/environment.hpp"
#include "../mwworld/esmstore.hpp"

namespace MWRender
{

    LandManager::LandManager(int loadFlags)
        : mLoadFlags(loadFlags)
    {
    }

    vsg::ref_ptr<ESMTerrain::LandObject> LandManager::create(ESM::ExteriorCellLocation cellIndex) const
    {
        // OpenEden: ESM4 terrain is not ported (task 0012). A worldspace of its own has no land here, rather than
        // Morrowind's at the same coordinates.
        if (ESM::isEsm4Ext(cellIndex.mWorldspace))
            return vsg::ref_ptr{ new ESMTerrain::LandObject() };
        const ESM::Land* land = MWBase::Environment::get().getESMStore()->get<ESM::Land>().search(cellIndex.mX, cellIndex.mY);
        if (land)
            return vsg::ref_ptr{ new ESMTerrain::LandObject(*land, mLoadFlags) };
        else
            return vsg::ref_ptr{ new ESMTerrain::LandObject() };
    }
 
    vsg::ref_ptr<ESMTerrain::LandObject> LandManager::getLand(ESM::ExteriorCellLocation cellIndex) const
    {
        return mCache.getOrCreate(cellIndex, *this);
    }
}
