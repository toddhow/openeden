#include "opas/markers/BodyMapFile.hpp"

#include "JsonSupport.hpp"

#include <cstdint>
#include <numbers>
#include <set>
#include <string>

namespace opas::markers {
namespace {

using detail::Json;
using detail::jsonString;
using detail::number;
using detail::vec3;

constexpr std::string_view kFormatName = "opas.bodymap";
// Version 1: limbs, capsules, IK chains. Version 2 adds "fingers" (A5c).
constexpr int kVersionWithoutFingers = 1;
constexpr int kVersionWithFingers = 2;

// The body-map rules (docs/formats/bodymap.md) on top of the shared reader.
// Within each object, unknown keys are checked first, then fields in the
// format's order, then (once the object is complete) uniqueness.
class BodyMapReader : public detail::JsonReader {
public:
    using JsonReader::JsonReader;

    // Finds a required field and reads it as a string.
    bool stringField(const Json& object, const std::string& pointer, std::string_view key,
                     std::string& out)
    {
        const Json* value = member(object, pointer, key);
        return value != nullptr && readString(*value, pointer + "/" + std::string(key), out);
    }

    // Finds a required field and reads it as a finite number; returns the
    // JSON value so later range checks can report at it.
    const Json* numberField(const Json& object, const std::string& pointer, std::string_view key,
                            double& out)
    {
        const Json* value = member(object, pointer, key);
        if (value == nullptr || !readNumber(*value, pointer + "/" + std::string(key), out)) {
            return nullptr;
        }
        return value;
    }

    bool readLimb(const Json& value, const std::string& pointer, LimbDefinition& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a limb object");
        }
        if (!onlyKeys(value, pointer, {"name", "bones"})) {
            return false;
        }
        if (!stringField(value, pointer, "name", out.name)) {
            return false;
        }
        const Json* bones = member(value, pointer, "bones");
        if (bones == nullptr) {
            return false;
        }
        if (!bones->is_array()) {
            return fail(*bones, pointer + "/bones", "must be an array of bone names");
        }
        if (bones->size() < 2) {
            return fail(*bones, pointer + "/bones", "needs at least 2 bones");
        }
        out.bones.resize(bones->size());
        for (std::size_t i = 0; i < bones->size(); ++i) {
            if (!readString((*bones)[i], pointer + "/bones/" + std::to_string(i), out.bones[i])) {
                return false;
            }
        }
        return true;
    }

    bool readCapsule(const Json& value, const std::string& pointer, CapsuleRule& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a capsule object");
        }
        if (!onlyKeys(value, pointer, {"region", "bone", "endBone", "radiusRatio"})) {
            return false;
        }
        if (!stringField(value, pointer, "region", out.region) ||
            !stringField(value, pointer, "bone", out.bone) ||
            !stringField(value, pointer, "endBone", out.endBone)) {
            return false;
        }
        const Json* ratio = numberField(value, pointer, "radiusRatio", out.radiusRatio);
        if (ratio == nullptr) {
            return false;
        }
        if (!(out.radiusRatio > 0.0)) {
            return fail(*ratio, pointer + "/radiusRatio", "must be > 0");
        }
        return true;
    }

