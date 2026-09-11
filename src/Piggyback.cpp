#include "Piggyback.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace
{
	// Entry and exit transition durations, in seconds. Without them the actor pops onto the host when
	// attached, and drops abruptly when released. These durations keep the transition readable.
	constexpr float kBlendIn = 0.90f;
	constexpr float kBlendOut = 0.80f;

	// Height of the arc travelled during a transition. Plain linear interpolation gives a mechanical
	// straight line; a vertical arc (zero at both ends, highest in the middle) reads as the creature
	// lifting off and settling down, which is far more natural for anything that floats.
	constexpr float kArcIn = 45.0f;
	constexpr float kArcOut = 28.0f;
	constexpr float kPi = 3.14159265f;
	constexpr float kTwoPi = 6.28318531f;

	// How fast the rider's heading chases the host's. Higher is stiffer; 8.0 is roughly 125 ms of lag,
	// enough to turn a snap into a sweep without the rider feeling detached from the host.
	constexpr float kYawRate = 8.0f;

	// How fast the applied offset chases a new one set through SetOffset. Slower than the yaw on
	// purpose: this is a settings change, and watching the rider slide to its new spot is the point.
	constexpr float kOffsetRate = 5.0f;

	// Minimum height above the host's feet. Keeps the rider from sinking into the ground (crouching,
	// sloped terrain), which would have the physics engine eject it over and over: that reads as jitter.
	constexpr float kMinHeight = 25.0f;

	// Sprint threshold, taken from values observed in the log in game: around 360 running, 500 sprinting.
	constexpr float kSprintSpeed = 450.0f;

	// Distance behind the host at which the rider is set down when released.
	constexpr float kReleaseDistance = 60.0f;

	// The rider is set down a little above the host's feet rather than exactly level with them.
	// Landing a capsule flush with the ground is fragile: a step, a slope or a slightly different
	// capsule centre puts it inside the floor, and an actor that starts inside the floor is as likely
	// to be pushed through it as out of it. A few units of clearance costs an imperceptible drop.
	constexpr float kReleaseClearance = 12.0f;

	// Height of the anchor node above the feet on a standard humanoid, in Skyrim units. This is the
	// build offsets are authored against; anything else is scaled relative to it.
	//
	// Measured in game rather than assumed: "NPC Spine2 [Spn2]" sits 89.6 units above the feet on an
	// unscaled humanoid, and tracks scale exactly (44.7 at setscale 0.5, 134.3 at 1.5). The first
	// version of this used a round 100, which quietly shrank every offset by 10% at default size.
	//
	// Calibrated for the spine nodes, which are the recommended anchors. A markedly higher or lower
	// node (the head, for instance) shifts the scale by the same ratio, so offsets authored against it
	// should be calibrated on a standard-sized character.
	constexpr float kReferenceNodeHeight = 89.6f;
	constexpr float kMinHostScale = 0.5f;
	constexpr float kMaxHostScale = 2.0f;

	// A change in the host's reported scale beyond this re-triggers the measurement mid-carry.
	constexpr float kScaleChangeEpsilon = 0.01f;

	// Name of a creature's "charge" animation event: unknown, and different from one species to the next.
	// NotifyAnimationGraph returns true when the graph RECOGNISES the event, so we try the usual
	// candidates and keep the first one accepted, logging which. Auto discovery rather than guesswork.
	constexpr const char* kSprintEvents[] = { "SprintStart", "sprintStart", "moveStartSprint", "MTStartSprint" };

	// --- Adaptive repositioning -------------------------------------------------------------------
	// We do not PREDICT the obstacle (havok raycasting is low level, risky plumbing): we DETECT it. If,
	// on the next frame, the actor is far from the position we asked for, the engine pushed it back, so
	// the spot is occupied. We then pull it in towards the host, and let it move back out to its nominal
	// distance as soon as the space frees up.
	// KNOWN DEAD CODE: the rider's character collisions are disabled while carried, so the engine never
	// pushes it back and the measured gap stays at zero. Harmless, and deliberately left in place until
	// the predictive raycast replaces it.
	constexpr float kBlockedTol = 12.0f;   // gap beyond which the spot is considered blocked
	constexpr float kPullInRate = 2.5f;    // pull-in speed towards the host (fraction per second)
	constexpr float kRestoreRate = 0.8f;   // return to nominal distance, slower so it feels less nervous
	constexpr float kMinScale = 0.15f;     // never collapse completely onto the host

	struct AttachData
	{
		RE::FormID        host{ 0 };
		RE::BSFixedString node;
		bool              matchRotation{ true };

		// offset is what is actually applied; offsetTarget is what the consumer asked for. The first
		// chases the second, so a live setting change slides the rider into place without teleporting.
		RE::NiPoint3 offset;
		RE::NiPoint3 offsetTarget;

		// Collision layer the rider's proxy had before it was carried, restored when it is set down.
		std::uint32_t savedLayer{ 0 };
		bool          hasSavedLayer{ false };
		bool          collisionReleased{ false };  // stop re-suppressing once it has been handed back

		// Transition: interpolate from 'fromPos' towards the target over 'blend' seconds.
		RE::NiPoint3 fromPos;
		float        blend{ 0.0f };
		bool         detaching{ false };
		int          moveState{ 0 };  // 0 = idle, 1 = moving, 2 = sprinting (so events fire on change only)

		// Adaptive repositioning: fraction of the configured distance actually applied.
		// 1.0 = nominal position; reduced when the spot is blocked (wall, rock).
		float        distScale{ 1.0f };
		RE::NiPoint3 lastTarget;
		bool         hasLastTarget{ false };

		// Host build factor, measured from the host's own geometry (see MeasureHostScale), plus the
		// reported scale at the time of that measurement so a resize mid-carry can be noticed.
		float hostScale{ 1.0f };
		bool  hostScaleMeasured{ false };
		float scaleAtMeasure{ 1.0f };

		// Heading actually used by the rig, chasing the host's real yaw with a fixed lag.
		float smoothedYaw{ 0.0f };
		bool  yawInitialised{ false };
	};

	// NOTE - a post-release trace (position and angles sampled for several seconds after each drop)
	// lived here while the "rider falls through the floor" bug was being chased. It earned its keep:
	// it showed the fall was gradual rather than instant, which ruled out tunnelling and redirected
	// the search. Removed once the bug was closed - twenty lines per dismount is not something to
	// leave in every player's log. Worth rebuilding the same way if another release-time bug appears.

	std::unordered_map<RE::FormID, AttachData> g_attached;
	std::mutex                                 g_mutex;
	float                                      g_logTimer{ 0.0f };  // throttle for the diagnostic log

	// Smoothstep easing: gentle start and arrival, no sudden change of speed.
	inline float Smooth(float t)
	{
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		return t * t * (3.0f - 2.0f * t);
	}

	// Wraps an angle into [-pi, pi]. Without it, a turn across the +/-180 degree seam reads as a nearly
	// full circle in the wrong direction, because the raw difference between two yaws either side of
	// the seam is huge while the real movement is small.
	inline float WrapPi(float a_angle)
	{
		return std::remainder(a_angle, kTwoPi);
	}

	inline RE::NiPoint3 Lerp(const RE::NiPoint3& a, const RE::NiPoint3& b, float t)
	{
		return RE::NiPoint3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	}

	// Transition path: eased interpolation plus a vertical arc. sin(pi*t) is 0 at both ends and 1 in the
	// middle, so the actor rises then comes back down exactly onto its target, with no jolt.
	inline RE::NiPoint3 ArcPath(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_t, float a_arc)
	{
		const float t = a_t < 0.0f ? 0.0f : (a_t > 1.0f ? 1.0f : a_t);
		RE::NiPoint3 pos = Lerp(a_from, a_to, Smooth(t));
		pos.z += a_arc * std::sin(kPi * t);
		return pos;
	}

	// --- Taking the rider out of the collision simulation -------------------------------------------
	//
	// While carried, the rider's character controller is teleported every frame. Whenever that capsule
	// overlaps another one, havok resolves the penetration by pushing the OTHER body away - one way
	// only, because a teleported proxy never yields to a simulated one. That is the lateral drift
	// players reported on the host, the jolt everyone gets when the host jumps or brakes out of a run
	// (the anchor bone is animated, so the target lurches towards the host during those peaks), and the
	// NPCs a carried creature shoves as it passes through them.
	//
	// Two things had to be understood the hard way here:
	//
	// 1. SetCollision(false) does not cover it. It acts on the reference's 3D, not on the
	//    actor-to-actor proxy. Neither does the ghost flag, which affects targeting and combat rather
	//    than the simulation: a permanently ghosted creature still pushes.
	//
	// 2. CHARACTER_FLAGS::kNoCharacterCollisions is not enough either, and it is worth spelling out
	//    why, because it looks like the obvious answer. That flag governs how a controller resolves
	//    collisions when IT integrates. The rider never integrates, it is teleported. The host does
	//    integrate, and it consults its OWN flag, not the rider's - so it still walks into the rider
	//    and gets pushed out. Measured in game: with the flag alone, the jolt on jumps and stops was
	//    gone but a close-in rider still shoved the host, and still shoved NPCs.
	//
	// What actually works is to take the rider's proxy out of collision filtering altogether, by
	// putting its phantom on the kNonCollidable layer. A carried actor needs no collision at all: it
	// is placed by hand every frame, so it does not need the ground, walls, or anything else. The
	// original layer is saved and restored when it is set down.
	//
	// Both are applied every frame rather than once on attach, because the character controller can be
	// recreated (cell change, resurrection). It costs nothing, and nothing is written to the save, so
	// an interrupted session always comes back clean.


	// The rider's physical body in the havok world.
	//
	// CommonLib 7 names controller virtual 0x10 GetRigidBody. Use its declared interface;
	// do not assume every actor has a bhkCharProxyController. A missing body is handled below.
	RE::hkpWorldObject* GetCollisionBody(RE::Actor* a_actor)
	{
		auto* cc = a_actor->GetCharController();
		return cc ? cc->GetRigidBody() : nullptr;
	}

	// Current collision layer of the rider, or -1 when it cannot be read.
	int GetProxyLayer(RE::Actor* a_actor)
	{
		auto* body = GetCollisionBody(a_actor);
		if (!body) {
			return -1;
		}
		return static_cast<int>(body->GetCollidableRW()->broadPhaseHandle.collisionFilterInfo.GetCollisionLayer());
	}

	// Moves the rider to a_layer. Returns false when there is no body to act on.
	bool SetProxyLayer(RE::Actor* a_actor, RE::COL_LAYER a_layer)
	{
		auto* body = GetCollisionBody(a_actor);
		if (!body) {
			return false;
		}
		auto& info = body->GetCollidableRW()->broadPhaseHandle.collisionFilterInfo;
		info.SetCollisionLayer(a_layer);
		return true;
	}

	void SetCharacterCollisions(RE::Actor* a_actor, bool a_enabled)
	{
		if (auto* cc = a_actor->GetCharController()) {
			const bool suppress = !a_enabled;
			cc->flags.set(suppress, RE::CHARACTER_FLAGS::kNoCharacterCollisions);
		}
	}

	// Makes the HOST immovable by other characters for the duration of the ride.
	//
	// This is the other half of the answer, and the half that finally lets the rider keep its own
	// collision. Suppressing collision on the rider stopped the pushing, but moving its proxy onto the
	// non-collidable layer while teleporting it across the world left havok's broadphase holding stale
	// information about where that body was: on release it re-entered the simulation misplaced, and
	// the creature settled below the floor.
	//
	// The tell came from testing: walking the creature to a spot on its own feet, then riding and
	// dismounting there, never failed - while arriving at the same spot carried, failed. What differs
	// is not the place, it is whether the body travelled through the world as a live collision object
	// or as a disabled one.
	//
	// kNotPushable acts on the host instead, so the rider stays a normal, fully simulated actor from
	// start to finish and there is nothing to re-synchronise when it is set down. The host simply
	// cannot be shoved while carrying, which is exactly the reported bug.
	void SetHostPushable(RE::Actor* a_host, bool a_pushable)
	{
		if (auto* cc = a_host->GetCharController()) {
			cc->flags.set(!a_pushable, RE::CHARACTER_FLAGS::kNotPushable);
			// kNotPushablePermanent is the stronger of the two: kNotPushable alone was measured to
			// hold for glancing contact but give way under a deep overlap, which is exactly the case
			// when a mod places the rider right on top of the host.
			cc->flags.set(!a_pushable, RE::CHARACTER_FLAGS::kNotPushablePermanent);
		}
	}

	// NOTE - kNoSim was tried here and removed in 1.1.1. It froze the rider's controller instead of
	// only suppressing collision, with two consequences: the angle written on the reference each frame
	// was no longer applied, so the rider kept whatever orientation it had when picked up, and
	// freezing then unfreezing the controller of a floating creature (which, unlike a humanoid, may
	// pitch and roll) left it spinning about a horizontal axis afterwards. It also never delivered
	// what it was added for - deep-overlap pushing was unchanged with it in place - so removing it
	// costs nothing. Do not reintroduce it to suppress collision; use the host-side flags above.

	// Reads back what is actually set, so a report says what the engine holds rather than what the
	// code intended. Bit 14 kNotPushable, 17 kNoSim, 27 kNoCharacterCollisions, 28 kNotPushablePerm.
	std::uint32_t GetControllerFlags(RE::Actor* a_actor)
	{
		auto* cc = a_actor->GetCharController();
		return cc ? cc->flags.underlying() : 0u;
	}

	// Puts a knocked-down actor back on its feet, if it is down.
	//
	// A creature that gets knocked over and never plays its get-up animation stays down, and that
	// state is written to the save: the mesh lies on its side and rolls, while the actor's own pitch
	// and roll read exactly zero. It looks like a rotation bug, it is not one - and it survives
	// reloading, which is what makes it so confusing to diagnose.
	//
	// The rig calls this when taking control and when handing it back. A carried actor has no
	// business being in a downed state, and releasing one in that state would strand it there with
	// no way out short of console commands. PotentiallyFixRagdollState is the engine's own recovery
	// path, so this asks the game to sort it out rather than forcing anything.
	void RecoverFromKnockdown(RE::Actor* a_actor)
	{
		const auto knock = a_actor->AsActorState()->GetKnockState();
		if (knock == RE::KNOCK_STATE_ENUM::kNormal && !a_actor->IsInRagdollState()) {
			return;  // nothing to do, and nothing touched in the overwhelmingly common case
		}

		SKSE::log::info("[knockdown] pet {:08X} is down (knock={}, ragdoll={}), asking the engine to recover it",
			a_actor->GetFormID(), static_cast<std::uint32_t>(knock), a_actor->IsInRagdollState());
		a_actor->PotentiallyFixRagdollState();
		a_actor->NotifyAnimationGraph("GetUpBegin");
	}

	// Keeps the rider's controller from simulating a fall while it is being held in the air.
	//
	// A carried actor is placed by hand every frame, but its character controller does not know that:
	// it sees an actor with no ground under it, so gravity accumulates a downward velocity and the
	// fall timer keeps running for as long as the ride lasts. Released after a while, the controller
	// therefore starts with a large downward speed and tunnels straight through the floor before
	// collision can catch it - the "she falls through the map" report, which survived every collision
	// fix because it was never a collision problem.
	//
	// Zeroing both every frame keeps the controller in step with where the actor actually is. It also
	// rules out fall damage on dismount, which the same accumulated fall would otherwise justify.
	void NeutraliseControllerMotion(RE::Actor* a_actor)
	{
		if (auto* cc = a_actor->GetCharController()) {
			cc->SetLinearVelocityImpl(RE::hkVector4());
			cc->fallStartHeight = 0.0f;
			cc->fallTime = 0.0f;
		}
	}

	// Gives the rider everything back: its 3D collision, its character collisions, and the collision
	// layer it had before being picked up. Called on release and on every eviction path, so an
	// attachment can never leave an actor stuck as a ghost.
	void RestoreCollision(RE::Actor* a_pet, AttachData& a_data)
	{
		a_pet->SetCollision(true);
		SetCharacterCollisions(a_pet, true);

		if (auto* host = RE::TESForm::LookupByID<RE::Actor>(a_data.host)) {
			SetHostPushable(host, true);
		}

		// Hand the actor back upright. Its own yaw is kept; only pitch and roll are cleared, which is
		// the correct resting orientation for an actor on the ground and costs nothing when it was
		// already level. This also recovers a creature left tilted or spinning by an earlier build.
		const RE::NiPoint3 angle = a_pet->GetAngle();
		a_pet->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, angle.z });

		// And never release an actor that is still down.
		RecoverFromKnockdown(a_pet);

		// Safety net for saves that rode through an earlier build, which did move the rider onto the
		// non-collidable layer. Nothing sets that layer any more, but an actor left on it would fall
		// through the world forever, so put it back on the layer an actor normally sits on.
		if (GetProxyLayer(a_pet) == static_cast<int>(RE::COL_LAYER::kNonCollidable)) {
			SetProxyLayer(a_pet, RE::COL_LAYER::kCharController);
			SKSE::log::warn("Detach: pet {:08X} was on the non-collidable layer, restoring kCharController.",
				a_pet->GetFormID());
		}
	}

	// How big is the host, measured from its own geometry rather than from GetScale().
	//
	// Offsets are authored in units that read well on a standard humanoid. On a smaller character the
	// very same distance is a much larger share of the body, and a vertical offset meant for the knees
	// ends up at the heels: the creature reads as "trailing behind" instead of "carried". That is what
	// a player on a shrunken character reported, and it is what led them to move the creature closer,
	// straight into the host's capsule.
	//
	// GetScale() is not usable here: it does not reflect the real size depending on how the character
	// was resized (RaceMenu, a race mod, the setscale console command). The anchor node's height above
	// the feet does, whatever the method used.
	float MeasureHostScale(RE::Actor* a_host, RE::NiAVObject* a_node)
	{
		const float nodeHeight = a_node->world.translate.z - a_host->GetPosition().z;
		// Clamped rather than rejected: attaching mid-crouch, an unusual anchor node or a non humanoid
		// host must not produce an absurd offset, but the rig still has to work.
		return std::clamp(nodeHeight / kReferenceNodeHeight, kMinHostScale, kMaxHostScale);
	}

	// Target position = node position + offset expressed in the host's FACING space.
	// Deliberately intuitive and predictable: X = right, Y = forward (negative = behind), Z = up. We do
	// NOT use the bone's local space: its axes cannot be reasoned about reliably, which would make any
	// setting (and any FOMOD option) impossible to think through.
	// GetAngleZ() returns RADIANS on the C++ side (unlike Papyrus, which gives degrees).
	RE::NiPoint3 ComputeTarget(const RE::NiPoint3& a_nodePos, float a_hostYaw, const RE::NiPoint3& a_offset)
	{
		const float s = std::sin(a_hostYaw);
		const float c = std::cos(a_hostYaw);
		// Skyrim heading: forward = (sin, cos, 0), right = (cos, -sin, 0).
		return RE::NiPoint3{
			a_nodePos.x + a_offset.x * c + a_offset.y * s,
			a_nodePos.y - a_offset.x * s + a_offset.y * c,
			a_nodePos.z + a_offset.z
		};
	}
}

