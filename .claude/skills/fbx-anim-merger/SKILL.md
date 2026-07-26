---
name: fbx-anim-merger
description: Drive the fam-cli tool to build an FBX animation library - merge animation-only FBX files (Mixamo takes, marketplace clips) onto one base character rig, inspect what is inside an FBX, check whether a clip's skeleton matches a rig, and export the result as FBX or glTF 2.0/GLB. Use whenever the task involves merging, batching, retargeting-free combining, inspecting or converting FBX/GLB character animation files.
---

# fbx-anim-merger CLI

`fam-cli` is the headless twin of the FBX Animation Merger desktop app. It exposes
the same pipeline: import a base model, merge clips from other FBX files onto its
skeleton, curate the clip list, export FBX or glTF. Only the interactive parts
(3D viewport, playback, camera) are GUI-only.

## Finding the binary

Look for `fam-cli` (`fam-cli.exe` on Windows) on `PATH`, then in `build/bin/`. If it
is missing, build it from the repository root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel --target fam_cli
```

## Commands

| Command | Purpose |
|---|---|
| `fam-cli info <file.fbx>` | what is inside a file: counts, clips, optionally nodes/bones/tracks |
| `fam-cli check --base <rig.fbx> --anim <clip.fbx>` | how well a clip's skeleton matches a rig; writes nothing |
| `fam-cli merge --base <rig.fbx> --anim <...> --out <file>` | the real pipeline: import, merge, export |
| `fam-cli convert --in <file> --out <file>` | re-export one file in another format or scale |

`--anim` is repeatable and accepts a directory (add `--recursive` to descend).
Exit codes: **0** success, **1** failure, **2** bad usage. `fam-cli --help merge`
prints every flag.

## The workflow that works

1. **Inspect first.** `fam-cli info rig.fbx` tells you the bone count and what clips
   the base already carries. Never guess at a rig you have not looked at.
2. **Check before merging** when the clips come from a different source than the
   character: `fam-cli check --base rig.fbx --anim clips/`. It reports a match
   percentage per file plus the exact node names that fail to bind. Below ~80% the
   merge will be partial, and the unmatched list tells you why (a namespace the
   stripper does not handle, a renamed spine, a completely different skeleton).
3. **Merge**, gating on that match so a bad pairing fails loudly instead of writing
   a half-empty file:

```bash
fam-cli merge --base character.fbx --anim clips/ --name-from-file --min-match 80 --out character_merged.glb
```

4. **Verify.** The merge report already lists every resulting clip; add `--json` and
   read `clips[]` and `sources[].unmatchedNodes` rather than trusting the exit code
   alone. `info` re-reads only FBX (import is ufbx), so a `.glb` result cannot be
   round-tripped through it — check the report, or export FBX and `info` that.

## Things you have to know to use this correctly

**Merged characters stretch if you copy translation.** A clip animates
`Lcl Translation` on every bone, and those values are the *source* rig's bone
lengths. The default `--translation root` takes rotation from every bone and
translation only from root bones, so the target keeps its own proportions. Change it
only when you know both rigs are proportioned identically (`--translation all`), or
to also drop constant translation channels (`--translation animated`). If a merged
character comes out stretched, someone passed `--translation all`.

**Root motion is retargeted by default** (`--retarget-root`): the root track is
re-anchored to the base rest pose and scaled by the hip-height ratio, so a clip
authored on a shorter rig does not leave a taller character hovering. Turn it off
only to reproduce raw source motion.

**Every Mixamo take is named `mixamo.com`.** Without `--name-from-file` a merge of
20 Mixamo files yields `mixamo.com`, `mixamo.com_1`, … `--name-from-file` names each
clip after its source file, which is nearly always what you want for batch merges.
`--prefix run_` prepends to whatever name results.

**Units.** The scene is metres internally. FBX conventionally stores centimetres, so
`--scale` defaults to **100 for FBX** and **1 for glTF/GLB**. Do not override it
unless the target engine asks for something else — mismatched scale is the usual
cause of a character arriving 100× too large or small.

**Format comes from the output extension** (`.fbx`, `.glb`, `.gltf`); `--format
fbx-ascii` is the only one that needs saying out loud. GLB embeds textures by
default, the others do not (`--embed-textures` / `--no-embed-textures`).

**Bake rate.** Curves are resampled, not curve-preserved. 30 fps is the default;
raise `--bake-rate` to 60 for fast motion, lower it for smaller files.

## Curating the clip list

These run after all merging, in this order: `--apply-policy`, `--rename`, `--drop`,
`--only`/`--exclude`.

- `--rename OLD=NEW` — `OLD` is an exact name, a `*`/`?` wildcard, or a clip index.
- `--drop PATTERN` — deletes clips outright.
- `--only PATTERN` / `--exclude PATTERN` — pick what gets written, keeping the rest
  in the model.
- `--apply-policy` — re-runs the translation/scale/root policy over **every** clip in
  the result, including the ones the base file arrived with. This is how you fix a
  file that was merged with the wrong settings, without re-importing the sources:

```bash
fam-cli convert --in stretched.fbx --out fixed.fbx --apply-policy --translation root
```

## Scripting it

The report always goes to **stdout** and the progress log to **stderr**, so the two
never interleave. Pass `--json` to get that report as one machine-readable document
and parse it rather than scraping the text:

```bash
fam-cli merge --base rig.fbx --anim clips/ --name-from-file --out out.glb --json
```

Useful fields: `ok`, `sources[].match` (0..1), `sources[].animationsAdded`,
`sources[].unmatchedNodes`, `clips[]` (final names, durations, selection),
`output.written`.

Other flags for automation:

- `--dry-run` — run the whole pipeline, report, write nothing. `--out` becomes optional.
- `--keep-going` — one unreadable source does not abort the batch; the exit code is
  still 1 and the failure appears in `sources[]`.
- `--min-match PERCENT` — refuse to write when any source matches the rig worse than
  this. Prefer this over checking the number yourself.
- `-q` silences the log; `--log FILE` mirrors it to disk.

## When a merge goes wrong

| Symptom | Cause | Fix |
|---|---|---|
| `nothing merged`, 0% match | different skeletons, or names the resolver cannot line up | run `check` and read `unmatchedNodes`; toggle `--no-strip-namespace` / `--no-ignore-case` to see which rule is misfiring |
| some tracks dropped | helper/mesh nodes in the clip | expected; `--skeleton-tracks-only` makes it deliberate |
| character stretched | `--translation all` on mismatched rigs | drop back to `--translation root` |
| character floats or sinks | `--no-retarget-root` on a rig of a different height | leave root retargeting on |
| clips all called `mixamo.com_N` | Mixamo naming | `--name-from-file` |
| model 100× off in the engine | scale override | let `--scale` default per format |
| `cannot tell the format from '.x'` | unknown output extension | pass `--format` |

Blend shapes, and every material map beyond base colour and normal, are not carried
through — say so rather than letting the user discover it downstream.
