#!/bin/bash -e

# Decision 0003: extensions read physics, never change it, have no fixed-step phases and no Lua API.
# Decision 0005: extensions do not affect save data. Task 0084 (SDK 4.0, decision 0013): the save chunk handler is back for
# game packs only, so it may be declared in sdk/include/openeden/game/ and named by the Host accessor, nowhere else.
# This fails if an SDK header declares any of those again. The only Lua words allowed are the ones
# that ask the core to forward an event; no extension code runs in Lua.

REMOVED='\b(createBody|destroyBody|createJoint|destroyJoint|createCollisionGroup|setSubGroupsCollide|destroyCollisionGroup|registerCollisionLayer|declareCollisionPair|CollisionLayer|setJointMotor|setJointPositionSteps|setJointTarget|applyImpulse|setVelocity|setBodyDamping|setKinematicTarget|setMotionType|fixedStepSeconds|stepIndex|FixedStep[A-Za-z]*|PhysicsService|getPhysicsService|isConstraintValid|BodyDesc|JointDesc|JointMotor|PhysicsWorldConfig|CreateResult|JointId|ConstraintId|CollisionGroupId|ScriptingService|getScriptingService|LuaTable|lua_State|sol::[A-Za-z_]+|registerModule|SaveService|getSaveService)\b|\bstep *\('

# git grep exits 0 on a match, 1 on none and higher on an error, which must not pass as "none".
status=0
git grep -nE "$REMOVED" -- 'sdk/include/openeden/' || status=$?
if [ "$status" -eq 0 ]; then
    echo 'An SDK header declares something decision 0003 removed: see docs/decisions/0003-plugins-do-not-change-physics-or-lua.md and 0005-plugins-do-not-affect-saves.md'
    exit 1
elif [ "$status" -ne 1 ]; then
    echo "git grep failed with status $status"
    exit "$status"
fi

# Any other mention of Lua, once the allowed forwarding words are taken out.
status=0
mentions=$(git grep -nE '[Ll]ua' -- 'sdk/include/openeden/') || status=$?
if [ "$status" -gt 1 ]; then
    echo "git grep failed with status $status"
    exit "$status"
fi
other=$(echo "$mentions" | sed -E 's/(mark|is)ForwardedToLua//g; s/[Ff]orward(ed|s)? to Lua//g; s/a Lua event//g; s/Lua scripts//g; s/in Lua//g; s/Lua API//g' | grep -E '[Ll]ua' || true)
if [ -n "$other" ]; then
    echo "$other"
    echo 'An SDK header mentions Lua other than to forward events: extensions have no Lua API (decision 0003)'
    exit 1
fi

# Task 0031 stage C (SDK 3.0, decision 0007): the runtime-side editor hooks are gone. Eden Studio's tools come from
# editor modules (sdk/include/openeden/editor_module/), which the game never loads.
status=0
git grep -nE '\b(EditorService|EditorWorkspace|EditorInspector|EditorGizmo|getEditorService)\b' -- 'sdk/include/openeden/' || status=$?
if [ "$status" -eq 0 ]; then
    echo 'An SDK header declares the removed runtime editor hooks: use an editor module instead (docs/decisions/0007-eden-studio-editor-design.md)'
    exit 1
elif [ "$status" -ne 1 ]; then
    echo "git grep failed with status $status"
    exit "$status"
fi

# Task 0084 (SDK 4.0, decision 0013): the game services live in sdk/include/openeden/game/ and are for packs of kind
# "game" only. Another SDK header may name the save chunk types only to declare the Host accessor (host.hpp).
status=0
other=$(git grep -nE '\bSaveChunk[A-Za-z]*\b|\b(ChunkWriter|ChunkReader)\b' -- 'sdk/include/openeden/' ':!sdk/include/openeden/game/' ':!sdk/include/openeden/host.hpp') || status=$?
if [ "$status" -eq 0 ]; then
    echo "$other"
    echo 'An SDK header outside openeden/game/ names the save chunk handler: it is for game packs only (decisions 0005 and 0013)'
    exit 1
elif [ "$status" -ne 1 ]; then
    echo "git grep failed with status $status"
    exit "$status"
fi

# A feature extension or a compatibility pack never includes a game-only header (the loader would hand it nullptr anyway).
# Game packs live in game_packs/ (decision 0013); the test game pack is the one other place allowed.
status=0
git grep -nE '#include *[<"]openeden/(game/|pack_kind\.hpp)' -- 'extensions/' 'editor_modules/' 'apps/openmw_tests/extension/testextension/' ':!apps/openmw_tests/extension/testextension/testgamepack.cpp' || status=$?
if [ "$status" -eq 0 ]; then
    echo 'A feature extension, compatibility pack or editor module includes a game-only SDK header: only a game pack (game_packs/) may (decision 0013)'
    exit 1
elif [ "$status" -ne 1 ]; then
    echo "git grep failed with status $status"
    exit "$status"
fi

# Task 0085 move 2 (review 2c, M2): the crafting contract states its version policy and its id tokens as opaque text.
if ! grep -q 'Version policy' sdk/include/openeden/game/crafting.hpp || ! grep -q 'OPAQUE TEXT' sdk/include/openeden/game/crafting.hpp; then
    echo 'sdk/include/openeden/game/crafting.hpp must keep its version policy and the note that id tokens are opaque text'
    exit 1
fi

# Task 0097 (stage A): the script bindings header states its version policy. It must not name the script language
# (the general check above already fails on any "Lua" in the SDK); the core, not the SDK, knows the target.
if ! grep -q 'Version policy' sdk/include/openeden/game/bindings.hpp; then
    echo 'sdk/include/openeden/game/bindings.hpp must keep its version policy'
    exit 1
fi

# Task 0084 leftovers (lane I, I1): the GameRules header states its version policy and carries the contract id.
if ! grep -q 'Version policy' sdk/include/openeden/game/game_rules.hpp || ! grep -q 'openeden.game.rules' sdk/include/openeden/game/game_rules.hpp; then
    echo 'sdk/include/openeden/game/game_rules.hpp must keep its version policy and the contract id openeden.game.rules'
    exit 1
fi

# Task 0086 (world model option 1): the world view header states its version policy and its main-thread rule.
if ! grep -q 'Version policy' sdk/include/openeden/game/world_view.hpp || ! grep -q 'kWorldViewVersion' sdk/include/openeden/game/world_view.hpp; then
    echo 'sdk/include/openeden/game/world_view.hpp must keep its version policy and kWorldViewVersion'
    exit 1
fi

# Decision 0016 (neutral world model, task 0099 N0): the value types header states its version policy and is types only.
if ! grep -q 'Version policy' sdk/include/openeden/game/world_model.hpp || ! grep -q 'kWorldModelVersion' sdk/include/openeden/game/world_model.hpp; then
    echo 'sdk/include/openeden/game/world_model.hpp must keep its version policy and kWorldModelVersion'
    exit 1
fi

# Task 0100 (stage N1): the source interface a pack implements to feed the core's neutral world store states its version.
# (it is an interface only; that is by review, not by this check).
if ! grep -q 'kWorldSourceVersion' sdk/include/openeden/game/world_source.hpp || ! grep -q 'Same version policy' sdk/include/openeden/game/world_source.hpp; then
    echo 'sdk/include/openeden/game/world_source.hpp must keep kWorldSourceVersion and its version policy line'
    exit 1
fi
