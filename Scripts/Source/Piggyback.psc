Scriptname Piggyback Hidden

; API du plugin SKSE Piggyback (composant generique reutilisable, voir docs/02-plugin-rig-moteur.md).
; Attache un acteur a un noeud du squelette d'un autre acteur, IMAGE PAR IMAGE - ce que Papyrus seul ne
; peut pas faire (plafonne a 10-20 Hz). Necessite Piggyback.dll ; sans lui ces fonctions echouent
; silencieusement, donc toujours prevoir un repli (chez nous : le suivi par IA native).

; asNodeName : nom d'un noeud du squelette de akHost (ex. "NPC Spine2 [Spn2]" pour le haut du dos ;
; nom vanilla, donc pas besoin de XPMSSE).
; afX/afY/afZ : offset exprime dans l'espace LOCAL du noeud - il suit donc la rotation de l'os
; (un offset "vers l'arriere" reste derriere l'hote quand il pivote).
; abMatchRotation : aligne aussi l'orientation du pet sur le cap de l'hote.
bool Function Attach(Actor akPet, Actor akHost, string asNodeName, float afX, float afY, float afZ, bool abMatchRotation = true) global native

; Met a jour l'offset d'un akPet DEJA attache, sans le detacher : la nouvelle position s'applique en
; douceur des la frame suivante (aucune "descente puis remontee"). Meme repere que Attach. Permet
; d'exposer un reglage de position a chaud (ex. curseurs MCM). false si akPet n'est pas attache.
bool Function SetOffset(Actor akPet, float afX, float afY, float afZ) global native

; Detache akPet : il redevient pilote par son IA normale.
bool Function Detach(Actor akPet) global native

bool Function IsAttached(Actor akPet) global native

; Sonde de presence : renvoie true si le DLL Piggyback est installe. S'il est absent, la fonction native
; n'est pas enregistree et Papyrus renvoie false (defaut) - un mod consommateur peut ainsi masquer
; proprement une option qui depend de Piggyback plutot que de la proposer puis echouer.
bool Function IsInstalled() global native
