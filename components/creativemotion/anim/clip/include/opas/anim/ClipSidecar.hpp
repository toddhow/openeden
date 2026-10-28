#pragma once

// Clip sidecar files (task 0020, stage C3b; question 4): events for the clips
// of one animation file, kept beside it as JSON until a glTF extension for
// them is settled. Later stages add markers and style tags here.
// Format: docs/formats/clipsidecar.md.
//
// Ownership: plain values.

#include "opas/anim/Clip.hpp"
#include "opas/format/LoadError.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opas::anim {

// The events of one clip, named as in the animation file.
struct ClipEvents {
    std::string clip;
    std::vector<ClipEvent> events; // sorted by time (stable)
    friend bool operator==(const ClipEvents&, const ClipEvents&) = default;
};

struct ClipSidecar {
    std::vector<ClipEvents> clips; // each clip name at most once
    friend bool operator==(const ClipSidecar&, const ClipSidecar&) = default;
};

struct ClipSidecarLoadResult {
    std::optional<ClipSidecar> value;
    format::LoadError error; // meaningful only when value is empty
};

// Parses and validates a sidecar. `fileName` is only used in errors. Never throws.
ClipSidecarLoadResult loadClipSidecar(std::string_view text, std::string_view fileName);
ClipSidecarLoadResult loadClipSidecarFile(const std::filesystem::path& path);

// Canonical text: loading it and writing again gives the same bytes.
std::string writeClipSidecar(const ClipSidecar& sidecar);

// "" if valid, else "<pointer>: <message>": clip names non-empty, without
// control characters, each at most once; event names non-empty, without
// control characters (they may repeat); event times finite, >= 0 and sorted
// (non-decreasing) within each clip; payloads any string.
std::string validateClipSidecar(const ClipSidecar& sidecar);

// Replaces the events of each named clip with the sidecar's. Checks everything
// first and changes nothing on failure: "" if applied, else "<pointer>:
// <message>" (pointer into the sidecar) when the sidecar is invalid, names a
// clip that `clips` lacks, or has an event after that clip's durationS.
// Clips the sidecar does not name keep their events.
std::string applyClipSidecar(const ClipSidecar& sidecar, std::span<Clip> clips);

// The sidecar beside an animation file: "walk.glb" -> "walk.events.json"
// (the extension replaced, same folder).
std::filesystem::path sidecarPathFor(const std::filesystem::path& animationFile);

} // namespace opas::anim
