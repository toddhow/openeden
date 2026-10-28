#include "opas/rig/ProfileFile.hpp"

#include "Problem.hpp"
#include "opas/format/FileReader.hpp"
#include "opas/format/FileWriter.hpp"

#include <cstdint>
#include <utility>

namespace opas::rig {
namespace {

using format::detail::FileReader;
using format::detail::Json;
using format::detail::jsonString;
using format::detail::number;

constexpr std::string_view kFormatName = "opas.profile";
// Version 2 (M5, docs/tasks/0017) adds feet and the behavior sections.
constexpr std::int64_t kNewestVersion = 2;

// The profile rules (docs/formats/profile.md) on top of the shared reader.
// Types and finiteness are checked while reading; ranges and duplicate
// overrides by detail::findProfileProblem on the complete profile.
class ProfileReader : public FileReader {
public:
    using FileReader::FileReader;

    // A motor object under `key` (`motor`, or `legMotor` in balance).
    bool readMotor(const Json& parent, const std::string& parentPointer, MotorGains& out,
                   std::string_view key = "motor")
    {
        const std::string pointer = parentPointer + "/" + std::string(key);
        const Json* motor = objectField(parent, parentPointer, key, "a motor object");
        return motor != nullptr &&
               onlyKeys(*motor, pointer,
                        {"stiffnessNmPerRad", "dampingNmsPerRad", "maxTorqueNm"}) &&
               numberField(*motor, pointer, "stiffnessNmPerRad", out.stiffnessNmPerRad) &&
               numberField(*motor, pointer, "dampingNmsPerRad", out.dampingNmsPerRad) &&
               numberField(*motor, pointer, "maxTorqueNm", out.maxTorqueNm);
    }

    bool readMaterial(const Json& parent, const std::string& parentPointer, Material& out)
    {
        const std::string pointer = parentPointer + "/material";
        const Json* material = objectField(parent, parentPointer, "material", "a material object");
        return material != nullptr &&
               onlyKeys(*material, pointer,
                        {"friction", "restitution", "linearDamping", "angularDamping"}) &&
               numberField(*material, pointer, "friction", out.friction) &&
               numberField(*material, pointer, "restitution", out.restitution) &&
               numberField(*material, pointer, "linearDamping", out.linearDamping) &&
               numberField(*material, pointer, "angularDamping", out.angularDamping);
    }

    bool readJointOverride(const Json& value, const std::string& pointer, JointOverride& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a joint override object");
        }
        return onlyKeys(value, pointer, {"bone", "motor"}) &&
               stringField(value, pointer, "bone", out.bone) &&
               readMotor(value, pointer, out.motor);
    }

    bool readBodyOverride(const Json& value, const std::string& pointer, BodyOverride& out)
    {
        if (!value.is_object()) {
            return fail(value, pointer, "must be a body override object");
        }
        return onlyKeys(value, pointer, {"bone", "material"}) &&
               stringField(value, pointer, "bone", out.bone) &&
               readMaterial(value, pointer, out.material);
    }

    bool readBalance(const Json& root, BalanceTuning& out)
    {
        const Json* balance = objectField(root, "", "balance", "a balance object");
        return balance != nullptr &&
               onlyKeys(*balance, "/balance",
                        {"legMotor", "ankleGainRadPerM", "hipGainRadPerM", "maxLeanRad"}) &&
               readMotor(*balance, "/balance", out.legMotor, "legMotor") &&
               numberField(*balance, "/balance", "ankleGainRadPerM", out.ankleGainRadPerM) &&
               numberField(*balance, "/balance", "hipGainRadPerM", out.hipGainRadPerM) &&
               numberField(*balance, "/balance", "maxLeanRad", out.maxLeanRad);
    }

    bool readBool(const Json& object, const std::string& pointer, std::string_view key, bool& out)
    {
        const Json* value = member(object, pointer, key);
        if (value == nullptr) {
            return false;
        }
        if (!value->is_boolean()) {
            return fail(*value, pointer + "/" + std::string(key), "must be true or false");
        }
        out = value->get<bool>();
        return true;
    }

