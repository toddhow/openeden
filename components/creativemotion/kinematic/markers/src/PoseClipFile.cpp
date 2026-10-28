// Pose clip files (docs/formats/poseclip.md) and detection profiles
// (docs/formats/detection.md): loaders and canonical writers.

#include "opas/markers/ContactDetection.hpp"
#include "opas/markers/PoseClip.hpp"

#include "JsonSupport.hpp"

#include <cmath>
#include <cstdint>
#include <set>
#include <string>

namespace opas::markers {
namespace {

using detail::Json;
using detail::jsonString;
using detail::number;

constexpr std::string_view kClipFormat = "opas.poseclip";
constexpr std::string_view kProfileFormat = "opas.detection";
constexpr int kProfileVersion = 1;
// How far a transform's quaternion may be from unit length (format doc).
constexpr double kUnitTolerance = 1e-6;

// Reads the "format" and "version" members shared by every format here.
bool readHeader(detail::JsonReader& r, const Json& root, std::string_view format,
                std::string_view formatMessage, int version)
{
    const Json* f = r.member(root, "", "format");
    if (f == nullptr) {
        return false;
    }
    if (!f->is_string() || f->get<std::string>() != format) {
        return r.fail(*f, "/format", std::string(formatMessage));
    }
    const Json* v = r.member(root, "", "version");
    if (v == nullptr) {
        return false;
    }
    if (!v->is_number_integer() || v->get<std::int64_t>() != version) {
        return r.fail(*v, "/version",
                      "unsupported version (expected " + std::to_string(version) + ")");
    }
    return true;
}

class PoseClipReader : public detail::JsonReader {
public:
    using JsonReader::JsonReader;

    bool readTransform(const Json& value, const std::string& pointer, math::Transform& out)
    {
        if (!value.is_array() || value.size() != 7) {
            return fail(value, pointer, "must be an array of 7 numbers [x, y, z, qx, qy, qz, qw]");
        }
        std::array<double, 7> v{};
        for (std::size_t i = 0; i < 7; ++i) {
            if (!readNumber(value[i], pointer + "/" + std::to_string(i), v[i])) {
                return false;
            }
        }
        out.translation = math::Vec3{v[0], v[1], v[2]};
        out.rotation = math::Quat{v[3], v[4], v[5], v[6]};
        if (!(std::abs(math::length(out.rotation) - 1.0) <= kUnitTolerance)) {
            return fail(value, pointer, "rotation must be a unit quaternion");
        }
        return true;
    }

    // `expectedFrames` is actor 0's frame count when reading actor 1.
    bool readActor(const Json& value, const std::string& pointer, ClipActor& out,
                   std::optional<std::size_t> expectedFrames)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be an actor object");
        }
        if (!onlyKeys(value, pointer, {"bodyMap", "bones", "frames"})) {
            return false;
        }
        const Json* bodyMap = member(value, pointer, "bodyMap");
        if (bodyMap == nullptr || !readString(*bodyMap, pointer + "/bodyMap", out.bodyMap)) {
            return false;
        }
        const Json* bones = member(value, pointer, "bones");
        if (bones == nullptr) {
            return false;
        }
        if (!bones->is_array()) {
            return fail(*bones, pointer + "/bones", "must be an array of bone names");
        }
        std::set<std::string> seen;
        out.bones.resize(bones->size());
        for (std::size_t i = 0; i < bones->size(); ++i) {
            const std::string p = pointer + "/bones/" + std::to_string(i);
            if (!readString((*bones)[i], p, out.bones[i])) {
                return false;
            }
            if (!seen.insert(out.bones[i]).second) {
                return fail((*bones)[i], p, "duplicate bone \"" + out.bones[i] + "\"");
            }
        }
        const Json* frames = member(value, pointer, "frames");
        if (frames == nullptr) {
            return false;
        }
        const std::string framesPointer = pointer + "/frames";
        if (!frames->is_array()) {
            return fail(*frames, framesPointer, "must be an array of frames");
        }
        if (frames->empty()) {
            return fail(*frames, framesPointer, "must hold at least one frame");
        }
        if (expectedFrames && frames->size() != *expectedFrames) {
            return fail(*frames, framesPointer,
                        "must have the same number of frames as actor 0 (" +
                            std::to_string(*expectedFrames) + ")");
        }
        if (out.bones.empty()) {
            return fail(*bones, pointer + "/bones", "must not be empty when there are frames");
        }
        out.frames.resize(frames->size());
        for (std::size_t f = 0; f < frames->size(); ++f) {
            const Json& frame = (*frames)[f];
            const std::string fp = framesPointer + "/" + std::to_string(f);
            if (!frame.is_array() || frame.size() != out.bones.size()) {
                return fail(frame, fp,
                            "must be an array of " + std::to_string(out.bones.size()) +
                                " transforms, one per bone");
            }
            out.frames[f].resize(out.bones.size());
            for (std::size_t b = 0; b < out.bones.size(); ++b) {
                if (!readTransform(frame[b], fp + "/" + std::to_string(b), out.frames[f][b])) {
                    return false;
                }
            }
        }
        return true;
    }

