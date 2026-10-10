# Doctrine

**Date** : 2026-09-21
**Dernière révision** : 2026-10-08
**Statut** : actif — gouverne les arbitrages ; la doctrine est celle de la session web, les passages
qui décrivaient un état daté en ont été retirés
**Référencé par** : `CLAUDE.md` (§ Doctrine, § Conventions), `7_Docs/MANUEL.md` (§ Arborescence)
**Dérivé de** : `DOCTRINE.md` de la ligne web (commit `b932a69`, zip de passation)

> *We build embedded systems with the discipline of a space agency and the headcount of one
> developer. Low-tech by conviction, over-engineered by principle.*

Ce document dit **pourquoi** le code de ce dépôt est écrit comme il l'est. Il gouverne les
arbitrages, humains comme agentiques. Il est court exprès.

---

## 1. Le désaccord de fond

L'ingénierie logicielle n'a pas dégénéré : elle s'est **scindée en deux métiers qui portent le
même nom**. Ce qui les sépare n'est pas le goût, c'est la structure de coût d'un défaut.

| | Coût d'un défaut | Pratique rationnelle |
|---|---|---|
| Web, mobile, SaaS | un rollback, parfois un tweet | itérer vite, corriger en production |
| Embarqué, critique | un rappel, un vol perdu, un mort | prouver avant, car il n'y a pas d'après |

« Move fast and break things » n'est pas une faute morale : c'est une réponse correcte à un coût
réversible. Cela devient une faute quand on l'applique à un coût irréversible.

**Le critère n'est donc pas la nostalgie, c'est la réversibilité.** Un firmware dans une bague,
sur une monture, dans un champ, à trois heures du matin, n'a pas de rollback. Cette phrase suffit
à trancher tous les arbitrages qui suivent.

---

## 2. Ce que la lignée a réellement appris

Les quatre leçons qui comptent ne sont pas « écrire du code propre ». Elles sont plus précises,
et plus dérangeantes.

**Apollo 11, alarme 1202.** L'exécutif n'a pas « bien fonctionné » : il a **dégradé proprement**.
Surchargé, il a abandonné le travail de basse priorité et tenu l'alunissage. La leçon n'est pas
« ne jamais échouer », c'est **définir le comportement en surcharge aussi soigneusement que le
comportement nominal**.

**Therac-25.** Les morts ne viennent pas d'un code sale mais d'une course sur une fenêtre de
saisie, d'un compteur qui déborde à un octet — et surtout du retrait des **verrouillages
matériels** au profit d'un logiciel que personne ne savait prouver. La leçon : on ne supprime
jamais une barrière physique parce que le logiciel « gère ».

**Ariane 501.** Du code correct, réutilisé **hors de son domaine de validité**. La leçon : un
composant n'est pas correct dans l'absolu, il est correct sous des hypothèses — et ces hypothèses
doivent être écrites, sinon elles voyagent avec le code sans lui.

**Dijkstra.** *Le test montre la présence de bugs, jamais leur absence.* C'est l'épistémologie qui
tient tout le reste : la question n'est pas « est-ce que ça marche ? » mais **« pourquoi est-ce
que ça ne peut pas ne pas marcher ? »**.

---

## 3. La contradiction de la devise

« La discipline d'une agence spatiale, l'effectif d'un développeur. » Il faut regarder la
contradiction en face : la discipline d'une agence est **institutionnelle**. Revue indépendante,
IV&V, comité de configuration, plusieurs paires d'yeux qui n'ont pas écrit le code. Un
développeur seul ne reproduit pas ça en se concentrant plus fort. La fatigue et l'angle mort ne
se compensent pas par la volonté.

