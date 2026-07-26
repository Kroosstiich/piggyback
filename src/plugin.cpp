#include "log.h"

#include "Piggyback.h"
#include "hook.h"

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		SKSE::log::info("kDataLoaded : pret.");
		break;
	default:
		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();
	SKSE::log::info("Piggyback v0.2.0 : attache un acteur a un noeud du squelette d'un autre, par frame.");

	Hooks::Install();

	// Fonctions natives exposees au script Papyrus "Piggyback".
	auto* papyrus = SKSE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(Piggyback::RegisterPapyrus)) {
		SKSE::log::critical("Echec de l'enregistrement des fonctions Papyrus.");
		return false;
	}

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	return true;
}