    bool readChain(const Json& value, const std::string& pointer, IkChain& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be an IK chain object");
        }
        if (!onlyKeys(value, pointer,
                      {"name", "rootBone", "midBone", "endBone", "minBendRad", "maxBendRad",
                       "poleDirection", "limbRadiusM"})) {
            return false;
        }
        if (!stringField(value, pointer, "name", out.name) ||
            !stringField(value, pointer, "rootBone", out.rootBone) ||
            !stringField(value, pointer, "midBone", out.midBone) ||
            !stringField(value, pointer, "endBone", out.endBone)) {
            return false;
        }
        const Json* minBend = numberField(value, pointer, "minBendRad", out.limits.minBendRad);
        if (minBend == nullptr) {
            return false;
        }
        const Json* maxBend = numberField(value, pointer, "maxBendRad", out.limits.maxBendRad);
        if (maxBend == nullptr) {
            return false;
        }
        const double pi = std::numbers::pi;
        if (!(out.limits.minBendRad >= 0.0 && out.limits.minBendRad <= pi)) {
            return fail(*minBend, pointer + "/minBendRad", "must be in [0, pi]");
        }
        if (!(out.limits.maxBendRad >= 0.0 && out.limits.maxBendRad <= pi)) {
            return fail(*maxBend, pointer + "/maxBendRad", "must be in [0, pi]");
        }
        if (out.limits.minBendRad > out.limits.maxBendRad) {
            return fail(*minBend, pointer + "/minBendRad", "must be <= maxBendRad");
        }
        const Json* pole = member(value, pointer, "poleDirection");
        if (pole == nullptr || !readVec3(*pole, pointer + "/poleDirection", out.poleDirection)) {
            return false;
        }
        if (math::lengthSquared(out.poleDirection) == 0.0) {
            return fail(*pole, pointer + "/poleDirection", "must not be the zero vector");
        }
        const Json* radius = numberField(value, pointer, "limbRadiusM", out.limbRadiusM);
        if (radius == nullptr) {
            return false;
        }
        if (!(out.limbRadiusM > 0.0)) {
            return fail(*radius, pointer + "/limbRadiusM", "must be > 0");
        }
        return true;
    }

    bool readFinger(const Json& value, const std::string& pointer, FingerDefinition& out,
                    const std::set<std::string>& chainNames)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a finger object");
        }
        if (!onlyKeys(value, pointer, {"name", "chain", "bones", "hingeAxis", "maxCurlRad"})) {
            return false;
        }
        if (!stringField(value, pointer, "name", out.name) ||
            !stringField(value, pointer, "chain", out.chain)) {
            return false;
        }
        if (!chainNames.contains(out.chain)) {
            return fail(value.at("chain"), pointer + "/chain", "must name an IK chain of this map");
        }
        const Json* bones = member(value, pointer, "bones");
        if (bones == nullptr) {
            return false;
        }
        if (!bones->is_array() || bones->size() < 2) {
            return fail(*bones, pointer + "/bones", "must be an array of at least 2 bone names");
        }
        out.bones.resize(bones->size());
        for (std::size_t i = 0; i < bones->size(); ++i) {
            if (!readString((*bones)[i], pointer + "/bones/" + std::to_string(i), out.bones[i])) {
                return false;
            }
        }
        const Json* axis = member(value, pointer, "hingeAxis");
        if (axis == nullptr || !readVec3(*axis, pointer + "/hingeAxis", out.hingeAxis)) {
            return false;
        }
        if (math::lengthSquared(out.hingeAxis) == 0.0) {
            return fail(*axis, pointer + "/hingeAxis", "must not be the zero vector");
        }
        const Json* curls = member(value, pointer, "maxCurlRad");
        if (curls == nullptr) {
            return false;
        }
        if (!curls->is_array() || curls->size() != out.bones.size()) {
            return fail(*curls, pointer + "/maxCurlRad",
                        "must be an array with one number per bone");
        }
        out.maxCurlRad.resize(curls->size());
        for (std::size_t i = 0; i < curls->size(); ++i) {
            const std::string at = pointer + "/maxCurlRad/" + std::to_string(i);
            if (!readNumber((*curls)[i], at, out.maxCurlRad[i])) {
                return false;
            }
            if (!(out.maxCurlRad[i] >= 0.0 && out.maxCurlRad[i] <= std::numbers::pi)) {
                return fail((*curls)[i], at, "must be in [0, pi]");
            }
        }
        return true;
    }

    // Reads a required top-level array and each element with `readItem`.
    template <typename Item, typename ReadItem>
    bool readArray(const Json& root, std::string_view key, std::vector<Item>& out,
                   ReadItem readItem)
    {
        const std::string pointer = "/" + std::string(key);
        const Json* array = member(root, "", key);
        if (array == nullptr) {
            return false;
        }
        if (!array->is_array()) {
            return fail(*array, pointer, "must be an array");
        }
        out.resize(array->size());
        for (std::size_t i = 0; i < array->size(); ++i) {
            if (!readItem((*array)[i], pointer + "/" + std::to_string(i), out[i])) {
                return false;
            }
        }
        return true;
    }

    // Records `name` and reports it at `element`'s `field` if it was seen
    // before, so a duplicate is reported at its second occurrence, in file
    // order with every other check. `element` is the item just read, so the
    // field is known to exist.
    bool firstOccurrence(std::set<std::string>& seen, const std::string& name, const Json& element,
                         const std::string& pointer, std::string_view field, std::string_view what)
    {
        if (seen.insert(name).second) {
            return true;
        }
        const std::string key(field);
        return fail(element.at(key), pointer + "/" + key,
                    "duplicate " + std::string(what) + " \"" + name + "\"");
    }

    bool readDocument(const Json& root, BodyMapping& out)
    {
        if (!root.is_object()) {
            return fail(root, "", "the document must be a JSON object");
        }
        if (!onlyKeys(root, "",
                      {"format", "version", "limbs", "capsules", "ikChains", "fingers"})) {
            return false;
        }
        const Json* format = member(root, "", "format");
        if (format == nullptr) {
            return false;
        }
        if (!format->is_string() || format->get<std::string>() != kFormatName) {
            return fail(*format, "/format", "must be \"opas.bodymap\"");
        }
        const Json* version = member(root, "", "version");
        if (version == nullptr) {
            return false;
        }
        const std::int64_t v = version->is_number_integer() ? version->get<std::int64_t>() : -1;
        if (v != kVersionWithoutFingers && v != kVersionWithFingers) {
            return fail(*version, "/version", "unsupported version (expected 1 or 2)");
        }
        // Version 1 has no fingers; version 2 requires the array.
        if (v == kVersionWithoutFingers && root.contains("fingers")) {
            return fail(root.at("fingers"), "/fingers", "needs version 2");
        }

        std::set<std::string> limbNames;
        std::set<std::string> regions;
        std::set<std::string> chainNames;
        std::set<std::string> chainEnds;
        const auto limb = [&](const Json& v, const std::string& p, LimbDefinition& o) {
            return readLimb(v, p, o) &&
                   firstOccurrence(limbNames, o.name, v, p, "name", "limb name");
        };
        const auto capsule = [&](const Json& v, const std::string& p, CapsuleRule& o) {
            return readCapsule(v, p, o) &&
                   firstOccurrence(regions, o.region, v, p, "region", "region");
        };
        const auto chain = [&](const Json& v, const std::string& p, IkChain& o) {
            return readChain(v, p, o) &&
                   firstOccurrence(chainNames, o.name, v, p, "name", "chain name") &&
                   firstOccurrence(chainEnds, o.endBone, v, p, "endBone", "chain endBone");
        };
        std::set<std::string> fingerNames;
        const auto finger = [&](const Json& f, const std::string& p, FingerDefinition& o) {
            return readFinger(f, p, o, chainNames) &&
                   firstOccurrence(fingerNames, o.name, f, p, "name", "finger name");
        };
        return readArray(root, "limbs", out.limbs, limb) &&
               readArray(root, "capsules", out.capsules, capsule) &&
               readArray(root, "ikChains", out.ikChains, chain) &&
               (v == kVersionWithoutFingers || readArray(root, "fingers", out.fingers, finger));
    }
};

