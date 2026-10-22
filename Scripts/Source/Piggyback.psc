Scriptname Piggyback Hidden

; Papyrus API of the Piggyback SKSE plugin (a generic, reusable component).
; Attaches an actor to a skeleton node of another actor, FRAME BY FRAME - which Papyrus alone cannot do
; (it tops out at 10-20 Hz). Requires Piggyback.dll; without it these functions fail silently, so always
; provide a fallback. Full documentation: DOCUMENTATION.md.

; asNodeName : name of a node on akHost's skeleton (for example "NPC Spine2 [Spn2]" for the upper back;
; a vanilla node name, so XPMSSE is not required).
; afX/afY/afZ : offset expressed in the HOST'S FACING space - X = right, Y = forward (negative = behind),
; Z = up. NOT the bone's local space: a bone's local axes are not predictable across skeletons and
; animations, which would make any setting impossible to reason about or expose as a slider.
; The offset is scaled to the host's build, so the same values read the same on a small character and a
; large one.
; abMatchRotation : also aligns the pet's facing with the host's.
bool Function Attach(Actor akPet, Actor akHost, string asNodeName, float afX, float afY, float afZ, bool abMatchRotation = true) global native

; Updates the offset of an ALREADY attached akPet without detaching it: the new position applies
; smoothly from the next frame (no "drop then climb back up"). Same space as Attach. This is what lets
; you expose a live position setting, such as MCM sliders. Returns false if akPet is not attached.
bool Function SetOffset(Actor akPet, float afX, float afY, float afZ) global native

; Detaches akPet: it is set down behind the host and returns to its normal AI.
bool Function Detach(Actor akPet) global native

bool Function IsAttached(Actor akPet) global native

; Presence probe: returns true when Piggyback.dll is installed. When it is absent the native is never
; registered and Papyrus returns false (the default) - so a consumer mod can cleanly hide a feature that
; depends on Piggyback instead of offering something that will fail.
bool Function IsInstalled() global native

; --- Added in 1.2.0 (SetFollowMaxDistance: 1.2.1). Check GetVersion() before calling these: on an older
; DLL they are not registered, and the call only logs a Papyrus error and returns the default.

; Follow lag: afMoveLag and afTurnLag are response times in seconds for the position and the heading
; (0 = off, the default). The pet eases into moves and turns instead of reacting on the very same frame,
; then settles without bouncing. 0.25 is subtle, 0.5 clearly visible; no upper limit. Takes effect smoothly,
; even while carrying, so it can be driven by an MCM slider. The setting belongs to the attachment:
; call it again after every Attach. Returns false if akPet is not attached.
bool Function SetFollowLag(Actor akPet, float afMoveLag, float afTurnLag) global native

; Since 1.2.1 (GetVersion() >= 10201). How far akPet may fall behind its spot while lagging, in units at
; standard build (scaled with the host like the offset). 0 = no limit, the default. The setting belongs
; to the attachment: call it again after every Attach. Returns false if akPet is not attached.
bool Function SetFollowMaxDistance(Actor akPet, float afMaxDistance) global native

; Since 1.2.2 (GetVersion() >= 10202). abEnabled = true by default: the heading follows the host with a
; short ~125 ms lag, so fast turns do not throw a distant pet around. false = akPet turns exactly with
; the host, with no lag at all (afTurnLag of SetFollowLag is then ignored): use it for a pet sitting ON
; the host, on its back or in its arms. Call it again after every Attach. Returns false if not attached.
bool Function SetTurnSmoothing(Actor akPet, bool abEnabled) global native

; Since 1.2.2 (GetVersion() >= 10202). Turns akPet's facing by afDegrees relative to the host's heading
; (0 by default). Positive = clockwise seen from above (90 = facing the host's right, 180 = facing the
; host). Only applies when the pet was attached with abMatchRotation = true. Applied at once. Call it
; again after every Attach. Returns false if akPet is not attached.
bool Function SetRotationOffset(Actor akPet, float afDegrees) global native

; Since 1.2.2 (GetVersion() >= 10202). EXPERIMENTAL. abEnabled = true by default. false = while
; carrying akPet, the host ignores collisions with all characters and walks through them until akPet is
; released. It does NOT stop an NPC rider sitting on a running host from saying its "watch it" line
; (still being investigated). Call it again after every Attach. Returns false if not attached.
bool Function SetHostCollision(Actor akPet, bool abEnabled) global native

; Since 1.3.0 (GetVersion() >= 10300). abEnabled = true by default. false = akPet and its host stop
; colliding with EACH OTHER, while both keep colliding with the ground, the scenery and everyone else.
; For a pet held close against its host (in its arms, on its back), which a running host otherwise walks
; into. Restored on release. Call it again after every Attach. Returns false if akPet is not attached.
bool Function SetRiderCollision(Actor akPet, bool abEnabled) global native

; Since 1.3.0 (GetVersion() >= 10300). Where Detach sets akPet down, in the host's facing space like the
; offset: X = right, Y = forward (negative = behind), scaled to the host's build. Default (0, -60):
; behind the host. Use a positive afY to set a pet carried in front down in front, instead of through
; the host. Call it again after every Attach. Returns false if akPet is not attached.
bool Function SetReleaseOffset(Actor akPet, float afX, float afY) global native

; Version of the installed DLL as one number: major * 10000 + minor * 100 + patch (10300 for 1.3.0).
; Returns 0 when Piggyback.dll is absent or older than 1.2.0.
int Function GetVersion() global native

; Since 1.2.2. Same as Attach, but akPet first WALKS to a spot afApproachDistance units behind akHost,
; driven by the game's own AI (pathfinding, real walk/run animations), and only then does the short
; hop onto the anchor. Without it, Attach glides the pet straight from wherever it stands, which can
; mean a long leap through the host's body. Gives up waiting after afTimeout seconds and attaches anyway.
; LATENT: returns only once attached (or refused), so call it from a script that can wait. Do not call
; SetDontMove(true) on akPet before it returns, or it cannot walk. Returns what Attach returns, or false
; if either actor is missing or dead.
bool Function ApproachAndAttach(Actor akPet, Actor akHost, string asNodeName, float afX, float afY, float afZ, bool abMatchRotation = true, float afApproachDistance = 60.0, float afTimeout = 8.0) global
	if !akPet || !akHost || akPet == akHost || akPet.IsDead() || akHost.IsDead()
		return false
	endif
	akPet.KeepOffsetFromActor(akHost, 0.0, -afApproachDistance, 0.0)
	float waited = 0.0
	bool arrived = false
	while !arrived && waited < afTimeout
		if akPet.IsDead() || akHost.IsDead() || !akPet.Is3DLoaded()
			akPet.ClearKeepOffsetFromActor()
			return false
		endif
		; The spot behind the host, recomputed each time since the host may be moving. Skyrim heading:
		; forward = (sin, cos), angles in degrees.
		float angle = akHost.GetAngleZ()
		float dx = akPet.GetPositionX() - (akHost.GetPositionX() - afApproachDistance * Math.Sin(angle))
		float dy = akPet.GetPositionY() - (akHost.GetPositionY() - afApproachDistance * Math.Cos(angle))
		if dx * dx + dy * dy <= 900.0 ; within 30 units
			arrived = true
		else
			Utility.Wait(0.1)
			waited += 0.1
		endif
	endwhile
	akPet.ClearKeepOffsetFromActor()
	return Attach(akPet, akHost, asNodeName, afX, afY, afZ, abMatchRotation)
EndFunction
