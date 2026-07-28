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
