#include "Piggyback.h"

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace
{
	// Duree des transitions d'entree et de sortie, en secondes. Sans elles, l'acteur "pop" d'un coup sur
	// l'hote a l'attache, et tombe comme une pierre au detachement.
	// Rallongees le 2026-07-22 (retour Kevin : "cela va trop vite et c'est une ligne droite, c'est pas
	// tres immersif") - une transition lisible vaut mieux qu'une transition rapide.
	constexpr float kBlendIn = 0.90f;
	constexpr float kBlendOut = 0.80f;

	// Hauteur de l'arc parcouru pendant la transition. Une interpolation lineaire pure donne une ligne
	// droite "mecanique" ; un arc vertical (nul aux deux extremites, maximal au milieu) donne l'impression
	// que la creature s'eleve puis se pose - bien plus naturel pour un netch qui flotte.
	constexpr float kArcIn = 45.0f;
	constexpr float kArcOut = 28.0f;
	constexpr float kPi = 3.14159265f;

	// Hauteur minimale au-dessus des pieds de l'hote : empeche le porte de s'enfoncer dans le sol
	// (accroupi, terrain en pente...) et donc les saccades dues a l'ejection par la physique.
	constexpr float kMinHeight = 25.0f;

	// Seuil de sprint, cale sur les valeurs relevees dans le log en jeu : ~360 en course, 500 en sprint.
	constexpr float kSprintSpeed = 450.0f;

	// Nom de l'evenement d'animation de "charge" d'une creature : inconnu et variable selon l'espece.
	// NotifyAnimationGraph renvoie true si le graphe RECONNAIT l'evenement -> on essaie les candidats
	// usuels et on garde le premier accepte, en le tracant. Auto-decouverte plutot que devinette.
	constexpr const char* kSprintEvents[] = { "SprintStart", "sprintStart", "moveStartSprint", "MTStartSprint" };

	// --- Repositionnement adaptatif ---------------------------------------------------------------
	// On ne PREDIT pas l'obstacle (raycast havok = plomberie bas niveau et risquee) : on le DETECTE.
	// Si, a la frame suivante, l'acteur se retrouve loin de la position qu'on lui avait demandee, c'est
	// que le moteur l'a repousse -> l'emplacement est occupe. On le rapproche alors de l'hote, puis on
	// le laisse revenir a sa distance nominale des que la place se libere.
	constexpr float kBlockedTol = 12.0f;   // ecart au-dela duquel on considere l'emplacement obstrue
	constexpr float kPullInRate = 2.5f;    // vitesse de repli vers l'hote (fraction/seconde)
	constexpr float kRestoreRate = 0.8f;   // retour a la distance nominale, plus lent = moins nerveux
	constexpr float kMinScale = 0.15f;     // ne jamais coller completement a l'hote

	struct AttachData
	{
		RE::FormID        host{ 0 };
		RE::BSFixedString node;
		RE::NiPoint3      offset;
		bool              matchRotation{ true };

		// Transition : on interpole depuis 'fromPos' vers la cible pendant 'blend' secondes.
		RE::NiPoint3 fromPos;
		float        blend{ 0.0f };
		bool         detaching{ false };
		int          moveState{ 0 };  // 0 = arret, 1 = deplacement, 2 = sprint (evite de renvoyer l'evenement a chaque frame)

		// Repositionnement adaptatif : fraction de la distance configuree reellement appliquee.
		// 1.0 = position nominale ; on la reduit quand l'emplacement est obstrue (mur, rocher).
		float        distScale{ 1.0f };
		RE::NiPoint3 lastTarget;
		bool         hasLastTarget{ false };
	};

	std::unordered_map<RE::FormID, AttachData> g_attached;
	std::mutex                                 g_mutex;
	float                                      g_logTimer{ 0.0f };  // throttle du log de diagnostic

	// Lissage type smoothstep : demarrage et arrivee doux, pas de cassure de vitesse.
	inline float Smooth(float t)
	{
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		return t * t * (3.0f - 2.0f * t);
	}

	inline RE::NiPoint3 Lerp(const RE::NiPoint3& a, const RE::NiPoint3& b, float t)
	{
		return RE::NiPoint3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	}

	// Trajectoire de transition : interpolation lissee + arc vertical. sin(pi*t) vaut 0 au depart et a
	// l'arrivee, 1 au milieu -> l'acteur s'eleve puis redescend exactement sur sa cible, sans a-coup.
	inline RE::NiPoint3 ArcPath(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_t, float a_arc)
	{
		const float t = a_t < 0.0f ? 0.0f : (a_t > 1.0f ? 1.0f : a_t);
		RE::NiPoint3 pos = Lerp(a_from, a_to, Smooth(t));
		pos.z += a_arc * std::sin(kPi * t);
		return pos;
	}

	// Position visee = position du noeud + offset exprime dans le repere du CAP de l'hote.
	// Repere volontairement intuitif et previsible : X = droite, Y = avant (negatif = derriere),
	// Z = haut. On n'utilise PAS l'espace local de l'os : ses axes ne sont pas devinables de facon
	// fiable, ce qui rend tout reglage (et toute option FOMOD) impossible a raisonner.
	// GetAngleZ() renvoie des RADIANS cote C++ (contrairement a Papyrus qui donne des degres).
	RE::NiPoint3 ComputeTarget(const RE::NiPoint3& a_nodePos, float a_hostYaw, const RE::NiPoint3& a_offset)
	{
		const float s = std::sin(a_hostYaw);
		const float c = std::cos(a_hostYaw);
		// Cap Skyrim : avant = (sin, cos, 0), droite = (cos, -sin, 0).
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
			SKSE::log::warn("Attach refuse : acteur invalide (pet ou host null).");
			return false;
		}

		std::lock_guard lock(g_mutex);
		AttachData data{};
		data.host = a_host->GetFormID();
		data.node = a_node;
		data.offset = RE::NiPoint3{ a_x, a_y, a_z };
		data.matchRotation = a_matchRotation;
		data.fromPos = a_pet->GetPosition();  // point de depart de la transition d'entree
		data.blend = 0.0f;
		data.detaching = false;
		g_attached[a_pet->GetFormID()] = data;

		// Coupe la collision pendant le portage : sinon le corps physique du porte percute celui de
		// l'hote (constate au saut). Restauree a la fin du detachement.
		a_pet->SetCollision(false);

		SKSE::log::info("Attach : pet {:08X} -> host {:08X}, noeud '{}', offset ({}, {}, {}), matchRot={}",
			a_pet->GetFormID(), a_host->GetFormID(), a_node.c_str(), a_x, a_y, a_z, a_matchRotation);
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
			return false;  // pas attache (ou en cours de detachement) : rien a mettre a jour
		}
		// On ne touche QUE a l'offset : ni fromPos ni blend, pour ne pas relancer une transition
		// d'entree. UpdateAll lit data.offset a chaque frame -> la nouvelle position s'applique en
		// douceur des le frame suivant, sans "descendre puis remonter".
		it->second.offset = RE::NiPoint3{ a_x, a_y, a_z };
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
			return true;  // deja en cours de detachement
		}

		// On ne supprime pas tout de suite : on repose l'acteur au sol derriere l'hote pendant kBlendOut,
		// sinon il est relache en l'air et tombe brutalement.
		it->second.detaching = true;
		it->second.blend = 0.0f;
		it->second.fromPos = a_pet->GetPosition();
		SKSE::log::info("Detach : pet {:08X} (transition de sortie)", a_pet->GetFormID());
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
		return true;  // le seul fait que cet appel aboutisse prouve que le DLL est charge
	}

	void UpdateAll(float a_delta)
	{
		std::lock_guard lock(g_mutex);
		if (g_attached.empty()) {
			return;  // cas ultra majoritaire : sortie immediate, cout negligeable par frame
		}

		for (auto it = g_attached.begin(); it != g_attached.end();) {
			auto&       data = it->second;
			auto* const pet = RE::TESForm::LookupByID<RE::Actor>(it->first);
			auto* const host = RE::TESForm::LookupByID<RE::Actor>(data.host);

			if (!pet || !host) {
				it = g_attached.erase(it);
				continue;
			}

			// 3D non chargee (cellule dechargee, autre worldspace...) : on ne touche a rien ce frame-ci,
			// l'attache reprendra d'elle-meme au retour.
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

			// Bilan de la frame precedente : le moteur a-t-il repousse l'acteur ? On compare sa position
			// ACTUELLE (donc apres resolution physique) a celle qu'on lui avait demandee. Il faut le
			// faire ici, en debut de frame, avant de le repositionner.
			if (data.hasLastTarget && !data.detaching && data.blend >= 1.0f) {
				const RE::NiPoint3 actual = pet->GetPosition();
				const float dx = actual.x - data.lastTarget.x;
				const float dy = actual.y - data.lastTarget.y;
				const float dz = actual.z - data.lastTarget.z;
				if (std::sqrt(dx * dx + dy * dy + dz * dz) > kBlockedTol) {
					data.distScale -= kPullInRate * a_delta;  // obstrue : on se replie vers l'hote
					if (data.distScale < kMinScale) {
						data.distScale = kMinScale;
					}
				} else if (data.distScale < 1.0f) {
					data.distScale += kRestoreRate * a_delta;  // libre : on reprend la distance nominale
					if (data.distScale > 1.0f) {
						data.distScale = 1.0f;
					}
				}
			}

			const float hostYaw = host->GetAngleZ();

			// Seule la composante horizontale est reduite : la hauteur reste celle voulue.
			RE::NiPoint3 effOffset = data.offset;
			effOffset.x *= data.distScale;
			effOffset.y *= data.distScale;
			RE::NiPoint3 ridePos = ComputeTarget(node->world.translate, hostYaw, effOffset);

			// Garde-fou vertical (2026-07-22, retour "accroupi elle saccade" + capture ou elle s'enfonce
			// dans le sol). Le noeud d'ancrage descend quand l'hote s'accroupit : l'offset vertical fixe
			// envoie alors la cible SOUS le terrain, et Havok ejecte l'acteur en boucle -> saccades.
			// On ne descend jamais sous les pieds de l'hote + une marge. GetPosition().z d'un acteur est
			// a hauteur de ses pieds, donc c'est une reference fiable, y compris en pente.
			const float minZ = host->GetPosition().z + kMinHeight;
			if (ridePos.z < minZ) {
				ridePos.z = minZ;
			}

			// Diagnostic (2026-07-22) : retour "elle se fige sur place mais suit mes mouvements, donc
			// elle n'est pas derriere moi". Hypothese a trancher : le cap renvoye est-il constant (ce
			// qui figerait l'offset dans une direction fixe du monde) ou varie-t-il quand le joueur
			// tourne ? On trace ~1x/s pour pouvoir comparer sans noyer le fichier.
			// Immersion : le porte est deplace DE FORCE, donc son graphe d'animation le croit immobile et
			// le laisse en idle meme quand le joueur sprinte. On tente de lui recopier la vitesse du
			// porteur. Les deux appels renvoient un bool : on les trace pour savoir LEQUEL echoue (la
			// variable n'existe pas cote hote ? cote netch ?) plutot que de supposer.
			float      hostSpeed = 0.0f;
			const bool gotSpeed = host->GetGraphVariableFloat("Speed", hostSpeed);
			const bool setSpeed = gotSpeed ? pet->SetGraphVariableFloat("Speed", hostSpeed) : false;

			// Le log a montre que getSpeed ET setSpeed reussissent, avec des valeurs correctes (0 a
			// l'arret, ~360 en course, 500 en sprint) - pourtant le netch reste en idle. Conclusion :
			// son graphe n'utilise PAS "Speed" pour declencher sa locomotion. On passe donc par des
			// EVENEMENTS d'animation, envoyes uniquement sur changement d'etat (pas a chaque frame,
			// sinon on relancerait l'animation en boucle).
			int state = 0;
			if (hostSpeed > kSprintSpeed) {
				state = 2;
			} else if (hostSpeed > 1.0f) {
				state = 1;
			}

			if (state != data.moveState) {
				const int previous = data.moveState;
				data.moveState = state;

				// Sortie propre de l'etat sprint avant toute autre transition : sans ca le graphe peut
				// rester coince dans netch_sprintforward. (Animations du netch confirmees dans
				// "Skyrim - Animations.bsa" : netch_sprintforward.hkx, netch_runforward.hkx, etc.)
				if (previous == 2) {
					pet->NotifyAnimationGraph("SprintStop");
				}

				if (state == 0) {
					pet->NotifyAnimationGraph("moveStop");
				} else if (state == 1) {
					pet->NotifyAnimationGraph("moveStart");
				} else {
					// Sprint : on cherche l'animation de charge de la creature (voir kSprintEvents).
					bool accepted = false;
					for (auto* evt : kSprintEvents) {
						if (pet->NotifyAnimationGraph(evt)) {
							SKSE::log::info("[anim] evenement sprint accepte : '{}'", evt);
							accepted = true;
							break;
						}
					}
					if (!accepted) {
						pet->NotifyAnimationGraph("moveStart");  // repli : au moins l'animation de deplacement
						SKSE::log::info("[anim] aucun evenement sprint accepte, repli sur moveStart");
					}
				}
			}

			g_logTimer += a_delta;
			if (g_logTimer >= 1.0f) {
				g_logTimer = 0.0f;
				SKSE::log::info(
					"[diag] yaw={:.1f}deg | cible=({:.1f}, {:.1f}, {:.1f}) | pet=({:.1f}, {:.1f}, {:.1f}) | anim: getSpeed={} ({:.2f}) setSpeed={}",
					hostYaw * 57.2957795f,
					ridePos.x, ridePos.y, ridePos.z,
					pet->GetPosition().x, pet->GetPosition().y, pet->GetPosition().z,
					gotSpeed, hostSpeed, setSpeed);
			}

			if (data.detaching) {
				// Sortie : on le ramene doucement au sol, juste derriere l'hote, puis on le libere.
				data.blend += a_delta / kBlendOut;
				const RE::NiPoint3 hostPos = host->GetPosition();
				const RE::NiPoint3 release = ComputeTarget(hostPos, hostYaw, RE::NiPoint3{ 0.0f, -60.0f, 0.0f });
				pet->SetPosition(ArcPath(data.fromPos, release, data.blend, kArcOut), true);

				if (data.blend >= 1.0f) {
					pet->SetCollision(true);  // on lui rend sa collision, coupee pendant le portage
					SKSE::log::info("Detach : pet {:08X} libere (collision restauree)", it->first);
					it = g_attached.erase(it);
					continue;
				}
				++it;
				continue;
			}

			// Entree : glissement doux depuis la position de depart jusqu'au point d'accroche.
			RE::NiPoint3 target = ridePos;
			if (data.blend < 1.0f) {
				data.blend += a_delta / kBlendIn;
				target = ArcPath(data.fromPos, ridePos, data.blend, kArcIn);
			}

			// true = deplacer AUSSI le controleur de collision. Indispensable : c'est lui qui porte la
			// vraie position de l'acteur. Avec false (essai du 2026-07-22), l'acteur restait a sa
			// position d'origine et ne faisait que deriver en parallele du joueur - il ne venait jamais.
			// La collision avec l'hote (percussion au saut) est reglee autrement : SetCollision(false)
			// a l'attache, restauree au detachement.
			pet->SetPosition(target, true);
			data.lastTarget = target;  // reference pour mesurer, la frame suivante, si le moteur l'a repousse
			data.hasLastTarget = true;

			if (data.matchRotation) {
				pet->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, hostYaw });
			}

			++it;
		}
	}

	// --- Liaison Papyrus -------------------------------------------------------------------------

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
		SKSE::log::info("Fonctions Papyrus enregistrees : Piggyback.Attach / SetOffset / Detach / IsAttached / IsInstalled.");
		return true;
	}
}
