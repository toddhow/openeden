# Task: Eden Studio chooses its game pack

Design doc section: `docs/design/game-packs.md` section 8 ("The editor boundary": the editor "loads a game pack as a host would") and section 7 (failure behaviour); decision 0013 (game packs, one per session); decision 0016 (neutral world model, `components/worldmodel`); task 0084 (`game=` / `--game`, one game pack per session); task 0024 (Eden Studio shell and extension host); task 0086 (the Skyrim SE pack and its cell viewer); task 0107 (the session content list reaches the pack).

Status: **APPROVED by toddhow 2026-11-15 ("Yes, approve all", all eight recommendations), then REVISED the same day by his direction: "All the different applications should be opened from the OpenEden launcher. The launcher will be used to select the game which will then set the game pack used."** Section 8 below is the revision; where it differs from sections 2 and 7, section 8 wins. **The revision was approved by toddhow 2026-11-15 with all five recommendations of 8.5 ("1. yes 2. yes 3. yes 4. yes 5. yes").** Nothing is built. The survey below was read at `9b66d8e071` (`main`).

Goal: Eden Studio picks the game pack of its session the way the game does: `game=` in the config, `--game` on the command line, and a picker in the editor. The chosen pack's content and world come from the session's `data=` and `content=` lines, and no longer from `OPENEDEN_SKYRIMSE_PLUGINS`. With no choice made, or with `game=morrowind`, the editor behaves exactly as it does today.

Inherited code touched (at `9b66d8e071`):
- `apps/opencs/model/prefs/commandline.cpp`: `makeEditorOptions`, `parseCommandLine`
- `apps/opencs/editor.cpp`: `CS::Editor::Editor`, `readConfiguration`, `startExtensionHost`, `readConfig`
- `apps/opencs/model/extension/editorextensionhost.*`: `EditorExtensionHost::EditorExtensionHost`, `Config`, `start`, `summary`
- `apps/opencs/model/mesh/skyrimcellsource.*`: `configureFromEnvironment`, `configure`
- `apps/opencs/view/render/meshscene.cpp`: `MeshScene::loadSkyrimCell`
- `apps/opencs/view/doc/view.cpp`: `View::openSkyrimCell`
- `apps/opencs/view/doc/startup.cpp`: `StartupDialogue::createButtons`
- The preferences dialogue (`apps/opencs/view/prefs/`) and `CSMPrefs` state
- `apps/openmw/extensionhost.cpp`: the discovery and refusal block of `ExtensionHost::ExtensionHost` (lines 166 to 271) and `setSessionContent` (501 to 532). These move into shared code; their behaviour does not change.
- `apps/opencs/CMakeLists.txt`: the static link of `game_skyrimse_world` (line 110)

Inputs / outputs: new editor option `game` (config and command line); a game pack choice stored in the editor's own settings (see question 2); a game pack picker; the editor's extension host gains a game pack slot; the Skyrim cell viewer reads the host's neutral world store.

## 1. Survey: what exists today

### 1.1 How the game picks its pack

- **Option.** `game` is a string option, default empty (`apps/openmw/options.cpp:51`, `OpenMW::makeOptionsDescription`).
  - `parseArgs` copies it into `args.gamePack`, unless `--no-extensions` is given (`apps/openmw/main.cpp:150-153`).
  - The same option comes from `openmw.cfg` lines and from `--game`, because both go through the same options description.
- **Built-in pack.** The Morrowind pack is linked into the executable as a built-in game pack:
  - `args.builtInGamePacks.push_back({ game_morrowind::packManifest(), &game_morrowind::makePackExtension })` (`main.cpp:156`).
  - `runApplication` installs Morrowind's rules whatever `game=` says (`main.cpp:166-176`, `game_morrowind::installRules`). An empty `game=` therefore still plays Morrowind.
- **Host config.** `OMW::Engine::prepareEngine` builds `ExtensionHost::Config` (`apps/openmw/vsgengine.cpp:404-407`). The search directories are `<local>/extensions`, `<user data>/extensions`, `<local>/game_packs` and `<user data>/game_packs`. It also passes the enabled ids, `gamePackId`, the static packs and `[Game] neutral world`.
- **Loading rules.** In `ExtensionHost::ExtensionHost` (`apps/openmw/extensionhost.cpp:166-271`):
  - The `game=` pack counts as enabled without an `extension=` line (line 171).
  - Any other pack of `kind: "game"` is refused and logged as a load failure (lines 205-212).
  - If no pack of the chosen id is installed on disk, a static pack of that id stands in (lines 233-257).
  - If the chosen pack is not found at all, the failure says so: "the game pack chosen by game= is not installed" (lines 267-271).
