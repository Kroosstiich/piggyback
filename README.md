# Piggyback

**An SKSE framework that attaches one actor to a bone of another actor, every single frame.**

A creature carried on a character's back stays welded there: no drifting, no catching up, no stutter
when the host turns or jumps.

This plugin **does nothing on its own**. It is a tool for other mod authors, so that mods needing a
rider can simply ask for one.

- **Nexus page:** [Piggyback](https://www.nexusmods.com/skyrimspecialedition/mods/186556)
- **Documentation:** [DOCUMENTATION.md](DOCUMENTATION.md)

---

## Why native code

Papyrus, Skyrim's scripting language, updates around 10 to 20 times per second while the game draws 60
frames or more. A script repositioning a creature to keep it glued to a moving character can only act
between updates, and every gap is visible: lag, snapping, jitter, and actors sinking through the floor on
slopes.

Five scripted approaches were tried and abandoned while developing *Velyn the Netch*. A frame-accurate
rig **requires** native code, which is what this is.

---

## Papyrus API

```papyrus
bool Function Attach(Actor akPet, Actor akHost, string asNodeName, \
                     float afX, float afY, float afZ, bool abMatchRotation = true) global native
bool Function SetOffset(Actor akPet, float afX, float afY, float afZ) global native
bool Function Detach(Actor akPet) global native
bool Function IsAttached(Actor akPet) global native
bool Function IsInstalled() global native
```

```papyrus
if Piggyback.IsInstalled()
    Piggyback.Attach(myPet, Game.GetPlayer(), "NPC Spine2 [Spn2]", 18.0, -60.0, -90.0, true)
    myPet.SetDontMove(true)   ; Piggyback must be the only thing moving the pet
endif
```

`IsInstalled()` returns `false` when the DLL is absent (the native is simply never registered), which
lets your mod treat Piggyback as an **optional** dependency and hide the feature cleanly.

Full details, coordinate space, rules and examples: **[DOCUMENTATION.md](DOCUMENTATION.md)**.

---

## Building

Requirements: Visual Studio 2022 or newer with the Desktop development with C++
workload, CMake/Ninja tools, **MSVC v143 14.44**, Git and a bootstrapped vcpkg checkout.

```powershell
$env:VCPKG_ROOT = "D:\Tools\vcpkg"
.\build.ps1 -Jobs 4
# Optional, explicit DLL deployment:
.\build.ps1 -OutputFolder "D:\Mods\Piggyback-dev"
```

The default build does not modify the game or mod manager. Its DLL is written to
`build/skyrim-1.7-release/Piggyback.dll`. Use `-Config Debug` for a debug build.
The DLL build does not compile Papyrus. The API is unchanged, so a local test can
retain `Piggyback.pex` from the installed release. To package from source, compile
`Scripts/Source/Piggyback.psc` with the Creation Kit Papyrus compiler and the
matching game script imports.

CommonLibSSE-NG **7.5.1** is fetched from
[alandtse's maintained repository](https://github.com/alandtse/CommonLibSSE-NG)
at commit `bedcb1e05418baba7b316a650b6180c2dd6007a8`.
The vcpkg baseline is pinned separately. The build script and triplet select the
same compiler toolset.

## Compatibility — 1.1.1

Requires **Skyrim Steam 1.7.104**, **SKSE 2.3.1**, and the matching Address Library
database. In-game loading and carrying were confirmed on this runtime.
Other Skyrim versions, including 1.7.100, and VR have not been revalidated.

---

## Layout

| Path | Contents |
|---|---|
| `src/Piggyback.cpp`, `.h` | Attachment logic, transitions, Papyrus bindings |
| `src/hook.cpp`, `.h` | Per-frame hook on the actor update loop |
| `src/plugin.cpp` | SKSE entry point |
| `Scripts/Source/Piggyback.psc` | Papyrus API |
| `cmake/` | vcpkg triplet and source lists |

---

## Contributing

Fixes and improvements are welcome. If your change is useful to everyone, please open a pull request
rather than maintaining a fork, so the framework does not fragment into incompatible copies.

If you report a bug, `Documents/My Games/Skyrim Special Edition/SKSE/Piggyback.log` is the first thing to
attach.

---

## License and permissions

The current development tree is licensed under **GPL-3.0-or-later** — see
[LICENSE](LICENSE) and [COPYING.txt](COPYING.txt).

**Using Piggyback in your mod:** freely, including in published mods. No permission needed. A credit and
a link are appreciated but not required.

Modification and redistribution, including commercial redistribution, are permitted
under the GPL. Preserve copyright and license notices and provide corresponding
source when distributing binaries, as required by the license.

Previously published versions retain their original licenses. Third-party terms
are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

---

## Credits

- **Concept and direction:** Kroosstiich. Piggyback grew out of a need in *Velyn the Netch* and was built
  deliberately as a standalone, reusable component.
- **AI assistance:** written with the help of an AI assistant (Claude), which produced the C++ to
  specification and helped debug it. The design decisions are Kroosstiich's.
- The per-frame hook approach follows the pattern used by **TrueDirectionalMovement**.
- Built on **CommonLibSSE-NG**.
