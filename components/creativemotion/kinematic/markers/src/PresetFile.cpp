#include "opas/markers/PresetFile.hpp"

#include "JsonSupport.hpp"

#include <cstdint>
#include <tuple>

namespace opas::markers {
namespace {

using detail::Json;
using detail::jsonString;

constexpr std::string_view kFormatName = "opas.preset";
constexpr int kFormatVersion = 1;

// Characters an SPDX license expression uses: identifiers (letters, digits,
// '.', '-', '+'), "LicenseRef-..." references (':'), and the operators
// AND / OR / WITH with spaces and parentheses. The identifier itself is not
// checked against the SPDX list.
bool looksLikeSpdx(const std::string& text)
{
    if (text.empty() || text.front() == ' ' || text.back() == ' ') {
        return false;
    }
    for (const char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == ':' ||
                        c == '(' || c == ')' || c == ' ';
        if (!ok) {
            return false;
        }
    }
    return true;
}

class PresetReader : public detail::JsonReader {
public:
    using JsonReader::JsonReader;

    bool stringField(const Json& root, std::string_view key, std::string& out)
    {
        const Json* value = member(root, "", key);
        return value != nullptr && readString(*value, "/" + std::string(key), out);
    }

    bool readDocument(const Json& root, PresetManifest& out)
    {
        if (!root.is_object()) {
            return fail(root, "", "the document must be a JSON object");
        }
        if (!onlyKeys(root, "",
                      {"format", "version", "name", "author", "presetVersion", "license",
                       "priority", "requires", "description"})) {
            return false;
        }
        const Json* format = member(root, "", "format");
        if (format == nullptr) {
            return false;
        }
        if (!format->is_string() || format->get<std::string>() != kFormatName) {
            return fail(*format, "/format", "must be \"opas.preset\"");
        }
        const Json* version = member(root, "", "version");
        if (version == nullptr) {
            return false;
        }
        if (!version->is_number_integer() || version->get<std::int64_t>() != kFormatVersion) {
            return fail(*version, "/version", "unsupported version (expected 1)");
        }
        if (!stringField(root, "name", out.name) || !stringField(root, "author", out.author) ||
            !stringField(root, "presetVersion", out.presetVersion) ||
            !stringField(root, "license", out.license)) {
            return false;
        }
        if (!looksLikeSpdx(out.license)) {
            return fail(root.at("license"), "/license", "must be an SPDX license expression");
        }
        const Json* priority = member(root, "", "priority");
        if (priority == nullptr || !readInt(*priority, "/priority", out.priority)) {
            return false;
        }
        if (const auto it = root.find("requires"); it != root.end()) {
            if (!it->is_array()) {
                return fail(*it, "/requires", "must be an array of strings");
            }
            out.requirements.resize(it->size());
            for (std::size_t i = 0; i < it->size(); ++i) {
                if (!readString((*it)[i], "/requires/" + std::to_string(i), out.requirements[i])) {
                    return false;
                }
            }
        }
        const auto description = root.find("description");
        return description == root.end() ||
               readString(*description, "/description", out.description);
    }
};

} // namespace

PresetLoadResult loadPresetManifest(std::string_view text, std::string_view fileName)
{
    PresetLoadResult result;
    PresetManifest manifest;
    const std::optional<LoadError> error = detail::parseAndRead<PresetReader>(
        text, fileName, [&manifest](PresetReader& reader, const Json& root) {
            return reader.readDocument(root, manifest);
        });
    if (error) {
        result.error = *error;
    } else {
        result.value = std::move(manifest);
    }
    return result;
}

PresetLoadResult loadPresetManifestFile(const std::filesystem::path& path)
{
    PresetLoadResult result;
    const std::optional<std::string> text = detail::readTextFile(path, result.error);
    if (!text) {
        return result;
    }
    return loadPresetManifest(*text, detail::utf8FileName(path));
}

std::string writePresetManifest(const PresetManifest& m)
{
    std::string out =
        "{\n  \"format\": \"opas.preset\",\n  \"version\": " + std::to_string(kFormatVersion) +
        ",\n";
    out += "  \"name\": " + jsonString(m.name) + ",\n";
    out += "  \"author\": " + jsonString(m.author) + ",\n";
    out += "  \"presetVersion\": " + jsonString(m.presetVersion) + ",\n";
    out += "  \"license\": " + jsonString(m.license) + ",\n";
    out += "  \"priority\": " + std::to_string(m.priority);
    if (!m.requirements.empty()) {
        out += ",\n  \"requires\": [";
        for (std::size_t i = 0; i < m.requirements.size(); ++i) {
            out += (i > 0 ? ", " : "") + jsonString(m.requirements[i]);
        }
        out += ']';
    }
    if (!m.description.empty()) {
        out += ",\n  \"description\": " + jsonString(m.description);
    }
    out += "\n}\n";
    return out;
}

std::optional<std::size_t> chooseMarkerSource(std::span<const MarkerSource> candidates)
{
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!best) {
            best = i;
            continue;
        }
        const MarkerSource& a = candidates[i];
        const MarkerSource& b = candidates[*best];
        // Better = user file first, then higher priority, then smaller name.
        const auto key = [](const MarkerSource& s) {
            return std::tuple{s.userFile ? 0 : 1, -static_cast<std::int64_t>(s.priority),
                              std::string_view(s.presetName)};
        };
        if (key(a) < key(b)) {
            best = i;
        }
    }
    return best;
}

} // namespace opas::markers