    bool readVelocityTuning(const Json& stepping, VelocityTuning& out)
    {
        const std::string at = "/stepping/velocityTuning";
        const Json* vt =
            objectField(stepping, "/stepping", "velocityTuning", "a velocityTuning object");
        if (vt == nullptr ||
            !onlyKeys(*vt, at,
                      {"gainNsPerM", "maxTorqueNm", "afterTouchdownS", "trunkBones",
                       "ankleOffTiltRad"}) ||
            !numberField(*vt, at, "gainNsPerM", out.gainNsPerM) ||
            !numberField(*vt, at, "maxTorqueNm", out.maxTorqueNm) ||
            !numberField(*vt, at, "afterTouchdownS", out.afterTouchdownS)) {
            return false;
        }
        const Json* bones = member(*vt, at, "trunkBones");
        if (bones == nullptr) {
            return false;
        }
        if (!bones->is_array()) {
            return fail(*bones, at + "/trunkBones", "must be an array of bone names");
        }
        out.trunkBones.resize(bones->size());
        for (std::size_t i = 0; i < bones->size(); ++i) {
            if (!readString((*bones)[i], at + "/trunkBones/" + std::to_string(i),
                            out.trunkBones[i])) {
                return false;
            }
        }
        return numberField(*vt, at, "ankleOffTiltRad", out.ankleOffTiltRad);
    }

    bool readStepping(const Json& root, SteppingTuning& out)
    {
        const std::string at = "/stepping";
        const Json* stepping = objectField(root, "", "stepping", "a stepping object");
        if (stepping == nullptr) {
            return false;
        }
        // The M5c trunk assist was replaced by braking through the legs.
        if (stepping->contains("assist")) {
            return fail(stepping->at("assist"), at + "/assist",
                        "was removed in task 0019: braking is velocityTuning");
        }
        if (!onlyKeys(*stepping, at,
                      {"armTimeS", "stepTimeS", "maxSteps", "liftM", "stanceHalfWidthM",
                       "maxStepLegFraction", "velocityTuning", "swingGravityCompensation"}) ||
            !numberField(*stepping, at, "armTimeS", out.armTimeS) ||
            !numberField(*stepping, at, "stepTimeS", out.stepTimeS)) {
            return false;
        }
        const Json* maxSteps = member(*stepping, at, "maxSteps");
        if (maxSteps == nullptr || !readInt(*maxSteps, at + "/maxSteps", out.maxSteps)) {
            return false;
        }
        return numberField(*stepping, at, "liftM", out.liftM) &&
               numberField(*stepping, at, "stanceHalfWidthM", out.stanceHalfWidthM) &&
               numberField(*stepping, at, "maxStepLegFraction", out.maxStepLegFraction) &&
               readVelocityTuning(*stepping, out.velocityTuning) &&
               readBool(*stepping, at, "swingGravityCompensation", out.swingGravityCompensation);
    }

    bool readHitReaction(const Json& root, HitReactionTuning& out)
    {
        const std::string at = "/hitReaction";
        const Json* hit = objectField(root, "", "hitReaction", "a hit reaction object");
        if (hit == nullptr ||
            !onlyKeys(*hit, at, {"dropFraction", "spread", "recoverTimeS", "reachJoints"}) ||
            !numberField(*hit, at, "dropFraction", out.dropFraction) ||
            !numberField(*hit, at, "spread", out.spread) ||
            !numberField(*hit, at, "recoverTimeS", out.recoverTimeS)) {
            return false;
        }
        const Json* reach = member(*hit, at, "reachJoints");
        return reach != nullptr && readInt(*reach, at + "/reachJoints", out.reachJoints);
    }

    bool readArbiter(const Json& root, ArbiterTuning& out)
    {
        const std::string at = "/arbiter";
        const Json* arbiter = objectField(root, "", "arbiter", "an arbiter object");
        return arbiter != nullptr &&
               onlyKeys(*arbiter, at,
                        {"blendTimeS", "fallTiltRad", "standingSpeedMps", "standingTimeS",
                         "restSpeedMps", "restTimeS"}) &&
               numberField(*arbiter, at, "blendTimeS", out.blendTimeS) &&
               numberField(*arbiter, at, "fallTiltRad", out.fallTiltRad) &&
               numberField(*arbiter, at, "standingSpeedMps", out.standingSpeedMps) &&
               numberField(*arbiter, at, "standingTimeS", out.standingTimeS) &&
               numberField(*arbiter, at, "restSpeedMps", out.restSpeedMps) &&
               numberField(*arbiter, at, "restTimeS", out.restTimeS);
    }

    bool readDeath(const Json& root, DeathTuning& out)
    {
        const std::string at = "/death";
        const Json* death = objectField(root, "", "death", "a death object");
        if (death == nullptr ||
            !onlyKeys(*death, at,
                      {"jointPositionSteps", "linearDampingPerS", "angularDampingPerS"}) ||
            !numberField(*death, at, "linearDampingPerS", out.linearDampingPerS) ||
            !numberField(*death, at, "angularDampingPerS", out.angularDampingPerS)) {
            return false;
        }
        const Json* steps = member(*death, at, "jointPositionSteps");
        return steps != nullptr &&
               readInt(*steps, at + "/jointPositionSteps", out.jointPositionSteps);
    }

