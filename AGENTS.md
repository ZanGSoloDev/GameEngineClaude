# Starfall – agent & developer guide

Starfall is a 3D game engine (C++20, CMake) for Windows, macOS and Linux with a Vulkan renderer (nvrhi), Jolt physics,
miniaudio, Lua scripting and an ImGui editor. Production-grade rules apply: no hacks, everything tested, no code copied from
outside this repository.

## Layout

| Path | Contents |
|---|---|
| `Starfall/Source/Starfall/` | Engine static library. Modules: `Core` (log, asserts, UUID, filesystem, `Application`, `GameApplication`), `ECS`, `Math`, `Scene` (components, serializer, history), `Project`, `Platform` (window, input), `Renderer`, `Physics`, `Audio`, `Scripting` |
| `StarfallRuntime/` | Game player executable (also what gets exported) |
| `StarfallEditor/Source/` | Editor executable: `EditorApp`, `Panels/`, `EditorActions`, `EditorCamera`, `AssetFiles` (icons) |
| `Resources/Shaders/` | GLSL 4.50, compiled to SPIR-V at build time into `bin/Resources/Shaders` (`Include/` holds shared headers) |
| `Tools/` | `ShaderCompiler` (glslang), `GenerateTestProject` (writes `Projects/TestProject`) |
| `Projects/TestProject/` | Feature test project: every component, material feature, prefab, glTF, audio, HDRI, and a Lua self-test. **Generated** – edit `Tools/GenerateTestProject/main.cpp` and regenerate |
| `Tests/` | doctest unit/integration tests (`Core/`, `Renderer/`) |
| `Scripts/` | `Build.ps1` (build/test), `Smoke.ps1` (end-to-end check) |

## Build, test, run

```powershell
Scripts/Build.ps1 -Test                 # Debug build + ctest (VS toolchain + Ninja, see script)
Scripts/Build.ps1 -Config Release
Scripts/Smoke.ps1                       # tests + runtime/editor screenshots + export + run exported game
Build/Debug/tools/GenerateTestProject.exe Projects/TestProject   # regenerate the test project
```

Other platforms: `cmake -S . -B Build -G Ninja && cmake --build Build && ctest --test-dir Build` (dependencies are fetched by
CMake `FetchContent`; a Vulkan driver is required at runtime, shaders need no SDK).

Automation flags (runtime and editor): `--project <file>`, `--screenshot <png> --frames <n>`, `--width/--height`,
`--hidden`, `--validation`; editor only: `--select <entity>`, `--play`, `--game-view`, `--colliders`,
`--export <proj> <dir>`. Screenshots are how rendering/UI changes are verified – always look at the image.

Tests that need a GPU skip themselves when no Vulkan device exists (`SF_TEST_NO_GPU=1` forces that).
`SF_TEST_LOG=1` prints engine logs, `SF_TEST_SAVE_IMAGES=1` writes `render_*.png` from render tests.

## Code style (matches Hazel)

* Tabs, Allman braces, `if(` without a space, namespaces `Starfall` / `StarfallEditor`.
* Types, functions, enum values: `PascalCase`. Members `m_Name`, statics `s_Name`, locals/params `camelCase`.
* Macros `SF_*` (`SF_CORE_INFO/WARN/ERROR`, `SF_INFO` for app code, `SF_ASSERT`). Format strings use `{0}` placeholders.
* `Ref<T>` / `Scope<T>` aliases, `CreateRef` / `CreateScope`. Prefer pimpl to keep Jolt/sol2/miniaudio out of public headers.
* Public headers include only what they need; third-party heavy headers stay in `.cpp` / `*Internal.h`.
* Comments explain *why*; no commented-out code.
* Source files are UTF-8, keep them ASCII where possible.

## Conventions that bite

* **Rendering conventions**: nvrhi flips the Vulkan viewport, so clip space is Y-up like OpenGL (do **not** flip the projection)
  and render targets have uv (0,0) at the top-left. Convert ndc to uv with `vec2(ndc.x*0.5+0.5, 0.5-ndc.y*0.5)`.
  Front faces are counter-clockwise (`setFrontCounterClockwise(true)`).
* **nvrhi binding layouts**: slots must be unique per pipeline across layouts, so layouts use
  `setRegisterSpaceAndDescriptorSet(n)` (global = 0, scene/pass inputs = 1, material = 2) and `ZeroBindingOffsets()`.
  GLSL `set` numbers must match. Pipelines using the global layout declare **128 bytes** of push constants and every draw must set
  exactly 128 bytes (`MeshPushConstants` / `SetSmallPush`). Textures/buffers are created with `keepInitialState = true`.
* **Shutdown order**: every nvrhi handle (command lists, buffers, binding sets, cached assets) must be released before the
  `GraphicsDevice` is destroyed. `AssetManager::ReleaseGPU/Clear` and `Application::Run` do this – keep it that way.
* **Entity safety**: scripts hold `ScriptEntity` (scene pointer + UUID) and re-resolve on every access; never cache `Entity` across frames
  in scripting code. Destroying entities from scripts/systems uses `Scene::QueueDestroy`.
* **Lua**: 64-bit ids are exposed as strings (Lua numbers are doubles). Scripts run sandboxed with an instruction budget.
* Assets are referenced by project-relative path (`Models/X.gltf#<primitive>`, `builtin://Cube`); `Project::ResolvePath` rejects `..`.
* Scene/prefab/material files are versioned JSON; deserialization must tolerate malformed input (tests cover this).

## Workflow rules

1. Every change comes with tests (unit tests for logic; GPU tests render and compare images or assert no error logs).
2. Run `Scripts/Build.ps1 -Test` and for renderer/editor/runtime changes `Scripts/Smoke.ps1` before committing.
3. Review the diff for style, error handling, resource lifetime and tests before committing; do not commit failing builds.
4. New components: see `.claude/skills/starfall-add-component`. New shaders: `.claude/skills/starfall-add-shader`.
   New scripting API: `.claude/skills/starfall-script-api`.
5. Do not read or copy code from outside this repository; dependencies come only from the pinned `cmake/Dependencies.cmake`.
