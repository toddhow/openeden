// Task 0083: prints what ESMStore holds after the user's own Morrowind, Tribunal and Bloodmoon are loaded through
// MWWorld::EsmLoader, so a run before and after a change to the load path can be compared with cmp. It reads the
// folder named by OPENEDEN_TEST_MORROWIND_DATA ("Data Files") and writes aggregates only (counts and a hash of
// the identifiers); with the variable unset it does nothing. No game data is in the repository.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <components/esm3/loadcell.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/files/conversion.hpp>

#include "apps/openmw/mwstate/loading.hpp"
#include "apps/openmw/mwworld/esmloader.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/store.hpp"

namespace
{
    struct CountsLoading : MWState::Loading
    {
        bool run(float) override { return false; }
    };

    std::uint64_t fnv1a(std::uint64_t hash, const std::string& text)
    {
        for (const unsigned char c : text)
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    TEST(MwWorldEsmStoreCounts, print_counts_for_the_users_morrowind)
    {
        const char* folder = std::getenv("OPENEDEN_TEST_MORROWIND_DATA");
        if (folder == nullptr || *folder == '\0')
        {
            std::cout << "OPENEDEN_TEST_MORROWIND_DATA is not set, skipping" << std::endl;
            return;
        }

        CountsLoading loading;
        MWWorld::ESMStore store;
        ESM::ReadersCache readers;
        std::vector<int> versions(3, 0);
        MWWorld::EsmLoader loader(store, readers, nullptr, versions);
        int index = 0;
        for (const char* name : { "Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm" })
        {
            loader.load(std::filesystem::path(folder) / name, index, loading);
            ++index;
        }
        store.setUp();
        store.validateRecords(readers);

        std::ostringstream out;
        out << "refcount ex_common_door_01 " << store.getRefCount(ESM::RefId::stringRefId("ex_common_door_01")) << '\n';
        out << "refcount barrel_01 " << store.getRefCount(ESM::RefId::stringRefId("barrel_01")) << '\n';
        out << "format versions " << versions[0] << ' ' << versions[1] << ' ' << versions[2] << '\n';
        std::size_t storeIndex = 0;
        for (const MWWorld::DynamicStore* dynamicStore : store)
        {
            std::vector<ESM::RefId> ids;
            dynamicStore->listIdentifier(ids);
            std::vector<std::string> names;
            names.reserve(ids.size());
            for (const ESM::RefId& id : ids)
            {
                names.push_back(id.toDebugString());
            }
            std::sort(names.begin(), names.end());
            std::uint64_t hash = 14695981039346656037ull;
            std::uint64_t refHash = 14695981039346656037ull;
            std::size_t referenced = 0;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                hash = fnv1a(hash, names[i]);
            }
            for (const ESM::RefId& id : ids)
            {
                const int count = store.getRefCount(id);
                referenced += count != 0 ? 1 : 0;
                refHash = fnv1a(refHash, id.toDebugString() + ':' + std::to_string(count));
            }
            // refHash depends on the order of listIdentifier, which is the store's own order: that is part of
            // what is compared.
            out << "store " << storeIndex << " size " << dynamicStore->getSize() << " ids " << ids.size() << " idhash "
                << hash << " referenced " << referenced << " refhash " << refHash << '\n';
            ++storeIndex;
        }

        std::size_t infos = 0;
        std::uint64_t infoHash = 14695981039346656037ull;
        for (const ESM::Dialogue& dialogue : store.get<ESM::Dialogue>())
        {
            infos += dialogue.mInfo.size();
            for (const auto& info : dialogue.mInfo)
            {
                infoHash = fnv1a(infoHash, dialogue.mId.toDebugString() + '/' + info.mId.toDebugString());
            }
        }
        out << "dialogue infos " << infos << " infohash " << infoHash << '\n';
        out << "lua content " << store.getLuaScriptsCfg().mScripts.size() << '\n';

        // The "first cell": the cell the new game starts in, by name, with the plugin positions it has.
        const ESM::Cell* cell = store.get<ESM::Cell>().search(-2, -9);
        if (cell != nullptr)
        {
            out << "cell " << cell->mName << " x " << cell->mData.mX << " y " << cell->mData.mY << " contexts "
                << cell->mContextList.size() << '\n';
            for (const auto& context : cell->mContextList)
            {
                out << "  context index " << context.index << " pos " << context.filePos << '\n';
            }
        }
        else
        {
            out << "cell Seyda Neen not found\n";
        }

        std::cout << out.str();
        if (const char* path = std::getenv("OPENEDEN_TEST_PARITY_OUT"); path != nullptr && *path != '\0')
        {
            std::ofstream file(path, std::ios::binary);
            file << out.str();
        }
    }
}