    bool readDocument(const Json& root, Profile& out)
    {
        std::int64_t version = 1;
        if (!readHeader(root, kFormatName,
                        {"format", "version", "rig", "defaults", "joints", "bodies", "feet",
                         "balance", "stepping", "hitReaction", "arbiter", "death"},
                        kNewestVersion, &version) ||
            !stringField(root, "", "rig", out.rig)) {
            return false;
        }
        // Version 1 has no feet or balance; version 2 requires feet, and
        // balance needs them (reported at balance, as validateProfile does).
        for (const char* key : {"feet", "balance", "stepping", "hitReaction", "arbiter", "death"}) {
            if (version == 1 && root.contains(key)) {
                return fail(root.at(key), "/" + std::string(key), "needs version 2");
            }
        }
        if (version >= 2 && root.contains("balance") && !root.contains("feet")) {
            return fail(root.at("balance"), "/balance",
                        "needs feet (balance uses the foot sensors)");
        }
        const Json* defaults = objectField(root, "", "defaults", "a defaults object");
        if (defaults == nullptr || !onlyKeys(*defaults, "/defaults", {"motor", "material"}) ||
            !readMotor(*defaults, "/defaults", out.defaultMotor) ||
            !readMaterial(*defaults, "/defaults", out.defaultMaterial)) {
            return false;
        }
        const auto joint = [this](const Json& v, const std::string& p, JointOverride& o) {
            return readJointOverride(v, p, o);
        };
        const auto body = [this](const Json& v, const std::string& p, BodyOverride& o) {
            return readBodyOverride(v, p, o);
        };
        const auto foot = [this](const Json& v, const std::string& p, std::string& o) {
            return readString(v, p, o);
        };
        if (!readArray(root, "joints", out.joints, joint) ||
            !readArray(root, "bodies", out.bodies, body) ||
            (version >= 2 && !readArray(root, "feet", out.feet, foot))) {
            return false;
        }
        // An empty list would write back as version 1, so it is not allowed.
        if (version >= 2 && out.feet.empty()) {
            return fail(root.at("feet"), "/feet", "needs at least one foot in version 2");
        }
        if (version >= 2 && root.contains("balance") && !readBalance(root, out.balance.emplace())) {
            return false;
        }
        if (version >= 2 && root.contains("stepping") &&
            !readStepping(root, out.stepping.emplace())) {
            return false;
        }
        if (version >= 2 && root.contains("hitReaction") &&
            !readHitReaction(root, out.hitReaction.emplace())) {
            return false;
        }
        if (version >= 2 && root.contains("arbiter") && !readArbiter(root, out.arbiter.emplace())) {
            return false;
        }
        if (version >= 2 && root.contains("death") && !readDeath(root, out.death.emplace())) {
            return false;
        }
        if (const auto problem = detail::findProfileProblem(out)) {
            return failAt(root, problem->pointer, problem->message);
        }
        return true;
    }
};

std::string motorText(const MotorGains& m)
{
    return "{\"stiffnessNmPerRad\": " + number(m.stiffnessNmPerRad) +
           ", \"dampingNmsPerRad\": " + number(m.dampingNmsPerRad) +
           ", \"maxTorqueNm\": " + number(m.maxTorqueNm) + "}";
}

std::string materialText(const Material& m)
{
    return "{\"friction\": " + number(m.friction) + ", \"restitution\": " + number(m.restitution) +
           ", \"linearDamping\": " + number(m.linearDamping) +
           ", \"angularDamping\": " + number(m.angularDamping) + "}";
}

} // namespace

ProfileLoadResult loadProfile(std::string_view text, std::string_view fileName)
{
    ProfileLoadResult result;
    Profile profile;
    const std::optional<format::LoadError> error = format::detail::parseAndRead<ProfileReader>(
        text, fileName,
        [&profile](ProfileReader& r, const Json& root) { return r.readDocument(root, profile); });
    if (error) {
        result.error = *error;
    } else {
        result.value = std::move(profile);
    }
    return result;
}

ProfileLoadResult loadProfileFile(const std::filesystem::path& path)
{
    ProfileLoadResult result;
    const std::optional<std::string> text = format::detail::readTextFile(path, result.error);
    if (!text) {
        return result;
    }
    return loadProfile(*text, format::detail::utf8FileName(path));
}

