# Research note 0046: script runtimes for the common compatibility pack (Papyrus and ObScript)

2026-11-06. Docs only: no engine code, no new dependency, no game build. For decision 0014 (draft, with its 2026-11-06 amendment) and draft task 0092. toddhow, 2026-11-06: "Can you get started on the Papyrus/ObScript VM; we will need ObScript support for Fallout 3 / New Vegas."

How to read the facts (the honesty rules of the root guide):

- **READ** = a file of this repository at commit `4c8156e3fa` that was opened this session (path and function cited).
- **MEASURED** = produced by a read-only tool run on toddhow's installed data. The tools are throwaway Python in `I:\Projects\OpenEden-work\scriptvm\` (`arch.py` BSA v104/v105 and BA2 readers, `pex.py` a strict `.pex` parser, `survey.py`, `esmscan.py`; not in the repository). Only **aggregate numbers and the names of natives and classes** appear here. No script text, no opcode table and no game file was copied into the repository.
- **RECALLED-VERIFY** = general knowledge (xNVSE and GECK wiki, UESP, the CK wiki, Papyrus documentation). **No web page was opened this session**, so every statement about another project's current behaviour is RECALLED-VERIFY, however sure it sounds.

## 0. Summary

1. **Installed:** Skyrim SE (with SKSE-era mods, a 666-mod MO2 profile), Fallout 4 (281-mod profile), Fallout New Vegas (base, five story DLCs, the small packs). **Fallout 3 and Oblivion are not installed** (searched `I:\SteamLibrary`, `I:\GOG Galaxy` (Morrowind only), `C:\Program Files (x86)\Steam`); nothing is measured for them. There is no MO2 instance and no mod for New Vegas, so **NVSE usage in mods cannot be measured here**.
2. **ObScript is the cheaper and the more urgent runtime, and OpenMW's stack already covers its shape.** All 3,715 `SCPT` records of the New Vegas masters carry both source text (`SCTX`) and compiled bytecode (`SCDA`); another 11,000 or so fragments in `INFO`, `QUST`, `PACK`, `TERM`, `REFR` and `PERK` records carry them too. Recompiling from source (what OpenMW does for Morrowind) is therefore possible for everything shipped. The compiled form also decodes simply: a statement walker reached the exact end of **all 3,715** scripts.
3. **Papyrus is a clean, well-bounded VM:** the `.pex` layout was confirmed by a strict parse of 100 percent of 22,000-plus Bethesda scripts (Skyrim 3.2 big-endian, Fallout 4 3.9 little-endian). The native surface is 686 (Skyrim SE) and 891 (Fallout 4) functions, and 106 natives cover 90 percent of the call sites in Bethesda's own Skyrim scripts. The hard part is not Bethesda's scripts: **18 percent of the native call sites in the installed Skyrim mods go to functions that Bethesda does not provide** (SKSE and plugin libraries).
4. **Recommended order:** ObScript first (stages A to D below), the `.pex` loader and the VM interpreter second, one contract family `openeden.script.*` designed for both now. The first slice is task 0092.
5. **Conflict to settle:** `docs/design/game-packs.md` section 5.6 says the interpreter stays in the core, the compiler and command set go to `game_morrowind`, and Papyrus is out of scope. The placement proposed here (both runtimes in the common pack) changes that. It is recorded as a question for toddhow, not decided (section 7).

## 1. What the repository has (READ)

### 1.1 The Morrowind script stack

| Layer | Where | What it does |
| --- | --- | --- |
| Records | `components/esm3/loadscpt.*` (`ESM::Script`) | Reads `SCHR` (local counts, data size, string table size), `SCVR` (variable names), `SCDA` into `mScriptData`, `SCTX` into `mScriptText` (`loadscpt.cpp`, `load`/`save`). |
| Scanner and parsers | `components/compiler/scanner.cpp`, `fileparser`, `scriptparser`, `lineparser`, `exprparser`, `controlparser`, `declarationparser`, `quickfileparser` | Text to tokens to a recursive-descent parse. `FileParser` takes a whole script (`Begin ... End`), `ScriptParser` a bare statement list (used for dialogue result scripts), `QuickFileParser` skims for declarations. |
| Code generation | `components/compiler/generator.cpp`, `output.*`, `locals.*`, `literals.*` | Emits **OpenMW's own** instruction stream into `Interpreter::Program` (`mInstructions`, `mIntegers`, `mFloats`, `mStrings`: `components/interpreter/program.hpp`). Not Bethesda's bytecode. |
| Command table | `components/compiler/extensions.*` (`Compiler::Extensions`) | Keyword to function or instruction: return type character, **argument signature string**, opcode, explicit-reference opcode, segment. `components/compiler/extensions0.cpp` registers the core language set. |
| Interpreter | `components/interpreter/interpreter.*`, `runtime.*`, `*opcodes.hpp` | Stack machine with four opcode **segments** (0, 2, 3, 5 via `installSegment0/2/3/5`); `Runtime` holds the stack, literals and the program counter. `Interpreter::run` loops until the program counter leaves the program. |
| Context (the seam) | `components/interpreter/context.hpp` (`Interpreter::Context`), `components/compiler/context.hpp` (`Compiler::Context`) | The compiler asks `isId`, `getGlobalType`, `getMemberType`, `canDeclareLocals`; the interpreter asks locals, globals, member variables of other scripts, and a long list of **Morrowind-shaped** getters (`getNPCRace`, `getNPCFaction`, `getPCRank`, `getPCNextRank`, `getPCBounty`, `getActionBinding`...). |
| Game side | `apps/openmw/mwscript/` | `ScriptManager::compile` (builds from `script->mScriptText` through `Compiler::Scanner` and `FileParser`; `scriptmanagerimp.cpp` lines 41 to 78) and `run`; `InterpreterContext` (world access), `CompilerContext`, `Locals` (shorts, longs, floats per script instance; `write`/`read` against `ESM::Locals`, the save format), `GlobalScripts` (the running global and start scripts, saved), and about twenty `*extensions.cpp` files (for example `statsextensions.cpp` 1,625 lines, `transformationextensions.cpp` 879) each with an `installOpcodes` and a `registerExtensions`. |
| Dialogue results | `apps/openmw/mwdialogue/dialoguemanagerimp.cpp`, `DialogueManager::compile` (line 199) | Compiles an `INFO` result script (a **fragment with no `Begin`**) with `Compiler::ScriptParser` against the speaker's locals. This is the existing precedent for fragments. |
| Other games | `components/esm4/loadscpt.*`, `script.hpp` (`ESM4::ScriptDefinition`), `loadinfo.hpp`, `loadqust.hpp` | The TES4-family record loaders **read** script records and fragments; nothing in `apps/openmw` executes them (only `apps/esmtool/tes4.cpp` mentions them). |

