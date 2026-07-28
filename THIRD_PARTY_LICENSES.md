# Third-party components

FBX Animation Merger itself is MIT licensed (see [LICENSE](LICENSE)). It does not
vendor any third-party source: CMake fetches each dependency at configure time
into `build/_deps/`, where every project's own `LICENSE` file is present verbatim.

Every dependency is permissive and commercially usable. Nothing here is
copyleft, proprietary, or paid. In particular, the **Autodesk FBX SDK is
deliberately not used** — it is closed source and its licence is incompatible
with shipping this as free software. FBX reading is done by ufbx, writing by
Assimp.

| Component | Version | Licence | Upstream |
|---|---|---|---|
| ufbx | `master` | MIT (dual-licensed MIT / Unlicense) | https://github.com/ufbx/ufbx |
| Open Asset Import Library (Assimp) | 5.4.3 | BSD 3-Clause | https://github.com/assimp/assimp |
| Dear ImGui | `docking` | MIT | https://github.com/ocornut/imgui |
| GLFW | 3.4 | zlib/libpng | https://github.com/glfw/glfw |
| OpenGL Mathematics (GLM) | 1.0.1 | MIT (The Happy Bunny License / MIT) | https://github.com/g-truc/glm |
| nativefiledialog-extended | 1.2.1 | zlib | https://github.com/btzy/nativefiledialog-extended |
| stb_image | `master` | MIT / public domain (dual) | https://github.com/nothings/stb |
| zlib (bundled inside Assimp) | — | zlib | https://github.com/madler/zlib |

## Obligations when redistributing a binary

All three licences below are satisfied by shipping this file (or an equivalent
attribution notice) alongside the binary.

- **MIT** and **BSD 3-Clause** require the copyright notice and licence text to
  be reproduced in distributions.
- **BSD 3-Clause** additionally forbids using the project's or contributors'
  names to endorse derived products without permission.
- **zlib** requires that the origin not be misrepresented and that modified
  source versions be marked as such. It does not require attribution for binary
  distribution, though it is customary.

## Modifications made to dependencies

Four small fixes are applied to **Assimp 5.4.3** at configure time by
[`cmake/patches/PatchAssimp.cmake`](cmake/patches/PatchAssimp.cmake). They are
noted here as required by BSD 3-Clause.

- `code/AssetLib/FBX/FBXExporter.cpp`, function `to_ktime`. Upstream truncates
  keyframe times to whole seconds before scaling to FBX time units, which collapses
  every key of a sub-second-spaced clip and makes exported FBX animation unreadable
  (assimp's own importer reads back zero animations). Fixed by multiplying first,
  then truncating.
- `code/AssetLib/glTF2/glTF2Exporter.cpp`, `ExportAnimations`. Upstream registers an
  animation's glTF id as the raw clip name while giving each of its channels the id
  `<clipName>_<channelIndex>` via `FindUniqueID`. A clip named `X_3` therefore
  collides with channel 3 of clip `X` and `LazyDict::Create` throws. Fixed by routing
  the animation id through `FindUniqueID` as well; the human-readable name is
  untouched.
- `code/AssetLib/FBX/FBXExporter.cpp`, `WriteObjects` (skin clusters). Upstream
  discards `aiBone::mOffsetMatrix` and rebuilds each cluster's bind pose from the
  node rest pose, which is only the same thing when the file was authored with the
  skeleton sitting in the bind pose. Every other rig is exported with a wrong bind
  pose and deforms into garbage. Fixed by writing `mOffsetMatrix` as the cluster
  `Transform` and deriving `TransformLink` from it.
- `code/AssetLib/FBX/FBXExporter.cpp`, `WriteObjects` (animation curve nodes).
  Upstream connects scaling curves to a property called `Lcl Scale`; the FBX property
  is `Lcl Scaling`, so animated scale is written and then ignored by every reader.
  Fixed by using the correct name.

## Licence texts

### MIT License

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

### BSD 3-Clause License

> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice, this
>    list of conditions and the following disclaimer.
> 2. Redistributions in binary form must reproduce the above copyright notice,
>    this list of conditions and the following disclaimer in the documentation
>    and/or other materials provided with the distribution.
> 3. Neither the name of the copyright holder nor the names of its contributors
>    may be used to endorse or promote products derived from this software
>    without specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
> AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
> IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
> DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
> FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
> DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
> SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
> CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
> OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
> OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Assimp's own notice additionally covers its bundled contributions; the
authoritative text ships as `LICENSE` in the Assimp source tree.

### zlib/libpng License

> This software is provided 'as-is', without any express or implied warranty. In
> no event will the authors be held liable for any damages arising from the use
> of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it freely,
> subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not claim
>    that you wrote the original software. If you use this software in a
>    product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

The copyright lines for each project are in its own `LICENSE` file upstream and
in `build/_deps/<project>-src/` after a configure.
