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

**Requirements:** Visual Studio 2022+ with the *Desktop development with C++* workload, the **MSVC v143**
toolset component, and vcpkg.

```powershell
.\build.ps1            # Release by default; -Config Debug also works
```

Two traps documented in [DOCUMENTATION.md](DOCUMENTATION.md#building-from-source): the toolset must be
pinned to v143 **in the vcpkg triplet**, and `vcvarsall.bat` needs the VS Installer directory on `PATH`.

Built on [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG), so it runs on SE, AE and
VR.

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

**Using Piggyback in your mod:** freely, including in published mods. No permission needed. A credit and
a link are appreciated but not required.

**Modifying the source:** allowed, on one condition, **credit Piggyback and Kroosstii**. A dependency on
the Nexus page is *not* required, since depending on what you change it could cause more problems than it
solves.

Do not re-upload the plugin as-is under another name.

---

## Credits

- **Concept and direction:** Kroosstii. Piggyback grew out of a need in *Velyn the Netch* and was built
  deliberately as a standalone, reusable component.
- **AI assistance:** written with the help of an AI assistant (Claude), which produced the C++ to
  specification and helped debug it. The design decisions are Kroosstii's.
- The per-frame hook approach follows the pattern used by **TrueDirectionalMovement**.
- Built on **CommonLibSSE-NG**.
