# Protocole série `emount-bench`

Un seul protocole sur l'USB, compatible octet pour octet avec le driver INDI `pinefeat_cef`
sur les lettres `f r e a d s c`, et avec le driver ASCOM de Pinefeat sur tout ce qu'il envoie,
`v e r f m c a` (§ 2.1) — `v`, elle, rend la version du firmware de la carte (§ 2) —, étendu
par des lettres que ces drivers n'envoient jamais, avec Moonlite en repli et un espace de noms
« labo » en majuscules. **Pas de mode, pas de
négociation, rien de persistant côté protocole.**

## 1. Règles

1. **Ligne = message.** Requête terminée par `\n` (`\r` ignoré). Réponse : **une** ligne.
   Une ligne de texte (réponse, ligne `*` du journal) que la carte n'a écrite qu'en partie sur l'USB est perdue
   (`dropped`, `DEBUG`) ; sa fin de ligne est écrite en tête de la ligne de texte suivante, qui ne s'y colle donc pas.
   Une réponse Moonlite (§ 4), sans fin de ligne, n'en reçoit jamais : intercalée, elle reste telle quelle, collée à la
   ligne coupée s'il le faut.
2. **Dispatch par le premier octet** : `:` → Moonlite (terminé par `#`, pas de `\n`) ;
   `A`–`Z` → labo ; tout le reste → lettres minuscules (contrat driver).
3. **La carte ne parle jamais la première.** Aucune bannière au démarrage, aucune ligne
   spontanée — sauf après `LOG ON` (labo), où des lignes `* …` sont émises.
4. **Réponses aux lettres que le driver de série envoie** (`f r e a d s c`, et `m<n>` du driver
   ASCOM, § 2.1) : ≤ 15 caractères, forme figée (colonne « série » ci-dessous). `v`, que ce driver envoie aussi,
   n'est pas figée : elle rend la version du firmware de la carte (§ 2). Les autres lettres
   répondent aussi long que nécessaire.
5. **Erreurs** : `er <code> [jetons]`. Codes : `busy` (une commande en vol ; + `boot` pendant un démarrage, `home`
   pour `q` pendant le homing, `move` pour `b` pendant un déplacement), `range` (+ `num`, `nomark`, `ap` l'ouverture
   hors de la plage, la borne ou la forme attendue), `nolens`, `fault` (la session en `fault`, § 7), `nocap` (seul, ou
   + capacité courte : `ap` l'ouverture, `focal`, `custom`), `link` (+ quoi : `limits`, ou le jeton de l'échec,
   `timeout`, `bus`…), `refused`. Jamais la sous-chaîne `ok` dans une erreur (le driver de série teste
   `strstr(res, "ok")`). `nc` est la réponse « pas d'objectif » des lettres série `f r a d c` et de `m<n>` (le driver
   la connaît) ; `er nolens` pour les autres.
   `er busy move` : un déplacement est en cours (goto), une séquence ne démarre pas. Côté carte, le jeton de
   `er link …` est celui de l'énumération unique des erreurs (`4_Firmware/include/bsk_err.h`), jamais un texte.