std::string stringArray(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += (i > 0 ? ", " : "") + jsonString(items[i]);
    }
    out += ']';
    return out;
}

// Writes `items` as the body of a top-level array: "[]" when empty, else one
// entry per element (each produced by `write`), comma-separated.
template <typename Item, typename Write>
std::string arrayBlock(const std::vector<Item>& items, Write write)
{
    if (items.empty()) {
        return "[]";
    }
    std::string out = "[\n";
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += write(items[i]);
        out += i + 1 < items.size() ? ",\n" : "\n";
    }
    out += "  ]";
    return out;
}

} // namespace

BodyMapLoadResult loadBodyMap(std::string_view text, std::string_view fileName)
{
    BodyMapLoadResult result;
    BodyMapping mapping;
    const std::optional<LoadError> error = detail::parseAndRead<BodyMapReader>(
        text, fileName, [&mapping](BodyMapReader& reader, const Json& root) {
            return reader.readDocument(root, mapping);
        });
    if (error) {
        result.error = *error;
    } else {
        result.value = std::move(mapping);
    }
    return result;
}

BodyMapLoadResult loadBodyMapFile(const std::filesystem::path& path)
{
    BodyMapLoadResult result;
    const std::optional<std::string> text = detail::readTextFile(path, result.error);
    if (!text) {
        return result;
    }
    return loadBodyMap(*text, detail::utf8FileName(path));
}

