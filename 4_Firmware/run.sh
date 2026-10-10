#!/usr/bin/env bash
# SOURCE: la suite de 4_Firmware/, jouée par le job `firmware` de .gitea/workflows/ci.yml
# AUTHOR: engineer
# DATE: 2026-09-26
# STATUS: actif — la même commande en local et en CI : `bash 4_Firmware/run.sh` ; sources et chemins d'en-têtes lus
#         dans sim/test/programmes.sh, comme mutants.sh ; ticket #539 : la passe sous ASan et UBSan (étape 5)
#
# 1. chaque en-tête partagé de 4_Firmware/include compile seul, inclus deux fois ; la table de broches
#    (components/phy/pins.h) compile pour la cible esp32s3, et une cible sans table échoue avec son message ;
# 2. le faux 135 et ses tests compilent sans avertissement (-Werror) ;
# 3. test_lens135 : les comportements de la référence et les pannes, dans les deux sens ;
#    test_lens_std : le faux objectif standard (Tamron F051, sim/lens_std.c) piloté directement, sans PHY simulée ;
#    test_phy : la frontière R1, commune aux deux PHY (codec, E_FRAMING), E_BUS injecté, D2 ;
#    test_bench_core : la liste blanche de bench_core, puis la garde « bench_core seul émet »
#    (sim/test/garde_emission.sh), sur le dépôt et sur une copie fautive ;
# 4. replay : le faux 135 contre les traces réelles de traces/ (lues en place) ;
#    test_journal : le journal de debug seul, sur une horloge de test ;
#    test_led : le calcul de la LED de statut (components/led/led.c), des suites d'instantanés rejouées, sans PWM ;
#    test_session_script : SESSION et TRANSACTION contre un répondeur minimal (sim/test/phy_script.c) ;
#    test_session135 : SESSION et TRANSACTION contre le faux 135, requêtes d'init comparées aux traces ;
#    test_host : la couche HOTE contre une session scriptée, et les motifs que la page applique aux réponses
#    (la page de banc, PAGE, lue en place) ; test_host135 : des lignes jouées à travers HOTE, SESSION et le faux 135 ;
#    test_std : SESSION, TRANSACTION et HOTE contre le faux F051 ; les scénarios dont l'attendu n'est pas tenu sont
#    inscrits dans sim/test/constats_std.txt, que test_std lit et affiche : vert tant que chaque échec y est, rouge si
#    un scénario de la liste passe ;
# 5. sous ASan et UBSan : chaque programme de 3 et 4 recompilé avec -fsanitize=address,undefined et rejoué ; une erreur
#    détectée le fait échouer (R6 ; la garde de durée de test_session135 relevée, GARDE_S) ;
# 6. mutants : mutants.sh cherche le texte original sur le code seul, commentaires ignorés, dans la fonction que le
#    bloc nomme (sim/test/test_mutants.sh, sur une source jouet) ; chaque mutation de sim/test/mutations.txt et de sim/test/mutations_std.txt doit faire
#    échouer son programme (sim/test/mutants.sh).
# Prérequis : gcc, et ses bibliothèques libasan et libubsan (étape 5). Rien n'est écrit dans le dépôt (compilation
# dans un répertoire temporaire). Lus en place : les traces de l'objectif (traces/), CMakeLists.txt (PROJECT_VER) et
# la page de banc, seul fichier lu hors de 4_Firmware/.
# Se lance depuis n'importe quel répertoire : les chemins partent de celui de ce script. Chaque programme reçoit en
# argument ce qu'il lit (programme_args, sim/test/programmes.sh) ; mutants.sh reçoit TRACES, PAGE et VERSION.
set -euo pipefail
F2=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TRACES="$F2/traces"
VERSION="$F2/CMakeLists.txt"
# La page de banc, dont test_host lit les motifs de réponse (expectFor) : elle appartient au composant 5_App.
PAGE="$F2/../5_App/emount-bench.html"
SORTIE=$(mktemp -d)
trap 'rm -rf "$SORTIE"' EXIT
CFLAGS=(-std=c11 -Wall -Wextra -Wpedantic -Werror -O2)
. "$F2/sim/test/programmes.sh"
programme_includes "$F2"
CFLAGS+=("${INCLUDES[@]}")

