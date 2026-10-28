#include "opas/rig/RigFile.hpp"

#include "Problem.hpp"
#include "opas/format/FileReader.hpp"
#include "opas/format/FileWriter.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opas::rig {
namespace {

using format::detail::FileReader;
using format::detail::Json;
using format::detail::jsonString;
using format::detail::number;
using format::detail::quat;
using format::detail::vec3;

constexpr std::string_view kFormatName = "opas.rig";
// Version 2 added pyramid swing limits (docs/tasks/0014). Version 1 files still
// load; writeRig always writes the newest version.
constexpr std::int64_t kNewestVersion = 2;

// A bone as written in the file; the parent is resolved to an index once
// every bone has been read, so a parent listed too late can be told apart
// from an unknown one.
struct FileBone {
    std::string name;
    std::string parent;
    math::Transform rest;
};

// The rig rules (docs/formats/rig.md) on top of the shared reader. Types,
// finiteness and unit rotations are checked while reading; names, parents and
// every other rule are checked on the complete rig by detail::findRigProblem,
// so loaded files and programmatic rigs follow exactly the same rules.
class RigReader : public FileReader {
public:
    using FileReader::FileReader;

    bool readBone(const Json& value, const std::string& pointer, FileBone& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a bone object");
        }
        return onlyKeys(value, pointer, {"name", "parent", "translationM", "rotation"}) &&
               stringField(value, pointer, "name", out.name) &&
               stringOrEmptyField(value, pointer, "parent", out.parent) &&
               vec3Field(value, pointer, "translationM", out.rest.translation) &&
               quatField(value, pointer, "rotation", out.rest.rotation);
    }

    bool readShape(const Json& body, const std::string& bodyPointer, Shape& out)
    {
        const std::string pointer = bodyPointer + "/shape";
        const Json* shape = objectField(body, bodyPointer, "shape", "a shape object");
        if (shape == nullptr) {
            return false;
        }
        std::string type;
        if (!stringField(*shape, pointer, "type", type)) {
            return false;
        }
        if (type == "capsule") {
            CapsuleShape capsule;
            if (!onlyKeys(*shape, pointer, {"type", "halfHeightM", "radiusM"}) ||
                !numberField(*shape, pointer, "halfHeightM", capsule.halfHeightM) ||
                !numberField(*shape, pointer, "radiusM", capsule.radiusM)) {
                return false;
            }
            out = capsule;
        } else if (type == "box") {
            BoxShape box;
            if (!onlyKeys(*shape, pointer, {"type", "halfExtentsM"}) ||
                !vec3Field(*shape, pointer, "halfExtentsM", box.halfExtentsM)) {
                return false;
            }
            out = box;
        } else if (type == "sphere") {
            SphereShape sphere;
            if (!onlyKeys(*shape, pointer, {"type", "radiusM"}) ||
                !numberField(*shape, pointer, "radiusM", sphere.radiusM)) {
                return false;
            }
            out = sphere;
        } else {
            return fail(shape->at("type"), pointer + "/type",
                        "must be \"capsule\", \"box\" or \"sphere\"");
        }
        return true;
    }

