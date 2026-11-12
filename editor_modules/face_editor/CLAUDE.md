# face_editor

The face timeline module (task 0147, Eden Studio's "Facial Expression & Speech Timeline" of CreativeMotion's face-layer
write-up): a library only Eden Studio loads, from its core list (`CSMModule::coreModules()`). The game plays saved curves
only (decision of toddhow, 2026-11-03); Generate (slice 2, 2026-11-12) runs the Audio2Face networks here, in the editor.

- **Links:** the Editor Module API 1.4, `eden_face` (`components/face`: the `opas.facecurves` reader, canonical writer,
  SHA-256), `components/fuz` (compiled in) and FFmpeg (the game's dependency). No Qt or Studio type.
- **Files:** `facekeys.*` (frames to sparse keys and back, exact), `face_document.*` (the asset: open, key commands,
  canonical save, audio lookup, new file), `audio_pcm.*` (WAV and the dispatch), `audio_ffmpeg.cpp` (mp3, xWMA in `.fuz`,
  ogg ...), `models_folder.*` (the Audio2Face models folder), `face_editor_module.*` (what is registered),
  `face_editor_export.cpp` (the entry point, only in the shared library). Tests: `apps/opencs_tests/model/module/face/`.
- **A channel nobody edits is never rewritten** (its integers are kept; opening and saving gives the same bytes). An edited
  channel is resampled from its keys. `sparsify` and `resample` share one evaluation rule (`between`, `quantise`): change
  one and the exactness test (200 random channels) catches a disagreement.
- **`Face::validate` wants the audio duration within ONE frame of `frameCount / frameRate`:** a new file takes the smallest
  count that covers the audio (`ceil`), not round + 1 (a real Morrowind line failed with that).
- **The models folder** is `<user data>/models/audio2face/<variant>/`; the module writes `README.txt` there when it loads
  and never touches an existing one. Sizes are checked at once, the SHA-256 of `network.onnx` on request (a background
  task). The recorded hashes are in `models_folder.cpp` and `docs/tasks/0147-studio-face-timeline.md`; no model file is
  ever copied into the repository.
- **Pitfall:** a local named `audioRoot` shadows the member function of the same name inside a lambda (MSVC C3493).
- **Generation (task 0147 slice 2, 2026-11-12):** `a2f_npz.*`, `a2f_solve.*`, `a2f_generate.*` are pure (tests with a stub network, `apps/opencs_tests/model/module/face/testa2f*.cpp`); `ort_backend.*` (ONNX Runtime) exists only with `OPENEDEN_FACE_GENERATOR` (Windows x64, `extern/CMakeLists.txt`; NuGet packages pinned by SHA-512, `docs/dependencies.md`); `generate_run.*` is one whole run without Studio types. The runtime is DELAY LOADED: the module loads without `onnxruntime.dll`, `ortRuntimeAvailable` says why not, and it loads the DLL from the application directory only (Windows ships an older one in System32). The window layouts, the solve and the emotion input are INFERRED from the gate (task 0071 stage a, task 0147 slice 2), not NVIDIA's code: do not "fix" them from memory.
- **The solve is a port of the gate's `bssolve.py`; its pair penalty makes coordinate descent slow when `strengthSymmetry` is large** (the factor per sweep is about (2 ls)^2 / (H_aa H_bb)); the tests use ls = 10 for that reason. 100 sweeps are used.
- **Determinism:** one noise tensor per run from `GenerateOptions::mNoiseSeed` (own splitmix64 and Box-Muller; libm may differ in the last bit across platforms), the same provider gives identical curves; CPU and DirectML agree within about 0.02 in weight.
- **Pitfalls:** a `.npz` must be saved with `numpy.savez` (stored); `savez_compressed` is refused by name. `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` alone also restricts the DLL's own dependency search: add `LOAD_LIBRARY_SEARCH_SYSTEM32` (error 126 otherwise). A test executable that links `face_editor_objects` through `$<TARGET_OBJECTS>` gets no link usage requirements from it: link `openeden_onnxruntime` explicitly (`apps/opencs_tests/CMakeLists.txt`). Check: `tools/facegen_check/check.sh`.
