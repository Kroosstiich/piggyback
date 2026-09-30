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

; --- Added in 1.2.0. Check GetVersion() >= 10200 before calling these: on an older DLL they are not
; registered, and the call only logs a Papyrus error and returns the default.

; Follow lag: afMoveLag and afTurnLag are response times in seconds for the position and the heading
; (0 = off, the default). The pet eases into moves and turns instead of reacting on the very same frame,
; then settles without bouncing. 0.25 is subtle, 0.5 clearly visible; capped at 2.0. Takes effect smoothly,
; even while carrying, so it can be driven by an MCM slider. The setting belongs to the attachment:
; call it again after every Attach. Returns false if akPet is not attached.
bool Function SetFollowLag(Actor akPet, float afMoveLag, float afTurnLag) global native

; Version of the installed DLL as one number: major * 10000 + minor * 100 + patch (10200 for 1.2.0).
; Returns 0 when Piggyback.dll is absent or older than 1.2.0.
int Function GetVersion() global native