    bool readBody(const Json& value, const std::string& pointer, Body& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a body object");
        }
        return onlyKeys(value, pointer, {"bone", "shape", "translationM", "rotation", "massKg"}) &&
               stringField(value, pointer, "bone", out.bone) &&
               readShape(value, pointer, out.shape) &&
               vec3Field(value, pointer, "translationM", out.local.translation) &&
               quatField(value, pointer, "rotation", out.local.rotation) &&
               numberField(value, pointer, "massKg", out.massKg);
    }

    bool readJoint(const Json& value, const std::string& pointer, Joint& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a joint object");
        }
        // Version 1 joints are all cones and have no "swing" key.
        if (version_ >= 2 && !readSwingShape(value, pointer, out.swing)) {
            return false;
        }
        const bool cone = out.swing == SwingShape::Cone;
        const bool keysOk =
            version_ < 2 ? onlyKeys(value, pointer,
                                    {"bone", "twistAxis", "planeAxis", "normalHalfConeRad",
                                     "planeHalfConeRad", "twistMinRad", "twistMaxRad"})
            : cone       ? onlyKeys(value, pointer,
                                    {"bone", "twistAxis", "planeAxis", "swing", "normalHalfConeRad",
                                     "planeHalfConeRad", "twistMinRad", "twistMaxRad"})
                         : onlyKeys(value, pointer,
                                    {"bone", "twistAxis", "planeAxis", "swing", "planeSwingMinRad",
                                     "planeSwingMaxRad", "normalSwingMinRad", "normalSwingMaxRad",
                                     "twistMinRad", "twistMaxRad"});
        if (!keysOk || !stringField(value, pointer, "bone", out.bone) ||
            !vec3Field(value, pointer, "twistAxis", out.twistAxis) ||
            !vec3Field(value, pointer, "planeAxis", out.planeAxis)) {
            return false;
        }
        if (cone) {
            // Stored as -half, +half; the rules then check the half-angles.
            double normalHalf = 0.0;
            double planeHalf = 0.0;
            if (!numberField(value, pointer, "normalHalfConeRad", normalHalf) ||
                !numberField(value, pointer, "planeHalfConeRad", planeHalf)) {
                return false;
            }
            out.normalSwingMinRad = -normalHalf;
            out.normalSwingMaxRad = normalHalf;
            out.planeSwingMinRad = -planeHalf;
            out.planeSwingMaxRad = planeHalf;
        } else if (!numberField(value, pointer, "planeSwingMinRad", out.planeSwingMinRad) ||
                   !numberField(value, pointer, "planeSwingMaxRad", out.planeSwingMaxRad) ||
                   !numberField(value, pointer, "normalSwingMinRad", out.normalSwingMinRad) ||
                   !numberField(value, pointer, "normalSwingMaxRad", out.normalSwingMaxRad)) {
            return false;
        }
        return numberField(value, pointer, "twistMinRad", out.twistMinRad) &&
               numberField(value, pointer, "twistMaxRad", out.twistMaxRad);
    }

    bool readSwingShape(const Json& joint, const std::string& pointer, SwingShape& out)
    {
        std::string swing;
        if (!stringField(joint, pointer, "swing", swing)) {
            return false;
        }
        if (swing == "cone") {
            out = SwingShape::Cone;
        } else if (swing == "pyramid") {
            out = SwingShape::Pyramid;
        } else {
            return fail(joint.at("swing"), pointer + "/swing", "must be \"cone\" or \"pyramid\"");
        }
        return true;
    }

    bool readHelper(const Json& value, const std::string& pointer, HelperRule& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a helper object");
        }
        std::string type;
        if (!stringField(value, pointer, "type", type)) {
            return false;
        }
        if (type == "twist") {
            if (!onlyKeys(value, pointer, {"type", "bone", "driver", "fraction", "twistAxis"})) {
                return false;
            }
            TwistRule twist;
            if (!stringField(value, pointer, "bone", twist.bone) ||
                !stringField(value, pointer, "driver", twist.driver) ||
                !numberField(value, pointer, "fraction", twist.fraction) ||
                !vec3Field(value, pointer, "twistAxis", twist.twistAxis)) {
                return false;
            }
            out = std::move(twist);
        } else if (type == "swing") {
            if (!onlyKeys(value, pointer,
                          {"type", "bone", "driver", "fraction", "driverAxis", "drivenAxis",
                           "maxAngleRad"})) {
                return false;
            }
            SwingRule swing;
            if (!stringField(value, pointer, "bone", swing.bone) ||
                !stringField(value, pointer, "driver", swing.driver) ||
                !numberField(value, pointer, "fraction", swing.fraction) ||
                !vec3Field(value, pointer, "driverAxis", swing.driverAxis) ||
                !vec3Field(value, pointer, "drivenAxis", swing.drivenAxis) ||
                !numberField(value, pointer, "maxAngleRad", swing.maxAngleRad)) {
                return false;
            }
            out = std::move(swing);
        } else if (type == "chain") {
            if (!onlyKeys(value, pointer, {"type", "startBone", "endBone", "intermediates"})) {
                return false;
            }
            ChainRule chain;
            if (!stringField(value, pointer, "startBone", chain.startBone) ||
                !stringField(value, pointer, "endBone", chain.endBone)) {
                return false;
            }
            const std::string interPointer = pointer + "/intermediates";
            const Json* interArray = member(value, pointer, "intermediates");
            if (interArray == nullptr) {
                return false;
            }
            if (!interArray->is_array()) {
                return fail(*interArray, interPointer, "must be an array");
            }
            chain.intermediates.resize(interArray->size());
            for (std::size_t k = 0; k < interArray->size(); ++k) {
                const Json& item = (*interArray)[k];
                const std::string itemPointer = interPointer + "/" + std::to_string(k);
                if (!item.is_object()) {
                    return fail(item, itemPointer, "must be an intermediate object");
                }
                if (!onlyKeys(item, itemPointer, {"bone", "weight"}) ||
                    !stringField(item, itemPointer, "bone", chain.intermediates[k].bone) ||
                    !numberField(item, itemPointer, "weight", chain.intermediates[k].weight)) {
                    return false;
                }
            }
            out = std::move(chain);
        } else if (type == "breathing") {
            if (!onlyKeys(value, pointer, {"type", "bone", "rateHz", "expansionM", "pitchRad"})) {
                return false;
            }
            BreathingRule breathing;
            if (!stringField(value, pointer, "bone", breathing.bone) ||
                !numberField(value, pointer, "rateHz", breathing.rateHz) ||
                !numberField(value, pointer, "expansionM", breathing.expansionM) ||
                !numberField(value, pointer, "pitchRad", breathing.pitchRad)) {
                return false;
            }
            out = std::move(breathing);
        } else {
            return fail(value.at("type"), pointer + "/type",
                        "must be \"twist\", \"swing\", \"chain\" or \"breathing\"");
        }
        return true;
    }

    // Turns file bones into skeleton bones. Names must be clean (so a bad name
    // is reported as itself, not as an unknown parent of the next bone), each
    // parent must be a bone listed earlier, and duplicates are reported at
    // their second occurrence.
    bool resolveBones(const Json& root, const std::vector<FileBone>& fileBones, Skeleton& out)
    {
        out.bones.resize(fileBones.size());
        for (std::size_t i = 0; i < fileBones.size(); ++i) {
            const FileBone& bone = fileBones[i];
            const std::string pointer = "/bones/" + std::to_string(i);
            if (!detail::isValidName(bone.name)) {
                return fail(root.at(Json::json_pointer(pointer + "/name")), pointer + "/name",
                            detail::kInvalidNameMessage);
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (fileBones[j].name == bone.name) {
                    return fail(root.at(Json::json_pointer(pointer + "/name")), pointer + "/name",
                                "duplicate bone \"" + bone.name + "\"");
                }
            }
            int parent = -1;
            if (!bone.parent.empty()) {
                for (std::size_t j = 0; j < i && parent < 0; ++j) {
                    if (fileBones[j].name == bone.parent) {
                        parent = static_cast<int>(j);
                    }
                }
                if (parent < 0) {
                    return failParent(root, fileBones, i);
                }
            }
            out.bones[i] = Bone{bone.name, parent, bone.rest};
        }
        return true;
    }

    bool readDocument(const Json& root, Rig& out)
    {
        if (!readHeader(root, kFormatName,
                        {"format", "version", "name", "bones", "bodies", "joints", "helpers"},
                        kNewestVersion, &version_) ||
            !stringField(root, "", "name", out.name)) {
            return false;
        }
        if (version_ == 1 && root.contains("helpers")) {
            return fail(root.at("helpers"), "/helpers", "needs version 2");
        }
        std::vector<FileBone> fileBones;
        const auto bone = [this](const Json& v, const std::string& p, FileBone& o) {
            return readBone(v, p, o);
        };
        const auto body = [this](const Json& v, const std::string& p, Body& o) {
            return readBody(v, p, o);
        };
        const auto joint = [this](const Json& v, const std::string& p, Joint& o) {
            return readJoint(v, p, o);
        };
        const auto helper = [this](const Json& v, const std::string& p, HelperRule& o) {
            return readHelper(v, p, o);
        };
        if (!readArray(root, "bones", fileBones, bone) ||
            !resolveBones(root, fileBones, out.skeleton) ||
            !readArray(root, "bodies", out.bodies, body) ||
            !readArray(root, "joints", out.joints, joint) ||
            (root.contains("helpers") && !readArray(root, "helpers", out.helpers, helper))) {
            return false;
        }
        if (const auto problem = detail::findRigProblem(out)) {
            return failAt(root, problem->pointer, problem->message);
        }
        return true;
    }