**La résolution : à effectif un, la vérification mécanique remplace la revue institutionnelle.**
On n'achète pas des relecteurs, on achète des machines qui ne se fatiguent pas — le compilateur
tous avertissements dehors, l'analyse statique, la suite hôte qui compile le métier hors cible, le
test de propriété, le vérificateur de modèle. Ce n'est pas un contournement : c'est exactement la
raison pour laquelle Holzmann, qui a écrit le *Power of Ten*, a d'abord écrit SPIN.

Conséquence directe pour ce dépôt : **les suites hôte sont l'actif le plus précieux du projet.**
Elles ne sont pas un filet de sécurité, elles sont le comité de revue. Tout ce qui les affaiblit
coûte plus cher que ce qu'il rapporte.

---

## 4. Raffinement de la devise

« Over-engineered by principle » ne désigne pas l'accumulation de fonctions. L'effort démesuré
porte sur le **retrait** : les heures, les audits et les tests passés à supprimer tout ce qui
n'est pas strictement essentiel, en vérifiant à chaque ligne retirée que quelque chose casse.
Un Soyouz n'est pas sur-conçu par élaboration : il l'est par simplicité avec marge.

Ce geste a un nom dans la littérature — le **test par mutation** (DeMillo, Lipton, Sayward, 1978).
Et il a une propriété que sa formulation naïve masque : quand on retire une ligne et que **rien ne
casse**, il y a deux conclusions possibles, jamais une seule.

> La ligne était inutile. **Ou** la suite de tests est aveugle à ce qu'elle faisait.

Il faut trancher explicitement à chaque fois. Le vrai produit de l'exercice n'est donc pas un
fichier plus court : c'est la **carte des angles morts de la suite de tests**.

La forme opérationnelle :

> **Sur-spécifié, sous-implémenté.**

Savoir exactement ce que ça doit faire, par écrit, y compris aux bords et en surcharge.
Implémenter le moins qui le fasse. La rigueur va dans la spécification ; l'économie va dans le
code.

Et son corollaire, qui tranche à peu près tout :

> **Supprimer > simplifier > raccourcir.** Une ligne supprimée ne se teste pas, ne régresse pas,
> ne se documente pas, et ne peut pas être fausse.

### La limite du retrait

« Retirer jusqu'à ce que ça casse » optimise contre **les tests qu'on a**, pas contre le réel. Le
retrait des verrouillages du Therac-25 était, en son temps, une expérience réussie : on avait
retiré, et rien n'avait cassé. D'où une séparation qui n'est pas négociable :

| Nature | Critère de nécessité |
|---|---|
| **Code qui produit la sortie** | le retrait ; si rien ne casse, il part (ou la suite est aveugle) |
| **Code qui défend un cas** | le retrait ne prouve rien — son test, c'est le cas, et la suite ne le contient peut-être pas |

Une garde ne se justifie donc jamais par « ça ne casse pas sans elle », mais par **le cas qu'elle
défend, nommé, et l'endroit où ce cas est démontré**. Une garde sans cas nommé est du vrai
déchet ; une garde avec un cas nommé ne se retire pas parce qu'un test passe.

---

## 5. Les invariants de Basilisk

Ce que la doctrine donne, ici, concrètement.

**Ce qui est déjà juste, et qu'il faut nommer pour ne pas l'éroder :**

- Le refus d'écrire dans la flash de l'objectif, imposé par `bench_core`, **est un verrouillage
  au sens du Therac-25**. C'est la pièce la plus importante du projet. Elle ne se négocie pas,
  ne se contourne pas « pour tester », ne devient pas une option.
- Zéro allocation dynamique, zéro récursion, toutes les boucles bornées. Trois propriétés
  coûteuses ailleurs, tenues ici sans effort. On ne les perd pas.
- `PROTOCOL.md` est un contrat, pas une documentation. Les hypothèses y sont écrites — c'est la
  leçon d'Ariane appliquée.
- La carte ne parle jamais la première. Un comportement, pas une convention.

**Ce que la doctrine impose en plus :**

