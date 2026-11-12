#pragma once

// The face editor module (task 0147, "Facial Expression & Speech Timeline"): a library Eden Studio loads from its core
// list; the game never does. Slice 1: open a voice line's curve file, show the waveform and the channel curves, play,
// edit keys, save; the models folder check. Generating curves needs the ONNX runtime (slice 2, toddhow's decision).

#include <openeden/editor_module/module.hpp>

namespace face_editor
{
    struct DirectMlPackage;
}

namespace face_editor
{
    inline constexpr const char* kModuleId = "face_editor";

    /// The module's descriptor (the shared library's entry point returns it; tests use it directly).
    const openeden::editor::EditorModuleDescriptor* moduleDescriptor();

    /// TEST SEAM: makes Face > Install DirectML... fetch \a package instead of the pinned Microsoft package (the
    /// tests point it at a generated file:/// package). Null restores the pinned one. Nothing in Studio calls this.
    void setDirectMlPackageForTests(const DirectMlPackage* package);

    /// What Generate says until the ONNX runtime is in (slice 2).
    inline constexpr const char* kGenerateNeedsRuntime
        = "Generating curves needs the ONNX runtime, which is not part of this build yet (task 0147 slice 2).";
}
