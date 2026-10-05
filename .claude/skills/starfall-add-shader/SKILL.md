---
name: starfall-add-shader
description: Add or change a GLSL shader / render pass in Starfall (binding layouts, push constants, pipelines, verification). Use for any renderer change.
---

# Adding a shader or pass

* Shaders live in `Resources/Shaders/` (`name.vert|frag|comp`, shared code in `Include/`). They are compiled automatically by
  `starfall_compile_shaders`; a syntax error fails the build. Use `#extension GL_GOOGLE_include_directive : require`.
* Per-frame data is `FrameData` (C++ `Renderer/RenderTypes.h` <-> GLSL `Include/Frame.glsl`, std140) – keep both in sync and keep
  `static_assert`s on struct sizes.
* Descriptor sets: 0 global (frame UBO, samplers, 128B push constants), 1 pass inputs, 2 material. Match
  `setRegisterSpaceAndDescriptorSet` of the layout in C++. Binding numbers are unique per pipeline.
* Every draw using the global layout must set exactly 128 bytes of push constants.
* Add the pipeline in `SceneRenderer::CreatePipelines` (it logs when a pipeline cannot be created), record the pass in
  `SceneRenderer::Render`, rebuild binding sets in `RebuildBindingSets` when resources change.
* Remember the viewport flip: uv origin top-left, Y-up clip space, CCW front faces.
* **Verify visually**: `Scripts/Smoke.ps1`, or `StarfallRuntime --project Projects/TestProject/TestProject.sfproj --screenshot shot.png --frames 60`
  and look at `shot.png`. Add/extend a test in `Tests/Renderer/RenderingFeatureTests.cpp` (must assert that no error is logged –
  nvrhi's validation layer reports binding mistakes as errors).
