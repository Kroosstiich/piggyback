#include "hook.h"

#include "Piggyback.h"

namespace
{
	// Per-frame hook: PlayerCharacter::Update is called by the engine every frame. This is what makes a
	// TRULY smooth attachment possible (Papyrus tops out at 10-20 Hz, which is why every earlier
	// script-driven follow attempt failed).
	//
	// The vfunc index 0xAD (= Actor::Update) is not a guess: it comes from TrueDirectionalMovement
	// (ersh1), a maintained CommonLibSSE-NG plugin that hooks this exact vfunc the same way. A wrong
	// index would crash the game, so do not change it without checking an equivalent source.
	struct PlayerCharacterUpdateHook
	{
		static void Update(RE::Actor* a_this, float a_delta)
		{
			_Update(a_this, a_delta);        // always call the original first
			Piggyback::UpdateAll(a_delta);   // then re-stick attached actors, once the host is up to date
		}

		static inline REL::Relocation<decltype(Update)> _Update;
	};
}

void Hooks::Install()
{
	// VR moved Actor::Update two slots down (CommonLib: RelocateVirtual(0x0AD, 0x0AF, ...)). Hooking
	// 0xAD there would replace an unrelated function. VR support is untested in game.
	const auto index = REL::Relocate<std::size_t>(0xAD, 0xAD, 0xAF);
	REL::Relocation<std::uintptr_t> playerCharacterVtbl{ RE::VTABLE_PlayerCharacter[0] };
	PlayerCharacterUpdateHook::_Update =
		playerCharacterVtbl.write_vfunc(index, PlayerCharacterUpdateHook::Update);
	SKSE::log::info("Per-frame hook installed (PlayerCharacter::Update, vfunc 0x{:X}).", index);
}
