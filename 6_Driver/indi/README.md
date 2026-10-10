# indi_basilisk_focus — driver INDI pour la carte Basilisk

**Date** : 2026-09-24
**Dernière révision** : 2026-10-08
**Statut** : actif — driver 1.0
**Référencé par** : `README.md` (§ Firmware & software), `7_Docs/MANUEL.md`, `6_Driver/indi/CMakeLists.txt`, `6_Driver/indi/UPSTREAM.md`, job `indi` de `.gitea/workflows/ci.yml`

Focuser INDI (Ekos) pour la carte Basilisk et un objectif E-mount. C'est le driver Pinefeat d'origine
(`pinefeat_cef` du cœur d'INDI) avec un lot « étendu » par-dessus. Les défauts du code d'origine sont notés
dans [`UPSTREAM.md`](UPSTREAM.md). Sur décision de l'humain, notre fourche en corrige une partie : la réponse
de 15 caractères (2026-10-04), puis les points R9 de l'audit statique du 2026-10-06 (2026-10-07, plan de
robustesse de l'atelier) ; les autres restent tels quels. `UPSTREAM.md` dit lesquels.

## Base : le driver Pinefeat d'origine

`pinefeat_cef.h` et `pinefeat_cef.cpp` sont les fichiers d'`indilib/indi`,
`drivers/focuser/pinefeat_cef.{h,cpp}`, au commit `1dd9b34f24df6fd74ed45be17823fc937e192b47`. Leurs
propriétés, leurs noms et leur comportement sur `v f r e a d s c` sont repris tels quels :
`FIRMWARE_VERSION`, `FOCUS_DISTANCE`, `CALIBRATE`, `ABS_APERTURE`, `REL_APERTURE`, `RANGE_APERTURE`,
et les propriétés standard des focusers INDI (position absolue et relative, `FOCUS_MAX`, vitesse).
`CALIBRATE` est celui d'origine : la carte répond `ok` à `c` sans rien envoyer à l'objectif, le driver relit `r`. La
carte ne connaît pas la distance de mise au point : `FOCUS_DISTANCE` affiche sa réponse à `d`, `-`.

Les écarts au comportement d'origine :

- **`r` est toujours lu.** L'amont ne le lit que si le mineur de `v` est au moins 3 ; ce contrôle est
  retiré.
- **Garde-fou de version.** À la connexion, le majeur de `v` doit égaler celui du driver (1). Sinon
  la connexion est refusée, avec les deux versions dans le message
  (`Board firmware 2.0 does not match driver 1.0: …`).
- **La réponse à `v` est jugée sur sa forme** (audit R5). L'ouverture du port peut redémarrer la carte, et
  les lignes de sa ROM (`ESP-ROM:…`, `SPIWP:0xee`…) arriver après l'envoi de `v`. Une ligne qui n'a pas la
  forme `<maj>.<min>` n'est pas la réponse : elle est sautée, comme une ligne `* …`, jusqu'à la réponse ou
  3 s après la commande (lue dans `EXT_BUF`, une longue ligne de la ROM est sautée entière). Sans réponse,
  la connexion essaie `v` encore, trois fois en tout ; un `v` perdu dans le redémarrage est rattrapé par le
  suivant. Sans aucune ligne de cette forme, la carte n'est pas détectée.
- **Une ligne `* …` n'est jamais une réponse.** La page de banc peut laisser la carte en `LOG ON` ou
  `LOG ALL`, qui émet des lignes `* …` (`PROTOCOL.md` § 3). Chaque commande saute celles qui arrivent
  avant sa réponse, chacune jusqu'à son `\n`, quelle que soit sa longueur, et prend la ligne suivante ;
  le saut s'arrête 3 s après la commande : une carte qui journalise sans jamais répondre échoue comme
  une carte muette. L'entrée en attente avant une commande est lue et jetée
  (l'amont la vidait par `tcflush`) : si elle finit au milieu d'une ligne, une ligne du journal coupée
  entre deux paquets USB, la suite de cette ligne est sautée aussi. Le driver n'envoie jamais `LOG OFF`
  (les majuscules ne sont pas pour un driver). Une carte Pinefeat n'émet, à notre connaissance, aucune
  ligne `*` : rien n'y change pour elle.
- **Une réponse de 15 caractères est lue en entier et terminée.** L'amont la lit dans un tampon de 16
  octets que son `\n` remplit, sans zéro final : le journal, `strstr` et `sscanf` lisent au-delà
  (`er range limits`, à `f<n>` ou `f±n` hors des bornes). Les tampons des réponses de l'amont ont un
  octet de plus, et chaque réponse est terminée par un zéro ; le `\n` y reste rangé, comme avant
  (`UPSTREAM.md`), et ce qui l'affiche le retire (ci-dessous).
- **`a` est lu dans `EXT_BUF`, comme `t`** (audit S2). La carte répond à `a` en 15 caractères au plus ;
  une carte plus ancienne rendait `er nocap aperture` (17), que le tampon de 16 octets de l'amont coupait
  en erreur de lecture, la connexion et chaque relecture échouant avec lui.
- **Les points R9 de l'audit** (`UPSTREAM.md`) : `nc` comparé en entier (L09-05) ; les déplacements
  formatés `%u` (L09-08) ; l'état relu une fois par seconde, `e` et, à l'arrêt, `e f r d a` (L09-09 :
  l'amont les envoyait à chaque passage du minuteur, 50 ms) ; `FIRMWARE_VERSION`, `FOCUS_DISTANCE`,
  `RANGE_APERTURE` et le journal sans la fin de ligne de la réponse.

## Le lot étendu : seulement si `t` porte `ext=1`

Après `v`, le driver envoie `t`. Si la ligne porte le jeton `ext=1` (comparé en entier), le lot est
défini ; sinon (`er` d'une carte Pinefeat, pas de réponse, pas de jeton), le driver est l'amont plus le
garde-fou de version : aucune propriété du lot, aucune de ses lettres n'est envoyée, pas même si un
client écrit une de ses propriétés par son nom.

| Fonction | Lettres | Propriété | Note |
|---|---|---|---|
| ouverture relue | `o` | `APERTURE` (`FNUMBER`, lecture) | relue chaque seconde à l'arrêt : suit la bague de l'objectif ; `Idle` si l'objectif ne sert pas d'ouverture |
| arrêt | `q` | `FOCUS_ABORT_MOTION` | la capacité « abort » standard (`FOCUSER_CAN_ABORT`), posée au handshake |
| marque | `j` `js` `jg` `jx` | `FOCUS_MARK` (`MARK`, lecture) ; `FOCUS_MARK_CTL` (`MARK_SET`, `MARK_GOTO`, `MARK_CLEAR`) | rangée dans la carte ; la valeur est relue chaque seconde à l'arrêt (le bouton du fût la change aussi) |
| LED de statut | `k` `k<n>` | `LED_BRIGHTNESS` (`LEVEL`, 0 à 100 %, onglet Options) | la carte ne la garde pas : rangée dans la config INDI et renvoyée à chaque connexion ; la valeur affichée est celle que `k` relit |
| fin d'un déplacement | `g` | la position absolue (et relative, pour un déplacement relatif) | lue quand `e` répond `n` après un déplacement lancé par le driver (absolu, relatif, `jg`), puis pendant la fenêtre de l'arrêt (ci-dessous) |
| redémarrer l'objectif | `b` | `LENS_REBOOT` (`REBOOT`) | le dernier recours |
| onglet « Lens » | `i` `w` `n` | `LENS_INFO` (`LENS_NAME`, `LENS_FIRMWARE`, `LENS_MODULE`, lecture) | relu chaque seconde à l'arrêt ; une réponse `er …` laisse son champ vide (ci-dessous). Une lettre qui répond `er nocap` n'est pas servie, dans tout état (`PROTOCOL.md` § 2) : elle n'est plus envoyée de la connexion. Le firmware actuel de la carte ne sert ni `w` ni `n` : chacune part une fois par connexion, `LENS_FIRMWARE` et `LENS_MODULE` sont vides |

Le protocole est celui de `PROTOCOL.md`.

**La fin d'un déplacement se lit par `g`**, jamais par `DEBUG` (les majuscules ne sont pas pour un
driver). Quand `e` répond `n` après un déplacement lancé par le driver, il lit `g` :

| `g` | état de la position |
|---|---|
| `ok` | `Ok` |
| `aborted` (le `q` de `FOCUS_ABORT_MOTION`, seul à l'envoyer : le bouton du fût n'en envoie pas) | `Idle`, l'état qu'`INDI::Focuser` donne à un abandon réussi (libindi 1.9.9 et 2.1.0) : une annulation n'est pas un échec |
| `stall`, `unconfirmed`, `-`, pas de réponse | `Alert`, et le résultat au journal du driver |

Une position que `f` ne lit pas (`nc`, `er fault`) donne `Alert`, quel que soit `g`. Cet état reste
jusqu'au déplacement suivant : la relecture de la position à l'arrêt ne le remet pas à `Ok` (l'amont le
fait, à chaque relecture).

**La fenêtre de l'arrêt.** Après `stall` ou `aborted`, le driver relit `g` à chaque passage du minuteur
pendant 5 s (`unconfirmed` arrive au plus 4,5 s après le premier envoi du `0x1C`, `PROTOCOL.md` § 3). S'il
passe à `unconfirmed`, la position passe à `Alert`, et le journal dit `Stop not confirmed: the motor state
is unknown.` ; s'il change autrement, ou à la fin des 5 s, le suivi s'arrête et l'état reste. Un
déplacement lancé dans la fenêtre l'arrête aussi.

**Tout `Busy` posé par le lot étendu se termine**, quand `e` répond `n` : un déplacement par `g` (ci-dessus),
`b` par `f` (`Ok` s'il rend une position, `Alert` sinon). Un objectif retiré ou en `FAULT` (`e` à `n`, `f`
en erreur) donne donc `Alert`, jamais un `Busy` sans fin. Le `Busy` de `CALIBRATE` est celui de l'amont
(`UPSTREAM.md`).

**Une erreur n'est pas une valeur.** Dans `FOCUS_MARK` et `LENS_INFO`, une réponse `er …` laisse son
champ vide. L'état de la propriété : `Alert` si une lettre est refusée pour l'instant (`er nolens`,
`er busy boot`, `er fault` : la valeur existe, elle ne se lit pas) ; sinon `Ok` si un champ porte une
valeur ; sinon `Idle` (`er nocap` : la carte ne la sert pas, comme `w` et `n` aujourd'hui).

## Compilation

```
sudo apt install libindi-dev cmake make g++ pkg-config
cd indi && mkdir build && cd build && cmake .. && make && sudo make install
```

`bash 6_Driver/indi/build.sh`, depuis n'importe quel répertoire, compile et lie dans `6_Driver/indi/build` (recréé à chaque fois), sans
installer.

Vérifié en CI (job `indi` de `.gitea/workflows/ci.yml`) : compilé et lié contre libindi 1.9.9
(Ubuntu 24.04). Le fichier amont est écrit pour libindi 2.x ; sous 1.9.9, les propriétés de la classe
de base `INDI::Focuser` n'ont pas encore les accesseurs qu'il emploie : `pinefeat_cef.h` les leur
donne, sous `#if INDI_VERSION_MAJOR < 2`, sans toucher au reste ; un champ y est atteint par une petite
vue qui tient l'`INumber` lui-même (`NumberField`), pas par une conversion en `WidgetView<INumber>`, qu'il
n'est pas (audit R9).

Joué une fois, hors de la CI (2026-10-08, un essai du ticket #545 sur le runner, non gardé dans le dépôt) : sous
`indiserver` (libindi 1.9.9), contre une fausse carte sur un pty — la ROM puis la réponse à `v`, le premier `v` perdu,
une ligne courte de la ROM en tête (le driver d'avant y refusait la connexion) : connecté, la version lue, `a` de 17
caractères lu en entier, `w` et `n` une fois, `e` une fois par seconde. Aucune suite ne joue le driver. `pkg-config` est nécessaire :
`libindi-dev` ne fournit pas de `FindINDI.cmake`.

Puis dans Ekos : Focusers → « Basilisk E-mount Lens », port série de la carte, 115200. Le nom
d'installation est `indi_basilisk_focus` : il ne rentre pas en collision avec `indi_pinefeat_cef_focus`.

## Licence

Le driver est sous LGPL 2.1 ou ultérieure, comme le fichier d'origine, dont l'en-tête (Pinefeat LLP,
puis le Moonlite de Jasem Mutlaq) est conservé, suivi de la ligne des modifications. Le texte intégral
de la LGPL 2.1 est dans [`LICENSE`](LICENSE), à côté de ce fichier. Il ne couvre que `6_Driver/indi/`.