- **One pack per session.** `LifecycleManager` enforces this a second time: a second game pack is refused with its reason (`components/extension/lifecycle_manager.cpp:60-66`). The lifecycle records the session's pack (`getGamePackId`, `hasGamePack`, `initializeGamePack`).
- **Content list.** `ExtensionHost::setSessionContent` reads the header of each `content=` file found through the data folders (`collectSessionContent`, `Content::readSessionTag`). It hands the list to `WorldSourceService::setSessionContent` (`extensionhost.cpp:501-532`). Only the files that some registered content loader accepts get through (`setContentFilter` with `content().findLoader`). It does nothing unless `[Game] neutral world` built the store.
- **Skyrim pack start.** `SkyrimGamePack::start` (`game_packs/skyrimse/src/skyrimse_game_pack.cpp:44-90`) registers its content loader and its world view.
  - If the host reads world sources (`consumerEnabled()`), it loads the session's content list on a worker thread and registers its `SkyrimCellSource`.
  - Otherwise it throws "Skyrim SE rules are not implemented", which is a session error.

### 1.2 How Eden Studio reads its configuration

- **Options.** `CSMPrefs::makeEditorOptions` (`apps/opencs/model/prefs/commandline.cpp:22-63`) declares these options: help, `data`, `data-local`, `content`, `encoding`, `extension`, `fallback-archive`, `fallback`, and the common options (`config`, `replace`, `user-data`, `resources`). **It has no `game` option.**
- **Command line.** `parseCommandLine` has no `allow_unregistered` (comment at line 78), so an unknown option is refused. **`--game skyrimse` makes the editor exit with code 2 today.**
- **Config files.** `CS::Editor::readConfiguration` (`apps/opencs/editor.cpp:235-256`) reads them through `Files::ConfigurationManager::readConfiguration`. That function parses each file with `Files::parse_config_file(stream, description, true)`, where `true` allows unregistered options (`components/files/configurationmanager.cpp:386`). **A `game=` line in `openmw.cfg` is silently ignored by the editor.**
- **Data folders and archives.** `readConfig` (`editor.cpp:276` onward) builds them for `CSMDoc::DocumentManager::setFileData`, and the documents are ESM3 (`CSMDoc::Document`).

### 1.3 How the editor's extension host starts

- `CS::Editor::Editor` calls `startExtensionHost()` before `readConfig()` (`editor.cpp:108`).
- `startExtensionHost` (`editor.cpp:259-274`) builds `CSMExtension::EditorExtensionHost::Config`:
  - **search directories: `<local>/extensions` and `<user data>/extensions` only. The `game_packs/` folders are not searched;**
  - the enabled ids from the `extension=` lines.
- It attaches the Studio Jolt world for read-only queries, starts the host and logs `Extensions: <summary>`.
- `EditorExtensionHost::EditorExtensionHost` (`apps/opencs/model/extension/editorextensionhost.cpp:51-140`) discovers manifests and loads the enabled ones. It lists every manifest it finds (installed, active or failed) for the Extension Manager (task 0024 stage D). **It has no game pack slot:**
  - no `gamePackId`;
  - no static packs;
  - no refusal of a second pack of `kind: "game"`;
  - no "chosen pack not installed" message.
- It never enables the world-source consumer and never hands out a session content list. So if a `skyrimse` pack were enabled through `extension=skyrimse`, its `start()` would throw "rules not implemented". The pack's own comment says "the editor and tools never start the pack" (line 42), and `game-packs.md` section 7 assumes the same.
- Editor modules come from the core list first, then from the manifests' `editor` entries (`editor.cpp:178-191`).

### 1.4 How the Skyrim cell viewer finds its data today

- **Opening a cell.** `File > Open Skyrim Cell...` (`apps/opencs/view/doc/view.cpp:130-133` and `View::openSkyrimCell`, lines 1180-1190) asks for an editor id. Its prompt names `OPENEDEN_SKYRIMSE_PLUGINS`. It opens a `Type_Mesh` subview with the id `skyrimcell:<editor id>`. Captures use `--open-mesh skyrimcell:...` the same way.
- **Loading.** `MeshScene::loadSkyrimCell` (`apps/opencs/view/render/meshscene.cpp:583-`) takes the process-wide `CSMMesh::SkyrimCellSource::instance()`. If it is not yet ready, it calls `configureFromEnvironment`.
- **Environment.** `SkyrimCellSource::configureFromEnvironment` (`apps/opencs/model/mesh/skyrimcellsource.cpp:64-94`) reads `OPENEDEN_SKYRIMSE_PLUGINS`, a text file with one plugin path per line in load order. It also reads `OPENEDEN_SKYRIMSE_DATA`, or else uses the first plugin's folder.
- **Store.** `configure` (lines 30-62) builds a `game_skyrimse::DataFolderFiles`, a `SkyrimWorldView`, a pack-side `game_skyrimse::SkyrimCellSource` and **its own `NeutralWorld::NeutralWorldStore`**, all inside the editor.
- **Static link.** The editor does this by linking the pack's code statically (`game_skyrimse_world`, `apps/opencs/CMakeLists.txt:110`), not through the extension host. That is why the viewer works with no pack started (task 0086 stage log: "the editor with `skyrimse` as the active game pack" was not verified).

### 1.5 What Morrowind means in the editor today

