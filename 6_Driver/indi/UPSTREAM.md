# Défauts du driver Pinefeat d'origine

**Date** : 2026-10-04
**Dernière révision** : 2026-10-08
**Statut** : actif — constats ; corrigés dans notre fourche : la réponse de 15 caractères (décision de l'humain du 2026-10-04) et les points R9 de l'audit statique, L09-05, L09-08, L09-09 et la fin de ligne des textes affichés (décision de l'humain du 2026-10-07) ; les autres restent tels quels ; les remonter à indilib est une décision de l'humain
**Référencé par** : `6_Driver/indi/README.md`

Le driver est le `pinefeat_cef` d'`indilib/indi` au commit `1dd9b34f24df6fd74ed45be17823fc937e192b47`
(`6_Driver/indi/README.md`). Pas de pull request. La décision d'origine de l'humain (2026-10-04) laissait ses défauts
tels quels dans `6_Driver/indi/`, notés ici, à remonter un jour, sauf un (« on corrige, on ne laisse pas de côté un truc
vu ») : la réponse de 15 caractères sans fin de chaîne. Le 2026-10-07, la décision de l'humain sur l'audit statique du
2026-10-06 (`plans/robustesse-2026-10.md` de l'atelier : R9 « corrigé sans décision », proposition de l'audit validée)
la remplace pour les points R9 : `ERR_NC` (L09-05), le `%d` des déplacements (L09-08), la cadence de `TimerHit`
(L09-09) et la fin de ligne gardée dans les textes affichés sont corrigés dans notre fourche. Chaque entrée dit si elle
est corrigée, et comment ; **restent tels quels** : L09-03, L09-07, la part amont de L09-04, le `Busy` de `CALIBRATE` et
l'erreur répétée au journal sans objectif.

Chaque entrée nomme la fonction du fichier amont (`pinefeat_cef.cpp`), le symptôme, et la correction
qu'on proposerait. Chacune a été vérifiée sur le code ; celles qui touchent la lecture série ont aussi été
vérifiées sur `indicom.c` de libindi 1.9.9 (source du paquet Ubuntu 24.04). Les numéros `L09-…` sont ceux
de l'audit statique (lot 9).

## L09-03 — les bornes de `r` : la basse ignorée, la haute appliquée seulement après `CALIBRATE`

- **Où** : `readFocusMaxPosition`, `updateProperties(pos, max, dist, aper)`.
- **Symptôme** : `r` rend `<min>-<max>` ; `sscanf(res, "%*d-%d", &pos)` jette le minimum, aucun champ ne
  le reçoit, et la position absolue garde le minimum 0 d'`initProperties`. Le maximum n'entre dans
  `FOCUS_MAX` que si `FocusMaxPosNP` est `Busy`, ce que seul `CALIBRATE` pose : à la connexion et à
  chaque relecture, `r` est lu et son maximum ignoré. Sans `CALIBRATE`, un client peut demander une
  position hors des bornes de l'objectif ; la carte la refuse (`er range limits`), le driver ne l'a pas
  bornée.
- **Correction proposée** : lire les deux nombres, et poser `min`/`max` de la position absolue (et
  `FOCUS_MAX`) à chaque lecture de `r` réussie, connexion comprise.

## L09-05 — `ERR_NC` : `strcmp(...) > 0` au lieu d'un test d'égalité (corrigé dans notre fourche)

- **Où** : la macro `ERR_NC`, employée par `readFocusPosition`, `readFocusMaxPosition`, `moveFocusAbs`,
  `moveFocusRel`, `setSpeed`, `setApertureAbs`, `setApertureRel`, `calibrate`.
- **Symptôme** : `strcmp(res, "nc") > 0` est vrai pour toute réponse classée après `nc`, pas pour `nc`
  seul. `nc\n` y passe (le `\n` la classe après `nc`), mais aussi toute réponse qui commence par une lettre
  de `o` à `z`, ou par `nd`… : elle est journalisée « lens is not attached » à la place de son texte. Les
  erreurs de la carte commencent par `er` (classées avant) : avec elle, le message est juste par chance.
- **Correction proposée** : un test d'égalité sur la réponse sans sa fin de ligne
  (`strncmp(res, "nc\n", 4) == 0`, ou comparer la réponse débarrassée de `\n`) : `strcmp(res, "nc") == 0`
  seul ne marcherait pas, `tty_nread_section` garde le `\n`.
- **Corrigé dans notre fourche** : `ERR_NC` compare la réponse sans sa fin de ligne (`chomp(res) == "nc"`), et rend
  toute autre réponse sans sa fin de ligne (le journal ne la coupe plus d'un saut de ligne).

## L09-07 — les champs d'ouverture formatés sans décimale

- **Où** : `initProperties`, `ApertureAbsNP[0]` et `ApertureRelNP[0]`, format `"%.f"`.
- **Symptôme** : un client affiche f/5.6 en `6`, f/1.8 en `2`. La valeur envoyée à la carte est juste
  (`setApertureAbs` écrit `%.6g`), seul l'affichage arrondit.
- **Correction proposée** : `"%.1f"`.

## L09-08 — `%d` pour des `uint32_t` (corrigé dans notre fourche)

- **Où** : `moveFocusAbs` (`snprintf(cmd, CEF_BUF, "f%d\n", position)`), `moveFocusRel`
  (`"f%s%d\n"`, `offset`).
- **Symptôme** : le format ne correspond pas au type (signé contre non signé). Dans les bornes des
  propriétés (0 à 32767), aucun effet visible ; au-delà de `INT32_MAX`, la commande partirait négative.
  Aucun avertissement à la compilation par défaut (`-Wformat` ne contrôle pas le signe sans
  `-Wformat-signedness`).
- **Correction proposée** : `%u` (ou `PRIu32`).
- **Corrigé dans notre fourche** : `%u`, dans `moveFocusAbs` et `moveFocusRel`. Les commandes envoyées ne changent pas
  dans les bornes des propriétés.

## L09-09 — la seconde de `lastUpdate` ne cadence plus rien après la première (corrigé dans notre fourche)

- **Où** : `TimerHit`, le constructeur, `ISNewSwitch` (`CALIBRATE`).
- **Symptôme** : `TimerHit` ne relit l'état que si une seconde s'est écoulée depuis `lastUpdate`, mais
  `lastUpdate` n'est posé qu'à la construction du driver et à `CALIBRATE`. Une seconde après, le test est
  vrai à chaque passage du minuteur, toutes les 50 ms par défaut (`setDefaultPollingPeriod(50)`) : `e` à
  chaque passage, et à l'arrêt `e f r d a`, cinq commandes. La seconde ne sert plus que de délai après
  `CALIBRATE`.
- **Correction proposée** : si une cadence d'une seconde est voulue à l'arrêt, poser `lastUpdate` à chaque
  relecture dans `TimerHit` ; sinon, retirer le test et garder le seul délai de `CALIBRATE`.
- **Corrigé dans notre fourche** : `TimerHit` pose `lastUpdate` à chaque passage où la seconde est écoulée : `e`,
  et à l'arrêt `e f r d a`, une fois par seconde ; `CALIBRATE` garde son délai. Joué sous `indiserver` (libindi 1.9.9,
  2026-10-08), contre une fausse carte sur un pty : 6 `e` en 6 s, contre 98 à 116 pour le driver d'avant.

## Audit R9 — la fin de ligne gardée dans les textes affichés (corrigé dans notre fourche)

- **Où** : `readFirmwareVersion` (`FIRMWARE_VERSION`), `readFocusDistance` (`FOCUS_DISTANCE`), `readApertureRange`
  (`RANGE_APERTURE`).
- **Symptôme** : `tty_nread_section` garde le `\n` dans la réponse, et elle est rangée telle quelle dans le texte de la
  propriété (`1.0\n`) et dans le journal (« Detected firmware version 1.0\n. »).
- **Corrigé dans notre fourche** : ces trois textes sont rangés sans leur fin de ligne (`chomp`).

## L09-04, la part amont — une réponse tardive prise pour la réponse suivante

- **Où** : `sendCommand`.
- **Symptôme** : une réponse qui arrive après l'échéance de lecture (`CEF_TIMEOUT`, 3 s) reste en
  route. Le `tcflush` qui précède la commande suivante ne vide que ce qui est déjà reçu ; une réponse
  encore en vol arrive après lui, et elle est lue comme la réponse de la commande suivante, qui la prend
  pour la sienne (un `ok` pour un acquittement, un nombre pour une position). Le protocole n'a pas de
  corrélation requête-réponse. Notre driver lit et jette l'entrée en attente au lieu du `tcflush`, et saute
  la suite d'une ligne coupée (`README.md`) ; une réponse tardive entière, arrivée après, reste prise.
- **Correction proposée** : après une échéance, ne pas envoyer la commande suivante avant que la ligne
  soit restée muette un moment (lire et jeter jusqu'au silence), ou marquer la connexion à resynchroniser.

## Le `Busy` de `CALIBRATE` ne finit que par un `f` réussi

- **Où** : `ISNewSwitch` (`CALIBRATE`), `TimerHit`, `updateProperties(pos, max, dist, aper)`.
- **Symptôme** : un `CALIBRATE` accepté pose la position absolue et `FOCUS_MAX` à `Busy`. Seul
  `updateProperties` les en sort, et il n'est appelé que si `f`, `r`, `d` et `a` répondent tous, une fois
  `e` à `n`. Un objectif retiré ou en `FAULT` (`f` en erreur) les laisse `Busy` pour toujours. Le lot
  étendu de ce dépôt termine ses propres `Busy` ; celui de `CALIBRATE` est laissé à l'amont.
- **Correction proposée** : une fois `e` à `n`, une lecture qui échoue passe ces propriétés à `Alert`.

## Hors de l'audit — une réponse de 15 caractères n'a pas de fin de chaîne (corrigé dans notre copie)

- **Où** : `sendCommand`, appelé avec `size = CEF_BUF` (16) par toutes les lettres de l'amont, dans des
  tampons de 16 octets.
- **Symptôme** : `tty_nread_section` (libindi, `indicom.c`) écrit au plus `nsize` octets, `\n` compris ;
  quand la réponse les remplit, il n'y a pas de place pour un zéro final et il n'en écrit pas. Une réponse
  de 15 caractères et son `\n` remplissent le tampon : `strstr`, `sscanf`, `setText` et le journal lisent
  au-delà. La carte en rend une : `er range limits` (15 caractères), à `f<n>` et `f±n` hors des bornes
  (`PROTOCOL.md` § 2), ce que L09-03 rend possible. Les lettres du lot étendu lisent avec `EXT_BUF - 1`
  dans des tampons de `EXT_BUF` et ne sont pas touchées.
- **Corrigé dans notre copie** (décision de l'humain). Notre `sendCommand` n'appelle pas
  `tty_nread_section` comme l'amont : il lit le premier octet à part (pour sauter les lignes `* …`), le
  range dans `res[0]`, puis appelle `tty_nread_section(PortFD, res + 1, size - 1, …)` : jusqu'à `size`
  octets en tout, `\n` compris. La taille passée ne change pas ; les tampons des réponses de l'amont sont
  agrandis d'un octet (`CEF_RES`, `CEF_BUF + 1`), et `sendCommand` pose lui-même le zéro après le dernier
  octet lu (`res[1 + nbytes_read]`) : `res` doit tenir `size + 1` octets, ce que tiennent aussi les tampons
  `EXT_BUF` lus avec `EXT_BUF - 1`. Le `\n` reste rangé dans la réponse, comme avant, pour toutes les
  longueurs ; ce qui l'affiche le retire (`chomp` : `ERR_NC` et les textes de `v`, `d`, `a`, entrées L09-05 et R9).
- **Vérifié sur libindi 1.9.9** (Ubuntu 24.04, un pty, 2026-10-04) : `er range limits\n` lu par notre appel
  (`size` 16) rend `TTY_OK`, 15 octets, le `\n` consommé, aucun zéro écrit derrière ; le même flux lu
  avec `size` 15 rend `TTY_OVERFLOW` (« Read overflow ») après 14 octets et laisse le `\n` dans le flux.
  Une réponse courte (`ok\n`) reçoit, elle, son zéro de `tty_nread_section`.
- **À remonter à l'amont. Correction proposée** : celle de notre copie, transposée à son appel : des tampons de réponse de
  `CEF_BUF + 1` octets, `tty_nread_section(PortFD, res, size, …)` inchangé, puis `res[nbytes_read] = 0`.
  Pas « passer `size - 1` à `tty_nread_section` » : il rendrait `TTY_OVERFLOW` sur une réponse de 15
  caractères, devenue une erreur de lecture, son `\n` resté dans le flux pour la commande suivante.

## Hors de l'audit — une erreur au journal à chaque relecture sans objectif

- **Où** : `TimerHit`, `readFocusPosition`.
- **Symptôme** : sans objectif, `e` répond `n` et `f` répond `nc` ; `readFocusPosition` journalise
  « Can't read focus position: lens is not attached. » à chaque relecture, tant que l'objectif est absent : une ligne
  par seconde dans notre fourche (L09-09 corrigé), vingt par seconde à la période par défaut dans l'amont.
- **Correction proposée** : ne journaliser qu'au changement (la première erreur, puis le retour).
