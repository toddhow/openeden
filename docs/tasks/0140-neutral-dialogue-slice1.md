# Task 0140: Talk to a Skyrim NPC, slice 1 (milestone M-S4)

Status: APPROVED under toddhow's blanket "go with your recommendations" (2026-11-12). Full loop (D13): the engine
activation path, the SDK interface and the pack runtime are new code that decides what the player sees.

Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.20 (dialogue) and 4.24 (UI); decision 0017 (actors
as neutral objects); task 0137 (dialogue records, response choice, data-backed conditions world), task 0139 (quest
state), task 0121 (actor appearance interface, the pattern copied here).

Goal: activating a Skyrim NPC (with `[Game] neutral actors` on) opens a conversation that shows the NPC's greeting
(picked by the pack from the data, text from the string tables) and the topics the player may choose; choosing a topic
shows its response; Goodbye closes. Morrowind dialogue is unchanged.

## 1. Design: how an activation reaches a dialogue without Morrowind records

### What the engine does today (cited)

- **Activation.** `MWClass::Neutral::activate` (`apps/openmw/mwclass/neutral.cpp`) returns the base class activation for
  everything but doors and containers; for an actor the base `MWWorld::Class::activate` is a no-op. This is the one place
  a neutral actor's activation can be taken (task 0115/0128/0138 did it for doors and locks).
- **The dialogue window is Morrowind-shaped.** `MWGui::DialogueWindow::setPtr` (`apps/openmw/mwgui/dialogue.cpp`, line
  485) refuses an object whose class is not `isActor()`, then calls `MWBase::DialogueManager::startDialogue(actor,
  callback)`; it reads `mPtr.getClass().getCreatureStats(mPtr)` (line 541), `getServices` (line 600) and
  `getNpcStats` (line 810), and looks topics up by `ESM::RefId::stringRefId(keyword)` (line 861). A neutral object has
  none of these (decision 0017, task 0131 made the refused queries safe, not meaningful).
- **The dialogue manager is Morrowind-shaped.** `MWBase::DialogueManager` (`apps/openmw/mwbase/dialoguemanager.hpp`)
  speaks `ESM::RefId` topics and infos, `Ptr` actors, the journal, persuasion and `MWMechanics` services; its only
  implementation is `game_packs/morrowind` (task 0085 move 3).
- **Interactive message boxes.** `MWBase::WindowManager::interactiveMessageBox(message, buttons)` and
  `readPressedButton()` (`apps/openmw/mwbase/windowmanager.hpp`, implemented in `mwgui/windowmanagerimp.cpp` and
  `mwgui/messagebox.cpp`) show a modal box with buttons and return the index later; the scripts' `MessageBox` command
  uses them in normal play (`mwscript/interpretercontext.cpp`).
- **A pack's optional interface on its registered source.** `Neutral::insertActorRendering` finds the pack's
  `ActorAppearanceSource` by `dynamic_cast` on `NeutralWorldStore::source(gameId)` (task 0121); the frozen
  `WorldCellSource` stays untouched.

### Options

- **A. A game-neutral dialogue interface in the SDK that the pack implements, plus a minimal engine presentation.**
  `openeden::sdk::game::DialogueSource` (plain strings and opaque topic ids; found by `dynamic_cast` like the appearance
  source). The engine's part is a small session that shows each turn. The pack keeps every rule (which response passes,
  quests, say once). Cost: a new interface and a presentation of our own.
- **B. Reuse the existing dialogue window through a `DialogueManager` implementation for neutral sessions.** Needs
  `Class::isActor()`, creature and NPC stats, services and a Morrowind `RefId` topic model to be faked for Skyrim; the
  window also shows Morrowind-only things (disposition, persuasion, services). Every Morrowind-shaped assumption would
  have to be satisfied with invented values, and the dialogue manager is installed once for the whole executable
  (`MWDialogue::installFactories`), not per game.
- **C. A new RmlUi dialogue panel (decision 0015).** The right final form for Skyrim's own look, but it needs the
  document, the Lua component library and input routing for a game window, none of which is for slice 1.

### Recommendation: A, with the interactive message box as slice 1's presentation