namespace Piggyback
{
	bool Attach(RE::Actor* a_pet, RE::Actor* a_host, RE::BSFixedString a_node,
		float a_x, float a_y, float a_z, bool a_matchRotation)
	{
		if (!a_pet || !a_host) {
			SKSE::log::warn("Attach refused: invalid actor (pet or host is null).");
			return false;
		}

		std::lock_guard lock(g_mutex);
		AttachData data{};

		// Carry over what we already know about this pet's original collision layer. The layer change
		// outlives the AttachData: re-attaching before a detach transition has finished (or simply
		// re-attaching an already carried pet) replaces the entry, and starting from scratch would
		// lose the only record of what to restore. The pet would then be released still
		// non-collidable and fall through the world - which is exactly what happened in testing.
		if (const auto existing = g_attached.find(a_pet->GetFormID()); existing != g_attached.end()) {
			data.savedLayer = existing->second.savedLayer;
			data.hasSavedLayer = existing->second.hasSavedLayer;
		}

		data.host = a_host->GetFormID();
		data.node = a_node;
		data.offset = RE::NiPoint3{ a_x, a_y, a_z };
		data.offsetTarget = data.offset;  // no smoothing on attach, the entry transition covers it
		data.matchRotation = a_matchRotation;
		data.fromPos = a_pet->GetPosition();  // starting point of the entry transition
		data.blend = 0.0f;
		data.detaching = false;
		g_attached[a_pet->GetFormID()] = data;

		// Disable collision while carried: otherwise the rider's physical body slams into the host's
		// (seen on jumps). Restored at the end of the detach transition.
		a_pet->SetCollision(false);

		// Pick up a creature that is on its feet, even if it was lying down when asked. Attaching a
		// downed actor would carry the downed pose along for the whole ride.
		RecoverFromKnockdown(a_pet);

		// State of the rider's collision proxy, logged once so a bug report says whether the rig could
		// act on it at all rather than leaving it to be guessed.
		SKSE::log::info("Attach: pet {:08X} -> host {:08X}, node '{}', offset ({}, {}, {}), matchRot={}",
			a_pet->GetFormID(), a_host->GetFormID(), a_node.c_str(), a_x, a_y, a_z, a_matchRotation);
		// Knock state and ragdoll are logged on attach because a creature that was left knocked down
		// stays that way across saves: the mesh lies on its side while the actor's own angles read
		// perfectly upright, which looks exactly like a rotation bug and is not one.
		SKSE::log::info("[collision] pet {:08X}: charController={}, havok body={}, layer={}, knock={}, ragdoll={}",
			a_pet->GetFormID(),
			a_pet->GetCharController() ? "yes" : "NO",
			GetCollisionBody(a_pet) ? "yes" : "NO",
			GetProxyLayer(a_pet),
			static_cast<std::uint32_t>(a_pet->AsActorState()->GetKnockState()),
			a_pet->IsInRagdollState());
		return true;
	}

