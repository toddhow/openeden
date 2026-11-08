#ifndef OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_GAMEPACKGLUE_H
#define OPENEDEN_COMPONENTS_SCRIPT_OBSCRIPT_GAMEPACKGLUE_H

// Task 0092 item 4: what a game pack needs to run the ObScript runtime inside the game-pack tier of SDK 4.0 (task
// 0084).
//
// A game pack owns an ObScript runtime (it supplies the world and the functions, see obscriptruntime.hpp). The host
// gives game packs two services (sdk/include/openeden/game/): the script host service, where a pack declares which
// language it runs, and the save chunk service, where it registers the chunks of its save data. This file is the glue
// for both:
//  - ObScriptSaveChunk is the pack's save chunk handler "script.obscript": its bytes are exactly the runtime's save
//    blob (the runtime's own format version is the chunk's version, so a runtime fix does not invalidate saves);
//  - attachToGamePack declares the language "obscript" and registers the handler.
//
// The runtime holds no authority over the save: the pack owns the handler and the host only calls it from the thread
// that serialises the save (main thread). Nothing here throws across the SDK boundary.

#include <cstdint>
#include <string>
#include <string_view>

#include <openeden/game/save_chunks.hpp>
#include <openeden/game/script_host.hpp>
#include <openeden/script/obscript.hpp>

namespace ScriptObscript
{
    /// The language name the pack declares through the script host service.
    inline constexpr std::string_view kLanguageName = "obscript";

    /// The save chunk name (research note 0046 section 4.2: the chunk is keyed "script.obscript").
    inline constexpr std::string_view kSaveChunkName = "script.obscript";

    /// The pack's save chunk handler for the ObScript runtime.
    ///
    /// Ownership: the game pack owns it and the runtime it refers to; the runtime must outlive it, and it must outlive
    /// the host's registration (the host forgets it before the pack is destroyed). Main thread only.
    class ObScriptSaveChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        explicit ObScriptSaveChunk(openeden::script::ObScriptContract& runtime)
            : mRuntime(runtime)
        {
        }

        std::string_view chunkName() const override;
        std::uint32_t chunkVersion() const override;
        void write(openeden::sdk::ChunkWriter& writer) override;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

        /// Why the last read was refused, or empty. For the pack's log: the host only learns "refused".
        const std::string& lastError() const { return mLastError; }

    private:
        openeden::script::ObScriptContract& mRuntime;
        std::string mLastError;
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
        openeden::sdk::SaveChunkService* saveChunks, ObScriptSaveChunk& chunk) noexcept;
}

#endif
