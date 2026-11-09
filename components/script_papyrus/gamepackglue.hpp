#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_GAMEPACKGLUE_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_GAMEPACKGLUE_H

// Task 0093 (H1, 2026-11-09): what a game pack needs to run the Papyrus runtime inside the game-pack tier of SDK 4.0
// (task 0084). It mirrors the ObScript glue (components/script_obscript/gamepackglue.hpp).
//
// A game pack owns a Papyrus runtime (it supplies the PapyrusWorld and the natives, see papyrusruntime.hpp). The host
// gives game packs two services (sdk/include/openeden/game/): the script host service, where a pack declares which
// language it runs, and the save chunk service, where it registers the chunks of its save data. This file is the glue
// for both:
//  - PapyrusSaveChunk is the pack's save chunk handler "script.papyrus": its bytes are exactly the runtime's save
//    blob (the blob's own format version is the chunk's version, so a runtime fix does not invalidate saves);
//  - attachToGamePack declares the language "papyrus" and registers the handler.
//
// The pack owns the handler and the host only calls it from the thread that serialises the save (main thread), between
// ticks. Nothing here throws across the SDK boundary, except write(), which throws on purpose so the host refuses the
// whole save (a runtime that could not write its state must not look like one with nothing to save).

#include <cstdint>
#include <string>
#include <string_view>

#include <openeden/game/save_chunks.hpp>
#include <openeden/game/script_host.hpp>
#include <openeden/script/host.hpp>
#include <openeden/script/papyrus.hpp>

namespace ScriptPapyrus
{
    /// The language name the pack declares through the script host service.
    inline constexpr std::string_view kLanguageName = "papyrus";

    /// The save chunk name (research note 0046: the chunk is keyed "script.papyrus").
    inline constexpr std::string_view kSaveChunkName = "script.papyrus";

    /// The pack's save chunk handler for the Papyrus runtime.
    ///
    /// Ownership: the game pack owns it and the runtime it refers to; the runtime must outlive it, and it must outlive
    /// the host's registration (the host forgets it before the pack is destroyed). Main thread only.
    class PapyrusSaveChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        explicit PapyrusSaveChunk(openeden::script::PapyrusContract& runtime)
            : mRuntime(runtime)
        {
        }

        std::string_view chunkName() const override;
        std::uint32_t chunkVersion() const override;
        void write(openeden::sdk::ChunkWriter& writer) override;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

        /// Why the last read was refused, or empty. For the pack's log: the host only learns "refused".
        const std::string& lastError() const { return mLastError; }

        /// What the last SUCCESSFUL read left out (stacks of changed scripts, keys not checked...), when the runtime
        /// offers the optional RestoreDetailsSource; default values otherwise or after a refused read. The pack should
        /// log it: a restore that dropped parked stacks is not silent data loss only if someone says so.
        const openeden::script::RestoreDetails& lastDetails() const { return mLastDetails; }

    private:
        openeden::script::PapyrusContract& mRuntime;
        std::string mLastError;
        openeden::script::RestoreDetails mLastDetails;
    };

    struct GlueResult
    {
        bool ok = false;
        std::string error; ///< Set when !ok.
    };

    /// Declare the language and register \a chunk. Pass the two services as the host handed them to the pack; a pack
    /// that is not a game pack gets nullptr for both and a message back. Nothing is declared or registered unless both
    /// services are present. Call once per pack: a second call is refused (the language is already declared).
    [[nodiscard]] GlueResult attachToGamePack(openeden::sdk::ScriptHostService* scriptHost,
        openeden::sdk::SaveChunkService* saveChunks, PapyrusSaveChunk& chunk) noexcept;
}

#endif
