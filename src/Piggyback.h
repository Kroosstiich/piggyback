#pragma once

// Piggyback - attaches an actor to a skeleton node of another actor, frame by frame.
//
// Deliberately GENERIC: no reference to any particular mod or creature. It is published as a
// standalone component so the community can build on it. Do not introduce mod-specific logic here.
namespace Piggyback
{
	// Attaches a_pet to node a_node of a_host. The node only provides the ORIGIN POINT; the offset is
	// expressed in the host's facing space: X = right, Y = forward (negative = behind), Z = up.
	// Deliberately predictable, so the position can be tuned without guessing a bone's local axes.
	// A smooth transition brings the actor to the anchor point. Replaces any previous attachment of the
	// same pet. Returns false if either actor is invalid.
	bool Attach(RE::Actor* a_pet, RE::Actor* a_host, RE::BSFixedString a_node,
		float a_x, float a_y, float a_z, bool a_matchRotation);

	// Updates the offset of an ALREADY attached pet without detaching it: the new position is picked up
	// from the next frame (per-frame repositioning reads the current offset). This is what lets a mod
	// expose a live position setting, such as MCM sliders. Same space as Attach (X = right, Y = forward,
	// negative = behind, Z = up). Returns false if the pet is not attached.
	bool SetOffset(RE::Actor* a_pet, float a_x, float a_y, float a_z);

	// Detaches a_pet: exit transition (set down on the ground behind the host), then it returns to its
	// normal AI. Returns false if it was not attached.
	bool Detach(RE::Actor* a_pet);

	bool IsAttached(RE::Actor* a_pet);

	// Presence probe for consumer mods: returns true whenever the DLL is installed. When it is absent
	// the native is never registered, so the Papyrus call returns false (the default). This lets a mod
	// cleanly hide a feature that depends on Piggyback instead of offering something that will fail.
	bool IsInstalled();

	// Follow lag (1.2.0), opt-in: a_moveLag and a_turnLag are response times in seconds for the
	// position and the heading (0 = off, the 1.1 behaviour). The rider eases into moves and turns
	// instead of reacting on the same frame. Belongs to the current attachment: a new Attach resets
	// it. Takes effect smoothly, including while carrying. Returns false if the pet is not attached.
	bool SetFollowLag(RE::Actor* a_pet, float a_moveLag, float a_turnLag);

	// DLL version as a single number: major * 10000 + minor * 100 + patch (10200 for 1.2.0). Lets a
	// consumer check that a function exists before calling it; IsInstalled stays the presence probe.
	std::int32_t GetVersion();

	// Called EVERY FRAME from the PlayerCharacter::Update hook (see hook.cpp).
	// a_delta: time elapsed since the previous frame, used by the transitions.
	void UpdateAll(float a_delta);

	// Exposes the API to Papyrus (script "Piggyback").
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