What the Morrowind path does with compiled data: `ScriptManager::compile` ignores `mScriptData`; **OpenMW recompiles Morrowind's source text** every time. So no Bethesda bytecode interpreter exists in the tree.

### 1.2 Neutral and Morrowind-specific

Reusable almost as is (these carry no Morrowind rule): `Interpreter::Runtime`, `Program`, the segment dispatch and the control, math, generic and local-variable opcodes (`controlopcodes.hpp`, `mathopcodes.hpp`, `genericopcodes.hpp`, `localopcodes.hpp`), `Compiler::Scanner`, the expression and control parsers, `Generator`, `Compiler::Locals`, error handler classes, and the **idea** of `Extensions` (a table of keyword, signature, opcode).

Morrowind-specific and not reusable: the 20 `*extensions` files (every command), the Morrowind-shaped half of `Interpreter::Context`, `InterpreterContext` and `Locals` (they hold `MWWorld::Ptr`), the ids-are-strings assumption (`ESM::RefId`, `isId`), the global-variable model (name lookup in `getGlobalShort(string_view)`), and the save format (`ESM::Locals`, `ESM::GlobalScript`).

### 1.3 Gaps for a hostile or broken script

- `Interpreter::run` has **no instruction budget** and no stack depth cap, and it rethrows (`catch (...) { end(); throw; }`). Decision 0014 and the extension rules need no exception to cross the contract and a bounded step.
- `Compiler::Context` and `Interpreter::Context` are abstract classes in headers that include `ESM::RefId`: they carry an ESM type in their signature, which a contract header must not (contracts carry no implementation and no third-party types; the root guide).

## 2. ObScript and Papyrus, in one table (RECALLED-VERIFY unless marked)

| | ObScript (Oblivion, Fallout 3, New Vegas) | Papyrus (Skyrim, Fallout 4, Starfield) |
| --- | --- | --- |
| Attached to | Records: `SCPT` (object, quest and effect scripts) plus **fragments** inside `INFO`, `QUST`, `PACK`, `TERM`, `REFR`, `PERK` (MEASURED below) | Objects: a script is a class attached to forms and aliases through the plugin; quest, alias, topic-info and package fragments are generated scripts |
| Form | Procedural. `Begin <event> ... End` blocks, `Set`/`If`/`Return`, local variables of a few types, references and forms resolved by `SCRO`/`SCRV` tables | Object-oriented: inheritance, states, properties (auto and full), events, functions, arrays, structs (FO4) and guards (FO4), latent natives |
| Shipped form | `SCTX` source **and** `SCDA` bytecode (MEASURED: both in every one of 3,715 New Vegas `SCPT`) | Compiled `.pex` only for play; `.psc` source ships with the CK (MEASURED: `Scripts.zip`, 14,301 sources) |
| Extender | FOSE, NVSE (also OBSE): new commands, new opcodes at 0x1400 and above, `let`/expression language, arrays, user functions (RECALLED-VERIFY) | SKSE, F4SE: new natives and classes in DLLs; mods add libraries (PapyrusUtil, JContainers, ...) |
| Concurrency | Single script thread; every block runs to completion per frame | Many cooperative stacks; latent calls suspend a stack; script state, suspended stacks included, is saved |

## 3. Measurements (MEASURED, read-only)

### 3.1 What is installed

| Game | Where | Found |
| --- | --- | --- |
| Skyrim Special Edition | `I:\SteamLibrary\steamapps\common\Skyrim Special Edition`, MO2 instance `I:\Instances\Skyrim Special Edition` | yes; profile `Default` has 666 active mods |
| Fallout 4 | `I:\SteamLibrary\steamapps\common\Fallout 4`, MO2 instance `I:\Instances\Fallout 4` | yes; profile `Default` has 281 active mods |
| Fallout New Vegas | `I:\SteamLibrary\steamapps\common\Fallout New Vegas` | yes; `FalloutNV.esm` plus `DeadMoney`, `HonestHearts`, `OldWorldBlues`, `LonesomeRoad`, `GunRunnersArsenal` and four small packs. No MO2 instance, no NVSE, no mod plugin |
| Fallout 3, Oblivion | none | **absent; no numbers faked** |

### 3.2 Papyrus: format

