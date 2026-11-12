# face_editor

The face timeline module (task 0147, Eden Studio's "Facial Expression & Speech Timeline" of CreativeMotion's face-layer
write-up): a library only Eden Studio loads, from its core list (`CSMModule::coreModules()`). The game plays saved curves
only (decision of toddhow, 2026-11-03); nothing here generates curves yet (slice 2 needs the ONNX Runtime and his word).

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