- The editor links `game_morrowind_script_table` (`apps/opencs/CMakeLists.txt:108`) and works on the ESM3 structs directly (`game-packs.md` section 8: "a Morrowind-only editor until stage e's descriptors reach it").
- No Morrowind pack is started in the editor. Its manifest is never discovered from disk, because it is built in: `game_morrowind::packManifest()`, `game_packs/morrowind/include/game_morrowind/pack.hpp:15`.
- The start dialogue offers "Edit A Content File", "Create A New Addon" and "Create A New Game" (`apps/opencs/view/doc/startup.cpp:44-50`). All three are ESM3 documents.

## 2. Proposal

### 2.1 How the choice is made (recommended)

Precedence, highest first:

1. **`--game <id>`** on the editor's command line. `game` is added to `makeEditorOptions` with the game's wording and default (empty).
2. **The editor's own stored choice:** the picker writes it as `[Eden Studio] game pack` in `eden-studio.cfg`. That file is the editor's settings file, not `openmw.cfg`. Question 2 asks where it lives.
3. **`game=` in the config files.** These are read already. Once `game` is a declared option, the line is no longer dropped.
4. **Nothing:** Morrowind, as today.

Notes on the choice:
- `--no-extensions` clears the choice, as in the game.
- The id names one pack. The picker lists:
  - every discovered manifest of `kind: "game"` from the four folders the game searches;
  - the built-in Morrowind manifest (`game_morrowind::packManifest()`), always present.
- **One pack per session:** the host loads it in its constructor, so a change made in the picker takes effect after a restart. The picker says so, and offers "Restart now" when no document has unsaved changes.

### 2.2 The picker

- **Start dialogue:** a "Game" combo box above the three buttons (`StartupDialogue::createButtons`). It shows the pack's `name` and version, the Morrowind entry first, and the current choice selected. Changing it stores the choice and asks to restart.
- **Preferences:** an "Eden Studio" page row "Game pack" with the same list, for when no start dialogue is shown.
- **Status:** the window title or the status bar names the session's pack: "Game: Skyrim Special Edition 0.1.0".
- **Missing pack:** a pack that is chosen but not installed is shown as "<id> (not installed)" and is not started.
- **Translations:** all new Qt strings are added to the translation files, regenerated with `lupdate` (`CI/check_qt_translations.sh`).

### 2.3 One set of host rules for both hosts

`ExtensionHost`'s discovery and game pack rules (`extensionhost.cpp:166-271`) move into one function or class under `components/extension`, which both hosts call. The rules are:
- search order;
- the game pack enabled without `extension=`;
- other game packs refused;
- a static pack standing in;
- the "not installed" failure.

The session content collection (`setSessionContent`, lines 501-532) moves with them. The game's behaviour must stay byte-identical: same log lines, same failures, same order. Its existing tests, plus a new test of the shared function, show it. `EditorExtensionHost::Config` gains `mGamePackId`, `mStaticPacks` and the four search folders. `EditorExtensionHost::summary()` and the Extension Manager show the game pack row.

### 2.4 What happens when the choice is Morrowind (or nothing)

- No new pack is loaded or started. The Morrowind pack stays the built-in manifest, used only to list the entry in the picker.
- The start dialogue, documents, modules, `readConfig` and the Skyrim viewer behave as today. The only change is the picker widget in the start dialogue.
- **"Exactly as today" is checked by comparison:**
  - the `Extensions: ...` log line is identical to the build before, with an empty `game=` and with `game=morrowind`;
  - `tools/edenstudio_check/check.sh`, `tools/modulecheck/check.sh` and `tools/bodypanel_check/check.sh` pass;
  - `eden-studio-tests` pass unedited;
  - a capture of a Morrowind cell (`--open-cell ... --auto-screenshot`) is byte-identical to the build before (quiet window).
- Question 5 asks whether `game=morrowind` should actually start the built-in pack in the editor.

### 2.5 What changes when the choice is not Morrowind (Skyrim SE now, any `kind: "game"` pack later)

1. **The host starts the pack.**
   - The editor host enables the world-source consumer, builds a `NeutralWorldStore` and hands the session content list to the pack, using the shared code of 2.3.
   - The content list is the session's `content=` files found in its `data=` folders, filtered by the pack's content loaders.
   - The pack's `start()` then takes the "world source registered" path and does not throw.
   - This changes a stated assumption: the editor now starts a game pack. `game-packs.md` sections 7 and 8 and the comment in `SkyrimGamePack::start` get updated in the same change.
2. **The cell viewer reads the host's store** (recommended, option a).
   - `MeshScene::loadSkyrimCell` and `View::openSkyrimCell` take the store and game id from the editor host, not from `CSMMesh::SkyrimCellSource::instance()`.
   - The static link of `game_skyrimse_world` and the editor's own `SkyrimWorldView`/`NeutralWorldStore` are removed. Otherwise the pack's code would be in the process twice (once static, once in `skyrimse.dll`), with two world views: a risk of duplicate symbols and of two copies of the same data.
   - The menu item is renamed "Open Cell of <pack name>...". It is enabled only when the session's pack has registered a world source.
   - Option b, rejected: keep the static path and feed it the content list instead of the environment variable. Smaller, but it keeps two copies and does not exercise the pack as the game does.
