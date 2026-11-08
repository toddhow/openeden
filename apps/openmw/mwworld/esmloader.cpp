#include "esmloader.hpp"
#include "esmstore.hpp"

#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>

#include <components/contentsource/plugin.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm4/reader.hpp>
#include <components/files/conversion.hpp>
#include <components/files/openfile.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/resource/resourcesystem.hpp>

#include "../mwbase/environment.hpp"

namespace MWWorld
{

    EsmLoader::EsmLoader(MWWorld::ESMStore& store, ESM::ReadersCache& readers, ToUTF8::Utf8Encoder* encoder,
        std::vector<int>& esmVersions)
        : mReaders(readers)
        , mStore(store)
        , mEncoder(encoder)
        , mDialogue(nullptr) // A content file containing INFO records without a DIAL record appends them to the
                             // previous file's dialogue
        , mESMVersions(esmVersions)
    {
    }

    void EsmLoader::load(const std::filesystem::path& filepath, int& index, MWState::Loading& state)
    {

        // Which family the file is comes from its first four bytes, as before Content::Plugin existed (task 0083): the
        // strict header parse of Content::Plugin::open would refuse files (a short HEDR, many masters) that the typed
        // readers below decide about themselves.
        const std::optional<Content::Family> family = [&filepath] {
            const auto probeStream = Files::openBinaryInputFileStream(filepath);
            return Content::probeFamily(*probeStream);
        }();
        if (!family.has_value())
        {
            throw std::runtime_error(Files::pathToUnicodeString(filepath.filename())
                + ": not a TES3 or TES4 plugin (the first four bytes are neither)");
        }

        switch (*family)
        {
            case Content::Family::Tes3:
            {
                const ESM::ReadersCache::BusyItem reader = mReaders.get(static_cast<std::size_t>(index));
                reader->setEncoder(mEncoder);
                reader->setIndex(index);
                reader->open(filepath);
                reader->resolveParentFileIndices(mReaders);

                const std::vector<int>& parentIndices = reader->getParentFileIndices();
                assert(reader->getGameFiles().size() == parentIndices.size());
                for (std::size_t i = 0, n = parentIndices.size(); i < n; ++i)
                    if (parentIndices[i] == reader->getIndex())
                        throw std::runtime_error("File " + Files::pathToUnicodeString(reader->getName()) + " asks for parent file "
                + reader->getGameFiles()[i].name
                + ", but it is not available or has been loaded in the wrong order. "
                  "Please run the launcher to fix this issue.");

                mESMVersions[index] = reader->getVer();
                mStore.load(*reader, state, mDialogue);

                if (!mMasterFileFormat.has_value()
                    && (Misc::StringUtils::ciEndsWith(reader->getName().string(), ".esm")
                        || Misc::StringUtils::ciEndsWith(reader->getName().string(), ".omwgame")))
                    mMasterFileFormat = reader->getFormatVersion();
                break;
            }
            case Content::Family::Tes4:
            {
                ESM4::Reader reader(Files::openBinaryInputFileStream(filepath), filepath,
                    MWBase::Environment::get().getResourceSystem()->getVFS(),
                    mEncoder != nullptr ? &mEncoder->getStatelessEncoder() : nullptr);
                reader.setModIndex(index);
                reader.updateModIndices(mNameToIndex);
                mStore.loadESM4(reader);
                break;
            }
        }
        mNameToIndex[Misc::StringUtils::lowerCase(Files::pathToUnicodeString(filepath.filename()))] = index;
    }

} /* namespace MWWorld */
