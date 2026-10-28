#include "opas/anim/GltfReader.hpp"

#include "opas/anim/ClipSidecar.hpp"

#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>

namespace opas::anim {
namespace {

// Rules and conversions: docs/formats/gltfclips.md.

// Relative tolerance for "uniform" and "unit" scale, and for top joints
// seeing the same scale (docs/formats/gltfclips.md).
constexpr double kScaleTolerance = 1e-6;
// Below this length a quaternion from the file has no direction to normalize.
constexpr double kMinQuatLength = 1e-12;

// --- Errors ------------------------------------------------------------------

// A problem found while reading: where (JSON pointer into the glTF document)
// and why. Thrown nowhere; returned up through std::optional-like results.
struct Problem {
    std::string pointer;
    std::string message;
};

std::string at(std::string_view collection, std::size_t index)
{
    return "/" + std::string(collection) + "/" + std::to_string(index);
}

std::string cgltfReason(cgltf_result result)
{
    switch (result) {
    case cgltf_result_data_too_short:
        return "the data is too short (truncated file or buffer)";
    case cgltf_result_unknown_format:
        return "not a glTF 2.0 file, or a buffer URI that cannot be read";
    case cgltf_result_invalid_json:
        return "invalid JSON";
    case cgltf_result_invalid_gltf:
        return "invalid glTF";
    case cgltf_result_invalid_options:
        return "invalid reader options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "could not read the file";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "glTF 1.0 is not supported (glTF 2.0 only)";
    default:
        return "unknown cgltf error";
    }
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

// --- cgltf ownership and file access ------------------------------------------

struct CgltfFree {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using CgltfData = std::unique_ptr<cgltf_data, CgltfFree>;

// cgltf never opens files itself: external buffers are read by
// loadExternalBuffers (cgltf's own reader uses fopen with a narrow path, which
// fails for non-ASCII paths on Windows), and reading from memory must never
// touch the file system.
cgltf_result refuseFile(const cgltf_memory_options* /*memory*/, const cgltf_file_options* /*file*/,
                        const char* /*path*/, cgltf_size* /*size*/, void** /*data*/)
{
    return cgltf_result_io_error;
}

// --- Transforms ------------------------------------------------------------------

// A node's local transform split into translation, rotation and per-axis scale
// (glTF: T * R * S, §3.5.3), and whether it mirrors.
struct NodeTransform {
    math::Vec3 translation;
    math::Quat rotation;
    math::Vec3 scale{1.0, 1.0, 1.0};
    bool mirrors = false;
    bool degenerate = false; // a zero scale axis
};

// A rotation from an orthonormal matrix given by its columns (Shepperd's method).
math::Quat fromColumns(math::Vec3 c0, math::Vec3 c1, math::Vec3 c2)
{
    const double m00 = c0.x;
    const double m11 = c1.y;
    const double m22 = c2.z;
    const double trace = m00 + m11 + m22;
    math::Quat q;
    if (trace > 0.0) {
        const double s = 2.0 * std::sqrt(trace + 1.0);
        q = {(c1.z - c2.y) / s, (c2.x - c0.z) / s, (c0.y - c1.x) / s, 0.25 * s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
        q = {0.25 * s, (c1.x + c0.y) / s, (c2.x + c0.z) / s, (c1.z - c2.y) / s};
    } else if (m11 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
        q = {(c1.x + c0.y) / s, 0.25 * s, (c2.y + c1.z) / s, (c2.x - c0.z) / s};
    } else {
        const double s = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
        q = {(c2.x + c0.z) / s, (c2.y + c1.z) / s, 0.25 * s, (c0.y - c1.x) / s};
    }
    return math::normalized(q);
}

NodeTransform fromMatrix(const cgltf_float* m) // column-major 4x4
{
    NodeTransform out;
    const math::Vec3 c0{m[0], m[1], m[2]};
    const math::Vec3 c1{m[4], m[5], m[6]};
    const math::Vec3 c2{m[8], m[9], m[10]};
    out.translation = {m[12], m[13], m[14]};
    out.scale = {math::length(c0), math::length(c1), math::length(c2)};
    if (out.scale.x == 0.0 || out.scale.y == 0.0 || out.scale.z == 0.0) {
        out.degenerate = true;
        return out;
    }
    if (math::dot(math::cross(c0, c1), c2) < 0.0) {
        out.mirrors = true;
        return out;
    }
    out.rotation = fromColumns(c0 / out.scale.x, c1 / out.scale.y, c2 / out.scale.z);
    return out;
}

NodeTransform localTransform(const cgltf_node& node)
{
    const bool positiveScale =
        !node.has_scale || (node.scale[0] > 0.0F && node.scale[1] > 0.0F && node.scale[2] > 0.0F);
    if (node.has_matrix || !positiveScale) {
        // Negative or zero scale: the matrix tells mirroring from a rotation.
        // cgltf builds it assuming a unit rotation, so a copy is normalized first.
        cgltf_node unit = node;
        if (unit.has_rotation) {
            const math::Quat q{unit.rotation[0], unit.rotation[1], unit.rotation[2],
                               unit.rotation[3]};
            const math::Quat n =
                math::length(q) > kMinQuatLength ? math::normalized(q) : math::Quat{};
            unit.rotation[0] = static_cast<cgltf_float>(n.x);
            unit.rotation[1] = static_cast<cgltf_float>(n.y);
            unit.rotation[2] = static_cast<cgltf_float>(n.z);
            unit.rotation[3] = static_cast<cgltf_float>(n.w);
        }
        cgltf_float m[16];
        cgltf_node_transform_local(&unit, m);
        return fromMatrix(m);
    }
    NodeTransform out;
    if (node.has_translation) {
        out.translation = {node.translation[0], node.translation[1], node.translation[2]};
    }
    if (node.has_rotation) {
        const math::Quat q{node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]};
        out.rotation = math::length(q) > kMinQuatLength ? math::normalized(q) : math::Quat{};
    }
    if (node.has_scale) {
        out.scale = {node.scale[0], node.scale[1], node.scale[2]};
    }
    return out;
}

bool nearlyEqual(double a, double b)
{
    return std::abs(a - b) <= kScaleTolerance * std::max(std::abs(a), std::abs(b));
}

bool isUniform(math::Vec3 s)
{
    return nearlyEqual(s.x, s.y) && nearlyEqual(s.y, s.z);
}

bool isUnit(math::Vec3 s)
{
    return std::abs(s.x - 1.0) <= kScaleTolerance && std::abs(s.y - 1.0) <= kScaleTolerance &&
           std::abs(s.z - 1.0) <= kScaleTolerance;
}

// glTF (x, y, z) -> (-x, z, y): C = Rz(pi) * Rx(pi/2) (docs/formats/gltfclips.md).
math::Quat axisConversion()
{
    return math::fromAxisAngle({0.0, 0.0, 1.0}, std::numbers::pi) *
           math::fromAxisAngle({1.0, 0.0, 0.0}, 0.5 * std::numbers::pi);
}

// What is baked into one joint: the nodes that are not joints above it, as a
// rigid transform (tP, RP), plus the scale applied to the joint's own
// translation (inner, top joints) or to the whole result (outer, others).
struct Baked {
    math::Vec3 translation;
    math::Quat rotation;
    double innerScale = 1.0;
    double outerScale = 1.0;
};

class Converter {
public:
    math::Vec3 translation(const Baked& b, math::Vec3 t) const
    {
        const math::Vec3 inParent = b.translation + math::rotate(b.rotation, t * b.innerScale);
        return math::rotate(c_, inParent) * b.outerScale;
    }
    math::Quat rotation(const Baked& b, math::Quat q) const
    {
        return math::normalized(c_ * b.rotation * q * math::conjugate(c_));
    }

private:
    math::Quat c_ = axisConversion();
};

// --- Reading -----------------------------------------------------------------------

bool isValidName(const char* name)
{
    if (name == nullptr || *name == '\0') {
        return false;
    }
    for (const char* c = name; *c != '\0'; ++c) {
        const auto u = static_cast<unsigned char>(*c);
        if (u < 0x20 || u == 0x7f) {
            return false;
        }
    }
    return true;
}

// One joint of the chosen skin, as the reader sees it.
struct Joint {
    const cgltf_node* node = nullptr;
    std::size_t nodeIndex = 0;
    int parentJoint = -1;                      // index into joints, -1 for a top joint
    std::vector<const cgltf_node*> bakedNodes; // nodes that are not joints above it, nearest first
    Baked baked;
    int bone = -1; // index into the skeleton
};

class Reader {
public:
    Reader(const cgltf_data& data, const GltfReadOptions& options) : data_(data), options_(options)
    {}

    std::optional<Problem> read(GltfAnimations& out)
    {
        const cgltf_skin* skin = nullptr;
        if (auto problem = chooseSkin(skin)) {
            return problem;
        }
        if (auto problem = collectJoints(*skin)) {
            return problem;
        }
        if (auto problem = bake()) {
            return problem;
        }
        buildSkeleton(out.skeleton);
        // Catches what the reader does not check itself, such as a node matrix
        // with a number too large for a float (read as infinity).
        if (const std::string problem = rig::validateSkeleton(out.skeleton); !problem.empty()) {
            return Problem{"/nodes", "the skeleton is invalid: " + problem};
        }
        if (auto problem = chooseRoot()) {
            return problem;
        }
        for (std::size_t a = 0; a < data_.animations_count; ++a) {
            Clip clip;
            if (auto problem = readAnimation(a, out.skeleton, clip)) {
                return problem;
            }
            out.clips.push_back(std::move(clip));
        }
        out.warnings = std::move(warnings_);
        return std::nullopt;
    }

private:
    std::size_t nodeIndex(const cgltf_node* node) const { return cgltf_node_index(&data_, node); }

    std::optional<Problem> chooseSkin(const cgltf_skin*& out) const
    {
        if (data_.skins_count == 0) {
            return Problem{"/skins", "the file has no skin"};
        }
        if (options_.skin.empty()) {
            if (data_.skins_count > 1) {
                return Problem{"/skins", "the file has " + std::to_string(data_.skins_count) +
                                             " skins; name one (GltfReadOptions::skin)"};
            }
            out = &data_.skins[0];
            return std::nullopt;
        }
        for (std::size_t s = 0; s < data_.skins_count; ++s) {
            if (data_.skins[s].name != nullptr && options_.skin == data_.skins[s].name) {
                out = &data_.skins[s];
                return std::nullopt;
            }
        }
        return Problem{"/skins", "no skin named \"" + options_.skin + "\""};
    }

    std::optional<Problem> collectJoints(const cgltf_skin& skin)
    {
        const std::string skinAt = at("skins", cgltf_skin_index(&data_, &skin));
        std::set<std::string> names;
        for (std::size_t j = 0; j < skin.joints_count; ++j) {
            const cgltf_node* node = skin.joints[j];
            if (node == nullptr) {
                return Problem{skinAt + "/joints/" + std::to_string(j), "must name a node"};
            }
            const std::size_t index = nodeIndex(node);
            if (jointOfNode_.contains(index)) {
                return Problem{skinAt + "/joints/" + std::to_string(j), "duplicate joint"};
            }
            if (!isValidName(node->name)) {
                return Problem{at("nodes", index) + "/name",
                               "a joint needs a non-empty name without control characters"};
            }
            if (!names.insert(node->name).second) {
                return Problem{at("nodes", index) + "/name",
                               "duplicate joint name \"" + std::string(node->name) + "\""};
            }
            jointOfNode_[index] = static_cast<int>(joints_.size());
            joints_.push_back(Joint{node, index, -1, {}, {}, -1});
        }
        // Parents: the nearest ancestor that is a joint; the nodes passed on the
        // way are baked. The walk is bounded by the node count, so a malformed
        // hierarchy with a loop cannot hang it.
        for (Joint& joint : joints_) {
            std::size_t steps = 0;
            for (const cgltf_node* n = joint.node->parent; n != nullptr; n = n->parent) {
                if (++steps > data_.nodes_count) {
                    return Problem{at("nodes", joint.nodeIndex), "the node hierarchy has a loop"};
                }
                const auto found = jointOfNode_.find(nodeIndex(n));
                if (found != jointOfNode_.end()) {
                    joint.parentJoint = found->second;
                    break;
                }
                joint.bakedNodes.push_back(n);
            }
        }
        return std::nullopt;
    }

    // Reads a node's rest transform, rejecting mirroring and zero scale.
    std::optional<Problem> restOf(const cgltf_node& node, NodeTransform& out) const
    {
        out = localTransform(node);
        const std::string where =
            at("nodes", nodeIndex(&node)) + (node.has_matrix ? "/matrix" : "/scale");
        if (out.degenerate) {
            return Problem{where, "a scale of zero cannot be read"};
        }
        if (out.mirrors) {
            return Problem{where, "mirroring transforms (negative determinant) are not supported"};
        }
        return std::nullopt;
    }

    // Once per node, though a node between joints may be baked into several.
    void warnScale(const cgltf_node& node, const NodeTransform& t)
    {
        if (!isUnit(t.scale) && warnedNodes_.insert(nodeIndex(&node)).second) {
            warnings_.push_back(at("nodes", nodeIndex(&node)) +
                                (node.has_matrix ? "/matrix" : "/scale") +
                                ": scale ignored (clips carry no scale)");
        }
    }

    std::optional<Problem> bake()
    {
        bool haveScale = false;
        for (Joint& joint : joints_) {
            // Two negative scale axes hide a half turn in the rest rotation that
            // the joint's rotation keys would not carry, so any negative scale on
            // a joint is refused (docs/formats/gltfclips.md).
            const cgltf_node& jn = *joint.node;
            if (jn.has_scale && (jn.scale[0] < 0.0F || jn.scale[1] < 0.0F || jn.scale[2] < 0.0F)) {
                return Problem{at("nodes", joint.nodeIndex) + "/scale",
                               "a negative scale on a joint is not supported"};
            }
            NodeTransform own;
            if (auto problem = restOf(jn, own)) {
                return problem;
            }
            warnScale(*joint.node, own);
            // Compose the baked nodes from the farthest down to the nearest.
            math::Vec3 t;
            math::Quat r;
            double s = 1.0;
            for (auto n = joint.bakedNodes.rbegin(); n != joint.bakedNodes.rend(); ++n) {
                NodeTransform nt;
                if (auto problem = restOf(**n, nt)) {
                    return problem;
                }
                double nodeScale = 1.0;
                if (joint.parentJoint < 0) {
                    // Above the skeleton: a uniform scale is kept (docs).
                    if (!isUniform(nt.scale)) {
                        return Problem{at("nodes", nodeIndex(*n)) +
                                           ((*n)->has_matrix ? "/matrix" : "/scale"),
                                       "a scale above the skeleton must be uniform"};
                    }
                    nodeScale = nt.scale.x;
                } else {
                    warnScale(**n, nt);
                }
                t = t + math::rotate(r, nt.translation * s);
                r = math::normalized(r * nt.rotation);
                s *= nodeScale;
            }
            joint.baked.translation = t;
            joint.baked.rotation = r;
            if (joint.parentJoint < 0) {
                if (haveScale && !nearlyEqual(s, aboveScale_)) {
                    return Problem{at("nodes", joint.nodeIndex),
                                   "top joints see different scales above the skeleton"};
                }
                haveScale = true;
                aboveScale_ = s;
                joint.baked.innerScale = s;
            }
        }
        for (Joint& joint : joints_) {
            if (joint.parentJoint >= 0) {
                joint.baked.outerScale = aboveScale_;
            }
        }
        return std::nullopt;
    }

    // Skin order, except that every joint follows its parent. Iterative: a long
    // chain listed leaf first must not recurse once per joint.
    void emit(std::size_t j, rig::Skeleton& skeleton)
    {
        std::vector<std::size_t> pending; // j, then its ancestors not yet emitted
        for (int k = static_cast<int>(j); k >= 0 && joints_[static_cast<std::size_t>(k)].bone < 0;
             k = joints_[static_cast<std::size_t>(k)].parentJoint) {
            pending.push_back(static_cast<std::size_t>(k));
        }
        for (auto k = pending.rbegin(); k != pending.rend(); ++k) {
            emitOne(*k, skeleton);
        }
    }

    // Appends joint j; its parent is already in the skeleton.
    void emitOne(std::size_t j, rig::Skeleton& skeleton)
    {
        Joint& joint = joints_[j];
        const NodeTransform own = localTransform(*joint.node);
        rig::Bone bone;
        bone.name = joint.node->name;
        bone.parent =
            joint.parentJoint >= 0 ? joints_[static_cast<std::size_t>(joint.parentJoint)].bone : -1;
        bone.localRest.translation = convert_.translation(joint.baked, own.translation);
        bone.localRest.rotation = convert_.rotation(joint.baked, own.rotation);
        joint.bone = static_cast<int>(skeleton.bones.size());
        skeleton.bones.push_back(std::move(bone));
    }

    void buildSkeleton(rig::Skeleton& skeleton)
    {
        // A loop through joint parents is impossible: node parents were walked
        // without one, and joint parents are node ancestors.
        for (std::size_t j = 0; j < joints_.size(); ++j) {
            emit(j, skeleton);
        }
    }

    std::optional<Problem> chooseRoot()
    {
        if (!options_.rootBone.empty()) {
            for (const Joint& joint : joints_) {
                if (options_.rootBone == joint.node->name) {
                    rootBone_ = options_.rootBone;
                    return std::nullopt;
                }
            }
            return Problem{"",
                           "root bone \"" + options_.rootBone + "\" is not a joint of the skin"};
        }
        int tops = 0;
        for (const Joint& joint : joints_) {
            if (joint.parentJoint < 0) {
                ++tops;
                rootBone_ = joint.node->name;
            }
        }
        if (tops != 1) {
            rootBone_.clear();
        }
        return std::nullopt;
    }

    // Unpacks an accessor's floats with exactly `components` per element.
    std::optional<Problem> unpack(const cgltf_accessor* accessor, std::size_t components,
                                  const std::string& where, std::vector<float>& out) const
    {
        if (accessor == nullptr) {
            return Problem{where, "missing accessor"};
        }
        if (cgltf_num_components(accessor->type) != components) {
            return Problem{where,
                           "must have " + std::to_string(components) + " components per element"};
        }
        if (accessor->count > SIZE_MAX / components) {
            return Problem{where, "the accessor's count is too large"};
        }
        out.assign(accessor->count * components, 0.0F);
        if (cgltf_accessor_unpack_floats(accessor, out.data(), out.size()) != out.size()) {
            return Problem{where, "the accessor's data cannot be read"};
        }
        return std::nullopt;
    }

    std::optional<Problem> readTimes(const cgltf_animation_sampler& sampler,
                                     const std::string& samplerAt, std::vector<double>& out) const
    {
        const std::string where = samplerAt + "/input";
        std::vector<float> raw;
        if (auto problem = unpack(sampler.input, 1, where, raw)) {
            return problem;
        }
        if (raw.empty()) {
            return Problem{where, "needs at least one key time"};
        }
        out.assign(raw.begin(), raw.end());
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (!std::isfinite(out[i]) || out[i] < 0.0) {
                return Problem{where, "key times must be finite and at least 0"};
            }
            if (i > 0 && !(out[i] > out[i - 1])) {
                return Problem{where, "key times must be strictly increasing"};
            }
        }
        return std::nullopt;
    }

    std::optional<Problem> readAnimation(std::size_t a, const rig::Skeleton& skeleton, Clip& clip)
    {
        const cgltf_animation& animation = data_.animations[a];
        const std::string animationAt = at("animations", a);
        if (animation.name == nullptr || *animation.name == '\0') {
            clip.name = "animation" + std::to_string(a);
        } else if (isValidName(animation.name)) {
            clip.name = animation.name;
        } else {
            return Problem{animationAt + "/name", "must not contain control characters"};
        }
        if (!clipNames_.insert(clip.name).second) {
            return Problem{animationAt + "/name", "duplicate clip name \"" + clip.name + "\""};
        }
        std::vector<BoneTrack> trackOfBone(skeleton.bones.size());
        std::set<std::pair<std::size_t, int>> targets;
        double durationS = 0.0;
        for (std::size_t c = 0; c < animation.channels_count; ++c) {
            const cgltf_animation_channel& channel = animation.channels[c];
            const std::string channelAt = animationAt + "/channels/" + std::to_string(c);
            if (channel.target_node == nullptr) {
                return Problem{channelAt + "/target/node", "missing target node"};
            }
            if (channel.sampler == nullptr) {
                return Problem{channelAt + "/sampler", "missing sampler"};
            }
            const std::size_t node = nodeIndex(channel.target_node);
            if (!targets.insert({node, static_cast<int>(channel.target_path)}).second) {
                return Problem{channelAt + "/target", "the same node and path are animated twice"};
            }
            const cgltf_animation_sampler& sampler = *channel.sampler;
            const std::string samplerAt =
                animationAt + "/samplers/" +
                std::to_string(cgltf_animation_sampler_index(&animation, &sampler));
            if (sampler.interpolation == cgltf_interpolation_type_cubic_spline) {
                return Problem{samplerAt + "/interpolation",
                               "CUBICSPLINE is not supported (step and linear keys only)"};
            }
            std::vector<double> times;
            if (auto problem = readTimes(sampler, samplerAt, times)) {
                return problem;
            }
            durationS = std::max(durationS, times.back());
            if (channel.target_path == cgltf_animation_path_type_weights) {
                warnings_.push_back(channelAt + ": morph-weight channel ignored (faces are C8)");
                continue;
            }
            if (channel.target_path != cgltf_animation_path_type_translation &&
                channel.target_path != cgltf_animation_path_type_rotation &&
                channel.target_path != cgltf_animation_path_type_scale) {
                return Problem{channelAt + "/target/path", "unknown path"};
            }
            const auto joint = jointOfNode_.find(node);
            if (joint == jointOfNode_.end()) {
                return Problem{channelAt + "/target/node",
                               "node " + std::to_string(node) + " is not a joint of the skin"};
            }
            const Joint& j = joints_[static_cast<std::size_t>(joint->second)];
            const std::size_t components =
                channel.target_path == cgltf_animation_path_type_rotation ? 4 : 3;
            std::vector<float> values;
            if (auto problem = unpack(sampler.output, components, samplerAt + "/output", values)) {
                return problem;
            }
            if (values.size() != times.size() * components) {
                return Problem{samplerAt + "/output",
                               std::to_string(values.size() / components) + " values for " +
                                   std::to_string(times.size()) + " key times"};
            }
            const Interpolation interpolation =
                sampler.interpolation == cgltf_interpolation_type_step ? Interpolation::Step
                                                                       : Interpolation::Linear;
            BoneTrack& track = trackOfBone[static_cast<std::size_t>(j.bone)];
            track.bone = j.node->name;
            if (channel.target_path == cgltf_animation_path_type_scale) {
                for (std::size_t k = 0; k < times.size(); ++k) {
                    if (!isUnit({values[3 * k], values[3 * k + 1], values[3 * k + 2]})) {
                        warnings_.push_back(channelAt +
                                            ": scale channel ignored (clips carry no scale)");
                        break;
                    }
                }
            } else if (channel.target_path == cgltf_animation_path_type_translation) {
                Vec3Track t{times, {}, interpolation};
                for (std::size_t k = 0; k < times.size(); ++k) {
                    t.values.push_back(convert_.translation(
                        j.baked, {values[3 * k], values[3 * k + 1], values[3 * k + 2]}));
                }
                track.translation = std::move(t);
            } else {
                QuatTrack r{times, {}, interpolation};
                for (std::size_t k = 0; k < times.size(); ++k) {
                    const math::Quat q{values[4 * k], values[4 * k + 1], values[4 * k + 2],
                                       values[4 * k + 3]};
                    if (!(math::length(q) > kMinQuatLength)) {
                        return Problem{samplerAt + "/output", "a rotation key has zero length"};
                    }
                    r.values.push_back(convert_.rotation(j.baked, math::normalized(q)));
                }
                track.rotation = std::move(r);
            }
        }
        if (!(durationS > 0.0)) {
            return Problem{animationAt, "the animation has no length (its last key time is 0)"};
        }
        clip.durationS = durationS;
        for (BoneTrack& track : trackOfBone) {
            if (track.translation || track.rotation) {
                if (track.bone == rootBone_) {
                    clip.rootBone = rootBone_;
                }
                clip.tracks.push_back(std::move(track));
            }
        }
        clip.sourceRest = skeleton;
        if (const std::string problem = validateClip(clip); !problem.empty()) {
            return Problem{animationAt, "the converted clip is invalid: " + problem};
        }
        return std::nullopt;
    }

    const cgltf_data& data_;
    const GltfReadOptions& options_;
    Converter convert_;
    std::vector<Joint> joints_;
    std::map<std::size_t, int> jointOfNode_;
    double aboveScale_ = 1.0;
    std::string rootBone_;
    std::set<std::string> clipNames_;
    std::vector<std::string> warnings_;
    std::set<std::size_t> warnedNodes_;
};

GltfReadResult failure(std::string file, std::string pointer, std::string message)
{
    return {std::nullopt,
            format::LoadError{std::move(file), 0, std::move(pointer), std::move(message)}};
}

// Reads every buffer with a relative file URI from `folder` into `storage`,
// which must outlive `data` (cgltf is told not to free them).
std::optional<Problem> loadExternalBuffers(const cgltf_data& data,
                                           const std::filesystem::path& folder,
                                           std::vector<std::vector<char>>& storage)
{
    for (std::size_t b = 0; b < data.buffers_count; ++b) {
        cgltf_buffer& buffer = data.buffers[b];
        if (buffer.uri == nullptr || std::strncmp(buffer.uri, "data:", 5) == 0) {
            continue;
        }
        const std::string where = at("buffers", b) + "/uri";
        if (std::strstr(buffer.uri, "://") != nullptr) {
            return Problem{where, "only relative file URIs and data: URIs can be read"};
        }
        std::string uri(buffer.uri);
        uri.resize(cgltf_decode_uri(uri.data())); // %20 and the like (RFC 3986)
        std::filesystem::path relative;
        try {
            const auto* begin = reinterpret_cast<const char8_t*>(uri.data());
            relative = std::filesystem::path(std::u8string(begin, begin + uri.size()));
        } catch (const std::exception&) {
            return Problem{where, "the URI is not valid UTF-8"};
        }
        // Buffers live beside the file or below it: no absolute, drive or
        // network paths and no "..", so a file cannot read elsewhere
        // (docs/formats/gltfclips.md).
        if (relative.empty() || relative.has_root_name() || relative.has_root_directory()) {
            return Problem{where, "must be a relative path"};
        }
        for (const std::filesystem::path& part : relative) {
            if (part == "..") {
                return Problem{where, "must not leave the file's folder (\"..\")"};
            }
        }
        const std::filesystem::path full = folder / relative;
        std::error_code ec;
        // Devices and pipes (/dev/zero, CON) could block or never end.
        if (!std::filesystem::is_regular_file(full, ec)) {
            return Problem{where, "cannot open the buffer file \"" + uri + "\""};
        }
        const std::uintmax_t fileSize = std::filesystem::file_size(full, ec);
        if (ec || fileSize < buffer.size) {
            return Problem{where, "the buffer file is shorter than the buffer's byteLength"};
        }
        std::ifstream in(full, std::ios::binary);
        std::vector<char> bytes(buffer.size);
        if (!in || !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            return Problem{where, "cannot read the buffer file \"" + uri + "\""};
        }
        storage.push_back(std::move(bytes));
        buffer.data = storage.back().data();
        buffer.data_free_method = cgltf_data_free_method_none;
    }
    return std::nullopt;
}

// Parses, loads buffers, validates and reads. `folder` is where external
// buffers are found, or null for memory (no file access).
GltfReadResult readDocument(std::span<const std::byte> bytes, const std::string& fileName,
                            const std::filesystem::path* folder, const GltfReadOptions& options)
{
    cgltf_options cgltfOptions{};
    cgltfOptions.file.read = &refuseFile;
    // Declared before data, so the buffers outlive cgltf's references to them.
    std::vector<std::vector<char>> bufferStorage;
    cgltf_data* raw = nullptr;
    cgltf_result result = cgltf_parse(&cgltfOptions, bytes.data(), bytes.size(), &raw);
    const CgltfData data(raw);
    if (result != cgltf_result_success) {
        return failure(fileName, "", cgltfReason(result));
    }
    if (folder == nullptr) {
        for (std::size_t b = 0; b < data->buffers_count; ++b) {
            const char* uri = data->buffers[b].uri;
            if (uri != nullptr && std::strncmp(uri, "data:", 5) != 0) {
                return failure(fileName, at("buffers", b) + "/uri",
                               "external buffers cannot be read from memory");
            }
        }
    } else if (auto problem = loadExternalBuffers(*data, *folder, bufferStorage)) {
        return failure(fileName, problem->pointer, problem->message);
    }
    result = cgltf_load_buffers(&cgltfOptions, data.get(), nullptr);
    if (result != cgltf_result_success) {
        return failure(fileName, "", "loading buffers: " + cgltfReason(result));
    }
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        return failure(fileName, "", "validating: " + cgltfReason(result));
    }
    GltfAnimations animations;
    Reader reader(*data, options);
    if (const std::optional<Problem> problem = reader.read(animations)) {
        return failure(fileName, problem->pointer, problem->message);
    }
    return {std::move(animations), {}};
}

// Never throws: allocation failures from absurd sizes in a malformed file,
// and any library exception (std::filesystem, streams), become errors.
template <typename Read>
GltfReadResult guarded(const std::string& fileName, Read read)
{
    try {
        return read();
    } catch (const std::bad_alloc&) {
        return failure(fileName, "", "out of memory (sizes in the file too large)");
    } catch (const std::length_error&) {
        return failure(fileName, "", "out of memory (sizes in the file too large)");
    } catch (const std::exception& e) {
        return failure(fileName, "", std::string("cannot read the file: ") + e.what());
    }
}

// A path for messages; one that is not valid Unicode cannot throw here.
std::string displayName(const std::filesystem::path& path)
{
    try {
        return utf8(path);
    } catch (const std::exception&) {
        return "<a path that is not valid Unicode>";
    }
}

// "<pointer>: <message>" from applyClipSidecar into a LoadError.
format::LoadError sidecarError(const std::string& file, const std::string& problem)
{
    const std::size_t split = problem.find(": ");
    if (split == std::string::npos) {
        return {file, 0, "", problem};
    }
    return {file, 0, problem.substr(0, split), problem.substr(split + 2)};
}

} // namespace

GltfReadResult readGltfFile(const std::filesystem::path& path, const GltfReadOptions& options)
{
    const std::string fileName = displayName(path);
    return guarded(fileName, [&]() -> GltfReadResult {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return failure(fileName, "", "cannot open the file");
        }
        const std::filesystem::path folder = path.parent_path();
        const std::vector<char> text((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
        GltfReadResult result =
            readDocument(std::as_bytes(std::span(text)), fileName, &folder, options);
        if (!result.value || !options.loadSidecar) {
            return result;
        }
        const std::filesystem::path sidecarPath = sidecarPathFor(path);
        std::error_code ec;
        if (!std::filesystem::exists(sidecarPath, ec)) {
            return result;
        }
        const ClipSidecarLoadResult sidecar = loadClipSidecarFile(sidecarPath);
        if (!sidecar.value) {
            return {std::nullopt, sidecar.error};
        }
        if (const std::string problem = applyClipSidecar(*sidecar.value, result.value->clips);
            !problem.empty()) {
            return {std::nullopt, sidecarError(displayName(sidecarPath), problem)};
        }
        return result;
    });
}

GltfReadResult readGltf(std::span<const std::byte> bytes, std::string_view fileName,
                        const GltfReadOptions& options)
{
    const std::string name(fileName);
    return guarded(name, [&] { return readDocument(bytes, name, nullptr, options); });
}

} // namespace opas::anim
