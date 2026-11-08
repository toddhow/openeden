# components/script_obscript

The ObScript runtime of the common compatibility pack (task 0092, decisions 0013 and 0014). It implements the contract
`sdk/include/openeden/script/obscript.hpp` over `components/compiler` and `components/interpreter`. It holds no authority
over the world or the save: the game pack supplies the world (`ObScriptWorld`), the functions (`registerFunction`) and
the save chunk (it stores what `serialise` returns).

## Rules

- **Morrowind is unchanged.** The seam in the inherited compiler and interpreter is `Compiler::ParserOptions` (in
  `Compiler::Context`, all off by default) and `Interpreter::RunLimits` (off by default). The golden recording
  `apps/components_tests/compiler/morrowind_programs.golden.txt` is a byte comparison of Morrowind's compiled programs; it
  is golden data and is never edited or regenerated without toddhow's approval.
- **Source is compiled, bytecode is never read.** `SCDA` is only a cross-check in the gated real-data check.
- **Located errors, no exception across the contract.** Every public method is `noexcept`; a failure is an
  `ErrorReport` (`script:line:column: message`, 1-based). Compiler positions are the end of the token in the inherited
  code; `CollectingHandler` reports them as they are.
- **Caps** are in `obscriptlimits.hpp` and tested at the cap and one past it. Add a cap there with its test.
- **The instruction loop allocates nothing** (`apps/openmw_tests/script_obscript/testobscriptallocation.cpp`): do not add
  a `std::string` or `std::vector` to `InstanceContext`, `OpFunction::execute` or `RuntimeImpl::run`.
- **Determinism:** instances live in an ordered map, functions get opcodes in registration order, unknown names in order
  of first use; the save blob is byte-identical for the same state.
- **Unknown functions (the extender policy):** tolerant mode registers every name found in a call position as a function
  that skips its arguments, logs once per name and returns zero; strict mode (tests) makes it a located error. The
  places where a name can be a call are found by the line pass in `obscriptcompile.cpp` (`findCandidates`), not by the
  inherited parsers.
- **Function table:** clean-room. Nothing is copied from xNVSE, FOSE or xOBSE source or tables; no game executable is
  read. Names and argument shapes come from the user's installed data (note 0046 section 10).
- **Forms as values (item 1):** `ParserOptions::mFormValues` is on in `compileText`. A form name is fetched by name at
  run time (`OpFetchForm`, `InstanceContext::getFormId`, no allocation); forms compare and store but do not take part
  in arithmetic. `ObScriptWorld::resolveForm` answers `Player` like any name.
- **The function table (item 2):** `derive.*` pairs command names in `SCTX` with opcodes in `SCDA` (the format is
  written at the top of `derive.hpp`); `functiontable.*` holds the table, its canonical text, `mergeSeed` and
  `declareAll`. The table is derived on the user's machine and written to a user folder: **never commit or ship a
  table or any text read from game data**, and never print a command name from the data in a report. A seed entry
  needs a source that starts with READ or RECALLED-VERIFY; nothing may come from another project's tables.
- **Declared functions:** `declareFunction` gives a shape and no handler. Running one pops its arguments, logs once
  per name and returns zero. Each declared function has a second pair of opcodes (`mSkipCode`) for scripts compiled
  with its arguments skipped: tolerant mode retries a failed script that way (`compileScript` calls `compileOnce`
  twice) and adds one warning. Strict mode never retries.
- **Strict mode makes warnings errors in the `Reporter`, not in the compiler's handler**, so that the "Extra argument"
  warnings of skipped arguments can be told from the others (`ParserOptions::mQuietSkippedArguments`).

- **Grammar habits of the shipped scripts (item 3):** the front end sets three more `ParserOptions` (all off by default):
  `mDigitLeadingMembers` (with `Scanner::setDigitLeadingMembers`: `Quest.2ndStage`), `mDottedSkippedArguments` (a skipped
  argument may be `Quest.var`, a command name, or a member spelled like a keyword; `DiscardParser`) and
  `mUnknownMembersAreWarnings` (`Set Quest.unknown to 1` is a warning and the line is skipped; an error in strict mode).
  The line pass treats a record-named word at the start of a statement (or after if, elseif, while, to) as a command when
  arguments follow, never registers a name after `Set Owner.` as a command, and lets a name after a dot be a command
  unless the owner has a variable of that name. Punctuation-only lines outside the blocks and any text after the last
  End are warnings (errors in strict mode); words before or between blocks stay an error. **Not supported:** a command's
  result used as the reference of another (`GetFoo.Bar`), `%`, a skipped argument followed by a minus sign in an
  expression (read as a negative argument).
- **Game pack glue (item 4):** `gamepackglue.*`. `ObScriptSaveChunk` is the pack's save chunk handler `script.obscript`
  (its bytes are the runtime's save blob and the chunk version is `Limits::kSaveFormatVersion`; a chunk over
  `Limits::kMaxSaveBlobBytes` is refused unread); `attachToGamePack` declares the language `obscript` through
  `sdk::ScriptHostService` and registers the handler through `sdk::SaveChunkService`. The 0084 service only declares a
  language: the runtime itself is created and owned by the game pack. A pack that is not a game pack gets nullptr for
  both services and a message back.

## Pitfalls met

- `Compiler::ScriptParser::reset()` clears the `Locals` it was built with. Do not call it after declaring locals.
- `Compiler::Locals::get` has a private non-const overload: read through a `const Locals&`.
- Test objects built against an older `compiler/context.hpp` crash when linked with a newer library: rebuild every test
  object that includes it.
