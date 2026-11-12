# Task: Skyrim SE data survey tools (conditions, Papyrus natives and events, NIF block types)

Status: built (lighter loop, same hand wrote tests and code; see the CLAUDE.md changelog row of 2026-11-11 for what was and was not run).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.16, 4.20, 4.21, section 6 risk 2, section 8 item 2; results in `docs/research/0055-skyrim-rules-survey.md`.
Goal: measure on the user's own installed data which condition functions, Papyrus natives and events real content uses most, so the conditions engine and the Papyrus world natives are built in order of value.
Inherited code touched: none. New: `game_packs/skyrimse/{include/game_skyrimse,src}/survey.*`, `survey_scripts.*`, `survey_nif.*`; reuses `Content::Plugin`, `inflateRecord`, `LoadOrder`/`FormIdResolver`, `ScriptPapyrus::Pex` and `PapyrusRuntime::nativeCallSites`, `Nif::Reader`.
Inputs / outputs: the Data folder (read only); aggregate lines starting `SKYRIMSE-SURVEY`; nothing written.
Constraints: no condition function table or name (clean-room rule, decision 0014): functions are `fn_<index>`; aggregates only, nothing from the game committed; pure library code with no file access, the gated run reads files.
Acceptance criteria:
- [x] CTDA decode, subrecord walker (with XXXX), coverage curve, condition tallies, event tally and NIF header scan have unit tests on generated records, written first and run red against stubs.
- [x] The gated run prints the distinct functions, uses per function and per record type, the coverage curve, the parameter kinds, the ranked natives, the events and the NIF block types.
- [x] The CTDA layout is checked against the data (sizes, operator and run-on ranges) and what is verified is stated.
- [x] Nothing in the game changes.
Out of scope: function semantics, the conditions engine, Papyrus wiring, the mod list, Creation Club content.
