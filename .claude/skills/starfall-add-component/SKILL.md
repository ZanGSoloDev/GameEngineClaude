---
name: starfall-add-component
description: Add a new ECS component to Starfall (data, serialization, copy, inspector UI, scripting access, tests). Use when introducing or extending a scene component.
---

# Adding a component

1. **Data** – add the struct to `Starfall/Source/Starfall/Scene/Components.h` (plain data, defaults set, runtime-only fields at the end and
   reset in `ResetRuntimeState` in `Scene.cpp`). Add it to the `AllComponents` list – this drives `Scene::Copy`, `DuplicateEntity`
   and serialization.
2. **Serialization** – in `Scene/SceneSerializer.cpp` add `SerializeComponent` / `DeserializeComponent` overloads and a
   `ComponentName` overload. Use `ToEnum` / `ToVec3` helpers so malformed files fall back to defaults.
3. **Systems** – physics/audio/scripting hooks live in `Physics/`, `Audio/`, `Scripting/`. Components created at runtime must be picked up
   lazily (see `PhysicsWorld::OnUpdate`).
4. **Editor** – add a `ComponentHeader<T>` block in `StarfallEditor/Source/Panels/InspectorPanel.cpp` and an entry in
   `DrawAddComponentMenu`. Use the `Float/Check/Combo/Vec3/AssetField` helpers (they record undo steps).
5. **Scripting** – optional proxy usertype in `Scripting/ScriptBindings.cpp` (`ComponentRef<T>` re-resolves each access) plus
   `HasComponent/GetComponent/AddComponent` string cases.
6. **Test project** – use the component in `Tools/GenerateTestProject/main.cpp` and regenerate `Projects/TestProject`;
   extend `Tests/Core/TestProjectTests.cpp` ("contains every component type") and the Lua `SelfTest.lua` if it has a script API.
7. **Tests** – round trip in `Tests/Core/SceneTests.cpp` (serializer), behaviour tests next to the system.
