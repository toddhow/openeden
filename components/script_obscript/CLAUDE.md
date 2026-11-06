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

## Pitfalls met

- `Compiler::ScriptParser::reset()` clears the `Locals` it was built with. Do not call it after declaring locals.
- `Compiler::Locals::get` has a private non-const overload: read through a `const Locals&`.
- Test objects built against an older `compiler/context.hpp` crash when linked with a newer library: rebuild every test
  object that includes it.
