# FBX Animation Merger

A small, fast desktop tool for building an animation library on top of a single
rig: load a base FBX model, drop in as many animation-only FBX files as you like,
merge their clips onto the base skeleton, preview and rename them, then export the
result as **FBX** or **glTF 2.0 / GLB**.

Written in C++20 with an OpenGL 3.3 viewport and a dockable Dear ImGui interface.
Everything it depends on is free and permissively licensed — no Autodesk FBX SDK,
nothing proprietary, nothing paid.

![status](https://github.com/doctorspider42/fbx-anim-merger/actions/workflows/build.yml/badge.svg)

**[Download the latest Windows build](https://github.com/doctorspider42/fbx-anim-merger/releases/download/latest/FbxAnimMerger-windows-x64.zip)**
— a single statically linked `.exe`, no runtime to install. Every green build of
`main` refreshes it; see [all releases](https://github.com/doctorspider42/fbx-anim-merger/releases).

---

## What it does

- **Import a base model** — geometry, materials, skeleton, skinning, plus any
  animation takes the file already contains.
- **Textures, embedded or on disk** — packaged characters carry their skins inside
  the FBX and record paths from the machine that built them, so the embedded bytes
  are read straight out of the file. Base colour is shown in the preview; base
  colour and normal map are carried through to both export formats.
- **Import animation files** — every animation stack found is baked and merged
  onto the base rig. Track binding is by node name, with namespace stripping
  (`mixamorig:Hips` → `Hips`), `Armature|Hips` path stripping and optional
  case-insensitive matching, so clips from different exporters line up.
- **Proportions are preserved** — see below.
- **Merge report** — how many tracks bound, how many were dropped, and exactly
  which node names failed to match. No silent partial merges.
- **Preview** — GPU-skinned playback on the base model with a timeline, scrub,
  loop, playback speed, bind-pose toggle and a skeleton overlay.
- **Rename** — double-click a clip; names are kept unique automatically.
- **Export** — FBX (binary 7.4 or ASCII) and glTF 2.0 (`.glb` or `.gltf`+`.bin`),
  with per-clip selection, unit scaling and optional texture embedding.

## Design notes

**Everything is normalised on import** to a right-handed, Y-up, 1 unit = 1 metre
space with animation stored as baked TRS keyframe tracks. This is the whole reason
merging works at all: a Z-up centimetre rig from Blender and a Y-up centimetre
Mixamo clip would otherwise never agree. Curves are resampled at a configurable
rate (30 fps by default) instead of trying to preserve every DCC's tangent model.

**Import uses [ufbx](https://github.com/ufbx/ufbx)**, which handles the parts of
FBX that usually go wrong — pre/post rotations, geometric transforms, unit and
axis conversion, skin clusters.

**Export uses [Assimp](https://github.com/assimp/assimp)** as a writer only. Two
bugs in 5.4.3 are fixed by one-line patches applied at configure time — see
[`cmake/patches/PatchAssimp.cmake`](cmake/patches/PatchAssimp.cmake):

- its FBX exporter truncates keyframe times to whole seconds, destroying every clip;
- its glTF2 exporter throws when a clip's name matches another clip's
  `<name>_<channelIndex>` id, which is exactly what happens once you merge a second
  Mixamo take (they are all named `mixamo.com`).

The exported FBX declares its own `UnitScaleFactor`, so it round-trips correctly
whatever export scale you pick.

## Why merged clips don't stretch the model

An FBX clip animates `Lcl Translation` on *every* bone, and those values are the
**source rig's bone lengths**. Copy them verbatim onto a different skeleton and
you overwrite its rest offsets: limbs snap to the donor's proportions and the mesh
visibly stretches. This is the single most common failure when pulling a Mixamo
clip onto a custom character, and it happens even at a 100% bone-name match.

So by default the merge takes **rotation from every bone and translation only from
root bones** (the hips, where the actual displacement lives). Bone lengths come
from the base model. `Settings > Merging > Translation` offers:

| Mode | Behaviour |
|---|---|
| **Root bone only** (default) | Rotation everywhere, translation only on bones with no bone ancestor. Keeps the target's proportions. |
| **Only if animated** | Drops translation channels that are constant across the clip — the ones that merely restate the source rig's bind pose. |
| **Copy everything** | Verbatim. Correct only when both rigs share proportions. |

Scale tracks are ignored by default for the same reason. **Apply to loaded clips**
re-runs the policy over clips you already merged, so a wrong choice does not mean
re-importing everything; clips that shipped with the base model are never touched.

This is not full retargeting — bone rotations are copied as-is, so wildly
different skeleton orientations still need a proper retargeter. It does make
same-hierarchy, different-proportions rigs (the Mixamo case) work correctly.

## Units

The scene is held in metres. FBX conventionally stores centimetres, glTF mandates
metres, so the export dialog defaults to **×100 for FBX** and **×1 for glTF/GLB**.
Both are overridable.

## Controls

| Action | Input |
|---|---|
| Orbit | Left mouse drag |
| Pan | Middle / right drag, or Shift + left drag |
| Zoom | Mouse wheel |
| Frame model | `F` |
| Play / pause | `Space` |
| Import base model | `Ctrl+O` |
| Import animations | `Ctrl+I` |
| Export | `Ctrl+E` |
| Zoom interface in / out | `Ctrl+=` / `Ctrl+-` |
| Reset interface zoom | `Ctrl+0` |

## Interface scaling

The interface follows the display's scaling. Windows reports 150% or 200% through
the window content scale; without honouring it the UI renders one interface pixel
per screen pixel and ends up physically half the intended size on a HiDPI display.

The scale is re-read every frame, so dragging the window between monitors with
different scaling adjusts it live rather than only at startup. On top of that,
`Settings > Interface` has a manual zoom (also `Ctrl+=` / `Ctrl+-` / `Ctrl+0`) and a
switch to ignore the system value entirely. Both are saved to
`fbx-anim-merger.ini` alongside the panel layout.

Fonts are re-rasterised at the new size rather than bitmap-stretched, so text stays
sharp at any scale.

## Building

Requires CMake 3.24+ and a C++20 compiler. All dependencies are fetched
automatically by CMake, so the first configure needs a network connection.

### MinGW-w64 (what CI builds and what releases ship)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
```

```bash
cmake --build build --parallel
```

### MSVC

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

MSVC compiles the whole project cleanly (static CRT, so no redistributable is
needed). It is not what CI ships, because the link step reliably stalls on GitHub's
hosted Windows runners — reproduced on consecutive runs after every translation
unit had already compiled. Local MSVC builds are unaffected as far as we know; if
you hit the same stall, MinGW is the supported path.

The binary lands in `build/bin/`.

### Self-test

A headless test builds a synthetic two-bone rig, writes it to FBX, reads it back
through ufbx, merges a second clip, exports FBX and GLB, and re-parses both with
Assimp — covering the whole pipeline without a GPU.

```bash
ctest --test-dir build --output-on-failure
```

## Third-party components

| Component | Licence |
|---|---|
| [ufbx](https://github.com/ufbx/ufbx) | MIT |
| [Assimp](https://github.com/assimp/assimp) | BSD 3-Clause |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT |
| [GLFW](https://github.com/glfw/glfw) | zlib/libpng |
| [GLM](https://github.com/g-truc/glm) | MIT |
| [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) | zlib |
| [stb_image](https://github.com/nothings/stb) | MIT / public domain |

All are permissive and commercially usable. Assimp is BSD-3-Clause rather than
MIT/Apache; it carries the same freedoms and only adds an attribution and a
no-endorsement clause. Full texts in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

The OpenGL loader is hand-written (`src/gl/GL.h`) to avoid pulling in a code
generator or another dependency.

## Known limitations

- The preview shader supports 200 bones; heavier rigs render their surplus bones
  in bind pose. Export is unaffected.
- Animation is resampled, not curve-preserving. Raise the bake rate for fast
  motion.
- Blend shapes / morph targets are not carried through.
- Base colour and normal maps survive; specular, roughness, AO and other maps are
  dropped. The preview shades with base colour only — the normal map is
  pass-through, since the mesh carries no tangents.
- FBX and glTF disagree on where UV (0,0) sits. Assimp's glTF2 writer already
  performs that flip internally, so coordinates are passed through untouched;
  flipping them first cancels it out and ships upside-down textures.

## Licence

MIT — see [LICENSE](LICENSE).
