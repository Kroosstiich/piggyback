# Piggyback, documentation for mod authors

Everything you need to attach a creature to another actor and keep it there, frame by frame.

- [Quick start](#quick-start)
- [API reference](#api-reference)
- [Natural follow lag (1.2)](#natural-follow-lag)
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
Utility.Wait(0.9)          ; let the exit transition finish before the AI takes over
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

; Since 1.2.0
bool Function SetFollowLag(Actor akPet, float afMoveLag, float afTurnLag) global native
int  Function GetVersion() global native

; Since 1.2.1
bool Function SetFollowMaxDistance(Actor akPet, float afMaxDistance) global native
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

**This is asynchronous.** The call returns straight away; the transition itself takes about 0.8
seconds, and Piggyback keeps placing the actor for its duration. Do not give the actor back to its AI
before that (see [rules](#rules-you-must-follow)). Collision is handed back at the start of the
transition, so the actor settles onto the ground as a normal, solid actor.

### `IsAttached(akPet)`

`true` while the actor is carried. Returns `false` during the detach transition.

### `IsInstalled()`

Always `true` when the DLL is present. See [optional dependency](#treating-piggyback-as-an-optional-dependency).

### `SetFollowLag(akPet, afMoveLag, afTurnLag)` — since 1.2.0

Gives the carried actor a natural delay: instead of moving and turning on the very same frame as the
host, it eases into the movement and settles without bouncing. See
[natural follow lag](#natural-follow-lag).

| Parameter | Meaning |
|---|---|
| `akPet` | An actor currently attached |
| `afMoveLag` | Response time of the **position**, in seconds. `0` = off |
| `afTurnLag` | Response time of the **heading**, in seconds. `0` = off (the default ~125 ms smoothing) |

Negative values mean `0`. There is **no upper limit**: your mod decides. Returns `false` if the actor
is not attached. The setting belongs to
the current attachment: **call it again after every `Attach`**.

### `SetFollowMaxDistance(akPet, afMaxDistance)` — since 1.2.1

How far the carried actor may fall behind its spot while the follow lag is active, in units at
standard build (scaled to the host, like the offset). `0` = no limit, which is the default. Useful with
long delays or a fast host, to keep the actor from trailing too far. The setting belongs to the current
attachment: **call it again after every `Attach`**. Returns `false` if the actor is not attached.

### `GetVersion()` — since 1.2.0

The installed DLL version as one number: `major * 10000 + minor * 100 + patch`, so `10201` for 1.2.1.
Returns `0` when the DLL is missing **or older than 1.2.0** (the function does not exist there). Use it
before calling anything added after 1.1:

```papyrus
int version = Piggyback.GetVersion()
if version >= 10200
    Piggyback.SetFollowLag(myPet, 0.4, 0.4)
endif
if version >= 10201
    Piggyback.SetFollowMaxDistance(myPet, 200.0)
endif
```

`IsInstalled()` remains the way to check that Piggyback is present at all.

---

## Natural follow lag

By default the carried actor is glued to its anchor: when the host starts, stops or jumps, the rider
does the same thing on the same frame. That is precise, but it can look mechanical, especially for a
creature that is supposed to have a mind of its own, or several followers moving in formation.

`SetFollowLag` adds a delay to how the rider reacts:

- **`afMoveLag`** delays the **position**. The rider eases into a start, drifts slightly on a stop or a
  landing, then settles back into place.
- **`afTurnLag`** delays the **heading**. The rider turns a moment after the host and catches up
  smoothly.

The rider still sits exactly where you placed it while the host moves steadily in a straight line: the
delay only shows **when something changes** (starting, stopping, turning, jumping, landing). Your
offsets keep their meaning.

```papyrus
if Piggyback.Attach(myPet, Game.GetPlayer(), "NPC Spine2 [Spn2]", 18.0, -60.0, -90.0, true)
    myPet.SetDontMove(true)
    if Piggyback.GetVersion() >= 10200
        Piggyback.SetFollowLag(myPet, 0.5, 0.5)
    endif
endif
```

**Choosing values.** Around `0.25` the effect is subtle; from `0.5` it is clearly visible; beyond `1.0`
the rider reads as sluggish. There is no maximum: pick what suits your creature. The best value depends on the creature and its animations, so exposing
two sliders in your MCM is a good idea: the change applies smoothly while the actor is carried, there
is no need to detach or reload.

**Several followers.** Give each one a slightly different value (for example `0.45`, `0.5`, `0.55`) so
they do not all react on the same frame. That alone makes a group look far less robotic.

**Things worth knowing:**

- `0` for both restores the exact behaviour of earlier versions. Switching the position lag off while
  carried eases the rider back onto its spot instead of snapping it there.
- By default, nothing limits how far the rider can fall behind: with a long delay and a fast host,
  it can trail far back before catching up. If that is not what you want, set a limit with
  [`SetFollowMaxDistance`](#setfollowmaxdistanceakpet-afmaxdistance--since-121) (1.2.0 used a fixed
  150 units).
- A teleport of the host (fast travel, load door, `coc`) resets the delay: the rider does not fly
  across the map.
- The setting is not saved, like the attachment itself. Re-apply it whenever you re-attach, including
  after a save is loaded.
- A value of `0` for `afTurnLag` does not remove all smoothing: the heading keeps the fixed ~125 ms lag
  introduced in 1.1.0, which prevents the rider from being re-placed in one frame on a fast mouse turn.

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

### Offsets are scaled to the host's build

Since 1.1.0, the offset you pass is **relative to a standard humanoid**, and Piggyback scales it to the
actual host. The same values therefore read the same on a slight Breton and on an Orc, and your sliders
keep their meaning whatever body the player is on.

The scale is measured from the host's own geometry, once, when the pet is attached: the height of the
anchor node above the host's feet, divided by 89.6 (the height of `NPC Spine2 [Spn2]` on a standard
humanoid). `GetScale()` is deliberately
**not** used, because it does not reflect the real size depending on how the character was resized
(RaceMenu, a race mod, the `setscale` console command). The result is clamped to 0.5x to 2x so an
unusual anchor node or a non-humanoid host cannot produce an absurd offset.

One consequence worth knowing: if you attach while the host is crouching, the anchor node is lower, so
the measured scale is slightly under-estimated for the whole attachment.

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

Release it after detaching — **but wait for the exit transition to finish first**:

```papyrus
Piggyback.Detach(myPet)
Utility.Wait(0.9)            ; the exit transition takes about 0.8s
myPet.SetDontMove(false)     ; required, and only once the transition is done
```

`Detach` returns immediately, but Piggyback keeps placing the actor frame by frame while it sets it
down. Handing control back to the AI during that window means two systems are moving the same actor:
the AI walks it one way, the rig puts it back, and it visibly oscillates.

`IsAttached` returns `false` as soon as `Detach` is called, so it cannot be used to detect the end of
the transition. Wait a second, or drive it from your own timer.

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

Functions added after the first release are guarded the same way, with `GetVersion()`: on an older
DLL they are not registered, and calling one only writes an error to the Papyrus log.

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
        Utility.Wait(0.9)            ; exit transition, see "Rules you must follow"
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
- **The host cannot be pushed by other characters while carrying.** This is how the rider is kept
  from shoving its host around: rather than disabling the rider's collision, the host is made
  immovable for the duration, and returns to normal the moment the rider is set down. A side effect
  worth knowing is that NPCs cannot bump the host either while it carries something.
- **A carried actor can still push NPCs it passes through.** It remains a fully simulated actor, by
  design: earlier versions disabled its collision instead, and teleporting a disabled body across the
  world left the physics engine with stale information about where it was, so it could end up below
  the floor when released. Sidestepping the rider around obstacles is the planned answer; it is not
  implemented yet.
- **The rider turns with a slight lag.** Its heading chases the host's over roughly 125 ms rather than
  snapping to it, so a fast mouse turn reads as a sweep instead of a jump. Since 1.2.0, a longer and
  more natural delay can be set with [`SetFollowLag`](#natural-follow-lag).
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

**The host gets pushed sideways while carrying**
Fixed in 1.1.0: the host is made immovable by other characters for as long as it carries something.
Earlier versions pushed the host whenever the two capsules overlapped, which players hit as a jolt on
jumps and on braking out of a run, or as a constant drift when the rider was tuned closer than about
65 units. If you still see it on 1.1.0 or later, attach your log — the flag is re-applied every frame,
so it should hold across cell changes.

**`SetFollowLag` or `GetVersion` "not found"**
The installed DLL is older than 1.2.0, or an old `Piggyback.pex` is overriding the new one. Guard the
call with `GetVersion() >= 10200` and check there is only one `Piggyback.pex` in your load order.

**Nothing happens at all**
Check `Documents\My Games\Skyrim Special Edition\SKSE\Piggyback.log`. On startup it lists the registered
functions. If that line is missing, the DLL did not load: check SKSE and Address Library.

---

## Building from source

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
The DLL build does not compile Papyrus. Since 1.2.0 added functions, the DLL and
`Piggyback.pex` must come from the same version: compile `Scripts/Source/Piggyback.psc`
with the Creation Kit Papyrus compiler and the matching game script imports.

CommonLibSSE-NG **7.5.1** is fetched from
[alandtse's maintained repository](https://github.com/alandtse/CommonLibSSE-NG)
at commit `bedcb1e05418baba7b316a650b6180c2dd6007a8`.
The vcpkg baseline is pinned separately. The build script and triplet select the
same compiler toolset.

## Compatibility — 1.2.1

Tested on **Skyrim Steam 1.7.104** with **SKSE 2.3.1** and the matching Address Library
database. **Skyrim 1.6.1170** is reported working by players. Piggyback is built with
CommonLibSSE-NG for all runtimes, but other versions (1.5.97, 1.7.100) and VR have not been
tested. Feedback is welcome.

Piggyback 1.1.0 remains available for Skyrim 1.6.1170 if you prefer it.

## License

Since 1.1.1, Piggyback is GPL-3.0-or-later. See LICENSE, COPYING.txt and
THIRD-PARTY-NOTICES.md. Previously published releases retain their original terms.