private:
    bool failParent(const Json& root, const std::vector<FileBone>& fileBones, std::size_t i)
    {
        const std::string pointer = "/bones/" + std::to_string(i) + "/parent";
        const Json& value = root.at(Json::json_pointer(pointer));
        const std::string& parent = fileBones[i].parent;
        if (parent == fileBones[i].name) {
            return fail(value, pointer, "a bone cannot be its own parent");
        }
        for (std::size_t j = i + 1; j < fileBones.size(); ++j) {
            if (fileBones[j].name == parent) {
                return fail(value, pointer,
                            "\"" + parent + "\" must be listed before this bone (parents first)");
            }
        }
        return fail(value, pointer, "unknown bone \"" + parent + "\"");
    }

    std::int64_t version_ = 1; // the file's, set by readHeader
};

std::string shapeText(const Shape& shape)
{
    if (const auto* capsule = std::get_if<CapsuleShape>(&shape)) {
        return "{\"type\": \"capsule\", \"halfHeightM\": " + number(capsule->halfHeightM) +
               ", \"radiusM\": " + number(capsule->radiusM) + "}";
    }
    if (const auto* box = std::get_if<BoxShape>(&shape)) {
        return "{\"type\": \"box\", \"halfExtentsM\": " + vec3(box->halfExtentsM) + "}";
    }
    const auto& sphere = std::get<SphereShape>(shape);
    return "{\"type\": \"sphere\", \"radiusM\": " + number(sphere.radiusM) + "}";
}