	bool SetOffset(RE::Actor* a_pet, float a_x, float a_y, float a_z)
	{
		if (!a_pet) {
			return false;
		}
		std::lock_guard lock(g_mutex);
		auto it = g_attached.find(a_pet->GetFormID());
		if (it == g_attached.end() || it->second.detaching) {
			return false;  // not attached (or detaching): nothing to update
		}
		// Only the target is touched: neither fromPos nor blend, so no entry transition is restarted.
		// UpdateAll eases the applied offset towards this target, so the rider slides to its new spot
		// over a fraction of a second instead of jumping there.
		it->second.offsetTarget = RE::NiPoint3{ a_x, a_y, a_z };
		return true;
	}

	bool Detach(RE::Actor* a_pet)
	{
		if (!a_pet) {
			return false;
		}
		std::lock_guard lock(g_mutex);
		auto it = g_attached.find(a_pet->GetFormID());
		if (it == g_attached.end()) {
			return false;
		}
		if (it->second.detaching) {
			return true;  // already detaching
		}

		// Not removed straight away: the actor is set down behind the host over kBlendOut seconds,
		// otherwise it is released in mid-air and falls hard.
		it->second.detaching = true;
		it->second.blend = 0.0f;
		it->second.fromPos = a_pet->GetPosition();

		// Collision comes back NOW, at the start of the exit, not partway through and not at the end.
		// For the whole descent the actor is a normal, solid actor that the rig merely carries down to
		// the ground - so by the time it is let go, it has been colliding normally for most of a
		// second and there is nothing left to re-synchronise.
		//
		// Handing it back late was tried twice (at the end, then halfway) and neither worked: the log
		// showed the layer correctly restored to 30 while the creature still ended up under the floor.
		// The lesson was that the moment of handover was the problem, not the handover itself.
		it->second.collisionReleased = true;
		RestoreCollision(a_pet, it->second);

		// Put the animation graph back to idle before handing the actor over.
		//
		// While carried, the rig forwards the host's movement and sprint events so the creature plays
		// its walk instead of standing frozen. Releasing it mid-stride left the graph running a
		// locomotion animation for a creature that is no longer moving anywhere - and an animation
		// playing on the spot moves the MESH while the actor's own pitch and roll stay at zero. That
		// is what "spinning like a fairground ride" was: not the actor turning, but an animation left
		// running. Measured: pitch and roll logged at 0.0 throughout, only the mesh appeared to turn.
		//
		// Same reasoning as the SprintStop already sent on state changes, applied to the one state
		// change that had been overlooked: the end of the ride.
		if (it->second.moveState == 2) {
			a_pet->NotifyAnimationGraph("SprintStop");
		}
		a_pet->NotifyAnimationGraph("moveStop");
		it->second.moveState = 0;

		SKSE::log::info("Detach: pet {:08X} (exit transition, collision restored to layer {}, animation reset)",
			a_pet->GetFormID(), GetProxyLayer(a_pet));
		return true;
	}

