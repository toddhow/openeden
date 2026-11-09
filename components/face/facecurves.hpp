#ifndef OPENEDEN_COMPONENTS_FACE_FACECURVES_H
#define OPENEDEN_COMPONENTS_FACE_FACECURVES_H

// OpenEden facial speech animation, task 0071 stage b: the "opas.facecurves" v1 model, strict reader, canonical writer
// and validator, in JSON (authoring) and in a packed binary form (shipping; stored as an Eden Archive entry by a later
// stage). Engine-independent: no game, renderer, Qt, JSON library or NVIDIA type. The name and the owner follow the
// recommendation of the task (CreativeMotion's face module would own it, decision 0010); the code sits here so that it
// can move to components/creativemotion/face/ later.
//
// A curve file never contains audio: it records the audio file's name, duration and SHA-256, and a player ignores it
// when the player's own file differs (audioMatches).
//
// CANONICAL JSON (write(read(canonical)) is byte-identical):
//   {
//     "format": "opas.facecurves",
//     "version": 1,
//     "audio": {"name": "vo/a/f/Hlo_AF001.mp3", "durationSeconds": 1.5, "sha256": "<64 lowercase hex>"},
//     "generator": {"name": "...", "version": "...", "seed": 0, "emotion": "neutral"},   (emotion is optional)
//     "frameRate": 30,
//     "frameCount": 45,
//     "channels": [
//       {"name": "jawopen", "kind": "weight", "values": [0, 12, 255]},
//       {"name": "gazex", "kind": "signed", "values": [-255, 0, 255]}
//     ]
//   }
//   Two-space indent, "\n" line ends, one trailing newline, UTF-8. Objects that hold only scalars or flat arrays are
//   written on one line. Numbers: integers as integers; durationSeconds as the shortest text that reads back to the
//   same double. Keys are written in the order above; the reader accepts any order, refuses unknown and duplicate keys.
//   QUANTISATION: values are integers; a "weight" channel holds 0..255 (weight = v / 255), a "signed" channel holds
//   -255..255 (value = v / 255). No floating point is stored per value, so a round trip is exact.
// PACKED BINARY ("OPFC", little endian): see facecurves.cpp's header comment.
//
// LOCATED ERRORS: file name as passed, 1-based line of the offending value (1 when unknown, always for binary: the
// byte offset is then in the message), JSON pointer ("/channels/3/values/9"), one-line message. Readers never throw
// and never crash on any input; every count is checked against its cap (Limits) and against the bytes that remain
// BEFORE anything is allocated.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Face
{
    inline constexpr std::string_view sFormatName = "opas.facecurves";
    inline constexpr int sFormatVersion = 1;

    /// Caps on untrusted input. Exceeding one is a located error.
    namespace Limits
    {
        inline constexpr std::size_t sMaxFileBytes = 64u * 1024 * 1024;
        inline constexpr std::size_t sMaxNesting = 8;
        inline constexpr std::size_t sMaxStringBytes = 256; // every string in the file
        inline constexpr std::size_t sMaxChannels = 128;
        inline constexpr std::uint32_t sMaxFrameRate = 240;
        inline constexpr std::uint32_t sMaxFrameCount = 216000; // one hour at 60 frames per second
        inline constexpr std::size_t sMaxTotalValues = 8u * 1024 * 1024; // channels x frames
        inline constexpr int sMaxWeight = 255;
    }

    enum class ChannelKind : std::uint8_t
    {
        Weight, // 0..255
        Signed, // -255..255
    };

    struct Channel
    {
        std::string mName; // lower case [a-z0-9_.], 1..64 characters
        ChannelKind mKind = ChannelKind::Weight;
        std::vector<std::int16_t> mValues; // frameCount entries
    };

    struct Curves
    {
        std::string mAudioName;
        double mAudioDurationSeconds = 0.0;
        std::string mAudioSha256; // 64 lowercase hex characters
        std::string mGeneratorName;
        std::string mGeneratorVersion;
        std::int64_t mSeed = 0;
        std::string mEmotion; // empty = absent
        std::uint32_t mFrameRate = 30;
        std::uint32_t mFrameCount = 0;
        std::vector<Channel> mChannels;
    };

    struct LoadError
    {
        std::string mFile;
        std::size_t mLine = 1;
        std::string mPointer;
        std::string mMessage;
        /// "file:line: /pointer: message"
        std::string format() const;
    };

    struct ValidationIssue
    {
        std::string mPointer;
        std::string mMessage;
    };

    bool isValidChannelName(std::string_view name);

    /// Every semantic rule: ranges, name syntax, unique names, value counts and ranges, duration against the frame
    /// count (within one frame), hash syntax. Empty = valid. Never throws.
    std::vector<ValidationIssue> validate(const Curves& curves);

    /// Strict JSON reader; also runs validate() and reports the first issue with the line of the value.
    std::optional<Curves> readJson(std::string_view text, std::string_view fileName, LoadError& error);
    /// Canonical writer. An invalid model is refused: returns an empty optional.
    std::optional<std::string> writeJson(const Curves& curves);

    /// Packed binary form. Same validation as the JSON reader; trailing bytes are an error.
    std::optional<Curves> readBinary(
        const std::vector<std::uint8_t>& bytes, std::string_view fileName, LoadError& error);
    std::optional<std::vector<std::uint8_t>> writeBinary(const Curves& curves);

    /// Does a curve file belong to this audio? (The name is informational; the hash and a duration within one frame
    /// decide.)
    bool audioMatches(const Curves& curves, std::string_view sha256, double durationSeconds);
}

#endif
