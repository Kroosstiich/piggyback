# Piggyback, documentation for mod authors

Everything you need to attach a creature to another actor and keep it there, frame by frame.

- [Quick start](#quick-start)
- [API reference](#api-reference)
- [The coordinate space](#the-coordinate-space)
- [Rules you must follow](#rules-you-must-follow)
- [Optional dependency](#treating-piggyback-as-an-optional-dependency)
- [Complete example](#complete-example)
- [Limits and known behaviour](#limits-and-known-behaviour)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)

---

## Quick start

Add Piggyback's `Scripts\Source\Piggyback.psc` to your compiler's import paths. **Do not redistribute
it**, the compiled `.pex` ships with Piggyback itself.

```papyrus
if Piggyback.IsInstalled()
    ; behind the host, knee height, slightly to the right
    Piggyback.Attach(myPet, Game.GetPlayer(), "NPC Spine2 [Spn2]", 18.0, -60.0, -90.0, true)
    myPet.SetDontMove(true)
endif
```

To release:

```papyrus
Piggyback.Detach(myPet)
myPet.SetDontMove(false)
```

That is the whole feature.

---

## API reference

```papyrus
Scriptname Piggyback Hidden

bool Function Attach(Actor akPet, Actor akHost, string asNodeName, \
                     float afX, float afY, float afZ, bool abMatchRotation = true) global native
bool Function SetOffset(Actor akPet, float afX, float afY, float afZ) global native
bool Function Detach(Actor akPet) global native
bool Function IsAttached(Actor akPet) global native
bool Function IsInstalled() global native
```

### `Attach(akPet, akHost, asNodeName, afX, afY, afZ, abMatchRotation)`

Binds `akPet` to a skeleton node of `akHost` and keeps it there every frame.

| Parameter | Meaning |
|---|---|
| `akPet` | The actor being carried |
| `akHost` | The actor carrying it |
| `asNodeName` | Skeleton node used as the anchor point, for example `"NPC Spine2 [Spn2]"` |
| `afX` `afY` `afZ` | Offset from that node, see [coordinate space](#the-coordinate-space) |
| `abMatchRotation` | If `true`, the rider also turns with the host |

Returns `false` if either actor is invalid. Calling it again on an already attached pet replaces the
previous attachment.

The node only provides the **anchor point**. Vanilla nodes work fine, so **XPMSSE is not required**.
Common choices: `"NPC Spine2 [Spn2]"` (upper back), `"NPC Spine1 [Spn1]"`, `"NPC Head [Head]"`.

### `SetOffset(akPet, afX, afY, afZ)`

Changes the offset of an **already attached** actor without detaching it. The new position is applied
smoothly from the next frame, with no visible jump and no re-entry transition.

This is what you use to expose position sliders that work in real time. Returns `false` if the actor is
not currently attached.

### `Detach(akPet)`

Plays the exit transition, sets the actor down behind the host, restores its collision, and returns it to
its normal AI. Returns `false` if it was not attached.

### `IsAttached(akPet)`

`true` while the actor is carried. Returns `false` during the detach transition.

### `IsInstalled()`

Always `true` when the DLL is present. See [optional dependency](#treating-piggyback-as-an-optional-dependency).

---

## The coordinate space

The offset is expressed **relative to the host's facing**, not to the bone's local axes:

```
        +Z  up
         |
         |
         +-------- +X  right
        /
       /
     +Y  forward        (so negative Y = behind the host)
```

This is deliberate. A bone's local axes are not predictable across skeletons and animations, which makes
any setting impossible to reason about, and impossible to expose to a user as a slider. Here, "60 units
behind and 90 down" always means exactly that, whatever the host is doing.

Units are Skyrim units, roughly 1.4 cm each.

**Reference values** used by *Velyn the Netch* on `"NPC Spine2 [Spn2]"`:

| Axis | Value | Effect |
|---|---|---|
| X | `18.0` | Slightly to the right |
| Y | `-60.0` | Behind the host |
| Z | `-90.0` | Down to about knee height |

Those suit a small creature. Adjust for yours, and expose them if you can, since players like to place
their companion themselves.

---

## Rules you must follow

**1. Piggyback must be the only thing moving the carried actor.**

Call `SetDontMove(true)` on the pet when you attach it. Otherwise its AI packages keep trying to path it
somewhere while Piggyback repositions it every frame, and the two fight each other. The result is visible
jitter.

```papyrus
Piggyback.Attach(...)
myPet.SetDontMove(true)      ; required
```

Release it after detaching:

```papyrus
Piggyback.Detach(myPet)
myPet.SetDontMove(false)     ; required
```

**2. Re-attach after a save is loaded.**

The attachment state lives in memory and is **not written into the save**. This is deliberate: the plugin
stores no save data at all, so uninstalling it can never corrupt anything. It also means that after a
load, a previously carried creature is simply free again.

If your mod needs the rider to persist, re-attach on `OnPlayerLoadGame`, or clean up as *Velyn* does:
clear `SetDontMove` so the creature is not left frozen from a state that no longer exists.

**3. Do not attach an actor to itself**, and do not attach the player.

---

## Treating Piggyback as an optional dependency

`IsInstalled()` exists for exactly this. If the DLL is missing, the native function is never registered
and Papyrus returns the default value, `false`. You therefore never need a hard dependency:

```papyrus
if Piggyback.IsInstalled()
    ; show the "ride on my back" option
else
    ; hide it entirely, do not offer something that will fail
endif
```

This is the recommended pattern. *Velyn the Netch* uses two different menu records, one with the ride
entry and one without, and picks between them with this check. Players who do not want the plugin never
see a broken option.

> **Note:** your scripts still need `Piggyback.psc` **at compile time**, even for an optional
> dependency. Only the runtime is optional.

---

## Complete example

A quest script exposing "ride" as a toggle, with a position that can be tuned live:

```papyrus
Actor Property MyPet Auto
GlobalVariable Property MyRideX Auto    ; written by your MCM
GlobalVariable Property MyRideY Auto
GlobalVariable Property MyRideZ Auto

float _appliedX
float _appliedY
float _appliedZ

Function ToggleRide()
    if !Piggyback.IsInstalled()
        Debug.Notification("Piggyback is not installed.")
        return
    endif

    if Piggyback.IsAttached(MyPet)
        Piggyback.Detach(MyPet)
        MyPet.SetDontMove(false)
    else
        float x = MyRideX.GetValue()
        float y = MyRideY.GetValue()
        float z = MyRideZ.GetValue()
        if Piggyback.Attach(MyPet, Game.GetPlayer(), "NPC Spine2 [Spn2]", x, y, z, true)
            MyPet.SetDontMove(true)
            _appliedX = x
            _appliedY = y
            _appliedZ = z
        endif
    endif
EndFunction

; Call this from a periodic update so MCM sliders take effect while carried.
Function RefreshOffsetIfChanged()
    if !Piggyback.IsAttached(MyPet)
        return
    endif
    float x = MyRideX.GetValue()
    float y = MyRideY.GetValue()
    float z = MyRideZ.GetValue()
    if x != _appliedX || y != _appliedY || z != _appliedZ
        Piggyback.SetOffset(MyPet, x, y, z)
        _appliedX = x
        _appliedY = y
        _appliedZ = z
    endif
EndFunction
```

---

## Limits and known behaviour

- **Attachment does not survive a save/load**, by design. See rule 2.
- **Walls and tight corners.** The rider can clip into geometry behind the host in very narrow spaces.
  Detection by measuring how far the engine pushed the actor back does not work here, because the
  rider's collision is disabled while carried. A predictive raycast is the known solution and is not
  implemented yet.
- **Animations depend on the creature.** Movement and sprint events are forwarded, but a creature whose
  animation graph does not use them will stay in its idle. Graph *variables* such as `Speed` are set too,
  but on many creatures they do not drive locomotion, only the events do.
- **One host per pet.** Attaching a pet to a new host replaces the previous attachment.

---

## Troubleshooting

**`Static function X not found on object Piggyback. Aborting call and returning None`**
Your game is loading an **old or duplicate `Piggyback.pex`**. Search your whole mods folder for
`Piggyback.pex`: there must be exactly one, the one shipped by Piggyback. A stale copy left in another
mod will win by load order and hide the real API. This exact issue cost hours during Velyn's
development.

**The creature drifts alongside the host instead of being carried**
Something is calling `SetPosition` on it with collision movement disabled, or you forgot
`SetDontMove(true)` and its AI is fighting the attachment.

**Jitter when the host crouches or walks on a slope**
Should not happen, there is a vertical guard for this. If it does, report it with the node name and the
offset you used.

**Nothing happens at all**
Check `Documents\My Games\Skyrim Special Edition\SKSE\Piggyback.log`. On startup it lists the registered
functions. If that line is missing, the DLL did not load: check SKSE and Address Library.

---

## Building from source

**Requirements**
- Visual Studio 2022 or newer, with the **Desktop development with C++** workload
- The **MSVC v143** toolset component
- vcpkg

```powershell
.\build.ps1            # Release by default, -Config Debug also works
```

**Two build traps worth knowing:**

1. **Pin the toolset to v143 in the triplet** (`cmake\x64-windows-skse.cmake`:
   `VCPKG_PLATFORM_TOOLSET` and `VCPKG_PLATFORM_TOOLSET_VERSION`). Passing `-vcvars_ver` only affects
   the final build; vcpkg re-detects the compiler for every dependency and will pick a newer toolset,
   which breaks fmt 9.1.0 (`stdext::checked_array_iterator` was removed).
2. `vcvarsall.bat` calls `vswhere.exe` **without a full path**, so add
   `C:\Program Files (x86)\Microsoft Visual Studio\Installer` to `PATH` before invoking it.

**Layout**

| Path | Contents |
|---|---|
| `src\Piggyback.cpp` / `.h` | Attachment logic, transitions, Papyrus bindings |
| `src\hook.cpp` / `.h` | Per-frame hook on the actor update loop |
| `src\plugin.cpp` | SKSE entry point |
| `Scripts\Source\Piggyback.psc` | Papyrus API |

**License and permissions:** see the permissions section of the Nexus page. In short, use it freely in
your mods; if you modify the source, credit Piggyback and Kroosstii.
