#include "gamepackglue.hpp"

#include <exception>
#include <format>
#include <vector>

#include "obscriptlimits.hpp"

namespace ScriptObscript
{
    std::string_view ObScriptSaveChunk::chunkName() const
    {
        return kSaveChunkName;
    }

    std::uint32_t ObScriptSaveChunk::chunkVersion() const
    {
        // The chunk's version is the runtime's own blob format version, so a runtime fix does not invalidate saves.
        return Limits::kSaveFormatVersion;
    }

    void ObScriptSaveChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const openeden::script::SaveBlob blob = mRuntime.serialise();
        if (!blob.bytes.empty())
        {
            writer.write(blob.bytes.data(), blob.bytes.size());
        }
    }

    bool ObScriptSaveChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mLastError.clear();
        try
        {
            const std::size_t size = reader.remaining();
            if (size > Limits::kMaxSaveBlobBytes)
            {
                mLastError = std::format("the ObScript save chunk is too large ({} bytes; the limit is {})", size,
                    Limits::kMaxSaveBlobBytes);
                return false;
            }
            openeden::script::SaveBlob blob;
            blob.formatVersion = version;
            blob.bytes.resize(size);
            if (size != 0 && !reader.read(blob.bytes.data(), size))
            {
                mLastError = "the ObScript save chunk could not be read";
                return false;
            }
            const openeden::script::RestoreResult result = mRuntime.restore(blob);
            if (!result.ok)
            {
                mLastError = result.error.format();
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            mLastError = std::format("the ObScript save chunk could not be restored ({})", e.what());
        }
        catch (...)
        {
            mLastError = "the ObScript save chunk could not be restored (unknown error)";
        }
        return false;
    }

    GlueResult attachToGamePack(openeden::sdk::ScriptHostService* scriptHost,
        openeden::sdk::SaveChunkService* saveChunks, ObScriptSaveChunk& chunk) noexcept
    {
        GlueResult result;
        try
        {
            if (scriptHost == nullptr || saveChunks == nullptr)
            {
                result.error
                    = "the ObScript runtime needs the script host service and the save chunk service, which "
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
            result.error = std::format("the ObScript runtime could not be attached to the game pack ({})", e.what());
        }
        catch (...)
        {
            result.ok = false;
            result.error = "the ObScript runtime could not be attached to the game pack (unknown error)";
        }
        return result;
    }
}
