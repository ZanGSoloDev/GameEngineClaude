---
name: starfall-script-api
description: Extend the Lua scripting API of Starfall (bindings, sandbox, tests). Use when adding functions/types visible to game scripts.
---

# Extending the scripting API

* Bindings: `Starfall/Source/Starfall/Scripting/ScriptBindings.cpp` (sol2). Lifecycle, sandbox and the instruction budget:
  `ScriptWorld.cpp`. Script classes return a table with `OnCreate/OnUpdate/OnLateUpdate/OnDestroy/OnCollision*/OnTrigger*`.
* Handles passed to Lua must be safe against destroyed entities: use `ScriptEntity` / `ComponentRef<T>` (re-resolve every access, throw
  a `std::runtime_error` for stale access which sol2 turns into a Lua error).
* 64-bit values do not fit Lua numbers – pass ids as strings.
* Validate arguments and throw descriptive errors (`unknown key 'X'`) instead of crashing or silently ignoring.
* Do not expose file, os or debug libraries; keep the sandbox in `ScriptWorld::ScriptWorld`.
* Tests: `Tests/Core/ScriptingTests.cpp` (unit) and the self-test in `Tools/GenerateTestProject/main.cpp` (`Scripts/SelfTest.lua`,
  regenerate the project). Document new API in the header comment of `ScriptWorld.h`.