3. **`OPENEDEN_SKYRIMSE_PLUGINS` stops being the source of the load order.** Question 4 asks whether to remove it or keep it as a developer fallback.
4. **The documents stay Morrowind-only.** `CSMDoc` cannot open a Skyrim plugin (ESM3 structs; record descriptors do not exist yet, `game-packs.md` section 8). So, with a non-Morrowind pack:
   - the three start buttons are disabled, with a tooltip: "This game pack has no record editor yet".
   - The start dialogue offers "Browse the world" (opens the cell picker) and the Asset Browser instead.
   - The mesh viewer, the cell viewer, the LOD tile viewer and editor modules keep working.
   - Question 6 asks about this.
5. **Failure.** A chosen pack that is not installed, or whose `start()` fails, gives:
   - one message in the Output Log and a message box at start;
   - the Extension Manager row in Failed state with its reason;
   - the editor starts anyway, with Morrowind behaviour (`game-packs.md` section 7).
6. **Profiles.** A game id alone is not enough: the user's one `openmw.cfg` holds Morrowind's `data=` and `content=` lines, so `game=skyrimse` against it gives a Morrowind content list. The pack's loaders filter it down to nothing, and the viewer is empty with a message. The recommended way to run Skyrim is a separate config folder (`--config`, or the launcher profiles of task 0057) that holds `game=skyrimse` and Skyrim's `data=`/`content=`. Question 1 asks about this.

### 2.6 Loop

This changes the extension loader and lifecycle hosting (the shared rules of 2.3) and starts a game pack that reads untrusted input in a new host. Under CLAUDE.md it is **full loop**: a separate test writer works from this spec, then the implementation, then a fresh reviewer.

## 3. Constraints

- **Off means unchanged:** with an empty `game=` or `game=morrowind`, nothing new loads or starts. The comparison is in 2.4.
- **Game unchanged:** the shared host code must give the game the same log lines, failures and order. Proof: the existing `ExtensionHost` tests, the three extension game checks (`tools/extension_*_check`), and the `worldobjects_check` dump with the fixed Balmora frame against the build before.
- **Threading:** the pack's index worker runs as in the game. The viewer waits for the store's queries on the GUI thread. The wait is already bounded at 30 s by `SkyrimWorldView`, but a long block can still trigger the 5 s freeze monitor; stage d measures how long the first query takes.
- **Determinism:** the picker lists packs sorted by id, with Morrowind first.
- **No new setting in the game.** The editor setting is on `tools/testprofile/exclude.txt` with a reason (an editor preference, not a game feature), or in the profile if toddhow prefers (question 7).
- **Test runs:** `OPENEDEN_QUIET_WINDOW=1` for every editor run; large outputs under `I:\Projects\OpenEden-work`.

## 4. Stages

- **a.** The `game` option in `makeEditorOptions`. Tests: `--game x` is accepted, and a `game=` config line reaches `mConfigVariables`. Today the first exits with code 2 and the second is silently dropped.
- **b.** The shared host rules (2.3), moved out of `ExtensionHost`. The game must be proven unchanged at this stage.
- **c.** `EditorExtensionHost` with a game pack slot, the four search folders, the session content list and the neutral store. The Extension Manager shows the game pack row.
- **d.** The cell viewer on the host's store; the static link removed; the environment variable handled as question 4 decides.
- **e.** The picker: start dialogue, preferences, status line, restart; translations.
- **f.** The start dialogue for non-Morrowind packs (2.5 item 4); the documents in `game-packs.md`, the pack comment and the area `CLAUDE.md`.

## 5. Acceptance criteria

