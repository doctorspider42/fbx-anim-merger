# FBX Animation Merger

A small, fast desktop tool for building an animation library on top of a single
rig: load a base FBX model, drop in as many animation-only FBX files as you like,
merge their clips onto the base skeleton, preview and rename them, then export the
result as **FBX** or **glTF 2.0 / GLB**.

Written in C++20 with an OpenGL 3.3 viewport and a dockable Dear ImGui interface.
Everything it depends on is free and permissively licensed — no Autodesk FBX SDK,
nothing proprietary, nothing paid.

![status](https://github.com/doctorspider42/fbx-anim-merger/actions/workflows/build.yml/badge.svg)

---

## What it does

- **Import a base model** — geometry, materials, skeleton, skinning, plus any
  animation takes the file already contains.
- **Import animation files** — every animation stack found is baked and merged
  onto the base rig. Track binding is by node name, with namespace stripping
  (`mixamorig:Hips` → `Hips`), `Armature|Hips` path stripping and optional
  case-insensitive matching, so clips from different exporters line up.
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

**Export uses [Assimp](https://github.com/assimp/assimp)** as a writer only.
Assimp 5.4.3 has a bug in its FBX exporter (`to_ktime` truncates keyframe times to
whole seconds, which destroys every clip); the build applies a one-line fix at
configure time — see [`cmake/patches/FixAssimpFbxKeyTimes.cmake`](cmake/patches/FixAssimpFbxKeyTimes.cmake).
The exported FBX declares its own `UnitScaleFactor`, so it round-trips correctly
whatever export scale you pick.

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

## Building

Requires CMake 3.24+ and a C++20 compiler. All dependencies are fetched
automatically by CMake, so the first configure needs a network connection.

### MSVC

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --parallel
```

### MinGW-w64

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
```

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
- Only the base-colour texture is imported for preview and re-exported.

## Licence

MIT — see [LICENSE](LICENSE).
