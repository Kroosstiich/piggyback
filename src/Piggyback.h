#pragma once

// Piggyback - attache un acteur a un noeud du squelette d'un autre acteur, image par image.
//
// Composant volontairement GENERIQUE (aucune reference a Velyn ni a son mod) : il est inclus dans
// VelynTheNetch pour l'instant, mais destine a etre publie separement pour que la communaute puisse
// s'en servir. Ne pas y introduire de logique specifique a un mod.
namespace Piggyback
{
	// Attache a_pet au noeud a_node de a_host. Le noeud ne sert que de POINT D'ORIGINE ; l'offset est
	// exprime dans le repere du cap de l'hote : X = droite, Y = avant (negatif = derriere), Z = haut.
	// Repere volontairement previsible, pour que la position soit reglable sans deviner l'orientation
	// des axes locaux d'un os. Une transition douce amene l'acteur jusqu'au point d'accroche.
	// Ecrase une attache precedente du meme pet. Retourne false si un acteur est invalide.
	bool Attach(RE::Actor* a_pet, RE::Actor* a_host, RE::BSFixedString a_node,
		float a_x, float a_y, float a_z, bool a_matchRotation);

	// Met a jour l'offset d'un pet DEJA attache, sans le detacher : la nouvelle position est prise en
	// compte des la frame suivante (le repositionnement per-frame lit l'offset courant). Permet a un mod
	// d'exposer un reglage de position "a chaud" (ex. curseurs MCM). Meme repere que Attach
	// (X = droite, Y = avant/negatif = derriere, Z = haut). false si le pet n'est pas attache.
	bool SetOffset(RE::Actor* a_pet, float a_x, float a_y, float a_z);

	// Detache a_pet : transition de sortie (repose au sol derriere l'hote) puis il repasse sous le
	// controle de son IA. false s'il n'etait pas attache.
	bool Detach(RE::Actor* a_pet);

	bool IsAttached(RE::Actor* a_pet);

	// Sonde de presence pour les mods consommateurs : si le DLL est installe, renvoie toujours true.
	// S'il est absent, la fonction native n'est pas enregistree -> l'appel Papyrus renvoie false (defaut).
	// Permet a un mod (ex. Velyn) de masquer proprement une option qui depend de Piggyback.
	bool IsInstalled();

	// Appele a CHAQUE FRAME depuis le hook PlayerCharacter::Update (voir hook.cpp).
	// a_delta : temps ecoule depuis la frame precedente, pour les transitions.
	void UpdateAll(float a_delta);

	// Expose Attach/Detach/IsAttached a Papyrus (script "Piggyback").
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