	bool IsAttached(RE::Actor* a_pet)
	{
		if (!a_pet) {
			return false;
		}
		std::lock_guard lock(g_mutex);
		auto it = g_attached.find(a_pet->GetFormID());
		return it != g_attached.end() && !it->second.detaching;
	}

	bool IsInstalled()
	{
		return true;  // this call resolving at all proves the DLL is loaded
	}

	void UpdateAll(float a_delta)
	{
		std::lock_guard lock(g_mutex);
		if (g_attached.empty()) {
			return;  // overwhelmingly the common case: return immediately, negligible per-frame cost
		}

		for (auto it = g_attached.begin(); it != g_attached.end();) {
			auto&       data = it->second;
			auto* const pet = RE::TESForm::LookupByID<RE::Actor>(it->first);
			auto* const host = RE::TESForm::LookupByID<RE::Actor>(data.host);

			if (!pet || !host) {
				// The pair is gone. If the rider is still resolvable, hand its collision back before
				// dropping the entry, otherwise it stays a ghost nothing can bump into.
				if (pet) {
					RestoreCollision(pet, data);
				}
				it = g_attached.erase(it);
				continue;
			}

			// The rider keeps its own collision layer throughout - see SetHostPushable for why moving
			// it off the collidable layer had to be abandoned. Only two things are suppressed: its
			// character-to-character resolution, and the host's ability to be pushed.
			if (!data.collisionReleased) {
				SetCharacterCollisions(pet, false);
				SetHostPushable(host, false);
			}

			// Only while actually carried. During the descent the actor is solid again and settling
			// onto the ground on its own; zeroing its velocity every frame there would fight the very
			// landing we want it to make.
			if (!data.detaching) {
				NeutraliseControllerMotion(pet);
			}

			// 3D not loaded (cell unloaded, different worldspace...): touch nothing this frame, the
			// attachment picks itself back up on return.
			auto* const host3D = host->Get3D();
			if (!host3D) {
				++it;
				continue;
			}
			auto* const node = host3D->GetObjectByName(data.node);
			if (!node) {
				++it;
				continue;
			}

			// Measured once, on the first frame where the anchor node is available, and never per
			// frame: the anchor bone is animated (that is the very reason for the collision jolts), so
			// a continuous measurement would make the offset breathe.
			// Re-measured if the host is resized while carrying, so growing or shrinking mid-carry
			// lands in the same place as detaching, resizing, and picking the rider back up. Keyed on
			// the reported scale rather than on the node height, which moves with every animation.
			if (data.hostScaleMeasured &&
				std::fabs(host->GetScale() - data.scaleAtMeasure) > kScaleChangeEpsilon) {
				data.hostScaleMeasured = false;
			}

			if (!data.hostScaleMeasured) {
				data.hostScale = MeasureHostScale(host, node);
				data.hostScaleMeasured = true;
				data.scaleAtMeasure = host->GetScale();
				// Raw numbers as well as the result: if the scaling ever looks wrong, this says whether
				// the measurement is off or the host's geometry simply is not what we assume.
				SKSE::log::info(
					"[scale] pet {:08X}: node z={:.1f}, host feet z={:.1f}, height={:.1f} -> scale={:.2f} (GetScale reports {:.2f})",
					it->first, node->world.translate.z, host->GetPosition().z,
					node->world.translate.z - host->GetPosition().z, data.hostScale, host->GetScale());
			}

			// Ease the applied offset towards the one the consumer asked for, so a live settings change
			// slides the rider across instead of teleporting it.
			{
				const float k = 1.0f - std::exp(-kOffsetRate * a_delta);
				data.offset.x += (data.offsetTarget.x - data.offset.x) * k;
				data.offset.y += (data.offsetTarget.y - data.offset.y) * k;
				data.offset.z += (data.offsetTarget.z - data.offset.z) * k;
			}

			// Verdict on the previous frame: did the engine push the actor back? Compare its CURRENT
			// position (so, after physics resolution) with the one we asked for. This has to happen
			// here, at the start of the frame, before we reposition it.
			if (data.hasLastTarget && !data.detaching && data.blend >= 1.0f) {
				const RE::NiPoint3 actual = pet->GetPosition();
				const float dx = actual.x - data.lastTarget.x;
				const float dy = actual.y - data.lastTarget.y;
				const float dz = actual.z - data.lastTarget.z;
				if (std::sqrt(dx * dx + dy * dy + dz * dz) > kBlockedTol) {
					data.distScale -= kPullInRate * a_delta;  // blocked: fall back towards the host
					if (data.distScale < kMinScale) {
						data.distScale = kMinScale;
					}
				} else if (data.distScale < 1.0f) {
					data.distScale += kRestoreRate * a_delta;  // clear: return to nominal distance
					if (data.distScale > 1.0f) {
						data.distScale = 1.0f;
					}
				}
			}

			// The rig follows a SMOOTHED heading, never the host's instant yaw. Rebuilding both the ride
			// position and the rider's angle from the instant yaw meant a fast mouse turn moved the
			// target across the whole arc around the host in a single frame: the rider did not turn
			// with the host, it was re-placed, which players described as teleporting.
			// k = 1 - exp(-rate * delta) is framerate independent: the same visible lag at 40 fps and
			// at 144 fps. Both WrapPi calls matter: the inner one takes the short way round the seam,
			// the outer one keeps the stored angle from drifting out of range over a long session.
			const float rawYaw = host->GetAngleZ();
			if (!data.yawInitialised) {
				data.smoothedYaw = rawYaw;  // start on the truth, so there is no catch-up on frame one
				data.yawInitialised = true;
			} else {
				const float k = 1.0f - std::exp(-kYawRate * a_delta);
				data.smoothedYaw = WrapPi(data.smoothedYaw + WrapPi(rawYaw - data.smoothedYaw) * k);
			}
			const float hostYaw = data.smoothedYaw;

			// The offset is scaled to the host's build, so one setting reads the same on a slight
			// Breton and on an Orc. Adaptive repositioning only reduces the horizontal components: the
			// height stays as configured.
			RE::NiPoint3 effOffset = data.offset;
			effOffset.x *= data.distScale * data.hostScale;
			effOffset.y *= data.distScale * data.hostScale;
			effOffset.z *= data.hostScale;
			RE::NiPoint3 ridePos = ComputeTarget(node->world.translate, hostYaw, effOffset);

			// Vertical guard (from "she jitters when I crouch" plus a screenshot of the creature sunk
			// into the ground). The anchor node drops when the host crouches: a fixed vertical offset
			// then sends the target BELOW the terrain, and havok ejects the actor over and over, which
			// reads as jitter. Never go below the host's feet plus a margin. An actor's GetPosition().z
			// is at foot height, so it is a reliable reference, including on slopes.
			// Scaled with the host, like the offset: on a character half the size, a fixed 25-unit
			// floor would sit proportionally twice as high and quietly override the configured
			// position, making it look as though scaling had no effect.
			const float minZ = host->GetPosition().z + kMinHeight * data.hostScale;
			if (ridePos.z < minZ) {
				ridePos.z = minZ;
			}

			// Immersion: the rider is moved BY FORCE, so its animation graph believes it is standing
			// still and keeps it in idle even while the host sprints. We try to copy the carrier's speed
			// across. Both calls return a bool, and both are logged, so we know WHICH one fails (does
			// the variable not exist on the host? on the pet?) rather than assuming.
			float      hostSpeed = 0.0f;
			const bool gotSpeed = host->GetGraphVariableFloat("Speed", hostSpeed);
			const bool setSpeed = gotSpeed ? pet->SetGraphVariableFloat("Speed", hostSpeed) : false;

			// The log showed that getSpeed AND setSpeed both succeed, with correct values (0 idle, ~360
			// running, 500 sprinting) - and yet the creature stays in idle. Conclusion: its graph does
			// NOT use "Speed" to drive locomotion. So we go through animation EVENTS instead, sent only
			// on a state change (not every frame, which would restart the animation in a loop).
			// Once detaching, the rider is on its way back to the ground and is about to be its own
			// actor again: it must not be told to keep walking. Locomotion events are frozen here and
			// the graph is returned to idle by Detach itself.
			int state = 0;
			if (!data.detaching) {
				if (hostSpeed > kSprintSpeed) {
					state = 2;
				} else if (hostSpeed > 1.0f) {
					state = 1;
				}
			}

			if (!data.detaching && state != data.moveState) {
				const int previous = data.moveState;
				data.moveState = state;

				// Leave the sprint state cleanly before any other transition: without this the graph can
				// stay stuck in its sprint animation. (Creature animation names are in plain text inside
				// the BSAs, no need to look them up online.)
				if (previous == 2) {
					pet->NotifyAnimationGraph("SprintStop");
				}

				if (state == 0) {
					pet->NotifyAnimationGraph("moveStop");
				} else if (state == 1) {
					pet->NotifyAnimationGraph("moveStart");
				} else {
					// Sprinting: look for the creature's charge animation (see kSprintEvents).
					bool accepted = false;
					for (auto* evt : kSprintEvents) {
						if (pet->NotifyAnimationGraph(evt)) {
							SKSE::log::info("[anim] sprint event accepted: '{}'", evt);
							accepted = true;
							break;
						}
					}
					if (!accepted) {
						pet->NotifyAnimationGraph("moveStart");  // fallback: at least the movement animation
						SKSE::log::info("[anim] no sprint event accepted, falling back to moveStart");
					}
				}
			}

			// One diagnostic line per second. Deliberately kept in the released build: this is what made
			// it possible to diagnose a player's drift report remotely, from their log alone.
			g_logTimer += a_delta;
			if (g_logTimer >= 1.0f) {
				g_logTimer = 0.0f;
				// One line, everything a bug report needs. Physics state is read back FROM THE ENGINE
				// rather than echoed from what the code wrote: a flag the game clears behind us is
				// invisible otherwise, and that distinction has already resolved three bugs here.
				// Pitch and roll matter in particular - held at zero every frame while carrying, so a
				// creature that looks tilted while these read zero is a mesh or animation problem, not
				// an orientation one.
				const RE::NiPoint3  petAngle = pet->GetAngle();
				const std::uint32_t hostFlags = GetControllerFlags(host);
				SKSE::log::info(
					"[diag] yaw={:.1f}deg (raw {:.1f}) scale={:.2f} | target=({:.1f}, {:.1f}, {:.1f}) pet=({:.1f}, {:.1f}, {:.1f}) | pitch={:.1f} roll={:.1f} | speed={:.0f} ({}/{}) | hostNotPushable={} layer={} knock={} ragdoll={}",
					hostYaw * 57.2957795f, rawYaw * 57.2957795f, data.hostScale,
					ridePos.x, ridePos.y, ridePos.z,
					pet->GetPosition().x, pet->GetPosition().y, pet->GetPosition().z,
					petAngle.x * 57.2957795f, petAngle.y * 57.2957795f,
					hostSpeed, gotSpeed, setSpeed,
					(hostFlags & (1u << 14)) != 0, GetProxyLayer(pet),
					static_cast<std::uint32_t>(pet->AsActorState()->GetKnockState()),
					pet->IsInRagdollState());
			}

			if (data.detaching) {
				// Exit: bring it gently back to the ground just behind the host, then release it.
				data.blend += a_delta / kBlendOut;
				const RE::NiPoint3 hostPos = host->GetPosition();
				const RE::NiPoint3 release = ComputeTarget(
					hostPos, hostYaw,
					RE::NiPoint3{ 0.0f, -kReleaseDistance * data.hostScale, kReleaseClearance });
				pet->SetPosition(ArcPath(data.fromPos, release, data.blend, kArcOut), true);

				if (data.blend >= 1.0f) {
					if (!data.collisionReleased) {
						RestoreCollision(pet, data);  // safety: Detach normally does this up front
					}

					const RE::NiPoint3 finalPos = pet->GetPosition();
					SKSE::log::info("Detach: pet {:08X} released at ({:.1f}, {:.1f}, {:.1f}), layer {}",
						it->first, finalPos.x, finalPos.y, finalPos.z, GetProxyLayer(pet));

					it = g_attached.erase(it);
					continue;
				}
				++it;
				continue;
			}

			// Entry: smooth glide from the starting position to the anchor point.
			RE::NiPoint3 target = ridePos;
			if (data.blend < 1.0f) {
				data.blend += a_delta / kBlendIn;
				target = ArcPath(data.fromPos, ridePos, data.blend, kArcIn);
			}

			// true = ALSO move the collision controller. Essential: that is what carries the actor's
			// real position. With false, the actor stayed at its original position and merely drifted
			// alongside the player, never actually coming along. Collision against the host (slamming
			// into it on jumps) is handled separately, by SetCollision(false) on attach.
			pet->SetPosition(target, true);
			data.lastTarget = target;  // reference for measuring, next frame, whether the engine pushed it
			data.hasLastTarget = true;

			if (data.matchRotation) {
				pet->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, hostYaw });
			}

