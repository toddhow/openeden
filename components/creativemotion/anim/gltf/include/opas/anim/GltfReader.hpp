#pragma once

// Reads a skin and its animations from a glTF 2.0 file (.gltf or .glb) into
// the internal clip model (task 0020, stage C3b; decision 0005). Rules for
// what is read and how it is converted: docs/formats/gltfclips.md.
//
// Ownership: plain values; nothing from the glTF library escapes this call
// (cgltf is a private dependency of anim/gltf).

#include "opas/anim/Clip.hpp"
#include "opas/format/LoadError.hpp"
#include "opas/rig/Skeleton.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opas::anim
{

    struct GltfReadOptions
    {
        // Name of the skin to read; "" reads the file's only skin (an error if it
        // has none or several).
        std::string skin;
        // Joint whose motion becomes root motion; "" picks the skin's top joint
        // when it has exactly one, else none. A named bone must be a joint.
        std::string rootBone;
        // readGltfFile only: apply the sidecar beside the file
        // (sidecarPathFor(path)) when it exists.
        bool loadSidecar = true;
    };

    struct GltfAnimations
    {
        rig::Skeleton skeleton; // the skin's joints, rest pose, Z up
        std::vector<Clip> clips; // one per animation, in file order
        std::vector<std::string> warnings; // "<pointer>: <message>", for ignored data
    };

    struct GltfReadResult
    {
        std::optional<GltfAnimations> value;
        format::LoadError error; // meaningful only when value is empty
    };

    // Reads a .gltf or .glb file; external buffers are found relative to its
    // folder. Never throws.
    GltfReadResult readGltfFile(const std::filesystem::path& path, const GltfReadOptions& options = {});

    // Reads a .gltf or .glb document from memory. Only embedded data is allowed
    // (the .glb binary chunk or data: URIs); an external buffer is an error.
    // `fileName` is only used in errors. No sidecar is read. Never throws.
    GltfReadResult readGltf(
        std::span<const std::byte> bytes, std::string_view fileName, const GltfReadOptions& options = {});

} // namespace opas::anim