- **Un comportement non expliqué est un défaut, même si la fonction marche.** Ce n'est pas du
  *backlog* : c'est un endroit où le système fait quelque chose que personne ne sait expliquer.
- **La dégradation doit être spécifiée.** Que fait la carte quand l'objectif se tait, quand la
  tempête `0x02` arrive, quand l'USB décroche en plein mouvement ?
- **L'état partagé est la dette principale.** Chaque déclaration `extern` dans un en-tête du
  firmware est un contrat implicite avec tous les fichiers.
- **Le compilateur est embauché, et ce qu'il signale arrête le build.** C'est le relecteur le
  moins cher du monde, et un avertissement que personne ne relit ne protège de rien. Sur PC, les
  suites hôte compilent ce qu'elles jouent en `-Werror`. Sur la cible, les cinq composants que
  seul ESP-IDF compile (`phy`, `store`, `led`, `host`, `main`) ajoutent `-Wextra -Wconversion
  -Wshadow` à ce que pose ESP-IDF (`-Wall -Werror=all -Wextra`), fatals sur nos sources
  (`-Werror=extra -Werror=conversion -Werror=shadow`, dans leur `CMakeLists.txt`). Une seule
  exception, nommée à l'include : `hal/gpio_ll.h` d'ESP-IDF dans `components/phy/phy.c`, sous
  `-Wno-conversion -Wno-sign-conversion`, parce que ses fonctions inline convertissent sans cast et
  qu'on ne touche pas à ESP-IDF. Ce qu'ESP-IDF éteint ou laisse non fatal pour tout le monde
  (`-Wno-unused-parameter`, `-Wno-sign-compare`, `-Wno-enum-conversion` ; `-Wno-error=` des
  fonctions et variables inutilisées et des déclarations dépréciées) le reste chez nous.

---

## 6. Ce que ça change dans la manière de travailler

Pour tout intervenant sur ce dépôt, humain ou agent :

1. **Écrire l'invariant avant le code.** Si on ne sait pas énoncer ce qui doit rester vrai, on ne
   sait pas encore ce qu'on implémente.
2. **Pas d'abstraction pour un futur hypothétique.** Le deuxième cas d'usage justifie
   l'abstraction ; le premier ne la justifie jamais.
3. **Pas de dépendance pour économiser vingt lignes.** Une dépendance, c'est du code qu'on n'a pas
   lu qui tourne dans un système dont on répond. (**Le firmware** n'en a aucune hors ESP-IDF. C'est
   une propriété, pas un hasard. L'outillage de développement, lui, en a, et elles ne tournent
   jamais sur la carte : Playwright, épinglé à 1.49.1 dans `package.json`, pour `5_App/test/test_app.js` ;
   Python 3 pour les scripts du banc et des gardes — `mutants.py`, `verifie_combo.py`,
   `check_combo.py`, `test_raisons.py`, `test_tampon_t.py`, `mutants_hote.py` — et les
   générateurs de tables ; Node pour les tests de l'app.)
4. **« Ça marche » est le début de la question.** Ensuite vient : sous quelles hypothèses, jusqu'à
   quelle charge, et que fait-il quand elles tombent.
5. **Une transformation, un commit, une suite verte.** Pas de lot, pas de « pendant que j'y
   suis ».
6. **Ne pas refactorer ce qui a été payé par un incident.** La complexité issue d'un bug réel
   encode une connaissance que la relecture ne retrouvera pas.
7. **Dire quand on ne sait pas.** Un doute annoncé coûte une phrase ; un doute masqué coûte une
   nuit d'observation.

---

## 7. La ligne de fond

La doctrine vise l'état suivant, qui est le seul qui compte à trois heures du matin :

> **Ce n'est pas que ça marche. C'est qu'on sait pourquoi ça ne peut pas ne pas marcher.**

Tout le reste — l'épure, les entiers, le dispatch par table, les avertissements du compilateur —
n'est que la liste des travaux entre les deux.
