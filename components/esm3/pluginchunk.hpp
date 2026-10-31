#ifndef OPENEDEN_COMPONENTS_ESM3_PLUGINCHUNK_H
#define OPENEDEN_COMPONENTS_ESM3_PLUGINCHUNK_H

#include <cstdint>
#include <string>
#include <vector>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    /// One OpenEden plugin's data in a saved game: record REC_OEPL with subrecords NAME (plugin ID), VERS (the
    /// plugin's chunk version) and DATA (its bytes, which only the plugin can read).
    struct PluginChunk
    {
        std::string mPluginId;
        std::uint32_t mVersion = 0;
        std::vector<std::uint8_t> mData;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}

#endif