6. **Poll, pas de push** : position, mouvement, ouverture se lisent. L'app lit `f`/`e` à 5 Hz.
7. **Rien n'écrit dans l'objectif** : aucune commande ne permet de construire une charge
   arbitraire ni d'atteindre une écriture de calibration ou de firmware. La politique de
   `bench_core` reste compilée derrière tout ce qui émet, comme dernière barrière.
   **Une exception, une seule** (décision de l'humain) : `CUSTOM WRITE` (§ 3.1) écrit dans la
   flash du **Samyang AF 135** la configuration de son commutateur Custom, et rien d'autre ; `bench_core` ne la laisse
   passer qu'au 135 (`components/bench_core/bench_core.c`, `svc135_allowed`).
   Cette politique est une liste blanche, et ce qu'elle ne liste pas est refusé. Elle admet les types
   `0x01` `0x03` `0x04` `0x07` `0x08` `0x09` `0x0A` `0x0B` `0x0D` `0x10` `0x1C` `0x1D` `0x3F`, vers tout objectif, et le
   canal `0x40`, 19 octets complets, aux seules sous-commandes de § 3.1 (« L'exception de `bench_core` »), vers un
   Samyang reconnu. Une trame dont les messages ne se pavent pas exactement par leurs tailles est refusée ; `0x14`,
   `0x15` (mise à jour du firmware) et `0x16` le sont, comme tout autre type.

## 2. Lettres minuscules — contrat driver

**Les octets se comptent comme dans `7_Docs/E-Mount/protocol.md` § 0, la référence publique**, partout dans ce document :
l'**offset** *k* d'un message est son *k*-ième octet **après son octet de type**, compté depuis 0 (l'offset 0 suit le
type ; `msg[k + 1]` dans le code) ; la **taille** d'un message compte son octet de type (un `0x1D` fait 5 octets) ; une
position dans la trame entière est l'« octet *k* de la trame », compté depuis 0 (`F0` en 0, le type du premier message
en 5).

| Lettre | Requête | Réponse (série `f r e a d s c` = Pinefeat, figée) | Erreurs |
|---|---|---|---|
| `v` | version du firmware de la carte : `<maj>.<min>`, sans suffixe (décision de l'humain) : `PROJECT_VER` de `4_Firmware/CMakeLists.txt`, sa seule source, rendue telle quelle, dans tout état, objectif ou non. Hors du jeu figé : le driver INDI `pinefeat_cef` d'origine ne lit les bornes (`r`) que si le mineur est au moins 3 ; sous un mineur 3, il ne les lit plus, et c'est accepté. Le driver ASCOM Pinefeat ne fait que journaliser `v` | `1.0` | — |
| `f` | position, en `ready` quand elle est valide (un `0x06` valide, ligne `r`) ; sans elle — `ready` atteint sans aucun `0x06`, ou leur flux arrêté (§ 7) —, `er link pos`, jamais une valeur | `16384` · `nc` | `er link pos` (aucun `0x06` valide), `er busy boot`, `er fault` |
| `f<n>` | aller à (absolu, pas) | `ok` | `er busy` (une commande en vol), `er busy boot`, `er fault`, `er range 65535` / `er range num`, `er range limits` (cible hors des bornes de la ligne `r`, celles que l'objectif publie dans son `0x06` resserrées de 5 pas, ou aucun `0x06` valide, ligne `r` ; refusée avant émission), `nc` |
| `f+n` / `f-n` | relatif (calcul en 64 bits) | `ok` | idem, `er range 65535` si le résultat sort de 0..65535 (depuis la position lue, rien n'est émis) ; `er range limits` s'il sort des bornes (ligne `r`) |
| `r` | bornes (lecture pure, aucun effet de bord) : les bornes que l'objectif publie dans chaque `0x06`, offsets 7-8 et 9-10, **resserrées de 5 pas** à la lecture (borne basse + 5, borne haute − 5 : la butée n'est jamais atteinte), valides comme la position ; `er link limits` tant qu'aucun `0x06` n'est lu, et quand leur flux s'est arrêté : en `ready` (et pendant le retour à la marque), un flux de `0x06` arrêté depuis 2 s (la durée de la perte, § 7) alors que celui des `0x05` court rend la position et les bornes inconnues, comme si aucun `0x06` n'avait été lu, jusqu'au suivant. Rien n'est mesuré ni rangé | `13723-30988` | `er link limits` (aucun `0x06` valide), `er busy boot`, `er fault`, `nc` |
| `e` | en mouvement, ou un démarrage en cours, de `powering` à `restoring` et en `recovering` (§ 7) : `y` aussi pendant le retour à la marque qui finit un démarrage (`boot_state=restoring`) | `y` / `n` | — (répond dans tout état : `n` sans objectif et en `fault`) |
| `g` | résultat du **dernier déplacement suivi** : ceux du `move_rc` de `DEBUG` (`f<n>`, `f±n`, `m<n>`, `jg`, `:FG#`, l'appui court du bouton du fût ; pas le retour à la marque d'un démarrage). `ok` arrivé ; `aborted` arrêté par `q` ; `stall` bloqué (`STALLED` : la carte a émis un `0x1C`, § 3) ; `unconfirmed` **arrêt non confirmé** : le `0x1C` qui a fini ce déplacement (`q`, ou celui de `STALLED`) n'a été ni accusé ni suivi d'immobilité après son troisième envoi, 4,5 s après le premier (§ 3) — l'état du moteur est inconnu, et cela l'emporte : `aborted` ou `stall` passe à `unconfirmed` ; un `q` sans déplacement suivi en vol n'y change rien, ni l'arrêt du retour à la marque ; `-` aucun. Écrit à la fin de chaque déplacement suivi, `-` si cette fin arrive hors de `ready` (D2 retombé, perte) ; gardé sinon, sortie de `ready` comprise, jusqu'à la fin du suivant : pendant un déplacement, `g` rend le résultat du précédent (`e` dit s'il est en cours). Une lecture : rien n'est envoyé à l'objectif, rien ne change, dans tout état ; `g` suivi d'autre chose, une ligne inconnue (`er nocap`). Hors du jeu Pinefeat : aucun de ses deux drivers ne l'envoie | `ok` / `aborted` / `stall` / `unconfirmed` / `-` | — (répond dans tout état) |
| `a` | plage d'ouverture : la réponse de l'objectif au `0x08` de l'init, offsets 0-1 (min) et 2-3 (max), codes 256·(Av+16) convertis en f/ et arrondis au tiers de diaphragme usuel ; tout objectif, par le protocole standard ; sans `0x08` lu dans la session, ou si l'un de ses deux codes est hors du domaine que la carte convertit, f/0,5 à f/256 (`0x0E00` à `0x2000`, bornes comprises : la plage est alors inconnue, la réponse n'est pas renvoyée), `er nocap ap` | `1.8-22` | `er nocap ap`, `nc` |
| `a5.6` / `a+1` / `a-0.3` | ouverture absolue / relative (f/) : relative depuis l'ouverture relue (ligne `o`), et quand elle est inconnue (`o` rend `er link aperture`), `a+x` et `a-x` rendent `er nocap ap` sans rien envoyer (`a<f>` ne la lit pas) ; la consigne part dans chaque `0x03` de la boucle (offsets 3-4 et 5-6), bornée à la plage de `a` ; f/1,8 au début de chaque session, ramenée à la plage de `a` dès que l'objectif l'a donnée (sa réponse au `0x08` : f/4 à un objectif qui ouvre à f/4 ; `BF 11` au Samyang AF 135, dont la plage commence à f/1,83) ; `er busy` pendant un déplacement (une commande en vol). Face à un objectif qui publie son ouverture aux offsets 17-19 (la bague en ouverture native, « La bague », sous ce tableau), le dernier geste gagne : `a<f>` tient tant que la bague ne bouge pas ; au premier mouvement de la bague, l'objectif repart de **son** ouverture, pas de celle de `a<f>`, et la carte la recopie (décision de l'humain : la carte ne le resynchronise pas) | `ok` | `er range ap` (hors de la plage ± 0,05 ; la plage se lit par `a`), `er range num` (NaN, inf refusés), `er busy …`, `er nocap ap` (sans plage ; `a±x` sans ouverture relue) |
| `d` | distance de mise au point : non servie, la carte ne la connaît pour aucun objectif | `-` (inconnue ; forme Pinefeat : `1.25` / `inf` / `-`) | `nc` |
| `s<n>` | vitesse (Pinefeat) | `ok` | ignorée |
| `c` | en READY, `ok` sans rien envoyer à l'objectif, aucune séquence (`e` reste `n` hors mouvement) ; hors de READY, comme `f<n>` : `nc`, `er busy boot`, `er fault`. Les bornes de `r` sont celles que l'objectif publie, il n'y a rien à mesurer ; le `ok` est pour le driver ASCOM de Pinefeat (§ 2.1) et pour le driver INDI Pinefeat d'origine, qui exige `ok` à `c` et attend que `e` réponde `n` ; il ne relit `r` ensuite que si le mineur de `v` est au moins 3 (ligne `v`). Le homing n'a lieu qu'au démarrage | `ok` | `er busy boot`, `er fault`, `nc` |
| `cm` | non servie, une ligne inconnue | `er nocap`, dans tout état | — |
| `o` | ouverture courante (rapportée par l'objectif) : offsets 0-1 du `0x05`, bornée à la plage de `a` ; `er nocap ap` sans plage ; `er link aperture` quand elle est inconnue — aucun `0x05` lu, leur flux arrêté (§ 7), ou un code hors du domaine de la ligne `a` —, jamais une valeur ramenée à la plage | `5.6` | `er nocap ap`, `er link aperture` |
| `l` | focale : non servie | `er nocap focal`, dans tout état | — |
| `m<n>` | aller à `n` (absolu, pas) — le `Move` du driver ASCOM de Pinefeat (§ 2.1) —, **exactement comme `f<n>`** : `n` des chiffres, mêmes bornes, mêmes refus, même `ok`, même `nc` sans objectif, même suivi (`e`, `move_rc` de `DEBUG`). `m` suivi d'autre chose qu'un chiffre (`m`, `ms`, `mg`, `mx`, `m+5`) : une ligne inconnue, `er nocap`, dans tout état ; la marque est sur `j` | `ok` | `er busy`, `er busy boot`, `er fault`, `er range 65535` / `er range num`, `er range limits`, `nc` |
| `j` / `js` / `jg` / `jx` | **marque** (sa lettre, `j`, qu'aucun des deux drivers Pinefeat n'envoie) : une position focus choisie, mémorisée **dans la carte** (NVS, par objectif et par focale — rien n'est écrit dans l'objectif). `j` la lit, `js` la pose à la position courante, `jg` y va (bornes appliquées), `jx` l'efface ; toute autre ligne qui commence par `j` : `er nocap`. Le **bouton du fût** fait la même chose, décidé au relâchement : < 1 s = `jg`, 1 à 4 s = `js` (le long pour écrire : pas d'écrasement par mégarde), ≥ 4 s = rien ; jamais pendant une séquence, et un appui né pendant un homing (bouton tenu à la mise sous tension) est oublié à sa fin. La clé NVS : espace `mark`, les offsets 0-1 et 9-10 du `0x07`, l'offset 0 de la réponse au `0x08`, la focale nominale du `0x05` arrondie au mm ; la valeur porte aussi le sens du dernier changement de position vu au `0x06` avant la pose, relu inconnu d'une valeur rangée sans lui ; `js` dans les bornes de la ligne `r` (`er range limits`, sans `0x06` valide aussi) ; `er nocap` sans identité (pas de `0x07`) ; `er busy` pendant un déplacement, ou si l'écriture ne peut pas être prise (rien de changé) ; `ok` rend la marque tout de suite, l'écriture en NVS suit (une écriture ratée : `* mark … store=er`, et la marque rangée est relue) ; `j` rend `-` tant que la marque d'un nouvel objectif ou d'une nouvelle focale n'est pas relue. Le bouton : offset 64 du `0x05`, bit 3, sur tout objectif qui le publie ; en `READY` seulement, un appui commencé ailleurs est oublié ; pendant un déplacement, ignoré ; son goto (court) passe par l'admission de `f<n>` (bornes, `e`, `move_rc` de `DEBUG`) ; chaque relâchement au journal (`* btn`). La carte revient seule à la marque à la fin de chaque démarrage (§ 7), avec l'approche que `jg` et l'appui court n'ont pas ; rien n'y est écrit dans la NVS | `16340` / `-` · `ok` | `er range nomark`, `er range limits`, `er busy …`, `er nocap` (sans identité) |
| `i` | identité : **le nom que l'objectif donne lui-même**, réponse au message `0x3F` demandé dans l'init, juste après le `0x07` (300 ms) : 64 caractères au plus, espaces de fin retirés, rejeté s'il contient un octet hors `0x20`..`0x7E` ou un `"` ; oublié au changement d'objectif. **Repli**, sans nom (pas de réponse en 300 ms, nom vide ou rejeté) : le nom ExifTool du code LensType2 (`0x07` offsets 9-10, table `lens_names.c` générée depuis `Sony.pm` par `4_Firmware/gen_lens_names.py`) ; un code absent de la table : `#<code>`, en décimal ; sans `0x07` lu : `-` | `SAMYANG AF 135mm F1.8` (nom `0x3F`) · repli : `Sony FE 24-105mm F4 G OSS` · `#8` (le 135, absent de la table) | `er nolens` |
| `n` | module qui sert l'objectif : non servie | `er nocap`, dans tout état | — |
| `w` | firmware de l'objectif : non servie | `er nocap`, dans tout état | — |
| `h` | homing : non servie (le homing n'a lieu qu'au démarrage, § 7) | `er nocap`, dans tout état | — |
| `u` / `ux` / `uf` / `ua` | rôle de la bague : non servies, des lignes inconnues ; le rôle suit le commutateur de l'objectif, aucune ligne ne le force (ce serait inverser le commutateur) ; il est lisible dans `t` (`ring`, § 5) et au journal (`* ring`, § 3) | `er nocap`, dans tout état | — |
| `k` / `k<n>` | le **plafond de la LED de statut**, en pour cent du rapport cyclique plein. `k` le lit ; `k<n>`, `n` de 0 à 100 (chiffres seuls), le règle : tout motif de la LED est mis à l'échelle (100 % d'un motif = le plafond), `0` l'éteint. Dans tout état, objectif ou non ; rien n'est envoyé à l'objectif ; **rien n'est rangé** : 100 à chaque démarrage de la carte, le driver le renvoie à la connexion | `100` · `ok` | `er range 0 100` (hors de 0 à 100, ou illisible) |
| `q` | arrêt. En `READY`, le `0x1C`, même sans mouvement, et le mouvement en cours arrêté (`move_rc=aborted`) ; pendant le retour à la marque qui finit un démarrage (`boot_state=restoring`, § 7), le `0x1C`, le retour s'arrête, `boot_state=ready`, la position lue sur l'objectif ; dans les autres états, `ok` sans rien émettre. Une annulation n'est pas un échec, rien n'est repris tout seul. En `READY` et en `RESTORING`, un `0x1C` qui n'a pas pu être émis répond `er link <jeton>` (`er link bus`) ; le mouvement en cours est arrêté quand même (`move_rc=aborted`), et le retour à la marque quitté (`boot_state=ready`). Pendant un homing, `er busy home` : rien n'est arrêté, rien n'est émis ; un homing, c'est celui du pilote (`boot_state=homing`) et le `0x10` de l'init (`boot_state=identifying`, de son envoi à sa réponse ou à son échéance) | `ok` | `er busy home` (le homing ne s'interrompt pas), `er link …` (l'arrêt n'a pas pu être émis) |
| `p` / `pl0` / `pl1` / `pm0` / `pm1` | alimentations, un rail : non servies | `er nocap`, dans tout état | — |
| `p0` / `p1` | couper / rétablir les alimentations de l'objectif ; `p0` **maintient** l'alim coupée (aucun démarrage tant que `p1` ou `b` n'est pas passé, `t` → `power_off=1`) ; `p1`, dans tout état | `ok` (sans effet sur des rails non câblés) | `er busy boot` (`p0` pendant un démarrage), `er busy` (une commande en vol) |
| `b` | redémarrer l'objectif (oubli, alim, séquence complète ; asynchrone, `e`=`y` pendant) | `ok` | `er busy …` |
| `t` | état de la carte, une ligne `clé=valeur` | voir §5 | — |

Un driver ne connaît pas une lettre → il ne l'envoie pas. Une carte Pinefeat ne connaît pas
une lettre → elle répond `er` : c'est ainsi que notre driver sait à qui il parle. `t` le dit en
tête, `ext=1` (§ 5) ; à une carte Pinefeat, `t` rend `er`.

**Samyang reconnu.** Un Samyang est reconnu par son nom `0x3F`, commençant par `SAMYANG ` ou `LK SAMYANG `, ou par son
LensType2 (`0x07` offsets 9-10) parmi 8, 9, 12, 13, 20, 21, 23, `0xC938`, `0xC93A` ; la réponse `03 70` du `0x07` ne
suffit pas (le Tamron F051 la donne aussi), et aucun message du canal `0x40` ne part avant la reconnaissance. Le Samyang
AF 135 est le Samyang reconnu de LensType2 8. La reconnaissance décide du `0x08` de l'init (§ 3, `* std … step=08`) et du
canal `0x40` (§ 1, règle 7 ; § 3.1).

**La bague.** Le rôle de la bague de l'objectif suit son commutateur, sur tout objectif qui publie l'offset 62 du `0x05`
(bits 0-1 : `01` position AF → ouverture, `03` MF → focus ; le commutateur AF/MF du Sony, Custom du Samyang, M1 = `01`) — un
Sony en position AF a lui aussi sa bague sur l'ouverture. En rôle ouverture, le bit 1 de l'offset 3 du `0x04` est posé
(l'objectif lâche le focus), et la bague fait l'ouverture : deux impulsions de même sens (offset 60 du `0x05`, `FF` vers
l'ouvert, `01` vers le fermé ; lui seul : les offsets 32 à 38 du `0x06`, non nuls quand on tourne la bague d'un Sony en
position AF, sont la trace de vitesse de la position mesurée du focus — l'expression est de weiziqian,
`https://github.com/weiziqian/E-mount-protocol-RE` ; son asservissement qui tremble —, pas la bague)
font un tiers de diaphragme (un changement de sens repart de zéro, une pause de 400 ms clôt le geste ; trois tiers par
seconde au plus : un tiers dû moins de 1/3 s après le précédent est perdu, jamais appliqué plus tard, et rien ne bouge
quand la bague s'arrête), ou l'ouverture que l'objectif publie (offsets 17-19) est prise quand elle change ; la consigne
suit le chemin de `a<f>` (bornée à la plage de `a`, dans le `0x03`), en `READY` seulement, jamais pendant un déplacement
ni, après lui (arrivé, `q`, échéance), avant que l'offset 60 ne soit revenu à 0 (il y est l'octet de mouvement) ; sans
plage de `a`, rien ; `ring=-` tant que l'offset 62 n'est pas publié ; ni `'W'` ni canal `0x40`. Focus et ouverture
restent pilotables par les lettres dans tous les cas. La carte se présente au Samyang comme un boîtier moderne, le `0x08`
de l'init porte `0x06` (§ 3, `* std … step=08`) ; le commutateur suit la configuration que l'objectif garde dans sa flash
(APERTURE, AF ou MF pour chaque position ; usine M1 = AF, M2 = APERTURE), que rien ne force (elle se lit et s'écrit par
`CUSTOM`, § 3.1) ; le rôle de la bague suit l'offset 62 quelle que soit cette configuration.

### 2.1 Le driver ASCOM de Pinefeat

Le driver ASCOM de Pinefeat est
public (https://github.com/pinefeat/cef135, `PinefeatCEF/FocuserDriver/FocuserHardware.cs`, GPL v2 ; lu, rien n'en est
repris). Ce qu'il envoie, ce qu'il attend, ce que la carte rend en `READY` (hors de `READY`, la ligne de § 2) :

| Il envoie | Il attend | La carte rend |
|---|---|---|
| `v`, à la connexion | une chaîne, journalisée | la version du firmware de la carte (`1.0`) |
| `e` | `y` exact = en mouvement, sinon arrêté | `y` / `n` |
| `r` | découpée sur `-`, l'entier du dernier morceau = le pas maximal | `<min>-<max>` (`13723-30988`) |
| `f` | `int.Parse` de la réponse = la position | un entier seul (`16384`) |
| `m` + position (`Move`, absolu, `Math.Max(0, Position)`) | `ok` exact, sinon une exception | `ok` (comme `f<n>`), sinon une erreur `er …` / `nc` |
| `c` | `ok` exact | `ok`, rien d'envoyé à l'objectif |
| `a` + ouverture, au format `0.0####` (`a2.8`, `a22.0`) | `ok` exact | `ok` |
| `a` | la plage, sauf une réponse qui commence par `er` ou `nc` | `<min>-<max>` en f/ (`1.8-22`) |

Il n'envoie ni `Halt` (non implémenté) ni rien pour la compensation en température. Il n'envoie aucune lettre de la marque
(`j`) ni aucune autre lettre de § 2. Joué par `4_Firmware/sim/test/test_host.c:t_ascom`, réponses écrites à la main.

## 3. Majuscules — labo (jamais un driver)

| Commande | Rôle | Réponse |
|---|---|---|
| `LOG ON\|ALL\|OFF` | journal, en lignes `* …` (ci-dessous) bornées à 30 par seconde : les transitions, les séquences et les trames non périodiques ; `ALL` ajoute les périodiques (0x02, 0x03/0x04, 0x05/0x06). Les événements passent par un anneau de 8 Ko vidé en tâche de fond : jamais de tâche bloquée par l'USB ; anneau plein = lignes perdues, comptées dans `dropped` (`DEBUG`) — normal en `ALL`. Sous `ALL`, chaque trame, hors du plafond, en différence (« `LOG ALL` », ci-dessous). Mots insensibles à la casse ; tout autre argument : `er range LOG ON\|ALL\|OFF` | `ok log=1 all=0` |
| `DEBUG` | le diagnostic de la carte, sur une ligne, dans tout état, rien envoyé à l'objectif ; la ligne exacte, sans argument (`DEBUG …` : `er nocap`). `reset` : la cause du dernier redémarrage de la carte (`poweron`, `sw`, `panic`, `int_wdt`, `task_wdt`, `wdt`, `brownout`, `usb`, `jtag`, `unknown`) — `task_wdt` : le chien de garde des tâches a redémarré la carte, parce qu'une tâche qu'il surveille (la session, la réception de l'objectif, le magasin de la marque, ou, affamée, la tâche de repos du système) ne l'a pas nourri pendant 10 s ; une carte figée redémarre au lieu de se taire, objectif coupé et repris comme à la mise sous tension ; `panic` : la carte s'est arrêtée sur une erreur qu'elle ne doit jamais rencontrer (une assertion, comme une VD que la carte ne sait pas produire, ou une tâche qui ne peut pas être créée), puis a redémarré ; `dropped` : les lignes du journal perdues depuis ce redémarrage (l'anneau plein, une écriture ratée sur l'USB, le plafond de 30 lignes par seconde) ; `move_rc` : le résultat du dernier déplacement fini demandé par l'hôte (`f<n>`, `f±n`, `m<n>`, `jg`, `:FG#`) ou par l'appui court du bouton du fût — `ok`, le jeton de l'échec (`stall`, `aborted` par `q`, `timeout`…), `-` aucun, ou oublié quand la session a quitté `ready` ; le retour à la marque d'un démarrage n'y compte pas ; `refused` : les trames refusées par `bench_core` depuis ce redémarrage (rien n'en est émis ; chacune est aussi au journal, `* refused`, ci-dessous). Puis la mesure du temps réel de la carte, depuis ce redémarrage, un outil de diagnostic au banc (aucun driver ne la lit) ; compter n'y change rien : `evq_drop` les événements de la réception (trames, erreurs, fronts de LENS_CS et de VD, rebonds de XDETECT) jetés parce que leur file était pleine, le plus ancien au profit du nouveau ; `ev_back` les événements que la session a reçus datés avant le précédent, `ev_back_us` le plus grand de ces écarts, en µs ; `tx_wait_us` la plus longue attente de fin d'émission d'une trame vers l'objectif, en µs, `tx_timeout` celles qui ont atteint leur échéance de 100 ms ; `pair_late` les paires `0x03`/`0x04` jetées parce qu'une de leurs trames n'a pas pu partir dans sa fenêtre, 2 ms au plus après son échéance (VD + 8,6 ms, VD + 10,1 ms ; § 6), `pair_late_us` le plus grand retard constaté en les jetant, en µs. Puis la marge de pile de chaque tâche de la carte : la plus petite place restée libre sur sa pile depuis ce redémarrage, en octets (`stack_session` la session et cette ligne, `stack_phy` la réception de l'objectif, `stack_usb_rx` la lecture de l'USB, `stack_store` le magasin de la marque, `stack_journal` le journal) ; une marge proche de 0 annonce un débordement de pile, qui redémarre la carte (`reset=panic`) | `ok reset=poweron dropped=0 move_rc=- refused=0 evq_drop=0 ev_back=0 ev_back_us=0 tx_wait_us=0 tx_timeout=0 pair_late=0 pair_late_us=0 stack_session=<n> stack_phy=<n> stack_usb_rx=<n> stack_store=<n> stack_journal=<n>` (les marges, mesurées par la carte) |
| `CUSTOM READ` / `CUSTOM WRITE <h><b>` | la configuration du commutateur Custom du **Samyang AF 135**, rangée dans sa flash — lue, ou **écrite** (l'exception de la règle 7). Détail : § 3.1 | `ok 00 00 00 00 00 00 00 10 00 00 00 00 00 00 00 00` |

Toute autre ligne qui commence par une majuscule : `er nocap`.

Lignes `*` (seulement après `LOG ON`) : `* rx <t> <hex>` (trame > 64 octets : morceaux `* rx <t>
+<off>/<len>#<id> <hex>`, `id` = numéro de trame, identique sur tous ses morceaux), `* tx …`, `* rxflood <n>`,
`* refused <t> <hex>` à chaque trame que `bench_core` refuse, rien n'en étant émis : la trame telle qu'elle serait partie,
comme `* tx` (en morceaux `+<off>/<len>#<id>` au-delà de 64 octets), qu'elle ait été demandée seule ou retenue entre le
`0x03` et le `0x04` de la boucle et refusée à son départ derrière le `0x04` ; sous le plafond de 30 lignes par seconde, sous
`LOG ON` comme sous `LOG ALL` (jamais en différence) ; chaque refus est aussi compté dans `refused` de `DEBUG`, journal
éteint compris.
`* session <t> <état>` à chaque transition de la session (`boot_state`, § 5 ; en `fault`, suivie du jeton de la
raison), `* motion <t> <état>` à chaque transition du mouvement (`idle`, `planning`, `commanded`, `moving`, `settling`,
`arrived`, `stalled`, `aborted`). `* lens <t> 0x05[<off>]=<hh>` à chaque
changement de l'offset 62, 64 ou 66 du `0x05` (le premier de chaque session compris), valeur brute en hexa, jamais une ligne
par trame ; `* ring <t> mode=aperture|focus source=0x05` à chaque changement du rôle de la bague (le commutateur).
`* ring <t> gesture pulses=<n> thirds=<m> frames=<k> capped=<c>` à la fin de chaque
geste de la bague en rôle ouverture, clos par une pause de 400 ms ou par un changement de sens (une ligne par sens) :
`<n>` la somme signée de l'offset 60 de ses `0x05` (positif : vers le fermé, `01` chez le 135), `<m>` les
tiers de diaphragme qui ont changé la consigne pendant lui (0 au bord de la plage, ou sans elle), `<k>` ses `0x05` dont
l'offset 60 n'était pas nul, `<c>` les tiers que le cap de trois par seconde a perdus ;
un geste interrompu par un changement de rôle est oublié sans ligne ;
hors du plafond de 30 lignes par seconde, et pas comptée dans sa fenêtre (seul l'anneau plein la perd).
`* btn <t> press=short|long|held result=ok|er|ignore[ why=<jeton>]` au
relâchement du bouton du fût (`held` : 4 s ou plus, sans effet ; `er` : le jeton du refus, `nomark`, `limit`, `busy`… ;
`ignore` : l'état où l'appui a commencé, `homing`…, ou `busy`, un déplacement en cours) ; `* mark <t> set key=<clé>
pos=<n> dir=inc|dec|unknown`, `* mark <t> clear key=<clé>`, `* mark <t> store=er key=<clé>` (l'écriture NVS a échoué).
Le retour à la marque en fin de démarrage (§ 7), chaque ligne après la ligne `* session` de sa
transition : `* restore <t> mark=<n> dir=inc|dec|unknown x=<X> path=direct` ou `… path=overshoot via=<n>` à l'entrée en
`restoring` (la marque, son sens rangé, le dépassement X, le trajet : droit sur la marque, ou d'abord à `<n>`) ;
`* restore <t> end=arrived|stop|stall|changed|recovering|off|powering` à sa sortie (sur la marque, `q`, bloqué, une autre
focale nominale ou d'autres bornes lues, perte, D2, `b`) ;
`* restore <t> skip=nomark|timeout|limit[ mark=<n>]` quand un démarrage finit sans lui (pas de marque ; sa lecture en NVS
pas rendue en 500 ms ; la marque `<n>` hors des bornes de `r`).
Le contact de présence XDETECT (D2) : `* xdetect <t> absent` à sa retombée, `<t>` l'instant où
la carte a coupé les rails de l'objectif et mis ses lignes au repos, 2 ms après le premier front de la retombée, D2
toujours absent (une retombée plus courte est un rebond, rien n'est coupé, `* xdetect <t> bounce`, `<t>`
l'échéance de 2 ms, sous le plafond de 30 lignes par seconde, sans durée : la carte ne voit que les fronts de retombée) ;
`* xdetect <t> present` à
l'insertion confirmée (D2 tenu 300 ms). Elles alternent, chacune avant la ligne `* session` de la transition qu'elle cause
(`off`, `powering`), s'il y en a une. Elles ne passent pas par le plafond de 30 lignes par seconde (seul l'anneau plein
les perd).
Un message ponctuel renvoyé tel quel (trois envois au total au plus ; seulement ceux qu'un second
envoi ne peut rien changer si le premier a été reçu) : `* resend <t> msg=<hh> n=<k> why=<jeton>` à chaque renvoi (`<hh>` le
type en hexa, `<k>` le numéro de l'envoi, 2 ou 3, `<jeton>` la cause : `timeout`, son échéance ; `bus`, son émission
ratée ; `framing`, une réponse trop courte pour ce que l'init en lit, § 7 ; une erreur de réception pendant l'attente
n'en est pas une, § 6) ; `* resend <t> msg=<hh> giveup why=<jeton>` quand le troisième n'a pas abouti non
plus, puis la carte fait ce qu'elle faisait sans renvoi. Renvoyés : les requêtes de l'init `0x07`, `0x3F`, `0x08`, `0x0B`,
`0x09`, `0x0D` (le `0x0A` et le `0x10` jamais ; le `0x01` garde ses trois essais et le reset soft, sans ligne) ; le `0x1D`
d'un déplacement, seulement quand l'objectif n'a ni démarré ni accusé en 1 s (après un `0x1D` qui a pu être reçu deux fois, un
renvoi ou le report d'un `0x04` raté, § 6, l'accusé `1D 00` ne compte pas), là où, sans renvoi, la carte conclurait au blocage : le
même `0x1D` et une nouvelle seconde, le blocage (`STALLED`) à la fin de la troisième, 3 s après le premier envoi ; le `0x1C`
de `q` (parti ; `ok` ne change pas, c'est le résultat du premier envoi), et celui que la carte émet d'elle-même au blocage
d'un déplacement (`STALLED`, retour à la marque compris ; surveillé même s'il n'a pas pu partir : personne ne l'a demandé,
personne n'en reçoit le résultat), quand l'objectif ne l'a ni accusé ni ne s'est immobilisé en 1,5 s : le même `0x1C`, une
nouvelle fenêtre de 1,5 s ; après le troisième, `* resend <t> msg=1C giveup
why=timeout` dit l'arrêt non confirmé ; si ce `0x1C` a fini un déplacement suivi (`q` qui l'arrête, un `q` suivant pendant
cette surveillance, ou son `STALLED`), `g` rend `unconfirmed` (§ 2). Un déplacement demandé ensuite (ou la fin de la
session) arrête la surveillance : aucun `0x1C` renvoyé ne l'arrête.
Une erreur de réception :
`* rx <t> err=<jeton>` sans octets, ou `* rx <t> err=<jeton> n=<n>[ omitted=<k>] <hex>`
— `n` octets reçus écartés d'un seul tenant, les premiers (256 au plus, `k` omis au-delà), en morceaux
`+<off>/<len>#<id>` au-delà de 64 octets : `err=framing`, pas de trame valide (octet parasite, trame invalide,
début de trame sans suite depuis plus de 20 ms) ; `err=bus`, une erreur du pilote UART (débordement, erreur de
trame, de parité, break), qui vide la réception : les octets reçus qu'aucune trame n'a pris et pas encore rapportés
— les octets écartés qui attendaient leur `err=framing` (ils n'en auront pas), puis un début de trame qui attendait sa
suite —, aucun s'il n'y en avait pas, ni pour un front de LENS_CS ou de VD perdu. Un octet n'est rapporté qu'une fois.
La réception lit un flux, sans se découper sur LENS_CS. Une erreur de réception ne coûte que la trame qu'elle touche :
elle ne termine aucune requête (§ 6).
La poignée de main, à sa fin : `* std <t> step=handshake result=ok lens_cs_up_us=<µs>`
(LENS_CS vue haute `<µs>` après BODY_CS haute, 0 si elle l'était déjà, puis retombée),
`result=LENS_CS_never_high` (l'échéance de sa levée a expiré) ou `result=LENS_CS_stuck_high
lens_cs_up_us=<µs>` (vue haute, l'échéance de sa retombée a expiré) ; le `0x01` part dans les trois cas.
Le `0x08` de l'init, à son envoi, une fois par init (pas à ses renvois) :
`* std <t> step=08 body_flags=<hh> samyang=yes|no` — `<hh>` son offset 0 en hexa (`06` pour un Samyang reconnu, `00`
sinon), `samyang` la reconnaissance à cet instant (nom `0x3F` ou LensType2 du `0x07`, § 2, « Samyang reconnu ») ; hors du
plafond de 30 lignes par seconde, et pas comptée dans sa fenêtre (seul l'anneau plein la perd).

**`LOG ALL`.** Toute trame émise ou reçue, de tout type, est écrite, dans l'ordre, avec son instant
en millisecondes, hors du plafond de 30 lignes par seconde et sans compter dans sa fenêtre ; les autres lignes gardent leurs
règles (les erreurs de réception `* rx <t> err=…` aussi : sous le plafond). Sous `LOG ON`, rien ne change. Une trame s'écrit
sous l'une de deux formes :

- **entière**, `* rx|tx <t> <hex>` (en morceaux `+<off>/<len>#<id>` au-delà de 64 octets), comme sous `LOG ON`. Elle devient
  la **référence** de son sens (`rx` ou `tx`) et de son **type** (le premier octet de son message, l'octet 5 de la trame en
  comptant de 0). Le **pas** de séquence d'une référence écrite entière vaut 0 ;
- **en différence**, `* rx|tx <t> ~<tt>[ s<hh>][ <o>=<hex>]…` (`<tt>`, `<hh>` en hexa, `<o>` en décimal). La trame a le sens
  de la ligne, le type `<tt>`, la classe, la longueur et les octets de la référence de ce sens et de ce type, sauf :
  - son numéro de séquence : `<hh>` si `s<hh>` est donné, et le pas devient `<hh>` moins le numéro de la référence, modulo
    256 ; sinon le numéro de la référence plus le pas, modulo 256 ;
  - chaque `<o>=<hex>` remplace, à partir de l'**offset** `<o>`, compté dans le message après l'octet de type (l'octet
    `6 + <o>` de la trame en comptant de 0 : l'offset 60 du `0x05` est l'octet 66), les octets `<hex>`, deux chiffres par
    octet, sans espace. Seuls les octets qui ont changé y sont, en suites d'octets consécutifs ;
  - la longueur et la somme se recalculent (somme des octets 1 à longueur − 4 de la trame, sur 16 bits, poids faible
    d'abord ; `55` à la fin).

  `~<tt>` seul : la trame est identique à la précédente de son type, au numéro près. La trame reconstruite devient la
  référence, avec son pas.

Le type d'une trame est celui de son premier message : le `0x04` seul et le `0x04` suivi d'un `0x1D` sont le même type `04`,
mais leur longueur diffère. La carte écrit entière toute trame dont la classe ou la longueur n'est pas celle de la référence,
la première de chaque type après `LOG ALL`, après une ligne perdue ou tue, après un `0x0A` (la mise en page du `0x05` et du
`0x06` change) et toute trame dont la différence ne tiendrait pas sur une ligne. Un décodeur n'a rien de cela à suivre : une
ligne entière est toujours une référence. Les trames écrites en différence sont aujourd'hui celles de la boucle (`0x03`,
`0x04` émis ; `0x02`, `0x05`, `0x06` reçus) ; un décodeur n'en dépend pas.

`* lost <n>` (sous `LOG ALL`) : `<n>` lignes du journal perdues à cet endroit (l'anneau plein, une écriture ratée
sur l'USB ; comptées dans `dropped` de `DEBUG`). Le décodeur y oublie toutes ses références : une différence sans référence est
une trame dont il ne connaît que le sens, le type et l'instant, jusqu'à la prochaine ligne entière de son type, que la carte
écrit à la trame suivante de ce type une fois la perte constatée.
Sous `LOG ON`, `* lost <n>` ne dit qu'une ligne en morceaux (`+<off>/<len>#<id>`) que l'anneau plein n'a prise qu'en
partie : `<n>` ses morceaux perdus (comptés dans `dropped`), dit avant le morceau suivant qui tient, ou s'il n'y en a plus,
avant la ligne suivante qui tient. Une ligne perdue entière, en morceaux ou non, n'y est pas dite (seulement comptée).

Un extrait (les `0x03` de la boucle du Sony FE 24-105 G), chaque ligne suivie, ici seulement, de ce qu'elle dit :

```
* tx 70000 F0 1D 00 01 10 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 CE 02 55
* tx 70017 ~03 s12          identique, numéro 12 : le pas devient 2
* tx 70034 ~03              identique, numéro 14
* tx 70051 ~03 11=10        l'offset 11 (l'octet 17 de la trame) vaut 10, numéro 16
* tx 70068 ~03 11=0005      les offsets 11 et 12 valent 00 et 05, numéro 18
```

Le débit, mesuré au simulateur sur une minute à 60 Hz depuis la mise sous tension (`4_Firmware/sim/test/test_session135.c:
t_log_all_minute`, le faux 135 : son init, trois gotos, la bague tournée 20 s ; `test_std.c:log_all_minute`, le faux Tamron
F051) : environ 3,7 Ko/s en moyenne, 4,3 Ko/s au pire sur une seconde, moins de 3 Ko produits en 500 ms. Le lien USB
Serial/JTAG de l'ESP32-S3 et du C3 est un USB 2.0 pleine vitesse, CDC-ACM, en paquets en bloc de 64 octets (manuels de
référence technique ESP32-S3 et ESP32-C3, chapitre « USB Serial/JTAG Controller ») ; au plancher d'un paquet par trame
USB de 1 ms, 64 000 octets par seconde (la norme USB 2.0, § 5.8.4, en permet jusqu'à 19 par trame en bloc). Ces sources
sont citées de mémoire, sans relecture : le débit du lien sur la carte n'est pas mesuré.

### 3.1 `CUSTOM` — le commutateur Custom du Samyang AF 135

Le Samyang AF 135 F1.8 FE a un commutateur à deux positions, **M1** (haut) et **M2** (bas). Ce que fait chaque position est
une configuration rangée dans **sa flash** : `0` ouverture (APERTURE), `1` AF, `2` MF. D'usine, `0x10` : M1 = AF, M2 =
APERTURE. La carte se présente comme un boîtier moderne (`0x08` à `0x06`, § 2, « La bague ») : l'objectif applique cette
configuration. `CUSTOM` permet de la choisir sans l'outil de Samyang (Lens Station). L'outil `5_App/tool_station/` la sert.

- `CUSTOM READ` : la lit. La carte émet `0x40 'P' 0xFA`, octet de données `0x00` (19 octets, classe 2).
- `CUSTOM WRITE <h><b>` : l'écrit. `h` (M1) et `b` (M2), un chiffre chacun, `0`, `1` ou `2` (`CUSTOM WRITE 10` : le réglage
  d'usine). La carte émet `0x40 'P' 0x38`, octet de données `0x30 + (h << 4 | b)` (`WRITE 21` : `0x51`). L'objectif range la
  valeur en flash et la pose aussi en RAM (`7_Docs/E-Mount/samyang.md` § 6.3).
- **M1 = quartet haut** : l'analyse statique du firmware 1.06 lie le quartet haut à la position haute du commutateur
  (`7_Docs/E-Mount/samyang.md` § 5.1, § 5.2) ; les traces de l'objectif nomment cette position « haut = M1 »
  (`4_Firmware/traces`, astro-normal:35-37), et `0x10` d'usine donne M1 = AF, M2 = APERTURE, le réglage d'usine de Samyang. Pas vérifié sur l'objectif : le premier essai
  le confirme (`5_App/tool_station/`, `--man`).
- Mots insensibles à la casse (`CUSTOM read`) ; tout autre texte après `CUSTOM` : `er range CUSTOM READ|WRITE <0-2><0-2>`.
- **Réponse** : `ok` suivi des 16 octets de données de la réponse de l'objectif, en hexadécimal majuscule, séparés d'une
  espace, tels qu'il les a rendus. À la lecture, **la configuration est le 8ᵉ** (l'offset 9 du message `0x40` ;
  `ok 00 00 00 00 00 00 00 10 …` : `0x10`) : son quartet haut est M1, son quartet bas M2. À l'écriture, l'objectif rend
  l'écho, 16 octets à zéro, qu'il ait rangé la valeur ou non (une valeur hors de `0`-`2` n'est jamais émise) : seule une lecture après dit ce qui est rangé.
- La réponse **attend l'objectif** (il répond au moins 50 ms après ; 1 s au plus, puis `er link timeout`) : aucune autre ligne
  n'est servie entre-temps (une réponse par ligne). Échecs : `er link timeout` (pas de réponse), `er link framing` (une autre
  réponse `0x40` est arrivée d'abord), `er link aborted` (l'objectif retiré, ou la session quittée, pendant l'attente),
  `er refused` (refusé par `bench_core` : jamais pour une commande bien formée).
- **Conditions** : la carte en `ready` (sinon `er nolens`, `er busy boot`, `er fault`, le tableau d'état de `j`), l'objectif
  reconnu Samyang **et** de LensType2 `8` (le 135 ; sinon `er nocap custom`, rien d'émis) ; aucune commande en vol (`er busy`).
  Pendant l'attente, une commande de mouvement (le bouton du fût compris) est refusée, `q` passe.
- **Effets** : l'écriture range un octet de configuration en flash (`7_Docs/E-Mount/samyang.md` § 7.2) ; la
  lecture n'a que des effets en RAM, que l'objectif oublie à sa mise sous tension (branche « JIG », `samyang.md` § 6.3).
  Après l'une ou l'autre, **débrancher puis rebrancher l'objectif** : il repart de sa flash (`samyang.md` § 5.2), sans
  les drapeaux que la lecture a laissés en RAM, et la carte refait son init avec lui.
- **Après une reprise sans coupure de l'objectif** (une perte de liaison, `boot_state=recovering`, puis une nouvelle init, l'objectif resté alimenté), le
  Samyang AF 135 revient à **M1 en AF et M2 en MF**, quelle que soit sa configuration (accepté par l'humain :
  la carte ne s'en corrige pas). On retrouve sa configuration en **coupant son alimentation** (débrancher puis rebrancher
  l'objectif), ou en la réécrivant par **`CUSTOM WRITE`**, qui écrit aussi sa flash.
- **Le risque** : le format est établi par l'analyse statique du firmware **1.06** de l'objectif (`7_Docs/E-Mount/samyang.md`
  § 7.4) ; sur une autre version, il n'est pas vérifié, et la carte ne lit pas la version : l'opération se fait aux
  risques de l'utilisateur.

**L'exception de `bench_core`** (règle 7) : en plus de sa liste du canal `0x40` (`'V'` 00, `'F'` FA/FB/32, `'M'` 00/31, pour
un Samyang reconnu), il laisse passer, **pour le 135 que la session lui déclare seulement** (Samyang reconnu, LensType2 8),
`'P'` `0x38` avec un octet de données `0x30 + (h << 4 | b)`, `h` et `b` de `0` à `2` (neuf valeurs), et `'P'` `0xFA` avec un
octet de données différent de `0x53` (`0x53` pose le drapeau MTF, qui change la réponse à `'F'` FB). Tout autre `'P'` est
refusé (`0x11` et `0x31`-`0x37` écrivent aussi la flash), et ces deux-là vers tout autre objectif, un autre Samyang compris :
la même commande écrit leur mémoire avec une sémantique que personne n'a relue.

## 4. Moonlite — repli

`:GP#` → position hex 4 chiffres + `#` · `:GN#` → cible · `:SN xxxx#` → mémorise la cible ·
`:FG#` → va à la cible · `:FQ#` → arrêt · `:GI#` → `01#`/`00#` · `:GV#` → `10` · `:GT#` → `0000#`
· `:GC#` `:GD#` `:GH#` → `00#` `02#` `00#` · `:SD#` `:SH#` `:SF#` `:SC#` `:PO#` `:C#` `:+#` `:-#`
→ sans réponse · `:SP#` → ignoré · `:FQ#` = `q`. **Octets exacts, sans fin de ligne** (`4000#`, `10`), comme le
driver les lit. Focus seul, muet, pas d'erreur (une commande refusée ne bouge pas, `:GI#` dit
`00#`). `:GP#` rend la position quand elle est valide (`READY`, un `0x06` valide, ligne `r`) ;
sinon, hors `READY` comme en `READY` sans elle, la dernière position valide lue, oubliée au retrait de l'objectif
(`boot_state=off`) ou quand un autre objectif s'identifie, `0000#` sans elle. `:GN#` sans `:SN` rend ce que rend `:GP#` ; la
cible de `:SN` vit autant que la carte. `:GI#` répond comme `e` (`01#` pour `y`) ; `:FG#` passe par
l'admission de `f<n>` (bornes de `r`, déplacement en cours), un refus ne répond rien ; `:FQ#` est `q`.

## 5. `t` — l'état de la carte

Une ligne, `clé=valeur` séparées d'une espace, ces neuf clés dans cet ordre, dans tout état :

`ext=1 present=0|1 boot_state=<état> busy=boot|- power_off=0|1 last_op=<op>:<ok|er>[:"<raison>"] mf=0|1|- oss=0|1|- ring=focus|ap|-`

L'état de la carte, et rien d'autre : rien qu'une autre lettre rend déjà, aucun compteur, aucun diagnostic (`DEBUG`, § 3).

- `ext` : le lot de lettres que la carte sert au-delà du sous-ensemble Pinefeat (`f r e a d s c`, et `v`) : `1`, celles de ce
  document. En tête de ligne. Ce qu'un objectif ne sert pas, sa lettre le dit (`er nocap …`).
- `present` : `0` sans objectif (`boot_state=off`), `1` sinon.
- `boot_state` : l'état de la session, `off`, `powering`, `identifying`, `homing`, `restoring`, `ready`, `recovering`,
  `fault` (§ 7).
- `busy` : la séquence en cours, au jeton de `er busy` : `boot` de `powering` à `restoring` et en `recovering`, `-` sinon.
- `power_off` : `1` tant que `p0` tient l'alimentation de l'objectif coupée (aucun démarrage avant `p1` ou `b`), `0` sinon.
- `last_op` : le résultat du dernier démarrage, `-:ok` avant le premier, `boot:ok` à l'arrivée en `ready`, `boot:er:"<jeton>"`
  à l'arrivée en `fault` (le jeton de la raison : `lost`, `home_failed`…).
- `mf` : le commutateur AF/MF de l'objectif, offset 62 du `0x05`, bits 0-1 : `1` MF (`03`), `0` AF (`01` ;
  commutateur AF/MF du Sony, Custom du Samyang, M1 = `01`), `-` non publié (ou le flux des `0x05` arrêté, § 7), ou ni
  `01` ni `03`.
- `oss` : le stabilisateur optique, offset 64 du `0x05`, bit 4 : `1` en marche, `0` arrêté, `-` non publié (ou le flux
  des `0x05` arrêté, § 7).
- `ring` : le rôle de la bague, celui que la session tient, d'après
  le commutateur (offset 62 du `0x05`, bit 1 : posé, focus ; sinon ouverture, § 2, « La bague ») : `focus`, `ap`
  (ouverture), `-` tant que l'offset 62 n'est pas publié. Il n'est pas recalculé d'après `mf`, qui ne dit que `01` et `03`.

## 6. Transactions et tardives

Le protocole E ne porte aucun identifiant de requête : la réponse tardive à une demande X est
indistinguable de la réponse à la demande X suivante. Une requête est une transaction : une seule en vol, avec
échéance ; elle se termine par sa réponse (la première trame reçue dont le premier message est du type demandé), par
son échéance, ou par son émission ratée. Une erreur de réception (§ 3, `* rx <t> err=…`) ne coûte que la trame qu'elle
touche : la requête attend toujours sa réponse, jusqu'à son échéance. Aucune quarantaine : une réponse tardive au type de
la requête suivante la termine. Rien ne part entre le `0x03` et le `0x04` de la boucle : une requête demandée dans
l'intervalle part juste derrière le `0x04`, son échéance comptée de là. Les renvois d'un message ponctuel : § 3
(`* resend`).
La boucle : à chaque front de VD, le `0x03` à VD + 8,6 ms et le `0x04` à VD + 10,1 ms, chacun servi dans sa fenêtre de
2 ms (`PAIR_WINDOW_US`, `components/txn/txn.c`) : la trame n'est émise que si le pas de la carte qui la sert a commencé
au plus 2 ms après son échéance. La fenêtre se juge au début de ce pas, pas à l'émission : l'instant où la trame part
n'est pas borné, le temps que le pas a passé avant elle n'étant pas compté. Une trame dont le pas commence hors de sa
fenêtre (la carte retenue, une écriture en flash) est jetée avec sa paire, sans rattrapage, et comptée (`pair_late`,
`DEBUG`) : le `0x03` jeté, pas de `0x04` ; le `0x04` jeté, le `0x03` est parti seul, et ce qui attendait le `0x04` part à
sa place. Chaque front repart à zéro : des fronts passés quand le pas commence, seul le plus récent est servi. Le `0x1D`
d'un déplacement, accroché au `0x04`, part avec la première paire qui part : une position absolue, renvoyée telle quelle
à la paire suivante, jamais remplacée par un arrêt. Un `0x04` qui le portait et dont l'émission a raté a pu partir
quand même, et l'objectif le recevoir deux fois : comme après un renvoi (§ 3), un accusé `1D 00` reçu avant que
l'objectif ait démarré ne vaut pas arrivée ; l'arrivée se juge alors sur la position, égale à la cible, et
l'immobilité, sinon c'est le renvoi, puis le blocage.
Côté app : après un timeout, `v` puis 100 ms de silence avant la commande suivante (la carte
répond dans l'ordre : une tardive est passée quand la réponse à `v` arrive).

## 7. Démarrage (sans commande)

La carte démarre l'objectif **dès qu'il apparaît** : l'insertion confirmée par le contact de présence D2 (XDETECT, § 3),
sans commande ; pas après `p0`, tant que `p1` ou `b` n'est pas passé. Séquence fixe : rail logique, rail moteur, VD,
poignée de main, `0x01`, puis l'init : `0x07`, `0x3F`, `0x08`, `0x0B`, `0x09`, `0x0D`, `0x10` (le homing), `0x0A`, et la
boucle ; un `0x01` sans réponse a trois essais, puis un reset soft (un `0x0A`), une fois par session, et l'init repart.
Une réponse trop courte pour ce que la carte en lit est traitée comme l'absence de réponse de son étape : un `0x07` de
moins de 4 octets (l'identité) et un `0x08` de moins de 6 (la plage) sont renvoyés, puis tolérés sans eux ; un `0x10` sans
son octet de résultat (moins de 2 octets) arrête l'init comme sans réponse, et compte un échec au homing du pilote.
L'octet du `0x08` qui entre dans la clé de la marque est lu dès 3 octets ; une réponse au `0x3F` vide n'est pas tronquée.
**Les bornes ne sont ni mesurées ni rangées** : ce sont celles que l'objectif publie dans son `0x06` (ligne `r`), et
elles s'appliquent à tout goto (`f<n>`, `m<n>`, `jg`, le bouton, `:FG#`) : `er range limits`. Un échec (objectif muet
après le reset soft, pas de `0x05`, perte de liaison) est compté : `recovering`, puis un nouvel essai 3 s après, sans
couper les rails ; au quatrième consécutif, `fault` (`last_op=boot:er:"lost"`). La perte est celle des deux flux, `0x05`
et `0x06`, 2 s sans l'un ni l'autre en `ready` ou pendant le retour à la marque ; un seul des deux arrêté depuis 2 s, l'autre
courant, n'est pas une perte : ce qu'il porte devient inconnu, comme jamais reçu, jusqu'à son prochain message — pour le
`0x06`, la position et les bornes (`f`, `r`, `f<n>`, `f±n`, `js`) ; pour le `0x05`, l'ouverture relue (`o`), l'octet
de mouvement, les offsets 62 et 64 (`mf`, `oss` de `t`) et la marque (`j` rend `-`, relue à son retour). Le rôle de la
bague (`ring`) et l'ouverture qu'elle publie restent les derniers : au retour du `0x05`, une ouverture publiée par la
bague différente de la dernière est un changement, pris comme consigne ; l'offset 66 n'est redit au journal que s'il a
changé. Un `0x05` ou un
`0x06` trop court pour être lu (moins de 29 et de 12 octets) ne compte pas comme reçu.

`boot_state` (ligne `t`) suit la session : `off`, `powering`, `identifying`, `homing` (le homing du pilote,
seulement si l'init s'est arrêtée avant son `0x0A`), `restoring`, `ready`, `recovering`, `fault`. De
`powering` à `restoring` et en `recovering`, les lettres qui ont besoin de l'objectif répondent `er busy boot`, `r`
compris, `e` répond `y` ; `q` y répond `ok` (en `homing`, et en `identifying` du `0x10` de l'init à sa réponse ou à son échéance : `er busy home`). **Retour à la marque** : à la fin
de chaque démarrage (mise sous tension, `b`, reprise après une perte), la carte lit la marque de cet objectif et de cette
focale (ligne `j`) — 500 ms au plus, faute de quoi il n'y en a pas — et, s'il y en a une dans les bornes de `r`, y revient
d'elle-même (`restoring`) : elle arrive du côté du sens rangé avec la marque (inconnu : en décroissant) ; si le trajet
direct arriverait de l'autre côté, elle vise d'abord la marque ± X, bornée à `r`, puis la marque ; X = 200 pas sur le
Samyang AF 135 (LensType2 8), sinon 1 % de la course de `r`, au moins 1. Déjà sur la marque, elle n'y va droit que si le
dernier déplacement lu (`0x06`) dans la session était dans le sens de l'arrivée ; sinon, ou sans déplacement lu, par la
marque ± X, comme de l'autre côté : le jeu mécanique est repris. Le retour finit en `ready` : arrivé sur la
marque, arrêté par `q` (le `0x1C`), bloqué, ou abandonné comme par `q` (le `0x1C`) quand une autre focale nominale
(`0x05`, la clé de la marque) ou d'autres bornes (`0x06`) sont lues pendant lui, sans viser la marque de la focale
nouvelle — un flux arrêté, qui les rend inconnues, n'en est pas un changement ; une perte pendant lui est un échec
compté, comme pendant le homing, et la carte reprend. Sans marque, ou hors des bornes : `ready` sans mouvement. Rien n'est écrit dans la NVS ; `jg` et l'appui
court du bouton vont à la marque tels quels, sans cette approche. Journal : `* restore` (§ 3).
