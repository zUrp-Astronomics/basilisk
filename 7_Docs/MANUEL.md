# emount-bench — une carte pour piloter un objectif Sony E depuis un PC

**Date** : 2026-10-06
**Dernière révision** : 2026-10-10
**Statut** : actif — le manuel de la carte, en français : l'ancien `README.md` du dépôt, déplacé tel quel par le ticket #495 quand le README est devenu la vitrine du projet
**Référencé par** : `README.md` (§ Firmware & software)
**Dérivé de** : `README.md` avant le ticket #495 (révision du 2026-10-06, commit `6fd8349`)

Une petite carte (Seeed XIAO ESP32-S3) se branche entre un PC, en USB, et un objectif à monture Sony E, sur ses
contacts. Elle joue le rôle du boîtier : elle met l'objectif sous tension (si les interrupteurs de ses rails sont
câblés), fait son initialisation, puis le pilote à la demande — mise au point, ouverture, une position mémorisée
(« la marque »). Son usage premier est l'astrophotographie : l'objectif devient un focuser pour INDI/Ekos (Linux) ou pour ASCOM (Windows). Cible première :
**Samyang AF 135 mm F1.8 FE**.

Le PC lui parle par une ligne série : une commande par ligne, une réponse par ligne. Ce protocole est le contrat de la
carte, il est dans **`7_Docs/PROTOCOL.md`**.

## Matériel

**Aujourd'hui : Seeed XIAO ESP32-S3**, la seule cible pour laquelle le firmware se construit. Le brochage est dans
`4_Firmware/components/phy/pins.h` :

| XIAO | GPIO | Signal | Rôle |
|---|---|---|---|
| D0 | 1 | EN_LOGIC | commande du rail logique de l'objectif, active à 1 (optionnel) |
| D1 | 2 | EN_MOTOR | commande du rail moteur de l'objectif, active à 1 (optionnel) |
| D2 | 3 | XDETECT | contact de présence de l'objectif, tirage haut : 0 = présent |
| D3 | 4 | VD | impulsion basse d'environ 60 µs, 60 Hz |
| D7 | 44 | LENS_CS | entrée, tirage bas : l'objectif la lève quand il émet |
| D8 | 7 | TXD → objectif | UART1, 750 000 bauds |
| D9 | 8 | RXD ← objectif | UART1 |
| D10 | 9 | BODY_CS | sortie : la carte la lève quand elle émet |
| — | 21 | LED utilisateur de la XIAO | LED de statut, allumée à l'état bas |
| GND | — | GND | masse commune obligatoire |

- Les niveaux de l'objectif sont en 3,3 V : pas d'adaptation.
- La carte ne fournit pas le courant de l'objectif : EN_LOGIC et EN_MOTOR commandent deux interrupteurs, s'ils sont
  câblés.
- **XDETECT doit être câblé** : la carte ne démarre l'objectif que quand ce contact dit « présent » (tenu 300 ms), et
  elle coupe ses rails et met ses lignes au repos 2 ms après sa retombée, s'il est toujours absent. Tant qu'il ne dit pas « présent », rien n'est émis :
  TXD, BODY_CS et VD sont au repos (entrées, tirage bas), les rails coupés.

**À venir : une carte à base de XIAO ESP32-C3** (un seul cœur, rails de l'objectif commutés). Son brochage n'est pas
fixé et le firmware ne se construit pas encore pour elle.

## Le firmware

Le firmware est dans `4_Firmware/` (ESP-IDF 5.3.2, C, aucune dépendance hors ESP-IDF). `v` rend sa version,
`<majeur>.<mineur>` : celle de `PROJECT_VER`, dans `4_Firmware/CMakeLists.txt`.

### Le récupérer : l'artefact de la CI

À chaque run, la CI construit le firmware (job `firmware-build`) et publie l'artefact **`firmware`** : l'application
(`emount_bench2.bin`), `bootloader/bootloader.bin`, `partition_table/partition-table.bin` et `flash_args`. On le
télécharge depuis la page du run, ou par l'API de la forge (`GET /api/v1/repos/fleet/basilisk/actions/artifacts`, puis
`…/artifacts/<id>/zip`).

### Le flasher

Par l'USB de la XIAO, avec esptool (`pip install esptool`), depuis le dossier de l'artefact décompressé :

```sh
python -m esptool --chip esp32s3 -p PORT -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
```