std::string jointText(const Joint& j)
{
    std::string out = "    {\n      \"bone\": " + jsonString(j.bone) +
                      ",\n      \"twistAxis\": " + vec3(j.twistAxis) +
                      ",\n      \"planeAxis\": " + vec3(j.planeAxis);
    if (j.swing == SwingShape::Cone) {
        out += ",\n      \"swing\": \"cone\",\n      \"normalHalfConeRad\": " +
               number(j.normalSwingMaxRad) +
               ",\n      \"planeHalfConeRad\": " + number(j.planeSwingMaxRad);
    } else {
        out += ",\n      \"swing\": \"pyramid\",\n      \"planeSwingMinRad\": " +
               number(j.planeSwingMinRad) +
               ",\n      \"planeSwingMaxRad\": " + number(j.planeSwingMaxRad) +
               ",\n      \"normalSwingMinRad\": " + number(j.normalSwingMinRad) +
               ",\n      \"normalSwingMaxRad\": " + number(j.normalSwingMaxRad);
    }
    return out + ",\n      \"twistMinRad\": " + number(j.twistMinRad) +
           ",\n      \"twistMaxRad\": " + number(j.twistMaxRad) + "\n    }";
}

std::string twistText(const TwistRule& rule)
{
    return "    {\n      \"type\": \"twist\",\n"
           "      \"bone\": " + jsonString(rule.bone) + ",\n"
           "      \"driver\": " + jsonString(rule.driver) + ",\n"
           "      \"fraction\": " + number(rule.fraction) + ",\n"
           "      \"twistAxis\": " + vec3(rule.twistAxis) + "\n"
           "    }";
}

std::string swingText(const SwingRule& rule)
{
    return "    {\n      \"type\": \"swing\",\n"
           "      \"bone\": " + jsonString(rule.bone) + ",\n"
           "      \"driver\": " + jsonString(rule.driver) + ",\n"
           "      \"fraction\": " + number(rule.fraction) + ",\n"
           "      \"driverAxis\": " + vec3(rule.driverAxis) + ",\n"
           "      \"drivenAxis\": " + vec3(rule.drivenAxis) + ",\n"
           "      \"maxAngleRad\": " + number(rule.maxAngleRad) + "\n"
           "    }";
}

