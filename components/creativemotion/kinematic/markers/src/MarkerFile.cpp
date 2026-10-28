#include "opas/markers/MarkerFile.hpp"

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
using detail::vec3;

constexpr std::string_view kFormatName = "opas.markers";
// How far contactDirection may be from unit length (docs/formats/markers.md).
constexpr double kUnitTolerance = 1e-6;

// The marker-file rules on top of the shared JSON reader.
class MarkerReader : public detail::JsonReader {
public:
    using JsonReader::JsonReader;

    bool readWindow(const Json& value, const std::string& pointer, TimeWindow& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be an object {startS, endS}");
        }
        if (!onlyKeys(value, pointer, {"startS", "endS"})) {
            return false;
        }
        const Json* start = member(value, pointer, "startS");
        if (start == nullptr || !readNumber(*start, pointer + "/startS", out.startS)) {
            return false;
        }
        const Json* end = member(value, pointer, "endS");
        if (end == nullptr || !readNumber(*end, pointer + "/endS", out.endS)) {
            return false;
        }
        if (out.startS < 0.0) {
            return fail(*start, pointer + "/startS", "must be >= 0");
        }
        if (out.startS > out.endS) {
            return fail(*start, pointer + "/startS", "must be <= endS");
        }
        return true;
    }

    bool readMarker(const Json& value, const std::string& pointer, InteractionMarker& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a marker object");
        }
        if (!onlyKeys(value, pointer,
                      {"id", "sourceActor", "targetActor", "sourceBone", "targetRegion", "offsetM",
                       "contactDirection", "window", "priority", "toleranceM"})) {
            return false;
        }
        // Unknown keys are checked first (alphabetically), then fields in the
        // format's order; the first problem found is reported.
        const Json* id = member(value, pointer, "id");
        if (id == nullptr || !readString(*id, pointer + "/id", out.id)) {
            return false;
        }
        const Json* sourceActor = member(value, pointer, "sourceActor");
        if (sourceActor == nullptr ||
            !readInt(*sourceActor, pointer + "/sourceActor", out.sourceActor)) {
            return false;
        }
        if (out.sourceActor < 0) {
            return fail(*sourceActor, pointer + "/sourceActor", "must be >= 0");
        }
        const Json* targetActor = member(value, pointer, "targetActor");
        if (targetActor == nullptr ||
            !readInt(*targetActor, pointer + "/targetActor", out.targetActor)) {
            return false;
        }
        if (out.targetActor < 0) {
            return fail(*targetActor, pointer + "/targetActor", "must be >= 0");
        }
        if (out.targetActor == out.sourceActor) {
            return fail(*targetActor, pointer + "/targetActor", "must differ from sourceActor");
        }
        const Json* sourceBone = member(value, pointer, "sourceBone");
        if (sourceBone == nullptr ||
            !readString(*sourceBone, pointer + "/sourceBone", out.sourceBone)) {
            return false;
        }
        const Json* targetRegion = member(value, pointer, "targetRegion");
        if (targetRegion == nullptr ||
            !readString(*targetRegion, pointer + "/targetRegion", out.targetRegion)) {
            return false;
        }
        const Json* offset = member(value, pointer, "offsetM");
        if (offset == nullptr || !readVec3(*offset, pointer + "/offsetM", out.offsetM)) {
            return false;
        }
        const Json* direction = member(value, pointer, "contactDirection");
        if (direction == nullptr ||
            !readVec3(*direction, pointer + "/contactDirection", out.contactDirection)) {
            return false;
        }
        if (std::abs(math::length(out.contactDirection) - 1.0) > kUnitTolerance) {
            return fail(*direction, pointer + "/contactDirection", "must be unit length");
        }
        const Json* window = member(value, pointer, "window");
        if (window == nullptr || !readWindow(*window, pointer + "/window", out.window)) {
            return false;
        }
        const Json* priority = member(value, pointer, "priority");
        if (priority == nullptr || !readInt(*priority, pointer + "/priority", out.priority)) {
            return false;
        }
        const Json* tolerance = member(value, pointer, "toleranceM");
        if (tolerance == nullptr ||
            !readNumber(*tolerance, pointer + "/toleranceM", out.toleranceM)) {
            return false;
        }
        if (!(out.toleranceM > 0.0)) {
            return fail(*tolerance, pointer + "/toleranceM", "must be > 0");
        }
        return true;
    }

    bool readDocument(const Json& root, MarkerSet& out)
    {
        if (!root.is_object()) {
            return fail(root, "", "the document must be a JSON object");
        }
        if (!onlyKeys(root, "", {"format", "version", "markers"})) {
            return false;
        }
        const Json* format = member(root, "", "format");
        if (format == nullptr) {
            return false;
        }
        if (!format->is_string() || format->get<std::string>() != kFormatName) {
            return fail(*format, "/format", "must be \"opas.markers\"");
        }
        const Json* version = member(root, "", "version");
        if (version == nullptr) {
            return false;
        }
        if (!version->is_number_integer() ||
            version->get<std::int64_t>() != MarkerSet::kFormatVersion) {
            return fail(*version, "/version", "unsupported version (expected 2)");
        }
        const Json* markers = member(root, "", "markers");
        if (markers == nullptr) {
            return false;
        }
        if (!markers->is_array()) {
            return fail(*markers, "/markers", "must be an array");
        }
        std::set<std::string> ids;
        for (std::size_t i = 0; i < markers->size(); ++i) {
            const std::string pointer = "/markers/" + std::to_string(i);
            InteractionMarker marker;
            if (!readMarker((*markers)[i], pointer, marker)) {
                return false;
            }
            if (!ids.insert(marker.id).second) {
                return fail((*markers)[i]["id"], pointer + "/id",
                            "duplicate id \"" + marker.id + "\"");
            }
            out.markers.push_back(std::move(marker));
        }
        return true;
    }
};

} // namespace