- [ ] `eden-studio --game skyrimse` and a `game=skyrimse` config line both choose the Skyrim SE pack. `--no-extensions` chooses none. An unknown id gives one message, and the editor starts.
- [ ] The picker lists the built-in Morrowind entry and every installed `kind: "game"` pack. Changing it stores the choice and takes effect after a restart. The command line overrides the stored choice.
- [ ] With a Skyrim SE config folder (`game=skyrimse`, Skyrim's `data=` and `content=`), the editor starts the pack, the pack registers its world source, and "Open Cell..." shows the Bannered Mare (`WhiterunBanneredMare`) and `@WhiterunWorld,...` exterior squares. The frames are compared with the old environment-variable path, and the counts of cells, objects and meshes are equal. `OPENEDEN_SKYRIMSE_PLUGINS` is not set.
- [ ] Only one copy of the Skyrim pack's code is in the editor process. `game_skyrimse_world` is no longer linked into `eden-studio`, or question 3 decides otherwise.
- [ ] With an empty `game=` and with `game=morrowind`, everything in 2.4 matches the build before: the log line, the three check tools, `eden-studio-tests` unedited, and a byte-identical Morrowind cell capture.
- [ ] The game is unchanged by the shared host code (section 3, "Game unchanged").
- [ ] A missing or failing pack: one message, the Extension Manager row is Failed with its reason, and the editor runs.
- [ ] `CI/check_qt_translations.sh`, `CI/check_layer_rules.sh`, `CI/check_tests_registered.sh` and `tools/testprofile/coverage.sh` pass; the Linux gate passes before a push.

## 6. Out of scope

- Editing Skyrim records, record descriptors, or a Skyrim document type.
- Switching packs without a restart.
- Launcher changes (task 0057 owns profiles).
- Writing `game=` into `openmw.cfg` from the editor.
- Packs other than Morrowind and Skyrim SE beyond being listed and started through the same path.

## 7. Open questions for toddhow (recommendation first)

1. **Profiles.** The game id alone gives the wrong content list against a Morrowind `openmw.cfg`. Recommend: Skyrim runs from a separate config folder (`--config`), and the picker says "needs a config folder with this game's data and content" when the session's content list gives the pack nothing.
   - Alternative: the picker also chooses a config folder.
   - Alternative: wait for task 0057's profiles.
2. **Where the stored choice lives.** Recommend: `eden-studio.cfg`, `[Eden Studio] game pack`, used only by the editor, below `--game` and above `game=`.
   - Alternative: the editor writes `game=` into the user `openmw.cfg`. This also changes what the game plays, so it is not recommended.
   - Alternative: no stored choice, only `game=`/`--game`.
3. **Static link or the loaded pack.** Recommend option a: the viewer uses the pack the host loads, and the static `game_skyrimse_world` link goes.
   - Alternative, option b: keep the static path, fed by the content list. Two copies of the code.
4. **`OPENEDEN_SKYRIMSE_PLUGINS`.** Recommend: remove it once stage d works, and change the check scripts (`tools/skyrimview_check`, `tools/skyrimcell_check`) to a config folder.
   - Alternative: keep it as a developer fallback, used only when no pack registered a source.
5. **`game=morrowind` in the editor.** Recommend: list it, start nothing (as today). The Morrowind rules are not used by the editor.
   - Alternative: start the built-in pack through the lifecycle, as the game does with `game=morrowind`. This would change the `Extensions:` line, so "unchanged" would have to be redefined.
6. **The start dialogue for a non-Morrowind pack.** Recommend: disable the three ESM3 buttons with a reason, and add "Browse the world".
   - Alternative: hide them.
   - Alternative: leave them working on ESM3 content regardless of the pack (confusing).
7. **Test profile.** Recommend: the editor setting goes on `tools/testprofile/exclude.txt` (an editor preference).
   - Alternative: give the profile's `start_editor` scripts a `game=` of their own.
8. **Does the editor need `[Game] neutral world`?** Recommend: no. The editor builds the store whenever a non-Morrowind pack is chosen, because that is the editor's only use of the pack. The game keeps its setting.

## 8. Revision 2026-11-15: the launcher selects the game

toddhow, 2026-11-15: "All the different applications should be opened from the OpenEden launcher. The launcher will be used to select the game which will then set the game pack used."

### 8.1 Survey of the launcher (at `9b66d8e071`)

- **What it starts.** `Launcher::MainDialog::play` (`apps/launcher/maindialog.cpp:668-690`) writes the settings (`writeSettings`), checks that a game file is selected, and starts `openmw` detached (`mGameInvoker->startProcess`). The wizard is started at lines 200 and 518. **Nothing in the launcher starts Eden Studio or another tool.**
- **The game.** The launcher has no `game` key anywhere: `apps/launcher`, `components/config/gamesettings.cpp` and `components/config/launchersettings.cpp` never read or write `game=`.
- **Profiles.** Task 0057's Mods page has profiles (`components/modorganizer/profilestore.hpp`: `mods.json`, `profiles/<name>.json`, `active-profile.txt`, `base-data.txt` under `<user config>/openeden/`). Applying a profile rewrites the keys it owns in `openmw.cfg` (`applyActiveProfile`, with backup). A profile records nothing about which game it is for.
- **Packs.** The Extensions page already discovers manifests and shows their `kind` (`apps/launcher/extensionspage.cpp:89`). The discovery code can therefore list `kind: "game"` packs.

### 8.2 What changes from sections 2 and 7

1. **The choice is made in the launcher, not in Eden Studio.**
   - A **Game** selector sits at the top of the launcher's main window.
   - It lists the built-in Morrowind entry (always first) and every installed pack of `kind: "game"`, found in the four folders the game searches.
2. **The game selection writes `game=` into `openmw.cfg`.**
   - Selecting a game writes the `game=` line together with that game's `data=`/`content=` lines, in the same atomic write and with a backup, as the Mods page does.
   - This reverses question 2 of section 7: the launcher is now meant to own the choice. A direct start of `openmw` or `eden-studio` then also uses the last game selected.
   - Morrowind is written as **no** `game=` line, so a Morrowind config stays byte-identical to today's (question 8.5.3).
3. **Each game has its own profile set.** A profile gains a `game` field. The selector shows only that game's profiles, and switching game applies the game's last active profile.
   - This replaces question 1's "separate `--config` folder".
   - Existing profiles without the field are Morrowind's.
4. **The launcher opens every application.**
   - Buttons: **Play** (`openmw`), **Eden Studio** (`eden-studio`), and the existing wizard entry, Morrowind only.
   - Each is started detached with the same config folder (`--config`/`--user-data` forwarded when the launcher was given them). `writeSettings` runs first, as `play` does.
   - Other tools are question 8.5.4.
5. **Eden Studio has no game picker.**
   - Section 2.2's start dialogue combo box and preferences row are dropped.
   - The editor reads the game from `game=` (and `--game`), as in stages a to d, and names it in the window title.
   - Changing the game means going back to the launcher.
6. **The rest of the spec stands unchanged:**
   - 2.3: shared host rules;
   - 2.4: Morrowind unchanged;
   - 2.5 items 1 to 5: the editor starts the pack, the cell viewer reads the host store, the environment variable is removed, the documents stay Morrowind-only, failure handling;
   - 2.6: full loop;
   - section 3: constraints;
   - questions 3, 4, 5, 6 and 8 of section 7 as approved. Question 1 is replaced by item 3 above, question 2 is reversed by item 2, and question 7 is moot: the editor no longer has a setting of its own to put on the test profile list (its `start_editor` scripts use the profile's `openmw.cfg`, which carries `game=`).

### 8.3 Stages, revised

- **a to d:** unchanged (the editor's `game` option, shared host rules, the editor's game pack slot, the cell viewer on the host store).
- **e (launcher):**
  - the Game selector;
  - the `game` field on profiles, with the first-run rule (existing profiles become Morrowind's);
  - writing `game=` through the owned-keys writer;
  - the Eden Studio button and the forwarding of `--config`/`--user-data`;
  - translations regenerated with `lupdate`.
- **f:** the editor's window title names the game; the start dialogue for non-Morrowind packs (2.5 item 4); the docs (`game-packs.md`, the Skyrim pack's comment, `apps/launcher/CLAUDE.md`, `apps/opencs/CLAUDE.md`).

### 8.4 Acceptance criteria, revised

These replace the second criterion of section 5.

- [ ] The launcher's Game selector lists Morrowind and every installed `kind: "game"` pack.
  - Selecting Skyrim SE writes `game=skyrimse` and that game's profile into `openmw.cfg`, with a backup.
  - Selecting Morrowind removes the `game=` line, and the Morrowind config is byte-identical to the one before.
- [ ] Play and Eden Studio started from the launcher both run with the selected game. Eden Studio's log names the pack, and its `Extensions:` line for Morrowind is unchanged.
- [ ] A profile made for one game is not offered for another. Profiles from before the change appear as Morrowind's.

### 8.5 Open questions for toddhow (recommendation first)

1. **A game's first profile.** Recommendation: when a game is selected for the first time and has no profile, the launcher asks for that game's Data folder and builds a "Default" profile from the plugins found there. Content files are probed by the pack's own probe: task 0086's Skyrim SE probe reads plugin headers.
   - Alternative: a game is not selectable until a profile is made for it on the Mods page.
2. **Is `game=` written into `openmw.cfg`, or only passed on the command line?**
   - Recommendation: written, so that a direct start of either program agrees with the launcher.
   - Alternative: the launcher passes `--game` and leaves the file alone. Then a direct start always plays Morrowind.
3. **Morrowind's line.** Recommendation: no `game=` line, so today's configs and every check stay byte-identical.
   - Alternative: an explicit `game=morrowind` line. Morrowind behaves the same, but the file changes.
4. **Which applications get a launcher entry.** Recommendation: Play, Eden Studio, and the wizard (Morrowind only).
   - Alternative: also the command-line tools (`navmeshtool`, `bsatool`, `esmtool`, `edenpack`).
5. **Direct starts.** Recommendation: `openmw` and `eden-studio` still start on their own and use `game=`, unchanged.
   - Alternative: they show a message pointing to the launcher when started without it. Not recommended: it breaks scripts, checks and captures.

## 9. Stage log (2026-11-15, stages a to f built; full loop)

Built on branch `gamepackchoice` in the worktree `I:\Projects\OpenEden-work\gpchoice\src`, from `main` at `9b66d8e071`. Windows (MSVC) only.

**Loop.** Interfaces were written first as stub headers and stub bodies:
- `components/gamesession/gamesession.*` and `components/modorganizer/games.*`;
- the new fields of `Profile`, `Layout`, `Store` and `StorePaths`;
- the game pack slot of `EditorExtensionHost`, `CSMExtension::GameSessionSetup` and `CSMMesh::ActiveWorld`.

A separate test-writing agent wrote the tests from this spec and the headers alone. The tests were run red against the stubs: **components-tests 51 of 59 failed, eden-studio-tests 28 of 40 failed**. The 20 that passed check absence or "unchanged": no game pack, a Morrowind-only store, an empty list, an option still refused.

Then the implementation. One test failed on the first green run (`aChosenPackThatIsNotInstalledFailsWithAReasonAndTheHostGoesOn`: the failure text did not name the pack); the code was changed to the game's `<id>: <reason>` form. No test was edited. A fresh reviewing agent read the diff afterwards; its findings and the fixes are in 9.1.

**What was built.**

Stage a, the editor's option:
- `game` in `CSMPrefs::makeEditorOptions`. Before, a `game=` line in openmw.cfg was dropped and `--game` was refused with exit 2.
- The unattended capture (`capture.cpp`) reads `game` and `extension` too.

Stage b, shared rules: `components/gamesession` (layer host) has `searchDirectories`, `isEnabled`, `classify`, `refusedGamePackReason`, `collectContent` and `StoreListener`. The game's `ExtensionHost` and `Engine::prepareEngine` use them. The game's log lines, failures and order are unchanged: `ExtensionHost` keeps its own log statements, and only the verdicts come from `classify`.

Stage c, the editor host:
- `EditorExtensionHost::Config` gains `mGamePackId` and `mBuiltInGamePacks` (`morrowind`).
- The game pack has its own row, listed first (`ExtensionInfo::mGamePack`).
- Other game packs are refused, and a pack that is not installed fails, with the game's texts.
- A pack that is not built in, once loaded, gets a `NeutralWorldStore` and listener.
- `setSessionContent` passes the content list through the content-loader filter.
- `GameSessionSetup` builds the pack's VFS and content list.
- `CS::Editor`:
  - reads the configuration before it starts the host, so the pack gets the data folders;
  - searches the `game_packs` folders only when a pack other than Morrowind is chosen;
  - shows one message box when the pack is not available;
  - logs `Game pack: <name> (<id>), world source <id>`.

Stage d, the cell viewer:
- `MeshScene::loadSkyrimCell` reads `CSMMesh::activeWorld()`.
- The menu item is "Open Cell of <pack name>...", disabled when there is no world.
- `csmodel` no longer lists `skyrimcellsource` and no longer links `game_skyrimse_world`.
- `skyrimcellsource.cpp` and `game_skyrimse_world` are built into `eden-studio-tests` only. The two existing tests that use them stay unedited: `testcellsceneneutral.cpp`, and the gated `testcellsceneneutralreal.cpp`, which still reads `OPENEDEN_SKYRIMSE_PLUGINS`.
- A capture with a non-Morrowind game opens a new, never-saved, empty game document for the mesh view.

Stage e, the launcher:
- A Game box beside the version label, and "Launch Eden Studio" beside "Launch OpenMW".
- Profiles per game:
  - `Profile::mGame` is written only when not empty;
  - `game` is an owned config key, and `renderConfigLines` writes `game=<id>` first;
  - `base-data-<game>.txt` and `last-profiles.txt` are written only for games other than Morrowind;
  - the Mods page lists only the active game's profiles.
- A game's first profile is built from a Data folder on a worker thread.
- `forwardedConfigArguments`: `apps/launcher/main.cpp` now parses the launcher's own command line (`--config` and the like) before reading the configuration, and hands those options on to the programs it starts.
- `ModsPage::loadSettings` is now called in `MainDialog::setup`. Before, the Mods page was loaded only by `reloadSettings`, after the wizard.

Stage f, the start dialogue and documents:
- When the game is not Morrowind, the start dialogue disables its three content-file buttons with a reason and shows "Browse the world". This is never called for Morrowind.
- Docs updated: `apps/opencs/CLAUDE.md`, `apps/launcher/CLAUDE.md`, `docs/design/game-packs.md`, and the Skyrim pack's comment.
- `tools/skyrimcell_check/check.sh` writes a `game=skyrimse` config folder. `LEGACY=1` keeps the old config for an executable from before this task.

**Deviation.** Section 8.5 question 1 says the first profile is probed by the pack's own content loader. Instead, the probe is the plugin header's game guess (`Content::Header::mGame` through `expectedHeaderGame`), because using the pack's loader would mean loading and starting the pack in the launcher. Packs other than skyrimse, fallout4 and falloutnv get no first profile until a table entry is added.

**Run (Windows, build `I:\Projects\OpenEden-work\gpchoice\build`).**
- **New tests:** components-tests 59 of 59 and eden-studio-tests 40 of 40 pass.
- **Whole test programs:**
  - components-tests: 10,430 pass, 75 skipped. It was run with `-*Slang*:*Complement*:*Property*`, as other tasks run it.
  - openmw-tests: 2,578 pass, 2 skipped.
  - eden-studio-tests: 1,289 pass, 18 skipped.
- **Morrowind unchanged:**
  - `tools/edenstudio_check` passes with the new build, and with a copy of the build from before the change (`eden-studio-base.exe`).
  - Both logs have the same 41 lines, equal except for the work folder paths. Both show `Extensions: 1 of 1 extensions active`.
  - With `game=morrowind` added to the config, the log equals the old build's except for timings.
  - `tools/bodypanel_check` passes, after building `edenpack` in this build folder.
- **Skyrim SE:**
  - `tools/skyrimcell_check` was run twice: with the new build (a config folder with `game=skyrimse` and the five masters as `content=`), and with the old build (`LEGACY=1`, the environment variable).
  - Both runs give the same counts:

    | Cell | Objects | Meshes | Boxes | Triangles |
    | --- | --- | --- | --- | --- |
    | `WhiterunBanneredMare` | 697 | 655 | 15 | 284,695 |
    | `@WhiterunWorld,6,-1,1` | 1,984 | 1,661 | 216 | 1,006,276 |

  - **Both frames are byte-identical between the two paths.** The Bannered Mare frame was viewed.
  - The editor started without a capture logs `Game pack: Skyrim Special Edition (skyrimse), world source skyrimse`, and the pack builds its world view from 5 of 5 plugins.
  - The check config lists only the mesh and texture archives, so the pack reports its 5 string tables missing. The launcher's first profile adds every archive named after a plugin.
  - A config naming an uninstalled pack gives one error line, and the capture exits 1.
- **The game:**
  - `tools/extension_example_check` and `tools/extension_data_check` pass.
  - `tools/extension_world_check` FAILS. It fails the same way with the `integ2` build of `main` from before this change (`a9ecee32fe`): each run is ended by the freeze monitor while the new game starts (`openmw-freeze.dmp`). So it is not caused here.
- **Translations:** `lupdate -locations none apps/launcher` adds 12 strings to the six launcher files; a second run adds none. The wizard and components files are unchanged.
- **CI scripts and formatting:**
  - `CI/check_layer_rules.sh`: 104 exceptions. A `dir components/gamesession host` line was added; no new exception.
  - `check_tests_registered` and `tools/testprofile/coverage.sh` pass.
  - `check_file_names` lists only `tools/skyrimcell_check/rotation_ground.cpp`, which is already on `main`.
  - clang-format 14 (`clang-format-diff-14` in the `openeden-cf14` image) was run on the changed lines.

**Not run.**
- The Linux gate (GCC `-Werror`, the 32 Lua tests), clang-tidy, sanitizers.
- The launcher itself. It reads and writes the real user config folder, so the Game box, the first-profile dialog, switching games and "Launch Eden Studio" were not exercised by hand. Their library parts are unit tested.
- The editor's GUI paths with a person: the start dialogue's disabled buttons and "Browse the world", the message box for a missing pack, the renamed menu item. They are built, but not seen in a frame.
- A Skyrim mod list.
- `tools/modulecheck`.

### 9.1 Fresh review and fixes

A fresh reviewing agent read the diff. It confirmed that the game is unchanged:
- the same search folders and order;
- the same enable, duplicate and refusal rules, in the same order;
- the same log texts;
- the launcher's new argument parsing changes nothing when the launcher is given no arguments.

It also confirmed the lifetimes: the host's store and listener outlive the lifecycle, the VFS outlives the host in both the editor and the capture, and the active world is cleared before the store goes. A game id from a file cannot write outside the store folder.

**Fixed.** Tests were written first in `apps/components_tests/modorganizer/testgamesreview.cpp` (8 tests, by the implementing agent, not a separate writer) and run red: 7 of 8 failed. The one that passed checks a mismatch, which the stub's `false` also gave.

1. **A false "edited by hand" question on every switch back to a game.** The check compared the file with the new profile's own last apply. `ModOrganizer::configMatchesLastApply` now says when the file still holds the previous profile's last apply, and the game switch applies without asking in that case. A real hand edit still asks.
2. **A declined or failed apply on a game switch left the new game active while `openmw.cfg` held the old one.** The previous active profile and the last profile of each game are now restored and saved.
3. **A `game=` line in `openmw.cfg` was dropped by the first import.** `ConfigSnapshot::mGame` is read, and `importFromConfig` makes it the profile's game. `morrowind`, or an id a profile file cannot hold, is not imported.
4. **The editor's message box for a missing pack would block a quiet-window run.** In such a run the box is now skipped; the error line is logged as before.
5. **Bookkeeping when profiles are renamed or deleted.** Renaming a profile updates its last-profile entry. Deleting one removes it and prefers another profile of the same game.
6. **The first profile's name.** It is kept within the 64-byte limit, and a profile that cannot be made shows a message.
7. **Overlapping game choices.** The Game box is disabled while a game choice runs.
8. **The capture's active world.** The capture clears it on every return path, through a guard.

**Left as they are, for toddhow:**
- `GameSessionSetup::prepare` registers the pack's archives and reads the content headers on the GUI thread at start-up. For the five Skyrim masters this is about 1 s here; the pack's own index runs on its worker thread.
- The first cell query can wait for that index, as the old cell source did.
- A profile file with `game` cannot be read by a launcher from before this task, which refuses the unknown key. This affects non-Morrowind profiles only; the format version was not raised.
- `ModsPage::loadSettings` now also runs in `MainDialog::setup`. After the wizard, `reloadSettings` runs it again, as before.

**After the fixes:**
- components-tests: 194 mod organizer and session tests pass.
- eden-studio-tests: 1,289 pass.
- `tools/edenstudio_check`: the old and new builds give equal logs. Both read `Extensions: 1 of 2 extensions active`, because the example extension is now built in that folder too.
- `tools/skyrimcell_check`: the Bannered Mare frame is still byte-identical to the old build's.
- Two editors started from the main checkout's `build-jolt` by someone else blocked one run (single instance). The run waited for them to close; they were not touched.
