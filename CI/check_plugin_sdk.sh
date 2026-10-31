#!/bin/bash -e

# Decision 0003: plugins read physics, never change it, have no fixed-step phases and no Lua API.
# This fails if an SDK header declares any of those again. The only Lua words allowed are the ones
# that ask the core to forward an event; no plugin code runs in Lua.

REMOVED='\b(createBody|destroyBody|createJoint|destroyJoint|createCollisionGroup|setSubGroupsCollide|destroyCollisionGroup|registerCollisionLayer|declareCollisionPair|CollisionLayer|setJointMotor|setJointPositionSteps|setJointTarget|applyImpulse|setVelocity|setBodyDamping|setKinematicTarget|setMotionType|fixedStepSeconds|stepIndex|FixedStep[A-Za-z]*|PhysicsService|JointId|ConstraintId|CollisionGroupId|ScriptingService|getScriptingService|LuaTable|lua_State|sol::[A-Za-z_]+|registerModule)\b|\bstep *\('

# git grep exits 0 on a match, 1 on none and higher on an error, which must not pass as "none".
status=0
git grep -nE "$REMOVED" -- 'sdk/include/openeden/' || status=$?
if [ "$status" -eq 0 ]; then
    echo 'An SDK header declares something decision 0003 removed: see docs/decisions/0003-plugins-do-not-change-physics-or-lua.md'
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
    echo 'An SDK header mentions Lua other than to forward events: plugins have no Lua API (decision 0003)'
    exit 1
fi