LoadResult loadMarkerSet(std::string_view text, std::string_view fileName)
{
    LoadResult result;
    MarkerSet set;
    const std::optional<LoadError> error = detail::parseAndRead<MarkerReader>(
        text, fileName,
        [&set](MarkerReader& reader, const Json& root) { return reader.readDocument(root, set); });
    if (error) {
        result.error = *error;
    } else {
        result.value = std::move(set);
    }
    return result;
}

LoadResult loadMarkerSetFile(const std::filesystem::path& path)
{
    LoadResult result;
    const std::optional<std::string> text = detail::readTextFile(path, result.error);
    if (!text) {
        return result;
    }
    return loadMarkerSet(*text, detail::utf8FileName(path));
}

std::string writeMarkerSet(const MarkerSet& set)
{
    std::string out = "{\n  \"format\": \"opas.markers\",\n  \"version\": " +
                      std::to_string(MarkerSet::kFormatVersion) + ",\n  \"markers\": ";
    if (set.markers.empty()) {
        out += "[]\n}\n";
        return out;
    }
    out += "[\n";
    for (std::size_t i = 0; i < set.markers.size(); ++i) {
        const InteractionMarker& m = set.markers[i];
        out += "    {\n";
        out += "      \"id\": " + jsonString(m.id) + ",\n";
        out += "      \"sourceActor\": " + std::to_string(m.sourceActor) + ",\n";
        out += "      \"targetActor\": " + std::to_string(m.targetActor) + ",\n";
        out += "      \"sourceBone\": " + jsonString(m.sourceBone) + ",\n";
        out += "      \"targetRegion\": " + jsonString(m.targetRegion) + ",\n";
        out += "      \"offsetM\": " + vec3(m.offsetM) + ",\n";
        out += "      \"contactDirection\": " + vec3(m.contactDirection) + ",\n";
        out += "      \"window\": {\"startS\": " + number(m.window.startS) +
               ", \"endS\": " + number(m.window.endS) + "},\n";
        out += "      \"priority\": " + std::to_string(m.priority) + ",\n";
        out += "      \"toleranceM\": " + number(m.toleranceM) + "\n";
        out += i + 1 < set.markers.size() ? "    },\n" : "    }\n";
    }
    out += "  ]\n}\n";
    return out;
}

} // namespace opas::markers