- A strict parser (`pex.py`, with a check that each object's declared size equals the bytes consumed) parsed **100 percent** of the Bethesda scripts: Skyrim SE 15,092 (`Skyrim - Misc.bsa` 14,026 plus the Creation Club archives; the 62 native-class scripts come from loose `Data\Scripts`, installed with SKSE) and Fallout 4 2,399 (`Fallout4 - Misc.ba2` 7,875 files before de-duplication, plus the DLC archives).
- **Skyrim SE:** magic `0xFA57C0DE` read **big-endian**, version 3.2, game id 1. **Fallout 4:** the same magic stored **little-endian**, version 3.9, game id 2 (so the byte order must be detected, not assumed). 21 mod scripts in the Skyrim set are version 3.1 (older compiler).
- Layout notes found by the parse (RECALLED-VERIFY against the UESP page, but consistent with 22,000 files): the debug section differs (Fallout 4 adds property groups and struct orders); Fallout 4 objects carry struct definitions and a const flag; opcodes 0 to 35 occur in the Skyrim scripts, while 36 (`is`) and the struct operations (37 to 46 seen) occur in Fallout 4's; `callmethod`, `callparent` and `callstatic` carry a variable-length argument list after their fixed operands. **No guard table appeared in any Fallout 4 object of 3.9** (an earlier assumption that one exists was wrong and was removed after a failed parse).
- **Unparsed mod scripts:** 30 Skyrim and 51 Fallout 4 mod `.pex` files (0.1 and 1.6 percent) fail the object-size check (names include mods such as Acheron, Papyrus Extender and Bakaevents). **Cause not known**; do not assume it is corruption. A real loader must tolerate or report them. The failure is a finding to verify, not a diagnosed fact.

### 3.3 Papyrus: surface (Skyrim SE, then Fallout 4)

Scope: Bethesda base = the BSA or BA2 scripts; "SKSE layer" = the loose `Data\Scripts` files (62 Skyrim, 29 Fallout 4); mods = the winners of the `Default` profile, taking loose files first and then each mod's own archives (the first pass missed archived scripts and showed 14,500 calls it could not resolve; with archives it is 1,100). MO2's `modlist.txt` order (first line wins) is an assumption; the `overwrite` folder was not scanned.

| | Skyrim SE base | Skyrim SE mods | Fallout 4 base | Fallout 4 mods |
| --- | --- | --- | --- | --- |
| Scripts parsed | 15,092 | 12,855 | 2,399 | 604 |
| Objects, functions | 15,092, 59,233 | 12,855, 64,819 | 2,399, 6,864 | 604, 5,752 |
| Instructions | 327,385 | 559,607 | 93,915 | 129,437 |
| Properties (auto) | 47,726 (99.4 percent) | 45,523 (95 percent) | 9,478 (99.4 percent) | 7,383 (99 percent) |
| Non-default states | 1,881 | 2,190 | 645 | 44 |
| Event handlers (non-global functions named `On...`) | 6,161 | 7,420 | 3,230 | 824 |
| `callmethod` / `callstatic` / `callparent` | 127,990 / 11,267 / 88 | 145,308 / 27,405 / 137 | 33,192 / 3,319 / 28 | 25,035 / 9,770 / 2 |

- **Native declarations:** Skyrim SE vanilla **686** (from the CK's `Scripts.zip` `.psc` sources with a regular expression, cross-checked: 613 appear in the loose natives plus 73 in the BSA); Fallout 4 vanilla **891** (from the base BA2 `.pex` natives; the two methods differ). The SKSE layer declares **804** more in Skyrim (550 of all non-vanilla natives sit on vanilla classes such as `Form`, `Game`, `Actor`, `CombatStyle`) and mods declare 3,830 (the `.pex` of PapyrusUtil-type libraries). Fallout 4: 227 from the loose layer and 1,969 from mods.
- **Calls resolved to natives** (a type inference over locals, parameters, properties and the inheritance chain; 0.2 percent of Bethesda calls and 1.3 percent of Skyrim mod calls stay unresolved): Skyrim base **70,406** call sites to 493 distinct natives; Fallout 4 base 18,848 to 497.
- **Coverage (how many natives give how much):**

| Set | 50 percent | 80 percent | 90 percent | 95 percent | 99 percent | Distinct |
| --- | --- | --- | --- | --- | --- | --- |
| Skyrim SE Bethesda scripts | 12 | 54 | 106 | 166 | 296 | 493 |
| Skyrim SE mod scripts, vanilla natives only | 20 | 64 | 113 | 177 | 314 | 499 |
| Skyrim SE mod scripts, non-vanilla natives only | 54 | 259 | 543 | 849 | 1,353 | 1,506 |
| Fallout 4 Bethesda scripts | 26 | 88 | 156 | 225 | 371 | 497 |
| Fallout 4 mod scripts, vanilla only | 15 | 57 | 93 | 135 | 259 | 402 |

  The 114 Skyrim and 313 Fallout 4 vanilla natives that no installed script calls are not needed to run the installed set.
- **Top natives of Bethesda's Skyrim scripts** (call sites): `TopicInfo.GetOwningQuest` 6,762, `Game.GetPlayer` 6,053, `ReferenceAlias.GetReference` 4,563, `Quest.SetObjectiveDisplayed` 2,934, `Quest.SetObjectiveCompleted` 2,763, `ObjectReference.Disable` 2,191, `Utility.Wait` 2,182, `ObjectReference.Enable` 2,043, `ObjectReference.GetLinkedRef` 1,566, `ObjectReference.AddItem` 1,559, `Actor.EvaluatePackage` 1,348, `ObjectReference.MoveTo` 1,344, `Quest.Stop` 1,198, `Alias.GetOwningQuest` 1,175, `Scene.GetOwningQuest` 1,007, `ObjectReference.RemoveItem` 982, `Scene.Start` 936, `ReferenceAlias.ForceRefTo` 906, `ObjectReference.Activate` 897, `GlobalVariable.SetValue` 846, `ObjectReference.PlayAnimation` 708, `ObjectReference.PlaceAtMe` 706, `LeveledItem.AddForm` 570, `Actor.RemoveFromFaction` 563, `ObjectReference.DisableNoWait` 551. The full top 50 is in `I:\Projects\OpenEden-work\scriptvm\result_sky.json`. Fallout 4's top: `Game.GetPlayer` 1,309, `ObjectReference.GetLinkedRef` 1,035, `Utility.Wait` 705, `ObjectReference.PlayAnimation` 542, `ReferenceAlias.GetReference` 462, `ScriptObject.StartTimer` 437.
- **Extender (SKSE and plugin) natives called by the installed Skyrim mods:** Bethesda scripts call **0** non-vanilla natives. The mods (including the SKSE layer's own scripts) call **1,506 distinct non-vanilla natives at 15,392 call sites**: 17.9 percent of all 85,678 native call sites in mod scripts (the SKSE layer declares 295 of these, 7,028 sites; mod-shipped natives 1,211, 8,364 sites). By class, the largest are `Form` (SKSE additions such as `GetName`, `RegisterForModEvent`, `RegisterForKey`; 1,712), `StorageUtil` 1,444, `JsonUtil` 1,177, `ModEvent` 793, `StringUtil` 719, `Utility` 672, `OThreadBuilder` 506, `ObjectReference` 473, `NiOverride` 465, `Game` 426, `UI` 405, `MiscUtil` 394, `Math` 361, `PapyrusUtil` 322, then SexLab, MCM helper, PO3's Papyrus Extender and a long tail (117 classes in all). Fallout 4: 198 distinct non-vanilla natives at 1,472 sites (6.2 percent of mod native sites), led by `Form` 413 (F4SE additions) and a settlement-transfer mod's own natives.
- **Latent calls (a lower bound):** counting only `Utility.Wait`, `WaitMenuMode`, `WaitGameTime`, `ObjectReference.PlayAnimationAndWait`, `Actor.PathToReference` and `PlayGamebryoAnimation`, **1,004 of 15,092** Skyrim base scripts (6.7 percent) and 938 of 12,855 mod scripts (7.3 percent) use a latent native; Fallout 4 base 287 of 2,399 (12 percent). Which natives are latent is a property of the engine, not of the `.pex`, so the list is RECALLED-VERIFY and the real figure is higher.
- Instruction mix (Skyrim base): `callmethod` is 39 percent of all instructions; `cast` 15 percent, `assign` 14 percent, `jmpf` 7 percent, `jmp` 6 percent, `return` 5 percent. A plain interpreter with a fast `callmethod` path is enough; nothing here asks for a JIT.

### 3.4 ObScript: the New Vegas masters

Scanned with `esmscan.py` (TES4-family records of 24-byte headers, zlib records, subrecords `SCHR`, `SCDA`, `SCTX`, `SCRO`, `SCVR`):

| Item | Count |
| --- | --- |
| `SCPT` records, all ten masters | **3,715** (`FalloutNV.esm` 2,576; Dead Money 347; Old World Blues 278; Lonesome Road 242; Honest Hearts 239; Gun Runners' Arsenal 28; the small packs 5) |
| with `SCDA` (compiled) and with `SCTX` (source) | **3,715 and 3,715** (every one has both) |
| `SCDA` size | median 142 bytes, p90 837, p99 3,451, max 10,325, total 1.36 MB |
| `SCTX` size | median 503 bytes, p90 3,001, p99 12,491, max 33,364, total 5.0 MB, 162,994 lines |
| Script type in `SCHR` | 2,959 object, 490 quest, 266 effect |
| Local variables declared | 13,927 |
| Fragments with `SCDA` in other records | `INFO` 8,651 (result and before scripts), `QUST` 1,058 (stage and objective results), `PACK` 558, `TERM` 384, `REFR` 141, `PERK` 7; `SCTX` appears as often or a little more often (8,921 in `INFO`) because a record may carry source for a script with no compiled part |

- **A statement walker decodes all of them.** Each statement is a 16-bit opcode and a 16-bit length followed by that many bytes, with one exception: opcode `0x1C` is a 4-byte prefix (opcode plus a 16-bit value) with no payload. With that rule the walk ended exactly on the end of the data in **3,715 of 3,715** scripts (a first walk without the exception ended exactly in only 42 percent, so the exception is real; what `0x1C` means is RECALLED-VERIFY: it appears to be the explicit-reference prefix of `Ref.Function`). 121,350 statements in all.
- **Control statements** are exactly ten opcodes: `0x10`, `0x11`, `0x15`, `0x16`, `0x17`, `0x18`, `0x19`, `0x1C`, `0x1D`, `0x1E`. `0x10` and `0x11` occur equally often (5,702 each), which fits Begin and End; `0x1D` occurs once per script (3,715), which fits the script name. The names of the others (set, if, else, elseif, endif, return) are RECALLED-VERIFY, not derived from the data.
- **Function statements:** 29,977 statements with opcode `0x1000` or above, **199 distinct opcodes**, in the range `0x1002` to `0x127B`, none at `0x1400` or above. That is consistent with a vanilla command space below the extender's (RECALLED-VERIFY: NVSE starts at `0x1400`). Calls inside expressions are not counted (expressions carry their own encoding), so the 199 is a **lower bound** of the vanilla commands used by shipped scripts. The most frequent function opcodes (`0x1021` 2,592, `0x1022` 2,489, `0x1059` 1,475, `0x105E` 1,322, `0x109E` 1,187, `0x1039` 1,063...) cannot be named without the function table, which is **not in the data** (see below). Not invented.
- **Extender functions in the installed mod set: not measurable.** No New Vegas mod, no NVSE and no xNVSE tables exist on this machine. The question "how many scripts call NVSE functions" is open until a mod set is installed.

### 3.5 What the data could not tell

- The **function table** (name, opcode, parameter types, return type) of FO3 and New Vegas lives in the engine and the GECK, not in the plugins. Without it the compiler cannot type-check calls or parse parameters, and the bytecode cannot be disassembled to names. Sources (RECALLED-VERIFY, not opened): the GECK wiki's function reference, the xNVSE and FOSE repositories' command tables (licences not read), CommandTable definitions in OBSE-family source. A parser that treats an unknown identifier as a function with generic parameters (section 5, stage B) removes the table from the first slice.
- Fallout 3 and Oblivion are not installed. Their records differ in places (Oblivion's `SCPT` and `SCRI` with 20-byte `SCHR`; FO3 shares New Vegas's layout), which is RECALLED-VERIFY.
- Latent set, the 30 and 51 unparsed `.pex`, and the dependence on MO2 priority order are stated above.

## 4. Design

### 4.1 One contract family, two runtimes, in the common pack

```text
common compatibility pack (extension; no Lua, no save, no world)
 |- openeden.script.host      shared pieces: Value, ErrorReport, Budget, SaveBlob, EventToken
 |- openeden.script.obscript compile(source, scope) -> Program | located errors
 |                           run(program, target, event) -> Completion | error
 |                           registerFunction(name, signature, opcode, handler)
 |- openeden.script.papyrus  load(.pex bytes) -> ScriptClass | errors
 |                           instantiate(class, form) / send(event) / call(function)
 |                           registerNative(class, name, signature, handler)
game pack (privileged tier, decision 0013)
 |- ObScript: function table, record and form access, globals, quest variables, events, SCRO resolution
 |- Papyrus: natives, quest/alias/aliases, event dispatch, form types, game time
 |- save chunk (decision 0013): asks each runtime to serialise, stores the bytes
```

Rules (decision 0014 points 2 and 3 apply to both):

- **The runtimes hold no authority.** They never touch the world, the Lua host or the save. A game pack supplies every world-touching function and writes the save chunk.
- **Contracts carry no third-party or ESM types.** The runtimes' `Context` is a new, narrow interface in the contract header (formid and ref values as 32-bit integers, strings as `std::string_view`), replacing the use of `ESM::RefId`.
- **Reuse where neutral (ObScript):** the common pack links the scanner, parsers, generator and interpreter core from `components/compiler` and `components/interpreter`. These are **inherited code**; changing them is allowed only for a reason a task spec states. The first change is a seam: a parser option set (for example case rules, keyword spellings, the `ref` variable type, `Scn` as well as `Begin`...) and an `Extensions` table that a game pack fills, with the Morrowind table moved unchanged. Morrowind must compile and run exactly as before (a recorded comparison, not an assumption).
- **Recompile from source is the primary path.** It uses the engine's existing encoding and needs only the grammar and the function table. A decoder for `SCDA` (the walker above is already close) is a **fallback** for a script whose source is missing, and an oracle: compiling a New Vegas `SCTX` and comparing statement structure against the shipped `SCDA` is a free check that the grammar is right (opcode numbers need the table, but the statement skeleton does not).
- **Mods and the GECK:** the GECK stores source and compiled data (RECALLED-VERIFY, consistent with every shipped script above). A mod plugin built by tools that strip source would force the fallback decoder; expect it to be rare and measure it when a mod set exists.

### 4.2 Saves

- Each runtime serialises **on request** into a versioned blob; the **game pack** stores it in its own save chunk (decision 0013), keyed `script.obscript` and `script.papyrus`. The blob has its own format version (decision 0014 open question 2: recommended yes), so a runtime fix does not invalidate saves.
- ObScript state: per script instance local variables (short, long, float, ref), the current block and the running global/quest scripts and their delays. Per-record script state is mostly variables, so the blob is small.
- Papyrus state: per-object variables and properties set at runtime, current state name, the stacks of running or suspended scripts (frames, locals, program counter, the awaited latent token) and pending timers or registered events. This is the harder format and must be designed with the interpreter, not after it: a VM that cannot capture a suspended stack cannot save mid-wait.
- Both blobs are bounded (a size cap per object and per save), parsed with the same hostile-input rules as the Eden archive reader (the loader is told what is allowed before it trusts a count).

### 4.3 Threading, determinism, time

- **Main thread only** at first (decision 0014 open question 3: recommended). Scripts run in the game's `GameSimulation` phase (game-packs.md section 3, the phase exists only for game packs).
- **No wall clock.** All timing comes from the host's step and game time: ObScript block delays, `GetSecondsPassed`, Papyrus `Utility.Wait` and timers. A latent Papyrus call parks a stack with a game-time wake-up token and resumes when the host's time passes it, in a fixed order (wake time, then creation order).
- **Fixed order** for events and stack resumption (creation order, not hash order); no iteration over unordered containers where order affects results (root guide, Determinism).

### 4.4 Errors and sandboxing

A broken or hostile script must not crash the host.

- **Budget per frame** in instructions (a counter in the interpreter, checked on loops and calls, not per instruction: a backward `jmp`, a call and a loop end), with a per-script slice so a runaway cannot starve the others; an exhausted script is suspended and resumed next frame (Papyrus) or ended with a located error (ObScript block).
- **Stack depth cap** (Papyrus call depth, ObScript recursion in expressions and `call`), a cap on array and string sizes and on total script memory.
- **Recoverable errors with located messages:** `<script>:<line>` for source (ObScript; the recompiled stream carries line numbers), `<class>.<function>@<instruction>` for `.pex`. An error ends that script instance (or that stack), logs once per distinct message, and never throws across the contract (extension doc 6.8).
- The runtimes **validate before they run**: the `.pex` loader checks every index against the table sizes and every jump target before the first instruction (the 81 unparsed mod scripts show real files will exercise this); the ObScript compiler never trusts `SCDA`.
- Unknown function: compile warning and a runtime no-op that returns zero, in a mode the game pack chooses (strict in tests, tolerant in play). This is the **extender policy** lever (section 6).

### 4.5 Lua later (decision 0014 descriptors)

Neither runtime needs Lua. Later, a game pack registers **descriptors** (namespace, function, argument and result types, thread) and the core builds the bindings:

- **ObScript:** a read-only `core.<game>.scripts` namespace to read a script's variable by name, send a named event, and call a user function; nothing that runs a script body from Lua.
- **Papyrus:** the same, for a property or a function call by name with the VM's value types (`int`, `float`, `bool`, `string`, form references as handles), and `RegisterForCustomEvent`-style events the other direction. Values cross as plain data; Lua never holds a VM pointer.

## 5. What to build first

### 5.1 Staged plan (each stage is a task spec toddhow approves; effort is one full-loop person-session of "a day or two" unless stated; honest ranges, not estimates from a schedule)

| Stage | Runtime | What | Needs | Rough size |
| --- | --- | --- | --- | --- |
| A | contract | `openeden.script.*` headers: `Value`, `ErrorReport`, `Budget`, `SaveBlob`, the two runtime interfaces; an empty implementation; SDK minor change or part of 0084's SDK 4.0 | task 0084's game-pack tier (or a test host that mimics it) | small |
| B | ObScript | **Task 0092:** the seam in the compiler (parser options, `Extensions` filled by the caller, a fragment mode), a generated-script test suite, and an ObScript front end that compiles `SCTX` text with generic parameters for unknown functions and runs a handful of statement kinds against a stub world | A; no game data | medium, the first real work |
| C | ObScript | A real function table in the game pack (hundreds of commands), parameter typing, `SCRO` reference resolution, quest/object/effect script kinds and their events; the gated check compiles all 3,715 New Vegas scripts plus fragments | a table source decision (open question 1); Fallout New Vegas data | large |
| D | ObScript | Save blob; global and quest scripts running in game time; the first game-pack integration (this is also the Fallout 3 and New Vegas game-pack work of decision 0013's list) | game pack skeleton (task 0086-style) | large |
| E | Papyrus | `.pex` loader (done in throwaway form; becomes the real parser with hostile-input checks) and a validator; no execution; a corpus test on generated `.pex` | A | medium |
| F | Papyrus | The interpreter: stacks, states, events, properties, inheritance, latent parking, a budget; natives are stubs registered by the test; `OnInit` of one script runs | E | large |
| G | Papyrus | Save blob and resume; the Skyrim game pack's natives for the top 106 (90 percent of Bethesda's call sites), then the rest; extender policy | F; Skyrim game pack | very large |
| H | both | Lua descriptors | decision 0014 point 4's mechanism | medium |

ObScript's order is B, C, D because it lets the game packs for the two games toddhow named (FO3, New Vegas) and the shape of `openeden.script.*` be proved on one runtime and one data set where **the source is present and the loader already exists** (`components/esm4`). Papyrus does not wait for it: stages E and F share nothing but the contract, so E can start as soon as A exists. The two runtimes are **designed together** now (section 4.1, the same value type, error model, budget and save envelope) and **built in sequence**.

### 5.2 The minimum slice (task 0092)

- **ObScript:** compile generated scripts (written for the test; no game text) that use `Begin GameMode`/`End`, `Set`, `If`/`ElseIf`/`Else`/`EndIf`, locals of each type, a user function call through the `Extensions` table, a fragment without `Begin`, and run them against a stub world; unknown functions in a tolerant mode; a hostile script (an infinite loop, a deep expression) hits the budget with a located error.
- **Gated real-data check:** with the New Vegas masters present, parse and compile every `SCTX` (3,715 scripts and the 11,000-odd fragments) in the tolerant mode and report the **compile rate and the failure categories** (unknown construct, unknown function, type error); then walk every `SCDA` and compare statement counts and block structure with the recompiled stream. Aggregate numbers only; nothing is copied; the check is skipped (not failed) when the data is absent.
- **Papyrus (not in 0092):** a strict `.pex` reader on generated files comes in stage E.

## 6. Extender policy (SKSE, F4SE, NVSE, FOSE)

Facts: 17.9 percent of the native call sites of the installed Skyrim mods are non-vanilla, but the spread is wide (1,506 distinct functions; 543 of them give 90 percent of the non-vanilla sites; 54 give half). Bethesda scripts use none. Fallout 4's share is 6.2 percent. New Vegas is unmeasured.

Options (recommendation first):

1. **Tiered built-ins.** The game pack implements the vanilla surface first (stage G); then the **small set the data says matters** (SKSE's own `Form`, `ModEvent`, `StringUtil`, `Utility` additions, which are part of the platform every mod expects), then the well-known libraries as **optional per-library extensions** (StorageUtil/PapyrusUtil, JsonUtil, MiscUtil, NiOverride stubs, MCM helper) each a separate small extension with its own tests, provided its licence permits (licences not read: verify per library; some are permissive, some are not). An unimplemented native logs once and returns the type's zero (tolerant mode).
2. **Do not support DLL plugins at all**; document the list of what does not work. Cheapest, leaves the 18 percent broken.
3. **Load the real DLLs.** Rejected: they hook the engine's memory layout and do not exist for this engine (and are proprietary or differently licensed).

For New Vegas the same policy applies to NVSE: its expression language (`let`, arrays, user functions) is a **grammar extension**, not only commands, so it sits behind a parser option and a separate stage after D. Do not accept it silently: the NVSE grammar is large and RECALLED-VERIFY.

## 7. Conflicts with earlier documents

- `docs/design/game-packs.md` section 5.6 ("Script host") says the VM stays in the core, `components/compiler` and the mwscript command set move into `game_morrowind`, and Papyrus is out of scope. Decision 0014 (draft) already moves Papyrus into the common pack. This note proposes the same for ObScript. Either: (a) amend 5.6 to put both runtimes in the common pack as contracts (the amendment drafted in decision 0014), or (b) keep the interpreter in the core and let the common pack only provide front ends. (a) is recommended: a core with no game also means no script language in it. Waiting for toddhow.
- The root guide says the core "names no feature and no game": the existing mwscript files in `apps/openmw` are exactly the rules to be moved later (game-packs.md stage e), not in this slice.
- The inherited rule applies: changes to `components/compiler` and `components/interpreter` need a reason a task spec states (task 0092 states it) and Morrowind unchanged when off.

## 8. Open questions for toddhow (recommendation first)

1. **Where does the ObScript function table come from?** (Researched 2026-11-06: see section 10; xNVSE publishes no licence in what was read, so its tables cannot be copied.) Recommend: derive from the engine's own data where possible (opcodes and argument counts can be read off `SCDA` statements; types and names cannot), and write the first hundred commands by hand from the GECK wiki and from OpenMW's equivalents, each with a test and a source cited; xNVSE/FOSE table files are a candidate source only after their licence is read.
2. **Both runtimes in the common pack, or the interpreter in the core (section 7)?** Recommend the common pack.
3. **Order:** ObScript first, Papyrus after (recommended), or the reverse? The data favours ObScript for the games named; Skyrim's mod ecosystem favours Papyrus for reach.
4. **Extender policy (section 6):** recommend option 1, starting with the platform set and no library until a game pack runs.
5. **NVSE grammar (`let`, arrays, user functions):** a later stage behind an option, or never? Recommend later and optional.
6. **Tolerant mode default for unknown functions in play:** recommend yes with one log line per function; strict in CI.
7. **Save envelope:** one chunk per runtime with its own version (recommended) or a single script chunk.
8. **Installing a New Vegas mod set** (and NVSE) so the extender question can be measured: a small curated set, or the mods toddhow actually plays?
9. **Fallout 3 and Oblivion:** neither is installed. Skip them in the first slice (recommended) and design only for the record layout that New Vegas shares with Fallout 3.
10. **The 81 unparsed `.pex` files:** should a loader refuse them or try harder? Recommend refuse with a clear message until the cause is known (a short follow-up, not a blocker).

## 9. Not verified

Everything in section 2 beyond the measured columns; the meaning of every opcode name; NVSE, FOSE, OBSE, SKSE and F4SE behaviour; the licences of any extender or library; the latent set; the Fallout 3 and Oblivion layouts; any run in the game (nothing was built or run); Linux (not applicable to these tools). The measurement tools live outside the repository and can be rerun; their results are in `I:\Projects\OpenEden-work\scriptvm\result_sky.json`, `result_fo4.json` and `result_fnv.json`.

## 10. Where an ObScript function table can legitimately come from (added 2026-11-06)

toddhow's answers of 2026-11-06 (recorded in decision 0014's amendment): both runtimes in the common pack, ObScript first, tiered extender policy. The remaining blocker is the **function table** (name, opcode, parameter types, return type, for Fallout 3, New Vegas and Oblivion). Pages opened with WebFetch and WebSearch this session are marked **READ** (URL and what was seen); anything not opened is **RECALLED-VERIFY**. Several sites refused the fetch tool (HTTP 403 or an SSL handshake failure), listed below. This is not legal advice; "licence verdict" means what the published terms appear to allow, to be confirmed by toddhow.

### 10.1 What was read

| Source | Result | Status |
| --- | --- | --- |
| xNVSE, `https://github.com/xNVSE/NVSE` (and the GitHub API listings `.../contents/` and `.../contents/nvse/nvse`) | The repository root has 9 entries (`.github`, `.gitignore`, `DEVELOPMENT.md`, `README.md`, `common`, `nvse`, `nvse_plugin_example`, `package.lnk`, `package_nvse.ps1`): **no LICENSE or COPYING file**. The `nvse/nvse` directory (about 170 entries, all read) has `CommandTable.cpp/.h`, 42 `Commands_*.cpp/.h` files, `GameAPI`, `ParamInfos.h`, `ScriptTokens`, `PluginAPI.h` and **no licence or readme file**. The GitHub licence endpoint for the repository returned 404 (GitHub's detection found none). `DEVELOPMENT.md` says nothing about licensing. | **READ** (2026-11-06). A LICENSE inside `common/` or `nvse/nvse/Compiler/` was not looked for. |
| Search snippets on licences | JohnnyGuitarNVSE (a plugin, not xNVSE) states LGPLv2.1-only; the FOSE Nexus permissions page (mod 26125) shows the text of LGPL 2.1 but nothing confirms it covers FOSE itself; `https://fose.silverlock.org` says only that modders "are free to release mods based upon it" and gives no licence for the source. | **READ** as snippets and one page; FOSE and xOBSE terms **unconfirmed**. |
| xOBSE | The guessed GitHub API path returned 404; nothing else opened. Oblivion's table was not read. | Not read. |
| GECK wikis | `geckwiki.com` and `geck.uesp.net` returned HTTP 403 to the fetch tool; `geck.bethsoft.com` failed the SSL handshake. A search returned only titles and one-line snippets: the uesp.net GECK wiki is "no longer maintained" and the community moved to geckwiki.com; per-function pages show description, syntax and "added by NVSE x.y". The **wiki's content licence was not read**, and no snippet showed an opcode column. | Snippets **READ**; page content and licence **not read**. |

### 10.2 Licence verdicts (as far as read)

- **xNVSE source: no licence is published** in what was read. Under default copyright that means all rights reserved: its `CommandTable.cpp`, `Commands_*.cpp` and `ParamInfos.h` **must not be copied, adapted or transliterated** into the repository. Reading them in order to write our own table is a clean-room contamination risk (the Delirium Physics rule, "ideas only", is the model). The root guide's GPL-3.0-or-later compatibility rule cannot be met by code with no stated licence. Asking the maintainers to add a licence is possible and is toddhow's call.
- **FOSE and xOBSE:** unconfirmed (LGPL 2.1 is suggested for FOSE by one Nexus page). LGPL 2.1 would be usable in a GPL-3.0-or-later program only through its GPL-conversion clause and with their notices kept; verify from the source distribution's own file before anything is used.
- **Wiki text:** the pages' prose is someone's expression; whatever licence the wiki states (not read) governs copying. **Facts** (that a command exists, its name, the order and kinds of its parameters) are not themselves protected, but a wholesale transcription of a list can copy its selection and arrangement. Safe route: facts re-entered by hand into our own format, each source cited, no pasted text.
- **Bethesda's own data (the GECK and game executables, resource strings):** the guardrails say no game binaries, no decompiled game code and no Bethesda files in the repository. That also forbids a table that is a transcription of a decompiled engine table. Reading them **on the user's machine at run time** is a different act (nothing is shipped), but knowing where the table sits and how it is laid out in an executable is knowledge obtained by reverse engineering, so **the layout knowledge must come from a source we may use**. That is a legal question for toddhow before any attempt.

### 10.3 Routes, in recommended order

1. **Derive the table from the user's own plugin data (clean-room, no outside source).** The masters hold both `SCTX` (source: the command names as written) and `SCDA` (bytecode: the opcode and length of each statement). Walking source statements and compiled statements together (the walker of section 3.4 reaches the exact end of all 3,715 scripts) pairs a **name** with an **opcode** and an observed **argument shape** for every command the shipped scripts use. Built on the user's machine at first run (or by a dev tool) and cached; the repository holds the tool, never its output. Limits: only commands the data uses (at least 199 vanilla opcodes, section 3.4; mods add more); parameter *types* beyond what the encoding shows (reference, integer, float, string index) are inferred, not known; commands that appear only inside expressions need the expression encoding understood. **Not tested yet**: it is the first experiment of the ObScript front-end work and is cheap (about a day). It is also a check on the grammar: if names and opcodes pair up consistently, the compiler is right.
2. **A hand-written table of the commands the engine actually needs**, entered from the GECK wiki pages and OpenMW's Morrowind equivalents, in our own format, each entry with a test and a cited page, under whatever the wiki's licence allows (to be read: the pages refused the fetch tool; toddhow can open them in a browser, or the next session can use the Chrome tools). Grows with demand; this is the hundred-command start of section 8, question 1.
3. **A community table under a clear compatible licence:** none was found. The candidates (xNVSE, FOSE, xOBSE) are unlicensed or unconfirmed; other community tables (xEdit's data, the GECK wiki) were not read. One more search for a permissively licensed command list is worth doing; any find needs its licence file read first.
4. **Ask the extender maintainers for a licence grant or permission** for the command table only. Slow and uncertain; do not depend on it.
5. **Reading the installed GECK or game executable at run time:** rejected for now (needs layout knowledge we are not cleared to use, fragile across builds, and sits against the guardrails). Revisit only if toddhow decides the legal position.

### 10.4 What is allowed, in one list

- Allowed: our own code and tests; a table generated on the user's machine from their data; a table of facts re-entered by hand with cited sources and no pasted text; command names in tests as short facts (not a list copied from a page).
- Not allowed: copying or adapting xNVSE, FOSE or xOBSE source or tables; pasting wiki pages or their lists; adding any Bethesda file, decompiled code or dump of an engine table to the repository.
- Needs toddhow's decision: reading the GECK or game executable at run time; contacting the extender maintainers; relying on the wiki once its licence is read.

### 10.5 Open for toddhow (new)

- **Function-table source.** Recommend routes 1 then 2: derive from the user's data (a one-day experiment first), supplement by hand from the wiki with citations, leave the extender tables alone. May the next session read the GECK wiki's licence page and function lists in a real browser? Can you accept that unnamed commands print as `fn_0x1xxx` until named (tolerant mode already makes them harmless)?
- **NVSE-added functions** (opcodes from `0x1400`, RECALLED-VERIFY) cannot be derived from the shipped masters at all (section 3.4: none appear); they need route 2 per command, driven by which mods you play.
