#include "hook.h"

#include "Piggyback.h"

namespace
{
	// Hook per-frame : PlayerCharacter::Update est appele a chaque frame par le moteur. C'est ce qui
	// permet une attache VRAIMENT lisse (Papyrus plafonne a 10-20 Hz, d'ou l'echec des tentatives
	// precedentes de suivi par script).
	//
	// L'index de vfunc 0xAD (= Actor::Update) n'est pas devine : il est repris de TrueDirectionalMovement
	// (ersh1), plugin CommonLibSSE-NG maintenu qui hooke exactement ce vfunc de la meme facon. Un index
	// errone ferait planter le jeu, donc ne pas le modifier sans verifier sur une source equivalente.
	struct PlayerCharacterUpdateHook
	{
		static void Update(RE::Actor* a_this, float a_delta)
		{
			_Update(a_this, a_delta);        // toujours appeler l'original d'abord
			Piggyback::UpdateAll(a_delta);   // puis recoller les acteurs attaches, une fois l'hote a jour
		}

		static inline REL::Relocation<decltype(Update)> _Update;
	};
}

void Hooks::Install()
{
	REL::Relocation<std::uintptr_t> playerCharacterVtbl{ RE::VTABLE_PlayerCharacter[0] };
	PlayerCharacterUpdateHook::_Update =
		playerCharacterVtbl.write_vfunc(0xAD, PlayerCharacterUpdateHook::Update);
	SKSE::log::info("Hook per-frame installe (PlayerCharacter::Update, vfunc 0xAD).");
}
