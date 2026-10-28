#include "opas/style/StyleProfile.hpp"

#include "opas/format/FileReader.hpp"
#include "opas/format/FileWriter.hpp"
#include "opas/format/JsonSupport.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace opas::style {
namespace {

using format::detail::FileReader;
using format::detail::Json;
using format::detail::jsonString;
using format::detail::number;

constexpr std::string_view kFormatName = "opas.style";

bool isValidName(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) {
            return false;
        }
    }
    return true;
}

bool isValidLayerName(std::string_view name)
{
    return name == "limp" || name == "hunch" || name == "protect_arm" || name == "shiver";
}

class StyleReader : public FileReader {
public:
    using FileReader::FileReader;

    static const Json* optionalMember(const Json& obj, std::string_view key)
    {
        if (!obj.is_object()) {
            return nullptr;
        }
        const auto it = obj.find(key);
        return it != obj.end() ? &*it : nullptr;
    }

    bool checkRangeInclusive(const Json& value, const std::string& pointer, double v, double minVal,
                             double maxVal)
    {
        if (v < minVal || v > maxVal) {
            return fail(value, pointer,
                        "value " + number(v) + " outside valid range [" + number(minVal) + ", " +
                            number(maxVal) + "]");
        }
        return true;
    }

    bool checkRangeOpenClosed(const Json& value, const std::string& pointer, double v,
                              double minValExclusive, double maxValInclusive)
    {
        if (v <= minValExclusive || v > maxValInclusive) {
            return fail(value, pointer,
                        "value " + number(v) + " outside valid range (" + number(minValExclusive) +
                            ", " + number(maxValInclusive) + "]");
        }
        return true;
    }

    bool readPosture(const Json& obj, const std::string& pointer, PostureParams& out)
    {
        if (!obj.is_object()) {
            return fail(obj, pointer, "must be an object");
        }
        if (!onlyKeys(obj, pointer,
                      {"headPitchRad", "headYawRad", "shoulderElevationRad", "slouchRad",
                       "spineLeanRad"})) {
            return false;
        }
        if (const Json* m = optionalMember(obj, "slouchRad")) {
            if (!readNumber(*m, pointer + "/slouchRad", out.slouchRad) ||
                !checkRangeInclusive(*m, pointer + "/slouchRad", out.slouchRad, -0.6, 0.6)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "headPitchRad")) {
            if (!readNumber(*m, pointer + "/headPitchRad", out.headPitchRad) ||
                !checkRangeInclusive(*m, pointer + "/headPitchRad", out.headPitchRad, -0.6, 0.6)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "headYawRad")) {
            if (!readNumber(*m, pointer + "/headYawRad", out.headYawRad) ||
                !checkRangeInclusive(*m, pointer + "/headYawRad", out.headYawRad, -0.8, 0.8)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "shoulderElevationRad")) {
            if (!readNumber(*m, pointer + "/shoulderElevationRad", out.shoulderElevationRad) ||
                !checkRangeInclusive(*m, pointer + "/shoulderElevationRad",
                                     out.shoulderElevationRad, -0.3, 0.6)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "spineLeanRad")) {
            if (!readNumber(*m, pointer + "/spineLeanRad", out.spineLeanRad) ||
                !checkRangeInclusive(*m, pointer + "/spineLeanRad", out.spineLeanRad, -0.4, 0.4)) {
                return false;
            }
        }
        return true;
    }