    bool readDocument(const Json& root, PoseClip& out)
    {
        if (!root.is_object()) {
            return fail(root, "", "the document must be a JSON object");
        }
        if (!onlyKeys(root, "", {"format", "version", "sampleRateHz", "actors"}) ||
            !readHeader(*this, root, kClipFormat, "must be \"opas.poseclip\"",
                        PoseClip::kFormatVersion)) {
            return false;
        }
        const Json* rate = member(root, "", "sampleRateHz");
        if (rate == nullptr || !readNumber(*rate, "/sampleRateHz", out.sampleRateHz)) {
            return false;
        }
        if (!(out.sampleRateHz > 0.0)) {
            return fail(*rate, "/sampleRateHz", "must be > 0");
        }
        const Json* actors = member(root, "", "actors");
        if (actors == nullptr) {
            return false;
        }
        if (!actors->is_array() || actors->size() != 2) {
            return fail(*actors, "/actors", "must be an array of exactly 2 actors");
        }
        return readActor((*actors)[0], "/actors/0", out.actors[0], std::nullopt) &&
               readActor((*actors)[1], "/actors/1", out.actors[1], out.actors[0].frames.size());
    }
};

class ProfileReader : public detail::JsonReader {
public:
    using JsonReader::JsonReader;

    bool positive(const Json& root, std::string_view key, double& out)
    {
        const Json* value = member(root, "", key);
        const std::string pointer = "/" + std::string(key);
        if (value == nullptr || !readNumber(*value, pointer, out)) {
            return false;
        }
        if (!(out > 0.0)) {
            return fail(*value, pointer, "must be > 0");
        }
        return true;
    }

    bool readDocument(const Json& root, DetectionSettings& out)
    {
        if (!root.is_object()) {
            return fail(root, "", "the document must be a JSON object");
        }
        if (!onlyKeys(root, "",
                      {"format", "version", "distanceThresholdM", "minDurationS",
                       "maxRelativeSpeedMps", "markerToleranceM", "includeLowConfidence"}) ||
            !readHeader(*this, root, kProfileFormat, "must be \"opas.detection\"",
                        kProfileVersion)) {
            return false;
        }
        if (!positive(root, "distanceThresholdM", out.distanceThresholdM) ||
            !positive(root, "minDurationS", out.minDurationS) ||
            !positive(root, "maxRelativeSpeedMps", out.maxRelativeSpeedMps) ||
            !positive(root, "markerToleranceM", out.markerToleranceM)) {
            return false;
        }
        const Json* low = member(root, "", "includeLowConfidence");
        if (low == nullptr) {
            return false;
        }
        if (!low->is_boolean()) {
            return fail(*low, "/includeLowConfidence", "must be true or false");
        }
        out.includeLowConfidence = low->get<bool>();
        return true;
    }
};

