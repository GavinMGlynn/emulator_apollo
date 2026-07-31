# Third-party dependencies

Everything here is a pinned git submodule. Nothing in this directory is our
code, nothing here is modified in place, and nothing here is redistributed by
this repository — a clone gets URLs and commit SHAs, not sources.

## The two classes

Submodules fall into two groups, and the difference is a **licence boundary**,
not a convenience:

| Submodule | Upstream | Pinned at | Role | Licence |
| --- | --- | --- | --- | --- |
| `unity` | ThrowTheSwitch/Unity | `v2.7.0` | **Linked.** The test framework. Compiled into every suite | MIT |
| `zlib` | madler/zlib | `v1.3.2` | **Linked** (not yet used). Deflate for compressed disk/tape images | zlib |
| `libpng` | pnggroup/libpng | `v1.6.58` | **Linked** (not yet used). Framebuffer → PNG for display verification | libpng/zlib |
| `sdl` | libsdl-org/SDL | `release-3.4.12` | **Linked** (not yet used). The interactive frontend, Phase 5 | zlib |
| `mame` | mamedev/mame | branch tip | **Reference only.** Built out-of-tree and instrumented as the oracle | **GPL-2.0-or-later** |
| `musashi` | kstenerud/Musashi | branch tip | **Reference only.** A 68000 core read for cross-checking behaviour | MIT, but see below |

This core is MIT. `ext/mame` is GPL-2.0-or-later, so:

- **Never link `ext/mame` or `ext/musashi` into `src/core`** — or into anything
  else this repository builds.
- **Never copy code across** from either into ours.
- MAME is *run* as a separate program and its output compared with ours. That is
  an arms-length relationship, and it is the only one permitted.
- Musashi is MIT and would be legally safe to link, but it is listed as
  reference-only on purpose: the point of this project is a core derived from
  the manuals, and reading a second implementation to *check* a conclusion is
  different from importing one to *reach* it.

`LICENSE` states this boundary too; this file is where the per-submodule detail
lives.

## What the build actually needs

**`ext/unity`, and nothing else.** The other five are declared so that the
versions we build against are recorded, but no CMake target references them yet:
zlib, libpng and SDL land with the media and display phases, and mame/musashi
are never part of a build at all.

```sh
git submodule update --init --depth 1 ext/unity
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug
```

CI does exactly this — see `CI_SUBMODULES` in `.github/workflows/ci.yml` — which
is what keeps the claim honest rather than aspirational. `tests/CMakeLists.txt`
fails with an actionable message if `ext/unity` is missing, and
`tests/no_gpl_linkage.cmake` fails the configure if a GPL submodule is ever
wired into a target.

Do not init `ext/mame` casually: it is hundreds of megabytes, and CI never
compiles it.

## Why release tags rather than branch tips

The linked submodules were originally added at whatever branch tip was current —
zlib on `develop`, libpng on the `libpng18` development branch, SDL on `main`.
That is not a pin in any useful sense: the recorded SHA is stable, but it names
an arbitrary mid-development commit that upstream never released, tested as a
unit, or would support.

For a project whose entire premise is that a given workload produces
bit-identical output on every platform and build type, dependencies must sit on
deliberate, released versions. Each is now pinned to a release tag, and the
table above records which.

## Updating a pin

A pin is a decision, so it changes deliberately and on its own commit:

```sh
cd ext/<name>
git fetch --tags --depth 1 origin
git checkout <new-tag>
cd ../..
git add ext/<name>
```

Commit it alone, with the tag in the message and a note on why it moved. Update
the table above in the same commit. If the new version changes any emulated
result, that is not a dependency bump — it is a bug in us or in them, and it
gets investigated before the pin lands.

Submodules are cloned shallow (`--depth 1`), so `git describe` and `git tag`
find nothing until you `git fetch --tags` as above. A shallow checkout is why
the tag names in the table are recorded here rather than being derivable from a
fresh clone.