echo "== en-têtes partagés (include/)"
for h in "$F2"/include/*.h; do
    nom=$(basename "$h")
    printf '#include "%s"\n#include "%s"\nint main(void) { return 0; }\n' "$nom" "$nom" > "$SORTIE/entete.c"
    gcc "${CFLAGS[@]}" "$SORTIE/entete.c" -o "$SORTIE/entete"
    echo "   $nom : compile seul"
done

echo "== broches : une table par cible ESP-IDF (components/phy/pins.h), une cible sans table ne compile pas"
mkdir -p "$SORTIE/cible"
printf '#include "pins.h"\ntypedef int broches_t;\n' > "$SORTIE/cible/broches.c"
printf '#define CONFIG_IDF_TARGET_ESP32S3 1\n' > "$SORTIE/cible/sdkconfig.h"
gcc "${CFLAGS[@]}" -fsyntax-only -I"$SORTIE/cible" "$SORTIE/cible/broches.c"
echo "   cible esp32s3 : sa table compile"
attendu="aucune table de broches pour cette cible ESP-IDF : ajouter la sienne dans components/phy/pins.h"
for cible in CONFIG_IDF_TARGET_ESP32C3 ""; do
    printf '%s\n' "${cible:+#define $cible 1}" > "$SORTIE/cible/sdkconfig.h"
    if gcc "${CFLAGS[@]}" -fsyntax-only -I"$SORTIE/cible" "$SORTIE/cible/broches.c" 2> "$SORTIE/cible/cc.log"; then
        echo "   ÉCHEC : une cible sans table (${cible:-aucune cible}) compile"
        exit 1
    fi
    if ! grep -qF "$attendu" "$SORTIE/cible/cc.log"; then
        echo "   ÉCHEC : la cible ${cible:-(aucune)} échoue sans le message attendu :"
        sed 's/^/      /' "$SORTIE/cible/cc.log"
        exit 1
    fi
    echo "   ${cible:-aucune cible} : refusée, « $attendu »"
done

echo "== faux Samyang AF 135 : comportements et pannes"
programme_sources "$F2" test_lens135
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$SORTIE/test_lens135"
programme_args "$F2" test_lens135
"$SORTIE/test_lens135" "${ARGS[@]}"

echo "== faux objectif standard (Tamron F051) : comportements, couche physique et pannes, sans PHY simulée"
programme_sources "$F2" test_lens_std
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$SORTIE/test_lens_std"
programme_args "$F2" test_lens_std
"$SORTIE/test_lens_std" "${ARGS[@]}"

echo "== frontière PHY : le codec commun (components/phy/phy_common.c)"
programme_sources "$F2" test_phy
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$SORTIE/test_phy"
programme_args "$F2" test_phy
"$SORTIE/test_phy" "${ARGS[@]}"

echo "== bench_core : la liste blanche (spec § 7.1.3)"
programme_sources "$F2" test_bench_core
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$SORTIE/test_bench_core"
programme_args "$F2" test_bench_core
"$SORTIE/test_bench_core" "${ARGS[@]}"

echo "== bench_core, seul appelant de l'émission de PHY dans le firmware (spec § 2, § 7)"
bash "$F2/sim/test/garde_emission.sh" "$F2"
echo "   le dépôt : aucune émission hors de bench_core"
mkdir -p "$SORTIE/garde/components/fautif"
cp -r "$F2/include" "$F2/components" "$SORTIE/garde/"
printf '#include "bsk_phy.h"\nvoid fautif(const bsk_frame_t *f) { bsk_phy_send(f); }\n' > "$SORTIE/garde/components/fautif/fautif.c"
if bash "$F2/sim/test/garde_emission.sh" "$SORTIE/garde" > /dev/null; then
    echo "   ÉCHEC : la garde laisse passer un appel direct à PHY (components/fautif/fautif.c)"
    exit 1
fi
echo "   auto-test : un appel direct à PHY posé dans une copie est refusé"

echo "== faux Samyang AF 135 : rejeu contre les traces (traces/)"
programme_sources "$F2" replay
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$SORTIE/replay"
programme_args "$F2" replay
"$SORTIE/replay" "${ARGS[@]}"

echo "== le journal de debug (spec § 4.5.5) : sa forme, ses bornes"
programme_sources "$F2" test_journal
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$SORTIE/test_journal"
programme_args "$F2" test_journal
"$SORTIE/test_journal" "${ARGS[@]}"

echo "== la LED de statut : le calcul, des suites d'instantanés rejouées (components/led/led.c)"
programme_sources "$F2" test_led
gcc "${CFLAGS[@]}" "${SOURCES[@]}" -o "$SORTIE/test_led"
programme_args "$F2" test_led
"$SORTIE/test_led" "${ARGS[@]}"

echo "== SESSION et TRANSACTION : par une PHY scriptée (sim/test/phy_script.c)"
programme_sources "$F2" test_session_script
gcc "${CFLAGS[@]}" "${SOURCES[@]}" "${LIENS[@]}" -o "$SORTIE/test_session_script"
programme_args "$F2" test_session_script
"$SORTIE/test_session_script" "${ARGS[@]}"

echo "== SESSION et TRANSACTION : contre le faux 135 (requêtes d'init comparées aux traces)"
programme_sources "$F2" test_session135
gcc "${CFLAGS[@]}" "${SOURCES[@]}" "${LIENS[@]}" -o "$SORTIE/test_session135"
programme_args "$F2" test_session135
"$SORTIE/test_session135" "${ARGS[@]}"

echo "== la couche HOTE : la traduction contre une session scriptée, les motifs de la page de banc"
programme_sources "$F2" test_host
gcc "${CFLAGS[@]}" "${SOURCES[@]}" "${LIENS[@]}" -o "$SORTIE/test_host"
programme_args "$F2" test_host
"$SORTIE/test_host" "${ARGS[@]}"

echo "== la couche HOTE bout à bout : des lignes à travers SESSION, MOUVEMENT et le faux 135"
programme_sources "$F2" test_host135
gcc "${CFLAGS[@]}" "${SOURCES[@]}" "${LIENS[@]}" -o "$SORTIE/test_host135"
programme_args "$F2" test_host135
"$SORTIE/test_host135" "${ARGS[@]}"

echo "== le firmware contre le faux F051 : SESSION, TRANSACTION, HOTE ; les constats (sim/test/constats_std.txt)"
programme_sources "$F2" test_std
gcc "${CFLAGS[@]}" "${SOURCES[@]}" "${LIENS[@]}" -o "$SORTIE/test_std"
programme_args "$F2" test_std
"$SORTIE/test_std" "${ARGS[@]}"

# R6 (audit statique du 2026-10-06) : la suite, comité de revue, passe aussi sous ASan et UBSan. Chaque programme de 3 et
# 4, recompilé avec ses sources et ses options, puis -fsanitize=address,undefined : toute erreur détectée (débordement,
# pointeur nul passé à memcpy, décalage hors borne, fuite…) arrête le programme en échec (-fno-sanitize-recover).
# Les sanitizers ralentissent test_session135 au-delà de sa garde de 10 s : GARDE_S la relève, pour ce passage seul.
echo "== sous ASan et UBSan : chaque programme de la suite, recompilé et rejoué"
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DGARDE_S=60)
for p in test_lens135 test_lens_std test_phy test_bench_core replay test_journal test_led test_session_script \
         test_session135 test_host test_host135 test_std; do
    programme_sources "$F2" "$p"
    gcc "${CFLAGS[@]}" "${SAN[@]}" "${SOURCES[@]}" "${LIENS[@]}" -o "$SORTIE/san_$p"
    programme_args "$F2" "$p"
    if ! "$SORTIE/san_$p" "${ARGS[@]}" > "$SORTIE/san_$p.log" 2>&1; then
        tail -n 40 "$SORTIE/san_$p.log"
        echo "   ÉCHEC : $p sous ASan et UBSan"
        exit 1
    fi
    echo "   $p : sans erreur"
done

echo "== mutants : le texte original cherché sur le code seul, dans sa fonction, une source jouet (sim/test/test_mutants.sh)"
bash "$F2/sim/test/test_mutants.sh"

echo "== mutants (sim/test/mutations.txt, sim/test/mutations_std.txt)"
bash "$F2/sim/test/mutants.sh" "$TRACES" "$PAGE" "$VERSION"