std::string writeBodyMap(const BodyMapping& mapping)
{
    std::string out =
        "{\n  \"format\": \"opas.bodymap\",\n  \"version\": " +
        std::to_string(mapping.fingers.empty() ? kVersionWithoutFingers : kVersionWithFingers) +
        ",\n";
    out += "  \"limbs\": " +
           arrayBlock(mapping.limbs,
                      [](const LimbDefinition& l) {
                          return "    {\"name\": " + jsonString(l.name) +
                                 ", \"bones\": " + stringArray(l.bones) + "}";
                      }) +
           ",\n";
    out += "  \"capsules\": " +
           arrayBlock(mapping.capsules,
                      [](const CapsuleRule& c) {
                          return "    {\"region\": " + jsonString(c.region) +
                                 ", \"bone\": " + jsonString(c.bone) +
                                 ", \"endBone\": " + jsonString(c.endBone) +
                                 ", \"radiusRatio\": " + number(c.radiusRatio) + "}";
                      }) +
           ",\n";
    out += "  \"ikChains\": " + arrayBlock(mapping.ikChains, [](const IkChain& c) {
               return "    {\n      \"name\": " + jsonString(c.name) +
                      ",\n      \"rootBone\": " + jsonString(c.rootBone) +
                      ",\n      \"midBone\": " + jsonString(c.midBone) +
                      ",\n      \"endBone\": " + jsonString(c.endBone) +
                      ",\n      \"minBendRad\": " + number(c.limits.minBendRad) +
                      ",\n      \"maxBendRad\": " + number(c.limits.maxBendRad) +
                      ",\n      \"poleDirection\": " + vec3(c.poleDirection) +
                      ",\n      \"limbRadiusM\": " + number(c.limbRadiusM) + "\n    }";
           });
    if (!mapping.fingers.empty()) {
        out += ",\n  \"fingers\": " + arrayBlock(mapping.fingers, [](const FingerDefinition& f) {
                   std::string curls = "[";
                   for (std::size_t i = 0; i < f.maxCurlRad.size(); ++i) {
                       curls += (i > 0 ? ", " : "") + number(f.maxCurlRad[i]);
                   }
                   curls += ']';
                   return "    {\n      \"name\": " + jsonString(f.name) +
                          ",\n      \"chain\": " + jsonString(f.chain) +
                          ",\n      \"bones\": " + stringArray(f.bones) +
                          ",\n      \"hingeAxis\": " + vec3(f.hingeAxis) +
                          ",\n      \"maxCurlRad\": " + curls + "\n    }";
               });
    }
    out += "\n}\n";
    return out;
}

} // namespace opas::markers
