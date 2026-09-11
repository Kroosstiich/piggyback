# Changelog

All notable changes to Piggyback are documented here.
This project follows [Semantic Versioning](https://semver.org/): the Papyrus API is the public
contract, and no released function ever changes signature.

## [1.1.1] - 2026-09-11

- Added compatibility with Skyrim Special Edition 1.7.104.

## [1.1.0] - 2026-07-28

Three fixes, all reported by players on the Nexus page. No API change: mods built against 1.0.0 keep
working untouched.

### Fixed

- **The host no longer gets pushed by the actor it carries.** The rider's proxy is teleported every
  frame, and whenever its capsule overlapped the host's, havok resolved the penetration by pushing
  the *host* away, one way only. Players hit this as a jolt on jumps and when braking out of a run,
  and as a constant sideways drift when the rider was positioned close in. The host is now made
  immovable by other characters for the duration of the attachment, and returns to normal as soon as
  the rider is set down.

  This removes the distance floor that made a rider tucked against the host's back impossible.

  Suppressing the *rider's* collision instead was tried first and abandoned: teleporting a
  non-collidable body across the world leaves the broadphase holding stale information about where
  that body is, and the actor could then settle below the floor when released. Acting on the host
  keeps the rider a normally simulated actor from start to finish, so there is nothing to
  re-synchronise. The trade-off is that a carried actor can still push NPCs it passes through.

- **A rider whose host disappears is no longer left permanently ghosted.** Collision state is now
  restored on that path too.

- **`SetOffset` no longer teleports the rider.** The applied offset eases towards the requested one,
  so live position sliders slide the rider across instead of snapping it. The documentation had
  always claimed this was the behaviour; only the absence of a re-entry transition was ever true.

- **Fast turns no longer look like a teleport.** The ride position and the rider's angle were both
  rebuilt every frame from the host's instant yaw, so a quick mouse turn moved the target across the
  whole arc around the host in a single frame: the rider was re-placed rather than rotated. The rig
  now follows a smoothed heading that chases the host's with a fixed lag of roughly 125 ms,
  framerate-independent, and wrapping correctly across the +/-180 degree seam.
  Reported by *ellder4mk*.

- **Offsets are now scaled to the host's build.** They were expressed in absolute units, so on a
  smaller character the same distance was a much larger share of the body and a vertical offset meant
  for the knees ended up at the heels: the creature read as trailing behind rather than being carried.
  The scale is measured once per attachment from the host's own geometry, not from `GetScale()`, which
  does not reflect the real size depending on how the character was resized.

  **If you tuned position sliders on a resized character, re-check them after updating.**

### Changed

- Comments, log messages and the Papyrus API documentation are now in English throughout.
- The version reported in the log is read from the build instead of being hardcoded, so it can no
  longer disagree with the release it ships in.
- The diagnostic log line is deliberately kept in release builds: it is what made it possible to
  diagnose a player's report remotely, from their log alone.
- `build.ps1` auto-detects Visual Studio and vcpkg, and takes `VS_PATH` / `VCPKG_ROOT` overrides.

### Fixed in the documentation

- The comment in `Piggyback.psc` described the offset as being in the bone's local space. It never
  was: it is in the host's facing space, as `DOCUMENTATION.md` always said.

## [1.0.0] - 2026-07-26

First public release.

- `Attach`, `SetOffset`, `Detach`, `IsAttached`, `IsInstalled`.
- Frame-accurate attachment through a hook on the player's update loop.
- Smooth entry and exit transitions along a vertical arc; the rider is set down on release rather
  than dropped.
- Vertical guard so the rider never sinks below the host's feet.
- Movement and sprint animation events forwarded to the rider.
- No ESP, no MCM, and nothing written to the save.
