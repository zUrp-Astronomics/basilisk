# emount-bench — briefing et conventions du dépôt code

**Date** : 2026-09-23
**Dernière révision** : 2026-10-10
**Statut** : actif — commandes jouées par `.gitea/workflows/ci.yml` ; l'historique des révisions est dans `git log`, pas ici
**Référencé par** : la fleet (recopie des sections dans le contexte des producteurs), la sonde `probe-test-relevance.yml`, tout humain ou agent qui ouvre le dépôt (briefing)
**Dérivé de** (sections de briefing) : `CLAUDE.md` de la ligne web (commit `1aa6b89`, zip de passation)

**On ne modifie ce fichier que si une convention, une commande, un piège ou une règle en vigueur
change, et on met alors la date à jour. Ce qu'un ticket a fait va dans ses messages de commit, pas
ici.** (L'humain, 2026-10-01 : ce fichier est un point stable, pas un journal.) **Le comportement du
firmware n'y figure pas** : il est dans `7_Docs/PROTOCOL.md`, dans la spec de l'atelier et dans les en-têtes de
code. Ce fichier décrit l'environnement de travail du projet ; ce n'est ni un journal, ni le README, ni
une spec.

Lis ce fichier en entier avant de toucher au dépôt. Le briefing (jusqu'à `## Lectures`) existe
parce qu'une session entière a été dépensée à reconstruire ce cadre par la conversation ; les
sections `## Build` à `## Gotchas` sont les conventions que la fleet et la CI appliquent.

Carte de bord XIAO ESP32-S3 pilotant des objectifs Sony E-mount pour l'astrophotographie, exposée
à Ekos/INDI. Cible n° 1 : **Samyang AF 135 F1.8 FE**.

## Conventions

### Interdits — non négociables

- **Jamais d'écriture dans la mémoire d'un objectif, hors les exceptions décidées par l'humain.**
  `bench_core` est la dernière barrière : tout ce que la carte émet vers l'objectif passe par lui,
  et ce qu'il ne liste pas est refusé. La liste exacte, exceptions comprises, est dans
  `4_Firmware/components/bench_core/bench_core.c` et `7_Docs/PROTOCOL.md`. C'est un **verrouillage**, au
  sens du Therac-25 : ça ne devient pas une option, ça ne se contourne pas « juste pour tester »,
  et ses tests (`4_Firmware/sim/test/test_bench_core.c`, `garde_emission.sh`) restent. Une commande
  nouvelle entre dans la liste par un ticket : avec la preuve, sur les décompilés, qu'elle n'écrit
  rien ; ou, si elle écrit, sur décision expresse de l'humain, réservée au modèle déclaré qu'il a
  nommé. Les décompilés restent privés : `bench_core.c` décrit chaque sous-commande, admise ou
  refusée, en termes de protocole, et renvoie à la preuve publiée, `7_Docs/E-Mount/samyang.md`
  (§ 6.3 les sous-commandes, § 7.2 la carte des écritures) ; le ticket qui ouvre une entrée écrit
  ce qu'il a établi dans le document de `7_Docs/E-Mount/` qui lui correspond, selon ses règles. Un
  objectif briqué ne se dé-brique pas.
- **Aucune modification du dépôt `samyang-reverse`** de l'utilisateur (la copie telle quelle qu'en
  portait la v1 est archivée dans l'atelier, `sources/firmware-v1/components/emount`).
- **Aucune pull request vers indilib.**
- **`PROTOCOL.md` est le contrat**, pas de la documentation. Figé : `f r e a d s c` compatibles
  Pinefeat octet pour octet (réponses ≤ 15 caractères, jamais `ok` dans une erreur), repli
  Moonlite intact. `v` n'en est pas : elle rend la version du firmware de la carte, `<maj>.<min>`,
  sans suffixe (décision de l'humain). Le reste évolue quand ça apporte quelque chose, et
  `PROTOCOL.md` change dans le même commit que le code.
- **La carte ne parle jamais la première** : les événements `* …` seulement après `LOG ON`.
- **Un pilote n'envoie de message hors norme qu'à un objectif reconnu par signature** : un FE
  24-105 G qui reçoit un `'V'` part en tempête de `0x02` jusqu'à coupure d'alimentation.
- **Bornes mesurées, jamais codées** : rien ne suppose un modèle d'objectif, aucune position de
  repos en dur. (Seule exception, la précédente : la signature qui autorise un pilote à parler.)

### Méthode

- **Code embarqué : simple, robuste, fiable, sans fioriture** (l'humain, 2026-09-25). Pas de
  fonction qui ne fait pas mieux marcher la carte. Un test défend un comportement réel du firmware
  ou un défaut constaté, jamais un problème imaginé. Une règle de ce fichier vient de l'humain ou
  d'un fait technique démontré, et on peut dire lequel.
- Une transformation, un commit, une suite verte (les commandes de § Test). Pas de lot, pas
  de « pendant que j'y suis ».
- Ne pas refactorer ce qui a été payé par un incident : la complexité qu'il a laissée encode une
  connaissance que la relecture ne retrouvera pas.
- Écrire l'invariant avant le code.
- Une garde ne se justifie jamais par « ça ne casse pas sans elle », mais par le cas qu'elle
  défend, nommé, et l'endroit où ce cas est démontré.
- Pas d'abstraction pour un futur hypothétique ; le deuxième cas d'usage la justifie, le premier
  jamais.
- Pas de dépendance pour économiser vingt lignes. **Le firmware n'en a aucune hors ESP-IDF** —
  c'est une propriété, pas un hasard. L'outillage de développement en a, et elles ne tournent
  jamais sur la carte : Playwright épinglé dans `package.json` (`5_App/test/test_app.js`), Python 3
  (`tool_station` et son test, le générateur de la table des noms), Node (tests de l'app).
- Dire quand on ne sait pas. Sans matériel, on n'affirme aucun comportement de l'objectif : on dit
  ce que prouvent les tests hôte, et face à quel faux objectif.
- `PROJECT_VER` (`4_Firmware/CMakeLists.txt`) se bumpe **avant** de construire un binaire qu'on livre,
  sinon il porte l'ancienne version (l'erreur a déjà été commise).
- **Il n'y a pas de gel** : le firmware se modifie par ticket, comme le reste (l'humain a levé le gel
  le 2026-09-23 : « un code buggé se corrige »).
- **Ce qui juge à la place du matériel** : la CI (`.gitea/workflows/ci.yml`) — les suites de
  § Test et les builds — et, dans la suite de `4_Firmware/`, ses faux objectifs (le faux 135 et le faux
  F051). Ils disent ce que fait notre firmware face à notre modèle de l'objectif ; ce que le modèle
  simplifie ou fixe sans source est déclaré dans la sienne (simplifications en tête de
  `4_Firmware/sim/lens135.c` et de `4_Firmware/sim/lens_std.c`, paramètres [NÉ] de
  `4_Firmware/sim/lens_std.h`). Ce fichier ne dit pas ce qui a été validé sur matériel.
- **Le blocage au boot** observé une fois par la session web **n'est pas attribué au code** : câble
  USB fatigué et fils Dupont sur un proto sont les hypothèses de tête ; il ne redevient un défaut logiciel que s'il réapparaît avec un câblage sain
  (aucune trace n'en est dans le dépôt).

## Doctrine

`7_Docs/DOCTRINE.md` gouverne les arbitrages. Le minimum à en retenir :

- Le critère est l'**irréversibilité** : un firmware dans une bague, sur une monture, dans un champ,
  à trois heures du matin, n'a pas de rollback.
- **Supprimer > simplifier > raccourcir.** Une ligne supprimée ne se teste pas, ne régresse pas,
  ne peut pas être fausse.
- **Sur-spécifié, sous-implémenté.** La rigueur dans la spécification, l'économie dans le code.
- **Un mineur vu est un défaut.** S'il existe une catégorie « vu, jugé sans importance », le mot
  « propre » ne porte plus d'information.
- Les suites hôte ne sont pas un filet : **ce sont le comité de revue**. Rien ne les affaiblit.

## En attente

La file de travail est `/home/projects.workshop/basilisk/backlog.md` (face atelier, hors du
produit).

## Lectures, dans l'ordre

1. `7_Docs/DOCTRINE.md` — pourquoi le code est écrit comme il l'est
2. `7_Docs/PROTOCOL.md` — le contrat
3. Ce fichier, § Suites — ce que la suite de `4_Firmware/` impose à qui y écrit

## Build

Firmware ESP-IDF 5.3.2, cible `esp32s3`. `4_Firmware/` se construit par `bash 4_Firmware/build.sh`
(environnement ESP-IDF chargé : `. "$IDF_PATH/export.sh"` ; sdkconfig généré dans `4_Firmware/build` ;
échoue si la console n'est pas muette), job CI `firmware-build`, qui publie à chaque run, PR comprises,
l'artefact `firmware` : `.bin`, `bootloader.bin`, `partition-table.bin` et `flash_args`. On le récupère
sur la page du run, ou par l'API : `GET /api/v1/repos/fleet/basilisk/actions/artifacts`, puis
`…/artifacts/<id>/zip`. Ses tests hôte, gcc seul : `bash 4_Firmware/run.sh` (§ Test). Son comportement
n'est pas décrit ici : le contrat USB est `PROTOCOL.md`, la spec (états, décisions) est celle de
l'atelier, et chaque composant porte ses invariants et ses pièges en commentaire, dans son `.c` et son
`.h`. Le reste de cette section est ce que l'environnement de `4_Firmware/` impose et qu'un agent
casserait de bonne foi.

L'ancien firmware (la v1) n'est plus dans le dépôt : il est archivé dans l'atelier, `sources/firmware-v1/`.

**Cible.** La carte cible sera une XIAO ESP32-C3 (un cœur, rails de l'objectif commutés, brochage non
fixé) ; la S3 reste la seule cible compilée. Toutes les tâches sont épinglées sur le cœur 0, sur toutes
les cibles (décision de l'humain) : sur la S3, un cœur réservé à la PHY la testerait dans de meilleures
conditions que le C3 ; rien ne dépend du nombre de cœurs. Les broches sont dans une table par cible
(`components/phy/pins.h`, LED comprise) : seule la S3 en a une, une autre cible ne compile pas. La
console ESP-IDF est muette (niveaux de journal à NONE, bootloader compris, `sdkconfig.defaults`).
`CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD=y` (`sdkconfig.defaults`) est exigé par l'`esp_timer`
rappelé en interruption de `components/phy/phy.c` (l'anti-rebond de D2).

**Carte des composants.** En-têtes partagés dans `4_Firmware/include/bsk_*.h`, privés à côté de leur `.c`.
Une flèche dit « appelle » ; aucune ne remonte. Les tâches, et qui peut appeler quoi depuis laquelle :
l'en-tête de chaque composant (`bsk_session.h`, `bsk_host.h`, `bsk_journal.h`, `bsk_store.h`).

- `main/main.c` (`app_main`, la tâche de SESSION) : la boucle des pas — `bsk_session_step`,
  `bsk_host_observe`, `bsk_host_usb_service`, `bsk_led_board_service`.
- `host` (HOTE : `host.c`, `lens_names.c` en C pur ; `host_usb.c`, l'USB de la carte) -> `session`,
  `journal`, `led` : la ligne USB de `PROTOCOL.md`.
- `led` (`led.c` en C pur, les valeurs des motifs en tête du fichier ; `led_board.c`, le PWM de la carte)
  -> `session` (l'instantané seul).
- `session` -> `txn`, `phy`, `journal`, `store`. Le superviseur `session.c` -> `lens_rx`, `motion`, `ring`,
  `mark`, `drive`, `still`, `cmd`, `init`, `restore` ; `cmd.c` (la boîte de commandes et le bouton du fût)
  -> `motion`, `mark`, `ring`, `lens_rx` ; `init.c` (la séquence d'init) -> `txn`, `lens_rx`, `journal` ;
  `restore.c` (le retour à la marque) -> `motion`, `mark`, `lens_rx`, `journal` ; `motion.c` (MOUVEMENT)
  -> `drive`, `lens_rx`, `still` ; `ring.c` (la bague) -> `txn`, `lens_rx` ; `mark.c` (la marque) ->
  `store`, `lens_rx` ; `drive.c` (la consigne de mise au point) -> `txn` ; `still.c`, l'immobilité.
  `lens_rx.c` est le seul décodeur des trames reçues : un champ publié de plus s'y ajoute, sans toucher
  `session.c`.
- `txn` (TRANSACTION) -> `bench_core`, `journal` ; `bench_core` -> `phy` ; `journal` -> `phy_common`
  (`fr_encode`) ; `store` -> NVS ; `phy` -> les pilotes ESP-IDF. `phy_common.c` est commun à la PHY de la
  carte (`phy.c`) et à la PHY simulée (`sim/phy_sim.c`).
- `bench_core` est le seul appelant de `bsk_phy_send` (`sim/test/garde_emission.sh`) ; SESSION commande à
  la PHY les rails, les lignes, BODY_CS, la VD et lit ses événements, jamais l'émission.

## Test

    bash 5_App/run.sh                   # 5_App/ (la page de banc) : job `test`
    bash 5_App/tool_station/run.sh      # 5_App/tool_station/ : job `test`
    bash 4_Firmware/run.sh              # 4_Firmware/ : job `firmware`

## Suites

Trois suites, deux jobs CI ; aucune n'est toute la suite.

`bash 4_Firmware/run.sh` (gcc seul, rien d'écrit dans le dépôt ; `4_Firmware/traces`, les traces de
l'objectif, `4_Firmware/CMakeLists.txt` et `5_App/emount-bench.html` lus en place) joue, dans l'ordre :

1. chaque en-tête de `4_Firmware/include` compilé seul (inclus deux fois) ; la table de broches compilée
   (`-fsyntax-only`) pour la cible S3, puis refusée, avec son message, pour une cible sans table ;
2. le faux 135 et ses tests compilés avec `-Werror` (comme tout programme de la suite) ;
3. `test_lens135` (le faux 135 : les comportements de la référence Samyang, les pannes reçues et
   émises), `test_lens_std` (le faux Tamron F051 piloté directement, sans PHY simulée), `test_phy` (le
   code commun aux deux PHY, `phy_common.c` : le flux, E_FRAMING et ses octets, E_BUS injecté, la présence,
   le verrou et l'anti-rebond de D2, la trame perdue), `test_bench_core` (la liste blanche de `bench_core`),
   puis `garde_emission.sh` (seul `bench_core` appelle l'émission de PHY), sur le dépôt et sur une copie
   où un appel fautif est posé ;
4. `replay` (le faux 135 contre `4_Firmware/traces` : chaque paire requête -> réponse tracée, 100 % attendu),
   `test_journal` (le journal seul : forme des lignes, périodiques tus sous `LOG ON`, plafond, anneau de
   8 Ko, `dropped`), `test_led` (le vrai `led.c` seul : des suites d'instantanés rejouées à la cadence de
   10 ms, l'intensité lue à des instants donnés), `test_session_script` (SESSION, TRANSACTION et HOTE
   contre un répondeur minimal, `sim/test/phy_script.c`), `test_session135` (SESSION et TRANSACTION contre
   le faux 135 ; requêtes d'init comparées à `astro-normal.txt:222-254`), `test_host` (HOTE contre une
   session scriptée : chaque lettre en READY et dans chaque colonne du tableau d'état de la spec § 4.5.1,
   `last_op`, `move_rc`, `t`, et les motifs `expectFor` lus dans `5_App/emount-bench.html`, appliqués à
   chaque réponse), `test_host135` (des lignes à travers HOTE, SESSION, MOUVEMENT et le faux 135),
   `test_std` (SESSION, TRANSACTION et HOTE contre le faux F051) ;
5. chaque programme de 3 et 4 recompilé sous `-fsanitize=address,undefined -fno-sanitize-recover=all` et
   rejoué : une erreur d'ASan ou d'UBSan le fait échouer (`test_session135` y reçoit `-DGARDE_S=60`, sa garde de
   durée, 10 s sinon) ;
6. les mutants de `sim/test/mutations.txt` puis de `sim/test/mutations_std.txt` (`sim/test/mutants.sh`).

Ce que la suite de `firmware` impose à qui y écrit :

- **Une seule liste de sources** : `sim/test/programmes.sh`, lue par `run.sh` et par `mutants.sh` (sous
  la racine qu'on leur donne : le dépôt, ou la copie mutée). Un programme ou une source ajoutés le sont
  là. Tout programme qui compile `components/host/host.c` compile `components/led/led.c` et est lié avec
  `-lm`. `test_session135`, `test_std` et `test_host135` sont liés avec `--wrap` (`LIENS`) pour noter ce
  que la carte fait sur le fil sans toucher à la PHY simulée.
- **Les attendus sont écrits à la main**, d'après la référence (trace, capture, décompilé, spec), jamais
  calculés par le code testé ni par ses constantes : les 2 ms de l'anti-rebond s'écrivent `DROP`, pas
  `D2_DROP_US`, qu'un mutant change ; les clés de la marque, les cibles de 0x1D et les codes d'ouverture
  attendus sont calculés à la main. Les bornes attendues sont celles du 0x06 ± 5.
- **Chaque mutant doit être tué** : un bloc `@ <programme> | <fichier relatif à 4_Firmware/> | <fonction>`
  nomme la fonction (le nom de sa définition dans le code, pas un commentaire) où il remplace un texte ; ce
  texte est cherché sur le code seul (commentaires retirés, de la source comme du bloc, qui n'en contient
  pas : `mutants.sh --code <fichier>`) et doit apparaître exactement une fois dans le corps de la fonction
  (`mutants.sh --fonctions <fichier>`, règles de repérage en tête de `mutants.sh`). Hors de toute fonction
  (table, macro, `constats_std.txt`), le bloc n'en nomme aucune et le texte est unique dans tout le
  fichier ; un bloc sans fonction dont le texte est dans une fonction est refusé. Le mutant doit compiler
  (`-Werror` compris) et faire sortir son programme en 1. Une mutation qui ne s'applique pas à exactement un
  endroit, une fonction inconnue, un mutant qui ne compile pas, qui survit ou qui meurt autrement (signal,
  délai) font échouer la suite ; `run.sh` joue `sim/test/test_mutants.sh` (ce repérage, sur une source
  jouet) avant les mutants. Les mutants d'un changement s'ajoutent en fin de fichier ; quand le code déplacé
  emporte l'ancre d'un bloc, le bloc est réancré, même règle, même programme ; un bloc dont la règle est
  renversée est retiré.
- **Les constats de `test_std`** : un scénario contre le faux F051 dont l'attendu n'est pas tenu n'est ni
  corrigé ni affaibli : il est inscrit, avec sa raison et sa citation, dans `sim/test/constats_std.txt`,
  que `test_std` lit (chemin donné par `programme_args`, sous le dépôt ou sous la copie mutée de
  `mutants.sh`) et affiche. `test_std` rougit sur un échec hors de la liste, sur un scénario de la liste qui
  passe (le constat réglé en sort) et sur une entrée qui ne nomme aucun scénario ou n'a ni raison ni
  citation.
- **Les faux objectifs** se branchent dans la PHY simulée et le banc par l'interface commune
  `sim/lens_sim.h`, chacun par un adaptateur (`lens_sim_135.c`, `lens_sim_std.c`), sans que leur code
  change ; un adaptateur refuse ce que son modèle ne sait pas représenter (l'état « resté alimenté » porte
  le mode service du 135 et la mise en page du standard). Le faux 135 a pour bornes celles de son `0x06`,
  13873 / 30738, est en READY en 16384 après son init (`POS_REST`), publie 01 à l'offset 62 (sa trace, M1 :
  ses bancs tournent bague en ouverture, bit posé) et publie un appui du bouton sur demande du test
  (`l135_inputs_t`, offset 64 bit 3, sans autre effet). Le faux F051 (`sim/lens_std.c`) est écrit à la main
  d'après `7_Docs/E-Mount/tamron.md` et l'analyse statique privée de son firmware ; ses réponses constantes sont
  reconstruites des champs que `tamron.md` publie, tout octet non publié à zéro ; chaque paramètre [NÉ] de
  `lens_std.h` est joué sur deux valeurs.
- **La PHY simulée** porte le verrou de XDETECT comme la carte : `phy_sim_d2(false)` est l'interruption
  (coupure et verrou dans l'appel même), qui arme l'échéance de 2 ms (`d2_due`, un instant de
  `phy_sim_next`) où D2 est relu (`d2_expire`). `phy_sim_next` ne compte pas l'anti-rebond de
  l'insertion : un banc qui pose D2 réveille lui-même la simulation 300 ms après. `test_lens135` et
  `replay`, qui jouent le fil sans SESSION, montent l'objectif avant le banc (`phy_sim_mounted`, sans
  événement) : sans lui, le verrou refuse la VD. La PHY simulée note l'état des rails et des lignes
  (`phy_sim_wires` : `test_session135` vérifie après chaque commande qu'aucune ligne n'est pilotée sans les
  deux rails ; des lignes relâchées y posent BODY_CS basse, le tirage bas de la carte), injecte une émission
  de l'objectif hors du faux (`phy_sim_lens_raw` ; `phy_sim_lens_raw_late`, ses derniers octets après la
  retombée de `LENS_CS`, le Sony) et perd une trame dans un sens ou dans l'autre, ou la reçoit fausse (un
  E_FRAMING à sa place, qui porte ses octets), au choix du test (`phy_sim_fault`) ; les faux objectifs n'en
  sont pas changés.
- **La PHY scriptée** (`sim/test/phy_script.c`) n'a pas le verrou (ses commandes rendent `true`) ; chacun
  de ses messages est cité ; un moteur scripté (`motor()` de `test_session_script.c`, branché par `on_send`)
  isole chaque transition ; `fail_sends` fait échouer des émissions. Les trames du Sony FE 24-105 G y sont
  copiées des captures de l'humain (`7_Docs/E-Mount/traces/emount-bench-2026-09-28T…`) et décodées par
  `fr_decode`.
- **Le magasin en mémoire** (`sim/store_sim.c`, même interface que `components/store/store.c` ; `hold`
  retient les résultats comme la tâche de la carte) : chaque banc part d'un magasin vide
  (`store_sim_reset`) ; une marque attendue au démarrage est préchargée (`store_sim_poke`, clé calculée à
  la main).
- **Les arguments des programmes** sont dans `programme_args` (`sim/test/programmes.sh`), obligatoires :
  les traces pour `test_lens135`, `replay` et `test_session135` ; la page puis `CMakeLists.txt` (PROJECT_VER)
  pour `test_host` ; la liste des constats pour `test_std`, sous la racine donnée. `run.sh` déclare les trois
  fichiers lus hors de `sim/test` (`TRACES`, `PAGE`, `VERSION`) et les passe à `mutants.sh`
  (`mutants.sh <traces> <page> <version>`). La traduction des motifs de la page par `test_host` (`js_re`)
  rend `\d` d'un ensemble (`[\d.]`).
- **Ce qu'aucun test hôte ne joue** : ce qui n'est compilé que par ESP-IDF — `components/phy/phy.c` (le
  flux et le relevé sur l'UART, l'interruption de D2, sa coupure, ses sections critiques, l'`esp_timer` de
  l'anti-rebond, son rappel et son repli), `components/store/store.c`, `components/led/led_board.c` (le
  rendu sur la XIAO), `components/host/host_usb.c`.

`bash 5_App/run.sh` est la même commande que le job `test` de la CI : `5_App/test/test_app_vm.cjs` (qui
reçoit le répertoire des traces de l'objectif) et `5_App/test/test_app.js`, les tests de la page de banc. Le
même job joue ensuite `bash 5_App/tool_station/run.sh`, le test de l'outil `tool_station` (python3,
`5_App/tool_station/test_tool_station.py`). Les traces de l'objectif sont dans `4_Firmware/traces`, lues en
place par `4_Firmware/run.sh` et par `5_App/run.sh`.

**Chemins.** Chaque script et chaque test résout ses chemins depuis sa propre place, jamais depuis le
répertoire courant : un script se lance depuis n'importe quel répertoire, et un programme de test reçoit
en argument, obligatoire, ce qu'il lit hors de son répertoire. La CI n'appelle qu'un script d'entrée par
composant, qui vit dans ce composant (`5_App/run.sh`, `4_Firmware/run.sh`, `4_Firmware/build.sh`,
`5_App/tool_station/run.sh`, `6_Driver/indi/build.sh`). Un composant qui lit un fichier d'un autre le
déclare en une ligne de son script d'entrée, et nulle part ailleurs.

## Harness

5_App/test/ 5_App/run.sh 5_App/tool_station/test_tool_station.py 5_App/tool_station/run.sh 4_Firmware/traces/ 4_Firmware/sim/test/ 4_Firmware/run.sh

## Gotchas

- **Prérequis de `5_App/run.sh`** : node ; pour `5_App/test/test_app.js`, le module
  `playwright` (`npm install` à la racine, version épinglée dans `package.json`) et un Chromium sous
  `/opt/pw-browsers/chromium-*/chrome-linux/chrome` ou désigné par `CHROMIUM_PATH`. En CI (et dans la
  sonde), `.gitea/setup_host.sh` pose tout ça HORS de `$HOME` : la sonde joue la suite sous `env -i`
  avec un `HOME` vierge.
- **Playwright est épinglé à 1.49.1 exprès** : `test_app.js` cherche la disposition `chrome-linux`
  sous `/opt/pw-browsers`. Une montée de version peut la changer ; `.gitea/setup_host.sh` le vérifie.
- **Même image, même outillage** pour le job `test` de `ci.yml` et pour `probe-test-relevance.yml`
  (`catthehacker/ubuntu:act-latest` + `.gitea/setup_host.sh`) : on change l'un, on change l'autre.
- **`6_Driver/indi/` (driver INDI) est compilé et lié en CI, pas testé** : le job `indi` de `ci.yml` (image
  `ubuntu:24.04`, pas `act-latest`, étiquette mobile) installe `libindi-dev` d'Ubuntu 24.04, **libindi
  1.9.9**, et joue `6_Driver/indi/build.sh` : `cmake` puis `make` sur `6_Driver/indi/CMakeLists.txt` tel quel (branche pkg-config :
  `libindi-dev` n'expédie pas de `FindINDI.cmake`, et son `libindi.pc` ne donne aucune bibliothèque, seul
  `indidriver` est lié). Contre libindi 2.x, seule la syntaxe a été vérifiée (`g++ -fsyntax-only`,
  2.2.5), jamais l'édition de liens. Aucun comportement du driver n'est testé, et il n'entre dans
  aucune suite de § Test. Il est sous LGPL 2.1 (`6_Driver/indi/LICENSE`, texte copié de
  `/usr/share/common-licenses/LGPL-2.1`) ; le reste du dépôt : OCL v1.1 (`LICENSE-HARDWARE`) pour le hardware
  et la conception, MIT (`LICENSE`) pour le logiciel, exceptions comprises dans `README.md` § « Licences ».
- **La table des noms d'objectifs est générée** : `4_Firmware/components/host/lens_names.c` et la table
  `LENS_NAMES` de `5_App/emount-bench.html` sortent de `4_Firmware/gen_lens_names.py` (ExifTool `Sony.pm`,
  téléchargé, ou un `Sony.pm` local en argument). On régénère, on n'édite pas à la main ; aucune suite
  ne joue le générateur.
- `5_App/test/test_app.js` : toute `pageerror` (exception JavaScript non rattrapée dans la page) est
  affichée et comptée comme un échec, même si les vérifications fonctionnelles passent. Ne pas la
  signaler par `process.exitCode` : le `process.exit(fails ? 1 : 0)` final l'écraserait.