std::string transformText(const math::Transform& t)
{
    return "[" + number(t.translation.x) + ", " + number(t.translation.y) + ", " +
           number(t.translation.z) + ", " + number(t.rotation.x) + ", " + number(t.rotation.y) +
           ", " + number(t.rotation.z) + ", " + number(t.rotation.w) + "]";
}

std::string actorText(const ClipActor& actor)
{
    std::string out = "    {\n      \"bodyMap\": " + jsonString(actor.bodyMap) + ",\n";
    out += "      \"bones\": [";
    for (std::size_t i = 0; i < actor.bones.size(); ++i) {
        out += (i > 0 ? ", " : "") + jsonString(actor.bones[i]);
    }
    out += "],\n      \"frames\": ";
    if (actor.frames.empty()) {
        out += "[]\n";
    } else {
        out += "[\n";
        for (std::size_t f = 0; f < actor.frames.size(); ++f) {
            out += "        [";
            for (std::size_t b = 0; b < actor.frames[f].size(); ++b) {
                out += (b > 0 ? ", " : "") + transformText(actor.frames[f][b]);
            }
            out += f + 1 < actor.frames.size() ? "],\n" : "]\n";
        }
        out += "      ]\n";
    }
    out += "    }";
    return out;
}

} // namespace

PoseClipLoadResult loadPoseClip(std::string_view text, std::string_view fileName)
{
    PoseClipLoadResult result;
    PoseClip clip;
    const std::optional<LoadError> error = detail::parseAndRead<PoseClipReader>(
        text, fileName, [&clip](PoseClipReader& reader, const Json& root) {
            return reader.readDocument(root, clip);
        });
    if (error) {
        result.error = *error;
    } else {
        result.value = std::move(clip);
    }
    return result;
}

PoseClipLoadResult loadPoseClipFile(const std::filesystem::path& path)
{
    PoseClipLoadResult result;
    const std::optional<std::string> text = detail::readTextFile(path, result.error);
    if (!text) {
        return result;
    }
    return loadPoseClip(*text, detail::utf8FileName(path));
}

std::string writePoseClip(const PoseClip& clip)
{
    std::string out = "{\n  \"format\": \"opas.poseclip\",\n  \"version\": " +
                      std::to_string(PoseClip::kFormatVersion) +
                      ",\n  \"sampleRateHz\": " + number(clip.sampleRateHz) +
                      ",\n  \"actors\": [\n";
    out += actorText(clip.actors[0]) + ",\n" + actorText(clip.actors[1]) + "\n  ]\n}\n";
    return out;
}

DetectionProfileLoadResult loadDetectionProfile(std::string_view text, std::string_view fileName)
{
    DetectionProfileLoadResult result;
    DetectionSettings settings;
    const std::optional<LoadError> error = detail::parseAndRead<ProfileReader>(
        text, fileName, [&settings](ProfileReader& reader, const Json& root) {
            return reader.readDocument(root, settings);
        });
    if (error) {
        result.error = *error;
    } else {
        result.value = settings;
    }
    return result;
}

DetectionProfileLoadResult loadDetectionProfileFile(const std::filesystem::path& path)
{
    DetectionProfileLoadResult result;
    const std::optional<std::string> text = detail::readTextFile(path, result.error);
    if (!text) {
        return result;
    }
    return loadDetectionProfile(*text, detail::utf8FileName(path));
}

std::string writeDetectionProfile(const DetectionSettings& s)
{
    return "{\n  \"format\": \"opas.detection\",\n  \"version\": " +
           std::to_string(kProfileVersion) +
           ",\n  \"distanceThresholdM\": " + number(s.distanceThresholdM) +
           ",\n  \"minDurationS\": " + number(s.minDurationS) +
           ",\n  \"maxRelativeSpeedMps\": " + number(s.maxRelativeSpeedMps) +
           ",\n  \"markerToleranceM\": " + number(s.markerToleranceM) +
           ",\n  \"includeLowConfidence\": " + (s.includeLowConfidence ? "true" : "false") +
           "\n}\n";
}

} // namespace opas::markers