    bool readGait(const Json& obj, const std::string& pointer, GaitParams& out)
    {
        if (!obj.is_object()) {
            return fail(obj, pointer, "must be an object");
        }
        if (!onlyKeys(obj, pointer,
                      {"armSwingScale", "cadenceScale", "hipSwayScale", "stepWidthOffsetM",
                       "strideScale"})) {
            return false;
        }
        if (const Json* m = optionalMember(obj, "strideScale")) {
            if (!readNumber(*m, pointer + "/strideScale", out.strideScale) ||
                !checkRangeOpenClosed(*m, pointer + "/strideScale", out.strideScale, 0.1, 3.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "cadenceScale")) {
            if (!readNumber(*m, pointer + "/cadenceScale", out.cadenceScale) ||
                !checkRangeOpenClosed(*m, pointer + "/cadenceScale", out.cadenceScale, 0.1, 3.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "armSwingScale")) {
            if (!readNumber(*m, pointer + "/armSwingScale", out.armSwingScale) ||
                !checkRangeInclusive(*m, pointer + "/armSwingScale", out.armSwingScale, 0.0, 3.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "hipSwayScale")) {
            if (!readNumber(*m, pointer + "/hipSwayScale", out.hipSwayScale) ||
                !checkRangeInclusive(*m, pointer + "/hipSwayScale", out.hipSwayScale, 0.0, 3.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "stepWidthOffsetM")) {
            if (!readNumber(*m, pointer + "/stepWidthOffsetM", out.stepWidthOffsetM) ||
                !checkRangeInclusive(*m, pointer + "/stepWidthOffsetM", out.stepWidthOffsetM, -0.2,
                                     0.5)) {
                return false;
            }
        }
        return true;
    }

    bool readIdle(const Json& obj, const std::string& pointer, IdleParams& out)
    {
        if (!obj.is_object()) {
            return fail(obj, pointer, "must be an object");
        }
        if (!onlyKeys(obj, pointer,
                      {"breathingAmplitudeM", "breathingRateHz", "fidgetIntervalS", "seed",
                       "swayAmplitudeM", "swayRateHz"})) {
            return false;
        }
        if (const Json* m = optionalMember(obj, "breathingRateHz")) {
            if (!readNumber(*m, pointer + "/breathingRateHz", out.breathingRateHz) ||
                !checkRangeInclusive(*m, pointer + "/breathingRateHz", out.breathingRateHz, 0.05,
                                     2.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "breathingAmplitudeM")) {
            if (!readNumber(*m, pointer + "/breathingAmplitudeM", out.breathingAmplitudeM) ||
                !checkRangeInclusive(*m, pointer + "/breathingAmplitudeM", out.breathingAmplitudeM,
                                     0.0, 0.1)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "swayRateHz")) {
            if (!readNumber(*m, pointer + "/swayRateHz", out.swayRateHz) ||
                !checkRangeInclusive(*m, pointer + "/swayRateHz", out.swayRateHz, 0.02, 1.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "swayAmplitudeM")) {
            if (!readNumber(*m, pointer + "/swayAmplitudeM", out.swayAmplitudeM) ||
                !checkRangeInclusive(*m, pointer + "/swayAmplitudeM", out.swayAmplitudeM, 0.0,
                                     0.08)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "fidgetIntervalS")) {
            if (!readNumber(*m, pointer + "/fidgetIntervalS", out.fidgetIntervalS) ||
                !checkRangeInclusive(*m, pointer + "/fidgetIntervalS", out.fidgetIntervalS, 1.0,
                                     60.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "seed")) {
            if (!m->is_number_unsigned()) {
                return fail(*m, pointer + "/seed", "must be an unsigned integer");
            }
            out.seed = m->get<std::uint64_t>();
        }
        return true;
    }

    bool readTiming(const Json& obj, const std::string& pointer, TimingParams& out)
    {
        if (!obj.is_object()) {
            return fail(obj, pointer, "must be an object");
        }
        if (!onlyKeys(obj, pointer,
                      {"dampingMultiplier", "followThroughLagS", "stiffnessMultiplier"})) {
            return false;
        }
        if (const Json* m = optionalMember(obj, "stiffnessMultiplier")) {
            if (!readNumber(*m, pointer + "/stiffnessMultiplier", out.stiffnessMultiplier) ||
                !checkRangeOpenClosed(*m, pointer + "/stiffnessMultiplier",
                                      out.stiffnessMultiplier, 0.05, 5.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "dampingMultiplier")) {
            if (!readNumber(*m, pointer + "/dampingMultiplier", out.dampingMultiplier) ||
                !checkRangeOpenClosed(*m, pointer + "/dampingMultiplier", out.dampingMultiplier,
                                      0.05, 5.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "followThroughLagS")) {
            if (!readNumber(*m, pointer + "/followThroughLagS", out.followThroughLagS) ||
                !checkRangeInclusive(*m, pointer + "/followThroughLagS", out.followThroughLagS, 0.0,
                                     0.5)) {
                return false;
            }
        }
        return true;
    }

    bool readGaze(const Json& obj, const std::string& pointer, GazeParams& out)
    {
        if (!obj.is_object()) {
            return fail(obj, pointer, "must be an object");
        }
        if (!onlyKeys(obj, pointer,
                      {"glanceFrequencyHz", "glanceSpeedScale", "headGazeFraction"})) {
            return false;
        }
        if (const Json* m = optionalMember(obj, "glanceFrequencyHz")) {
            if (!readNumber(*m, pointer + "/glanceFrequencyHz", out.glanceFrequencyHz) ||
                !checkRangeInclusive(*m, pointer + "/glanceFrequencyHz", out.glanceFrequencyHz, 0.0,
                                     3.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "glanceSpeedScale")) {
            if (!readNumber(*m, pointer + "/glanceSpeedScale", out.glanceSpeedScale) ||
                !checkRangeOpenClosed(*m, pointer + "/glanceSpeedScale", out.glanceSpeedScale, 0.1,
                                      3.0)) {
                return false;
            }
        }
        if (const Json* m = optionalMember(obj, "headGazeFraction")) {
            if (!readNumber(*m, pointer + "/headGazeFraction", out.headGazeFraction) ||
                !checkRangeInclusive(*m, pointer + "/headGazeFraction", out.headGazeFraction, 0.0,
                                     1.0)) {
                return false;
            }
        }
        return true;
    }

    bool readLayers(const Json& arr, const std::string& pointer, std::vector<LayerConfig>& out)
    {
        if (!arr.is_array()) {
            return fail(arr, pointer, "must be an array");
        }
        std::set<std::string> seenNames;
        for (std::size_t i = 0; i < arr.size(); ++i) {
            const Json& item = arr[i];
            const std::string at = pointer + "/" + std::to_string(i);
            if (!item.is_object()) {
                return fail(item, at, "must be an object");
            }
            if (!onlyKeys(item, at, {"bone", "name", "weight"})) {
                return false;
            }
            LayerConfig cfg;
            if (!stringField(item, at, "name", cfg.name)) {
                return false;
            }
            if (!isValidLayerName(cfg.name)) {
                return fail(item["name"], at + "/name", "unknown style layer \"" + cfg.name + "\"");
            }
            if (!seenNames.insert(cfg.name).second) {
                return fail(item["name"], at + "/name",
                            "duplicate style layer \"" + cfg.name + "\"");
            }
            if (!numberField(item, at, "weight", cfg.weight)) {
                return false;
            }
            if (!checkRangeInclusive(item["weight"], at + "/weight", cfg.weight, 0.0, 1.0)) {
                return false;
            }
            if (const Json* b = optionalMember(item, "bone")) {
                if (!b->is_string()) {
                    return fail(*b, at + "/bone", "must be a string");
                }
                cfg.bone = b->get<std::string>();
                if (!isValidName(cfg.bone)) {
                    return fail(*b, at + "/bone", "must be non-empty, without control characters");
                }
            }
            out.push_back(std::move(cfg));
        }
        return true;
    }

    bool readDocument(const Json& root, StyleProfile& out)
    {
        if (!readHeader(root, kFormatName,
                        {"format", "gait", "gaze", "idle", "layers", "name", "posture", "timing",
                         "version"})) {
            return false;
        }
        if (!stringField(root, "", "name", out.name)) {
            return false;
        }
        if (!isValidName(out.name)) {
            return fail(root["name"], "/name", "must be non-empty, without control characters");
        }
        if (const Json* p = optionalMember(root, "posture")) {
            if (!readPosture(*p, "/posture", out.posture)) {
                return false;
            }
        }
        if (const Json* g = optionalMember(root, "gait")) {
            if (!readGait(*g, "/gait", out.gait)) {
                return false;
            }
        }
        if (const Json* idl = optionalMember(root, "idle")) {
            if (!readIdle(*idl, "/idle", out.idle)) {
                return false;
            }
        }
        if (const Json* t = optionalMember(root, "timing")) {
            if (!readTiming(*t, "/timing", out.timing)) {
                return false;
            }
        }
        if (const Json* gz = optionalMember(root, "gaze")) {
            if (!readGaze(*gz, "/gaze", out.gaze)) {
                return false;
            }
        }
        if (const Json* l = optionalMember(root, "layers")) {
            if (!readLayers(*l, "/layers", out.layers)) {
                return false;
            }
        }
        return true;
    }
};

std::string writeLayer(const LayerConfig& l)
{
    std::string out = "    {";
    if (!l.bone.empty()) {
        out += "\"bone\": " + jsonString(l.bone) + ", ";
    }
    out += "\"name\": " + jsonString(l.name) + ", \"weight\": " + number(l.weight) + "}";
    return out;
}

} // namespace

StyleLoadResult loadStyleProfile(std::string_view text, std::string_view fileName)
{
    StyleLoadResult result;
    StyleProfile profile;
    const std::optional<format::LoadError> err = format::detail::parseAndRead<StyleReader>(
        text, fileName,
        [&profile](StyleReader& r, const Json& root) { return r.readDocument(root, profile); });
    if (err) {
        result.error = *err;
    } else {
        result.value = std::move(profile);
    }
    return result;
}

StyleLoadResult loadStyleProfileFile(const std::filesystem::path& path)
{
    StyleLoadResult result;
    format::LoadError error;
    const std::optional<std::string> text = format::detail::readTextFile(path, error);
    if (!text) {
        result.error = std::move(error);
        return result;
    }
    return loadStyleProfile(*text, format::detail::utf8FileName(path));
}

std::string writeStyleProfile(const StyleProfile& profile)
{
    std::string out = "{\n  \"format\": " + jsonString(kFormatName) +
                      ",\n  \"version\": 1" +
                      ",\n  \"name\": " + jsonString(profile.name) +
                      ",\n  \"posture\": {\n" +
                      "    \"headPitchRad\": " + number(profile.posture.headPitchRad) + ",\n" +
                      "    \"headYawRad\": " + number(profile.posture.headYawRad) + ",\n" +
                      "    \"shoulderElevationRad\": " + number(profile.posture.shoulderElevationRad) + ",\n" +
                      "    \"slouchRad\": " + number(profile.posture.slouchRad) + ",\n" +
                      "    \"spineLeanRad\": " + number(profile.posture.spineLeanRad) + "\n" +
                      "  },\n  \"gait\": {\n" +
                      "    \"armSwingScale\": " + number(profile.gait.armSwingScale) + ",\n" +
                      "    \"cadenceScale\": " + number(profile.gait.cadenceScale) + ",\n" +
                      "    \"hipSwayScale\": " + number(profile.gait.hipSwayScale) + ",\n" +
                      "    \"stepWidthOffsetM\": " + number(profile.gait.stepWidthOffsetM) + ",\n" +
                      "    \"strideScale\": " + number(profile.gait.strideScale) + "\n" +
                      "  },\n  \"idle\": {\n" +
                      "    \"breathingAmplitudeM\": " + number(profile.idle.breathingAmplitudeM) + ",\n" +
                      "    \"breathingRateHz\": " + number(profile.idle.breathingRateHz) + ",\n" +
                      "    \"fidgetIntervalS\": " + number(profile.idle.fidgetIntervalS) + ",\n" +
                      "    \"seed\": " + std::to_string(profile.idle.seed) + ",\n" +
                      "    \"swayAmplitudeM\": " + number(profile.idle.swayAmplitudeM) + ",\n" +
                      "    \"swayRateHz\": " + number(profile.idle.swayRateHz) + "\n" +
                      "  },\n  \"timing\": {\n" +
                      "    \"dampingMultiplier\": " + number(profile.timing.dampingMultiplier) + ",\n" +
                      "    \"followThroughLagS\": " + number(profile.timing.followThroughLagS) + ",\n" +
                      "    \"stiffnessMultiplier\": " + number(profile.timing.stiffnessMultiplier) + "\n" +
                      "  },\n  \"gaze\": {\n" +
                      "    \"glanceFrequencyHz\": " + number(profile.gaze.glanceFrequencyHz) + ",\n" +
                      "    \"glanceSpeedScale\": " + number(profile.gaze.glanceSpeedScale) + ",\n" +
                      "    \"headGazeFraction\": " + number(profile.gaze.headGazeFraction) + "\n" +
                      "  },\n  \"layers\": ";

    if (profile.layers.empty()) {
        out += "[]";
    } else {
        out += "[\n";
        for (std::size_t i = 0; i < profile.layers.size(); ++i) {
            out += writeLayer(profile.layers[i]);
            out += (i + 1 < profile.layers.size() ? ",\n" : "\n");
        }
        out += "  ]";
    }
    out += "\n}\n";
    return out;
}

} // namespace opas::style