A keeps the pack's rules in the pack (architecture rules, root `CLAUDE.md`) and makes the presentation replaceable: when
the RmlUi panel exists (option C), only `NeutralDialogueSession` changes; the SDK interface and the pack do not.
B would put invented Morrowind values in front of the Skyrim rules and ties Skyrim to the Morrowind window.

The message box is deliberately plain: the message is the speaker's name and the line, the buttons are the topics (8 a
page, "More topics..." cycles the pages), then "Goodbye". It is the stopgap that shows the whole data path in a game
today; it is not the final UI (no history log, no portrait, no camera).

## 2. Built in this slice

- `sdk/include/openeden/game/dialogue_source.hpp` (DRAFT 0.1): `DialogueSource::beginConversation(base, reference,
  cell, turn, error)`, `chooseTopic(id, turn, error)`, `endConversation()`; `DialogueTurn` = speaker name, text,
  topic choices, `endsConversation`, `hadUnrunEffects`. No SDK version change (new interface found by `dynamic_cast`).
- Pack: `game_packs/skyrimse` `dialogue_runtime.*` (`DialogueRuntime`: greeting by `pickFromTopics("HELO")`; topics =
  DIAL records of category Topic with a name whose `selectInfo` gives a response for this speaker; the prompt (RNAM) is
  the label if the response has one; say once kept for the session; a Goodbye response ends the conversation; quest
  running state from `QuestState::initial`); `INFO` now notes a `VMAD` subrecord (`DialogInfo::mHasScript`); `SkyrimWorldView`
  builds the dialogue and quest indexes on the first conversation (another read of every plugin, like the actor index) and
  reads `.ILSTRINGS`/`.STRINGS` per plugin on demand; `SkyrimCellSource` forwards.
- Engine: `MWWorld::NeutralDialoguePager` (pure) and `NeutralDialogueSession` (`apps/openmw/mwworld/neutraldialogue.*`),
  `TalkAction` in `Neutral::activate`, one poll a frame in `Game::run` before the window manager's update, two l10n
  strings (`OMWEngine`).

Not built (counted or refused, never silent): voice, lip sync, camera and idle animations; INFO script fragments (counted
in `Stats::mScriptsNotRun`, `DialogueTurn::hadUnrunEffects`); link-to topics (`mLinksIgnored`); speech challenges, barter,
training and the other services; Goodbye topics (`GBYE`) are not looked up (the engine's own Goodbye button closes);
say-once memory beyond the session; quest changes from dialogue; saving (still refused in neutral sessions).

## 3. Acceptance criteria

- [ ] `DialogueRuntime` tests on generated plugins (greeting per speaker, topic list per speaker, response, say once,
      goodbye, refusals, script and topic cap counters) pass; shown RED against a stub first.
- [ ] `NeutralDialoguePager` tests (paging, wrap, goodbye, ended turn) pass.
- [ ] In a game with the five Skyrim SE masters (`tools/neutral_session_check`, the Bannered Mare, Hulda):
      activating Hulda opens the box with her greeting and topics; a topic shows its response; Goodbye closes.
- [ ] Off means unchanged: with `[Game] neutral actors` off an actor stays inert; a Morrowind session never reaches the
      new code (`Neutral::activate` is only for neutral objects). `tools/worldobjects_check` dump equal to a parent
      build; a fixed-frame Balmora frame hash equal; `tools/dialogue_check` (task 0085) gives the same conversation.

## 4. Out of scope

Everything under "Not built" above; the RmlUi dialogue panel; the Morrowind dialogue window; `MWBase::DialogueManager`.

## 5. Decisions for toddhow

1. Option A with the message box as slice 1, RmlUi panel later (recommended above).
2. Topic order is form-id order (the data has no better key; the game's own order is RECALLED-VERIFY unknown).
3. A topic is offered when its first passing response exists. Conditions the data cannot answer fail the whole response
   closed (task 0137), so some real topics will be missing until the conditions engine learns more functions; the count
   of offered topics chosen with an uncertain condition is `Stats::mUncertainTopics`.

## Stage log

(see the end of the file; filled in as the work is run)
