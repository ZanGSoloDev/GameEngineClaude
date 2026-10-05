---
name: starfall-verify
description: Verify a Starfall change end to end (build, unit tests, GPU tests, screenshots, export). Use before committing renderer, editor, runtime or scene changes.
---

# Verification checklist

1. `Scripts/Build.ps1 -Test` – must be warning-free for engine code and 100% green.
2. `Scripts/Smoke.ps1 -SkipBuild` – launches runtime and editor (edit and play mode), exports the game and runs it. Nonzero exit codes
   (e.g. a crash at shutdown) or blank screenshots fail it.
3. Open `Out/Smoke/*.png` and check the images (shading, shadows, UI layout, gizmos).
4. Review the diff: style (see AGENTS.md), error paths, resource lifetime (GPU objects released before the device), tests added.
5. Commit with a descriptive message; push only when the above passed.
