#include "gamepackglue.hpp"

#include <exception>
#include <format>
#include <stdexcept>

#include "papyrussave.hpp"

namespace ScriptPapyrus
{
    std::string_view PapyrusSaveChunk::chunkName() const
    {
        return kSaveChunkName;
    }

    std::uint32_t PapyrusSaveChunk::chunkVersion() const
    {
        // The chunk's version is the blob's own format version, so a runtime fix does not invalidate saves.
        return Save::kFormatVersion;
    }

    void PapyrusSaveChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const openeden::script::SaveBlob blob = mRuntime.serialise();
        if (blob.formatVersion == 0 || blob.bytes.empty())
        {
            // The runtime could not write its state (it is inside a native or a tick, or over its caps; the reason is
            // in its log). Writing nothing would save the game as if it had no scripts; an exception makes the host
            // refuse the whole save with this message (SaveChunkServiceImpl::collect).
            throw std::runtime_error("the Papyrus runtime could not write its state");
        }
        if (blob.formatVersion != Save::kFormatVersion)
        {
            // A chunk is read back by the version it is filed under; a blob of another version would be unreadable.
            throw std::runtime_error(std::format(
                "the Papyrus runtime wrote state version {}, this pack files version {}", blob.formatVersion,
                Save::kFormatVersion));
        }
        writer.write(blob.bytes.data(), blob.bytes.size());
    }

    bool PapyrusSaveChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mLastError.clear();
        mLastDetails = openeden::script::RestoreDetails{};
        try
        {
            const std::size_t size = reader.remaining();
            if (size > Save::kMaxBlobBytes)
            {
                mLastError = std::format(
                    "the Papyrus save chunk is too large ({} bytes; the limit is {})", size, Save::kMaxBlobBytes);
                return false;
            }
            openeden::script::SaveBlob blob;
            blob.formatVersion = version;
            blob.bytes.resize(size);
            if (size != 0 && !reader.read(blob.bytes.data(), size))
            {
                mLastError = "the Papyrus save chunk could not be read";
                return false;
            }
            const openeden::script::RestoreResult result = mRuntime.restore(blob);
            if (!result.ok)
            {
                // The runtime changed nothing: the pack decides what a refused script state means for the load.
                mLastError = result.error.format();
                return false;
            }
            if (const auto* source = dynamic_cast<const openeden::script::RestoreDetailsSource*>(&mRuntime))
            {
                mLastDetails = source->lastRestoreDetails();
            }
            return true;
        }
        catch (const std::exception& e)
        {
            mLastError = std::format("the Papyrus save chunk could not be restored ({})", e.what());
        }
        catch (...)
        {
            mLastError = "the Papyrus save chunk could not be restored (unknown error)";
        }
        return false;
    }

    GlueResult attachToGamePack(openeden::sdk::ScriptHostService* scriptHost,
        openeden::sdk::SaveChunkService* saveChunks, PapyrusSaveChunk& chunk) noexcept
    {
        GlueResult result;
        try
        {
            if (scriptHost == nullptr || saveChunks == nullptr)
            {
                result.error = "the Papyrus runtime needs the script host service and the save chunk service, which "
                               "only a game pack receives";
                return result;
            }
            if (!scriptHost->declareLanguage(kLanguageName))
            {
                result.error = std::format("the script language '{}' is already declared", kLanguageName);
                return result;
            }
            if (!saveChunks->registerHandler(chunk))
            {
                result.error = std::format("the save chunk '{}' is already registered", kSaveChunkName);
                return result;
            }
            result.ok = true;
        }
        catch (const std::exception& e)
        {
            result.ok = false;
            result.error = std::format("the Papyrus runtime could not be attached to the game pack ({})", e.what());
        }
        catch (...)
        {
            result.ok = false;
            result.error = "the Papyrus runtime could not be attached to the game pack (unknown error)";
        }
        return result;
    }
}