			++it;
		}

	}

	// --- Papyrus bindings ------------------------------------------------------------------------

	namespace
	{
		bool PapyrusAttach(RE::StaticFunctionTag*, RE::Actor* a_pet, RE::Actor* a_host,
			RE::BSFixedString a_node, float a_x, float a_y, float a_z, bool a_matchRotation)
		{
			return Attach(a_pet, a_host, a_node, a_x, a_y, a_z, a_matchRotation);
		}

		bool PapyrusSetOffset(RE::StaticFunctionTag*, RE::Actor* a_pet, float a_x, float a_y, float a_z)
		{
			return SetOffset(a_pet, a_x, a_y, a_z);
		}

		bool PapyrusDetach(RE::StaticFunctionTag*, RE::Actor* a_pet)
		{
			return Detach(a_pet);
		}

		bool PapyrusIsAttached(RE::StaticFunctionTag*, RE::Actor* a_pet)
		{
			return IsAttached(a_pet);
		}

		bool PapyrusIsInstalled(RE::StaticFunctionTag*)
		{
			return IsInstalled();
		}
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("Attach", "Piggyback", PapyrusAttach);
		a_vm->RegisterFunction("SetOffset", "Piggyback", PapyrusSetOffset);
		a_vm->RegisterFunction("Detach", "Piggyback", PapyrusDetach);
		a_vm->RegisterFunction("IsAttached", "Piggyback", PapyrusIsAttached);
		a_vm->RegisterFunction("IsInstalled", "Piggyback", PapyrusIsInstalled);
		SKSE::log::info("Papyrus functions registered: Piggyback.Attach / SetOffset / Detach / IsAttached / IsInstalled.");
		return true;
	}
}
