#include "log.h"

#include "Piggyback.h"
#include "hook.h"

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		SKSE::log::info("kDataLoaded: ready.");
		break;
	default:
		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();
	SKSE::log::info("Runtime Skyrim {} | CommonLibSSE-NG 7.5.1",
		skse->RuntimeVersion().string("."));

	// Version read from the plugin declaration (generated from the CMake project version), never
	// hardcoded here: the two used to drift apart, and the log claimed a version that had not shipped.
	const auto* declaration = SKSE::PluginDeclaration::GetSingleton();
	SKSE::log::info("Piggyback v{}: attaches an actor to a skeleton node of another, frame by frame.",
		declaration->GetVersion().string("."));

	Hooks::Install();

	// Native functions exposed to the "Piggyback" Papyrus script.
	auto* papyrus = SKSE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(Piggyback::RegisterPapyrus)) {
		SKSE::log::critical("Failed to register the Papyrus functions.");
		return false;
	}

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	return true;
}
