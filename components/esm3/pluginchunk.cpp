#include "pluginchunk.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{
    void PluginChunk::load(ESMReader& esm)
    {
        mPluginId = esm.getHNString("NAME");
        esm.getHNT(mVersion, "VERS");
        esm.getSubNameIs("DATA");
        esm.getSubHeader();
        mData.resize(esm.getSubSize());
        if (!mData.empty())
            esm.getExact(mData.data(), mData.size());
    }

    void PluginChunk::save(ESMWriter& esm) const
    {
        esm.writeHNString("NAME", mPluginId);
        esm.writeHNT("VERS", mVersion);
        esm.startSubRecord("DATA");
        if (!mData.empty())
            esm.write(reinterpret_cast<const char*>(mData.data()), mData.size());
        esm.endRecord("DATA");
    }
}