`PORT` : `/dev/ttyACM0` sous Linux, `COMx` sous Windows (la XIAO y apparaît comme « Périphérique série USB », sans
pilote). Si le flash ne démarre pas : maintenir BOOT, appuyer sur RESET, relâcher BOOT, relancer la commande.

### Le construire soi-même

ESP-IDF 5.3.2 chargé :

```sh
. "$IDF_PATH/export.sh"
bash 4_Firmware/build.sh
```

Le build se fait dans `4_Firmware/build` (le `sdkconfig` y est généré, rien de versionné n'est réécrit) ; la commande de
flash ci-dessus marche depuis ce dossier. La console ESP-IDF est muette : la carte n'écrit rien sur l'USB qui ne soit
pas une réponse.

## Ce que fait la carte

- **Elle ne parle jamais la première.** Aucune bannière, aucune ligne spontanée, sauf après `LOG ON`.
- **Elle démarre l'objectif seule** dès que XDETECT le dit présent : rails, VD, poignée de main, initialisation d'un
  boîtier, identification, homing de l'objectif. `t` suit cette séquence (`boot_state=powering`, `identifying`, …) ;
  pendant elle, `e` répond `y` et les commandes qui ont besoin de l'objectif `er busy boot`.
- **La marque** : une position de mise au point mémorisée **dans la carte** (NVS), par objectif et par focale — rien
  n'est écrit dans l'objectif. `j` la lit, `js` la pose à la position courante, `jg` y va, `jx` l'efface. Le bouton du
  fût fait de même au relâchement : appui de moins d'1 s, aller à la marque ; de 1 à 4 s, la poser ; plus long, rien. À
  la fin de chaque démarrage, la carte revient seule à la marque (`boot_state=restoring`), puis passe en `ready`.
- **Les bornes** de la mise au point (`r`) sont celles que l'objectif publie lui-même, resserrées de 5 pas ; elles ne
  sont ni mesurées ni codées. Une cible hors de ces bornes est refusée (`er range limits`).
- **L'ouverture** : `a` rend la plage, `a<f>` règle l'ouverture, `o` relit celle que l'objectif rapporte. Quand le
  commutateur de l'objectif met sa bague sur l'ouverture, tourner la bague règle l'ouverture, par tiers de diaphragme
  (trois par seconde au plus).
- **L'objectif perdu** (plus de trame pendant 2 s) : la carte refait son démarrage. Après quatre échecs consécutifs, elle
  s'arrête en `boot_state=fault` ; `b` relance, ou débrancher puis rebrancher l'objectif.
- **La LED de statut** dit l'état : un souffle lent sans objectif, un clignotement faible pendant l'initialisation, une
  respiration en `ready`, un motif de mouvement pendant un déplacement, des séries d'éclats en `fault`, dont le nombre
  dit la cause. Les motifs et leurs valeurs sont en tête de `4_Firmware/components/led/led.c`. `k` lit son
  intensité maximale, `k<n>` la règle (0 à 100 %, `k0` l'éteint) ; elle revient à 100 % à chaque démarrage de la carte.
- **`DEBUG`** donne une ligne de diagnostic : la cause du dernier redémarrage de la carte, les lignes du journal perdues,
  le résultat du dernier déplacement. **`LOG ON`**, **`LOG ALL`**, **`LOG OFF`** ouvrent et ferment le journal (lignes
  `* …`) ; `LOG ALL` y ajoute chaque trame échangée avec l'objectif.

Les lettres servies, leurs réponses et leurs erreurs exactes sont dans `7_Docs/PROTOCOL.md`. Une lettre ou une commande en
majuscules que la carte ne sert pas répond `er nocap`.

## S'en servir avec INDI / Ekos

**Le driver du dépôt, `6_Driver/indi/`** (« Basilisk E-mount Lens », `indi_basilisk_focus`) : le driver Pinefeat
d'INDI, avec en plus l'ouverture relue, l'arrêt, la marque, la LED de statut, le redémarrage de l'objectif et son nom. Sa compilation, son
installation et ses propriétés : `6_Driver/indi/README.md`.

**Le driver Pinefeat d'origine** (`indi_pinefeat_cef_focus`, livré avec INDI) : la carte le sert aussi, sur la position,
le mouvement et l'ouverture, sans rien configurer sur la carte ; la distance y reste inconnue (`d` rend `-`). Il ne lit les
bornes (`r`) que si le mineur de `v` est au moins 3 : avec la version actuelle de la carte (`1.0`), il ne les lit pas.

Dans les deux cas : le port série de la carte (la vitesse est sans effet, c'est un port USB).

Un logiciel qui ne connaît que le protocole Moonlite peut aussi piloter la mise au point (`:GP#`, `:FG#`…,
`PROTOCOL.md` § 4).

## S'en servir avec ASCOM (Windows)

Le **driver ASCOM de Pinefeat** (https://github.com/pinefeat/cef135) s'emploie tel quel : la carte sert tout ce qu'il
envoie, `v e r f m c a`, avec les réponses qu'il attend (`PROTOCOL.md` § 2.1). Choisir le port COM de la carte.

## La page de banc (`5_App/`)

`5_App/emount-bench.html` : une page seule, sans installation, qui parle à la carte par Web Serial. L'ouvrir dans **Chrome
ou Edge** sur ordinateur. Si le navigateur refuse Web Serial sur `file://` : `python3 -m http.server` dans `5_App/`, puis
`http://localhost:8000/emount-bench.html`.

- **Connecter** : la page envoie `v` jusqu'à une réponse (l'ouverture du port peut redémarrer la XIAO), puis lit l'état ;
  elle relit ensuite la position et le mouvement 5 fois par seconde, le reste une fois par seconde.
- **Objectif** : identité (`i`), état de la carte (`t`) ; Redémarrer l'objectif (`b`), Alim OFF / ON (`p0` / `p1`).
- **Focus** : ±10 / ±100 pas, curseur entre les bornes avec la marque, « Aller à », Stop (`q`), Marquer (`js`), Aller à
  la marque (`jg`).
- **Ouverture** : curseur au tiers de diaphragme entre les bornes lues (`a`), ±⅓, pleine ouverture ; la valeur affichée
  est celle que l'objectif rapporte (`o`).
- **Journal** : `LOG ON`, `LOG ALL`, `LOG OFF` ; les trames sont décodées ; une ligne de commande libre ; le journal
  s'exporte en fichier texte.

La page ne porte que ce que la carte sert : ni homing (`h`), ni calibration (`c`), ni module, firmware de l'objectif,
focale ou distance (`n`, `w`, `l`, `d`), ni `DUMP`, `SNIFF`, `SDRIVE`.

Toute autre application série convient aussi : une ligne par commande, `PROTOCOL.md`.

## Le commutateur Custom du Samyang AF 135 (`5_App/tool_station/`)

Le Samyang AF 135 F1.8 FE a un commutateur à deux positions, M1 et M2 ; ce que fait chaque position (AF, MF, ou la bague
sur l'ouverture) est une configuration rangée dans **sa flash**. `5_App/tool_station/tool_station.py` la lit et l'écrit
par la carte, sans l'outil de Samyang :

```sh
pip install -r 5_App/tool_station/requirements.txt     # pyserial
python 5_App/tool_station/tool_station.py --read --port /dev/ttyACM0
python 5_App/tool_station/tool_station.py --M1 AF --M2 AP --port /dev/ttyACM0    # le réglage d'usine
python 5_App/tool_station/tool_station.py --man        # le mode d'emploi complet
```

Avant d'écrire, l'outil montre la configuration lue et celle qu'il va écrire, et demande une confirmation ; après, il
relit. **Après une lecture ou une écriture, débrancher puis rebrancher l'objectif.** La carte refuse tout autre objectif
que le 135 (`er nocap custom`). Le format est établi sur le firmware 1.06 de l'objectif ; sur une autre version il n'est
pas vérifié : **l'écriture se fait aux risques de l'utilisateur.** À la console de la carte, les mêmes opérations sont
`CUSTOM READ` et `CUSTOM WRITE <M1><M2>` (`PROTOCOL.md` § 3.1).

## Sécurité : rien n'écrit dans un objectif

Un objectif dont la mémoire est abîmée ne se répare pas. Tout ce que la carte émet vers l'objectif passe par un seul
filtre, `4_Firmware/components/bench_core/bench_core.c`, compilé derrière chaque émission, sans option pour le lever. C'est
une **liste blanche** : ce qu'elle ne liste pas est refusé, dont la mise à jour du firmware de l'objectif (`0x14`, `0x15`)
et toute commande de service qui écrit sa mémoire. Aucune commande ne permet de construire une trame arbitraire.

**Une exception, une seule, décidée par l'humain** : l'écriture de la configuration du commutateur Custom du **Samyang AF
135** (`CUSTOM WRITE`, que sert `tool_station`). Le filtre ne la laisse passer qu'au 135 reconnu, et seulement pour les
neuf valeurs valides ; la même commande vers un autre objectif, un autre Samyang compris, est refusée.

Le canal de service des Samyang (`0x40`) n'est ouvert qu'à un Samyang reconnu, par le nom ou par le code que l'objectif
donne de lui-même.

## Tests (sans carte)

```sh
bash 4_Firmware/run.sh               # le firmware : gcc seul
bash 5_App/run.sh                    # la page de banc : node ; Playwright et Chromium pour test_app.js
bash 5_App/tool_station/run.sh       # tool_station : python3
```

Chaque script se lance depuis n'importe quel répertoire. La suite du firmware joue le vrai code de la carte (tout sauf
ce qui touche au matériel) contre deux faux objectifs, un Samyang AF 135 et un Tamron F051, et contre les traces réelles
du 135 (`4_Firmware/traces`), puis des mutants qui doivent tous la faire échouer. Pour `5_App/test/test_app.js` :
`npm install` à la racine (Playwright épinglé dans `package.json`) et un Chromium sous
`/opt/pw-browsers/chromium-*/chrome-linux/chrome` ou désigné par `CHROMIUM_PATH`. Le driver INDI est compilé et lié par
`bash 6_Driver/indi/build.sh`, sans test.

La CI (`.gitea/workflows/ci.yml`) joue ces trois commandes, construit le firmware, et compile et lie le driver INDI.

## Arborescence

```
README.md          la vitrine du projet, en anglais
CLAUDE.md          l'environnement de travail du dépôt : conventions, commandes, pièges (pour qui le modifie)
0_Datasheets/      les datasheets des composants de la carte
1_Board/           la carte v1.0 : schéma, vues, modèle 3D (le STEP, zippé), Gerbers, BoM, placement (son README)
2_Hardware/        le boîtier : STEP du dessus et du dessous ; la nomenclature de la quincaillerie (vide)
3_3D-Models/       le boîtier à imprimer (3MF)
4_Firmware/        le firmware (ESP-IDF) ; sim/, les faux objectifs et les tests ; traces/, les traces du 135 ; run.sh,
                   build.sh ; gen_lens_names.py, le générateur de la table des noms d'objectifs
5_App/             la page de banc (emount-bench.html) et ses tests (run.sh)
  tool_station/    le commutateur Custom du Samyang AF 135
6_Driver/indi/     le driver INDI « Basilisk E-mount Lens » (LGPL 2.1, son README)
7_Docs/
  PROTOCOL.md      le contrat série de la carte : lettres, réponses, erreurs, journal
  DOCTRINE.md      pourquoi le code est écrit comme il l'est
  MANUEL.md        ce fichier
8_References/      ce que le projet consulte, pas ce qu'il produit (la documentation d'interopérabilité des
                   objectifs ira dans 7_Docs/)
9_Assets/          le dossier de l'humain : la fiche et l'affiche lues par le site zUrp, les images du README
```

## Licence

- **Hardware et conception : OCL v1.1** (Open Community Licence, sans module complémentaire, `LICENSE-HARDWARE`,
  https://github.com/OpenCommunityLicence/OpenCommunityLicence) : `1_Board/`, `2_Hardware/`, `3_3D-Models/`,
  `7_Docs/` et `9_Assets/`.
- **Tout le reste : MIT** (`LICENSE`) : `4_Firmware/`, `5_App/`, `.gitea/`, `.github/` et les fichiers de la racine.

Exceptions, qui gardent leur licence : `6_Driver/indi/` est sous LGPL 2.1 (`6_Driver/indi/LICENSE`, il dérive du
driver Pinefeat) ; les datasheets de `0_Datasheets/` et les documents de tiers de `8_References/` restent à leurs
auteurs ; les deux tables de noms d'objectifs générées par `4_Firmware/gen_lens_names.py` depuis ExifTool (`Sony.pm`, licence Perl),
`4_Firmware/components/host/lens_names.c` et la table `LENS_NAMES` de `5_App/emount-bench.html` (la table seule, pas
le reste de la page), gardent la licence de leur source.