std::string writeProfile(const Profile& profile)
{
    // Version 1 while a profile has nothing from version 2, so version 1
    // files keep round-tripping byte for byte.
    const bool version2 = !profile.feet.empty() || profile.balance.has_value() ||
                          profile.stepping.has_value() || profile.hitReaction.has_value() ||
                          profile.arbiter.has_value() || profile.death.has_value();
    std::string out = "{\n  \"format\": \"opas.profile\",\n  \"version\": ";
    out += version2 ? "2" : "1";
    out += ",\n  \"rig\": " + jsonString(profile.rig) + ",\n";
    out += "  \"defaults\": {\n    \"motor\": " + motorText(profile.defaultMotor) +
           ",\n    \"material\": " + materialText(profile.defaultMaterial) + "\n  },\n";
    out += "  \"joints\": " +
           format::detail::arrayBlock(profile.joints,
                                      [](const JointOverride& o) {
                                          return "    {\"bone\": " + jsonString(o.bone) +
                                                 ", \"motor\": " + motorText(o.motor) + "}";
                                      }) +
           ",\n";
    out += "  \"bodies\": " + format::detail::arrayBlock(profile.bodies, [](const BodyOverride& o) {
               return "    {\"bone\": " + jsonString(o.bone) +
                      ", \"material\": " + materialText(o.material) + "}";
           });
    if (version2) {
        out += ",\n  \"feet\": [";
        for (std::size_t i = 0; i < profile.feet.size(); ++i) {
            out += (i > 0 ? ", " : "") + jsonString(profile.feet[i]);
        }
        out += ']';
    }
    if (profile.balance) {
        const BalanceTuning& b = *profile.balance;
        out += ",\n  \"balance\": {\n    \"legMotor\": " + motorText(b.legMotor) +
               ",\n    \"ankleGainRadPerM\": " + number(b.ankleGainRadPerM) +
               ",\n    \"hipGainRadPerM\": " + number(b.hipGainRadPerM) +
               ",\n    \"maxLeanRad\": " + number(b.maxLeanRad) + "\n  }";
    }
    if (profile.stepping) {
        const SteppingTuning& s = *profile.stepping;
        out += ",\n  \"stepping\": {\n    \"armTimeS\": " + number(s.armTimeS) +
               ",\n    \"stepTimeS\": " + number(s.stepTimeS) +
               ",\n    \"maxSteps\": " + std::to_string(s.maxSteps) +
               ",\n    \"liftM\": " + number(s.liftM) +
               ",\n    \"stanceHalfWidthM\": " + number(s.stanceHalfWidthM) +
               ",\n    \"maxStepLegFraction\": " + number(s.maxStepLegFraction);
        // Task 0019: velocityTuning on one line, its trunkBones a one-line
        // array, then swingGravityCompensation.
        const VelocityTuning& v = s.velocityTuning;
        out += ",\n    \"velocityTuning\": {\"gainNsPerM\": " + number(v.gainNsPerM) +
               ", \"maxTorqueNm\": " + number(v.maxTorqueNm) +
               ", \"afterTouchdownS\": " + number(v.afterTouchdownS) + ", \"trunkBones\": [";
        for (std::size_t i = 0; i < v.trunkBones.size(); ++i) {
            out += (i > 0 ? ", " : "") + jsonString(v.trunkBones[i]);
        }
        out += "], \"ankleOffTiltRad\": " + number(v.ankleOffTiltRad) + "}";
        out += ",\n    \"swingGravityCompensation\": ";
        out += s.swingGravityCompensation ? "true" : "false";
        out += "\n  }";
    }
    if (profile.hitReaction) {
        const HitReactionTuning& h = *profile.hitReaction;
        out += ",\n  \"hitReaction\": {\"dropFraction\": " + number(h.dropFraction) +
               ", \"spread\": " + number(h.spread) +
               ", \"recoverTimeS\": " + number(h.recoverTimeS) +
               ", \"reachJoints\": " + std::to_string(h.reachJoints) + "}";
    }
    if (profile.arbiter) {
        const ArbiterTuning& a = *profile.arbiter;
        out += ",\n  \"arbiter\": {\"blendTimeS\": " + number(a.blendTimeS) +
               ", \"fallTiltRad\": " + number(a.fallTiltRad) +
               ", \"standingSpeedMps\": " + number(a.standingSpeedMps) +
               ", \"standingTimeS\": " + number(a.standingTimeS) +
               ", \"restSpeedMps\": " + number(a.restSpeedMps) +
               ", \"restTimeS\": " + number(a.restTimeS) + "}";
    }
    if (profile.death) {
        const DeathTuning& d = *profile.death;
        out += ",\n  \"death\": {\"jointPositionSteps\": " + std::to_string(d.jointPositionSteps) +
               ", \"linearDampingPerS\": " + number(d.linearDampingPerS) +
               ", \"angularDampingPerS\": " + number(d.angularDampingPerS) + "}";
    }
    out += "\n}\n";
    return out;
}

} // namespace opas::rig
