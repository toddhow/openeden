#pragma once

// The face editor's asset (task 0147): one opas.facecurves file (.facecurves.json), its voice audio, editable on
// Studio's timeline as sparse keys. The format, reader and canonical writer are components/face (stage b of task 0071);
// this class only adapts them to the Editor Module API 1.4 curve and waveform tracks.
//
// Editing model: each channel is shown as keys (facekeys.hpp). A channel nobody edits keeps its original integers, so
// opening and saving a file never changes it; an edited channel is resampled from its keys when the file is saved.

#include "a2f_generate.hpp"
#include "audio_pcm.hpp"
#include "facekeys.hpp"

#include <components/face/facecurves.hpp>

#include <openeden/editor_module/types.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace face_editor
{
    /// ".facecurves.json"
    inline constexpr const char* kFileExtension = ".facecurves.json";
    inline constexpr const char* kFormatId = "opas.facecurves";

    /// The channels a new file starts with: 52 ARKit names are the long-term set (task 0071); a first set that covers
    /// the jaw, lips, blinks, brows and gaze keeps a new file small.
    const std::vector<std::string>& defaultChannelNames();

    /// Where the audio of a curve file is: next to it with the same name (any known extension), then the file name of
    /// audio.name next to it, then audio.name under \a audioRoot (a Studio setting; may be empty). Empty path = none.
    std::filesystem::path findAudio(
        const std::filesystem::path& curvesPath, const Face::Curves& curves, const std::filesystem::path& audioRoot);

    /// Writes a new curve file for \a audioPath (all channels at rest, \a frameRate frames per second, the audio's
    /// duration and SHA-256 recorded). False with \a error set when the audio cannot be read or the file written.
    bool createCurvesFile(const std::filesystem::path& audioPath, const std::filesystem::path& outPath,
        std::uint32_t frameRate, std::string& error);

    /// What a generated set of curves records in the file's generator block.
    struct GeneratedMeta
    {
        std::string mName; ///< generator.name
        std::string mVersion; ///< generator.version
        std::string mEmotion; ///< generator.emotion ("" = not recorded)
        std::uint64_t mSeed = 0; ///< generator.seed
    };

    class FaceDocument final : public openeden::editor::AssetDocument
    {
    public:
        /// \param audioRoot used by findAudio. Null result and \a error ("file:line: /pointer: message") on failure.
        static std::unique_ptr<FaceDocument> open(
            const std::filesystem::path& path, const std::filesystem::path& audioRoot, std::string& error);
        ~FaceDocument() override;

        // AssetDocument
        std::string_view assetId() const override;
        openeden::editor::PropertySource& properties() override;
        std::vector<openeden::editor::TimelineTrack*> tracks() override;
        bool isDirty() const override;
        bool save(std::string& error) override;

        const Face::Curves& curves() const;
        const std::filesystem::path& path() const;
        /// The audio file in use, empty if none was found or it could not be read.
        const std::filesystem::path& audioPath() const;
        /// What went wrong with the audio (empty when it loaded or none was looked for).
        const std::string& audioError() const;
        /// Whether the audio's SHA-256 and duration are the ones the file records: "matches", "differs", "no audio".
        std::string audioMatchText() const;
        /// Loads another audio file for this document (the file's record of its hash is updated on save). False with
        /// \a error when it cannot be read; the previous audio is kept.
        bool setAudio(const std::filesystem::path& audioPath, std::string& error);

        openeden::editor::CurveTrack& curveTrack();
        /// Null without audio.
        openeden::editor::WaveformTrack* waveformTrack();

        /// Task 0147 slice 2: replaces the curves of every channel that has a generated pose of the same name (lower
        /// case) by the generated weights, resampled to this file's frame rate (linear between generated frames,
        /// quantised like the file), and records \a meta as the generator. ONE undoable step; channels without a
        /// generated pose (gaze) are left alone, and generated poses without a channel are not added (the file keeps
        /// the channels it has). Null when no channel matches. \a written and \a skipped (may be null) get the names of
        /// the channels written and of the generated poses that had no channel.
        std::unique_ptr<openeden::editor::Command> makeGeneratedCommand(const GeneratedWeights& weights,
            const GeneratedMeta& meta, std::vector<std::string>* written, std::vector<std::string>* skipped);

        /// The keys of a channel now (for tests and the canvas).
        std::vector<openeden::editor::CurveKey> keysOf(std::string_view channel) const;

    private:
        class Impl;
        explicit FaceDocument(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };
}
