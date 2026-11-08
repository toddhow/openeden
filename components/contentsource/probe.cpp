#include "probe.hpp"

namespace Content
{
    PluginInfo probe(const Header& header, const TextConverter& convert)
    {
        const auto text = [&convert](const std::string& stored) { return convert ? convert(stored) : stored; };

        PluginInfo info;
        info.mFamily = header.mFamily;
        info.mVersion = header.mVersion;
        info.mAuthor = text(header.mAuthor);
        info.mDescription = text(header.mDescription);
        info.mMasters.reserve(header.mMasters.size());
        for (const MasterFile& master : header.mMasters)
            info.mMasters.push_back(text(master.mName));
        return info;
    }
}