std::string chainText(const ChainRule& rule)
{
    std::string out = "    {\n      \"type\": \"chain\",\n"
                      "      \"startBone\": " + jsonString(rule.startBone) + ",\n"
                      "      \"endBone\": " + jsonString(rule.endBone) + ",\n"
                      "      \"intermediates\": [";
    if (rule.intermediates.empty()) {
        out += "]\n    }";
    } else {
        out += "\n";
        for (std::size_t i = 0; i < rule.intermediates.size(); ++i) {
            out += "        {\"bone\": " + jsonString(rule.intermediates[i].bone) +
                   ", \"weight\": " + number(rule.intermediates[i].weight) + "}";
            out += (i + 1 < rule.intermediates.size() ? ",\n" : "\n");
        }
        out += "      ]\n    }";
    }
    return out;
}

std::string breathingText(const BreathingRule& rule)
{
    return "    {\n      \"type\": \"breathing\",\n"
           "      \"bone\": " + jsonString(rule.bone) + ",\n"
           "      \"rateHz\": " + number(rule.rateHz) + ",\n"
           "      \"expansionM\": " + number(rule.expansionM) + ",\n"
           "      \"pitchRad\": " + number(rule.pitchRad) + "\n"
           "    }";
}

std::string helperText(const HelperRule& helper)
{
    return std::visit(
        [](const auto& rule) {
            using T = std::decay_t<decltype(rule)>;
            if constexpr (std::is_same_v<T, TwistRule>) {
                return twistText(rule);
            } else if constexpr (std::is_same_v<T, SwingRule>) {
                return swingText(rule);
            } else if constexpr (std::is_same_v<T, ChainRule>) {
                return chainText(rule);
            } else if constexpr (std::is_same_v<T, BreathingRule>) {
                return breathingText(rule);
            }
        },
        helper);
}

} // namespace

RigLoadResult loadRig(std::string_view text, std::string_view fileName)
{
    RigLoadResult result;
    Rig rig;
    const std::optional<format::LoadError> error = format::detail::parseAndRead<RigReader>(
        text, fileName,
        [&rig](RigReader& r, const Json& root) { return r.readDocument(root, rig); });
    if (error) {
        result.error = *error;
    } else {
        result.value = std::move(rig);
    }
    return result;
}

RigLoadResult loadRigFile(const std::filesystem::path& path)
{
    RigLoadResult result;
    const std::optional<std::string> text = format::detail::readTextFile(path, result.error);
    if (!text) {
        return result;
    }
    return loadRig(*text, format::detail::utf8FileName(path));
}

std::string writeRig(const Rig& rig)
{
    const std::vector<Bone>& bones = rig.skeleton.bones;
    std::string out =
        "{\n  \"format\": \"opas.rig\",\n  \"version\": " + std::to_string(kNewestVersion) +
        ",\n  \"name\": " + jsonString(rig.name) + ",\n";
    out += "  \"bones\": " +
           format::detail::arrayBlock(
               bones,
               [&bones](const Bone& b) {
                   const std::string parent =
                       b.parent >= 0 && static_cast<std::size_t>(b.parent) < bones.size()
                           ? bones[static_cast<std::size_t>(b.parent)].name
                           : std::string();
                   return "    {\"name\": " + jsonString(b.name) +
                          ", \"parent\": " + jsonString(parent) +
                          ", \"translationM\": " + vec3(b.localRest.translation) +
                          ", \"rotation\": " + quat(b.localRest.rotation) + "}";
               }) +
           ",\n";
    out += "  \"bodies\": " +
           format::detail::arrayBlock(
               rig.bodies,
               [](const Body& b) {
                   return "    {\n      \"bone\": " + jsonString(b.bone) +
                          ",\n      \"shape\": " + shapeText(b.shape) +
                          ",\n      \"translationM\": " + vec3(b.local.translation) +
                          ",\n      \"rotation\": " + quat(b.local.rotation) +
                          ",\n      \"massKg\": " + number(b.massKg) + "\n    }";
               }) +
           ",\n";
    out += "  \"joints\": " + format::detail::arrayBlock(rig.joints, jointText);
    if (!rig.helpers.empty()) {
        out += ",\n  \"helpers\": " + format::detail::arrayBlock(rig.helpers, helperText);
    }
    out += "\n}\n";
    return out;
}

} // namespace opas::rig
