#!/usr/bin/env bash
# SOURCE: les mutants de la suite de 4_Firmware/ (sim/test/mutations.txt, puis sim/test/mutations_std.txt)
# AUTHOR: engineer
# DATE: 2026-09-26
# STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI) ; son repérage est tenu par sim/test/test_mutants.sh
# Chaque bloc des deux listes est un mutant du firmware ou du banc, qui doit être tué par le programme qu'il nomme.
#
# Pour chaque bloc : copie 4_Firmware/ (include/, sim/, components/) dans un répertoire temporaire, y remplace le texte
# original (qui doit y apparaître exactement une fois : dans sa fonction, ou dans le fichier entier pour un bloc sans
# fonction, règles ci-dessous) par le texte muté, compile les sources de programmes.sh (les mêmes que run.sh, LIENS
# compris) avec les options de run.sh, -Werror compris, mais en -O0 (sur toute la liste, les mêmes mutants tués qu'en
# -O2), et joue le programme nommé avec les arguments de programmes.sh (programme_args) sous la racine de la copie
# mutée (test_std y lit sim/test/constats_std.txt, qu'une mutation peut viser).
# `bash mutants.sh <traces> <page> <version>` : les trois fichiers lus hors de la copie, que run.sh déclare (TRACES,
# PAGE, VERSION) ; 4_Firmware/ est le répertoire deux niveaux au-dessus de ce script.
# Le mutant doit être TUÉ : le programme sort en 1 (une vérification échoue). Une mutation qui ne s'applique pas à
# exactement un endroit, un mutant qui ne compile pas, un mutant qui survit, ou qui meurt autrement (signal, délai),
# font échouer ce script. Prérequis : gcc, bash, coreutils, awk. Rien n'est écrit dans le dépôt.
#
# Le code seul : dans un fichier .c ou .h, un commentaire ne fait rien, il ne doit donc ni retrouver ni
# perdre une ancre. Le fichier visé de la copie est remplacé par son code seul — chaque commentaire retiré avec les
# blancs qui le précèdent (un blanc reste s'il séparait deux codes, l'indentation reste), puis les blancs de fin de
# ligne et les lignes vides retirés ; les chaînes et les caractères sont lus comme le compilateur les lit. Le texte
# original et le texte muté sont lus de la même façon ; l'original doit apparaître exactement une fois dans ce code,
# et la mutation est appliquée à ce code. Ni l'original ni le muté ne contiennent de commentaire (refus), et le muté
# doit changer le code (refus sinon). Les autres fichiers (sim/test/constats_std.txt) sont pris tels quels.
# `bash mutants.sh --code <fichier>` écrit le code seul d'un fichier : c'est sur lui qu'une ancre s'écrit.
#
# La fonction : un bloc s'écrit `@ <programme> | <fichier> | <fonction>`, la fonction étant le nom de sa
# définition dans le code (une signature change moins qu'un commentaire). Elle n'est nommée que dans un .c ou un .h.
# Les fonctions sont repérées sur le code seul, par la convention du dépôt :
#   - une définition commence en colonne 0 par un identifiant, et le nom est l'identifiant qui précède la première `(`
#     de la ligne (`__attribute__((…))` sauté) ; une ligne qui porte un `=` avant cette `(` n'en est pas une (une table,
#     une variable initialisée) ;
#   - son accolade ouvrante est sur la ligne de définition ou sur une suivante, avant toute ligne finie par `;` (un
#     prototype) et avant toute autre ligne en colonne 0 ;
#   - une fonction d'une ligne (`static inline … { … }` de sim/lens_sim.h) ouvre et ferme sur la ligne de définition,
#     qui finit par `}` ; sinon le corps finit à la première ligne qui commence par `}` en colonne 0.
# La fonction couvre sa ligne de définition jusqu'à cette accolade fermante, et le texte original doit apparaître
# exactement une fois dans cette étendue, sans en déborder : une ancre qui déborde de l'accolade fermante n'y est pas
# trouvée. Une fonction inconnue du fichier, ou définie plusieurs fois, est refusée.
# Sans fonction (une table, une macro, une variable du fichier, un fichier de données) : l'original doit apparaître
# exactement une fois dans le code du fichier entier, et ne pas commencer dans une fonction (sinon le bloc est refusé :
# il doit la nommer). Dans un fichier de données, rien n'est retiré comme commentaire (ci-dessus).
# `bash mutants.sh --fonctions <fichier>` écrit les fonctions repérées : nom, première et dernière ligne du code seul.
#
# Les mutants sont indépendants : MUTANTS_PROCS (un entier >= 1, `nproc` par défaut) d'entre eux sont joués à la fois,
# chacun dans sa copie, qu'il efface en finissant. Chacun écrit sa sortie et son issue (tué ou refusé) dans des
# fichiers à son rang ; une fois tous finis, les sorties sont écrites dans l'ordre des blocs et les issues comptées.
# Un mutant qui ne rend aucune issue (le jeu lui-même a échoué) est compté refusé.
set -euo pipefail
F2=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LISTES=("$F2/sim/test/mutations.txt" "$F2/sim/test/mutations_std.txt")
SORTIE=$(mktemp -d)
trap 'rm -rf "$SORTIE"' EXIT
CFLAGS=(-std=c11 -Wall -Wextra -Wpedantic -Werror -O0)

# Le code seul : stdin -> stdout, sort en 3 si un commentaire a été retiré (règle en tête de ce fichier).
CODE_SEUL="$SORTIE/code_seul"
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -x c - -o "$CODE_SEUL" <<'FIN_C'
#include <stdio.h>
#include <stdlib.h>

static char *o;
static size_t n, cap, deb;   /* deb : début de la ligne en cours dans o */

static void put(int c)
{
    if (n == cap) {
        cap = cap ? 2 * cap : 65536;
        o = realloc(o, cap);
        if (!o) exit(2);
    }
    o[n++] = (char)c;
    if (c == '\n') deb = n;
}

/* Un commentaire retiré : les blancs qui le précèdent sur la ligne aussi, sauf l'indentation d'une ligne qui n'a
 * encore que des blancs ; un blanc s'il sépare deux codes (le compilateur lit un commentaire comme un blanc). */
static void retire(void)
{
    size_t k = n;
    while (k > deb && (o[k - 1] == ' ' || o[k - 1] == '\t')) k--;
    if (k > deb) { n = k; put(' '); }
}

int main(void)
{
    int c, d, vu = 0, apres = 0;   /* apres : les blancs qui suivent un commentaire sont sautés */
    size_t i = 0, prem = 1;
    while ((c = getchar()) != EOF) {
        if (apres && (c == ' ' || c == '\t')) continue;
        apres = 0;
        if (c == '"' || c == '\'') {
            int q = c;
            put(c);
            while ((c = getchar()) != EOF) {
                put(c);
                if (c == '\\') { if ((c = getchar()) == EOF) break; put(c); }
                else if (c == q || c == '\n') break;
            }
            continue;
        }
        if (c == '/') {
            d = getchar();
            if (d == '*') {
                int p = 0;
                while ((c = getchar()) != EOF && !(p == '*' && c == '/')) p = c;
                vu = 1; retire(); apres = 1;
                continue;
            }
            if (d == '/') {
                while ((c = getchar()) != EOF && c != '\n')
                    if (c == '\\' && (c = getchar()) == EOF) break;
                vu = 1; retire();
                if (c == '\n') put('\n');
                continue;
            }
            put('/');
            if (d != EOF) ungetc(d, stdin);
            continue;
        }
        put(c);
    }
    while (i < n) {   /* les blancs de fin de ligne et les lignes vides retirés */
        size_t j = i, f;
        while (j < n && o[j] != '\n') j++;
        f = j;
        while (f > i && (o[f - 1] == ' ' || o[f - 1] == '\t' || o[f - 1] == '\r')) f--;
        if (f > i) {
            if (!prem) putchar('\n');
            fwrite(o + i, 1, f - i, stdout);
            prem = 0;
        }
        i = j + 1;
    }
    if (!prem) putchar('\n');
    return vu ? 3 : 0;
}
FIN_C

# Les fonctions du code seul (stdin) : une ligne « nom première dernière » par définition (règles en tête de ce fichier).
FONCTIONS='
{ L[NR] = $0 }
END {
    for (i = 1; i <= NR; i++) {
        l = L[i]
        gsub(/__attribute__[ \t]*\(\([^()]*(\([^()]*\)[^()]*)*\)\)/, "", l)
        if (l !~ /^[A-Za-z_]/) continue
        p = index(l, "(")
        if (p == 0) continue
        tete = substr(l, 1, p - 1)
        if (tete ~ /=/) continue
        sub(/[ \t]+$/, "", tete)
        if (!match(tete, /[A-Za-z_][A-Za-z0-9_]*$/)) continue
        nom = substr(tete, RSTART, RLENGTH)
        o = 0
        for (j = i; j <= NR; j++) {
            if (j > i && L[j] ~ /^[A-Za-z_#}]/) break
            if (L[j] ~ /\{/) { o = j; break }
            if (L[j] ~ /;[ \t]*$/) break
        }
        if (!o) continue
        if (o == i && L[i] ~ /\}[ \t]*$/) { print nom, i, i; continue }
        for (k = o + 1; k <= NR && L[k] !~ /^\}/; k++) ;
        if (k > NR) continue
        print nom, i, k
        i = k
    }
}'

case "${1:-}" in
    --code|--fonctions)
        [ $# -eq 2 ] && [ -f "$2" ] || { echo "usage : mutants.sh --code|--fonctions <fichier>" >&2; exit 2; }
        if [ "$1" = "--code" ]; then
            "$CODE_SEUL" < "$2" || [ $? -eq 3 ]
        else
            { "$CODE_SEUL" < "$2" || [ $? -eq 3 ]; } | awk "$FONCTIONS"
        fi
        exit 0 ;;
esac

[ $# -eq 3 ] || { echo "usage : mutants.sh <traces> <page> <version> | --code|--fonctions <fichier>" >&2; exit 2; }
TRACES=$1 PAGE=$2 VERSION=$3
. "$F2/sim/test/programmes.sh"
PROCS=${MUTANTS_PROCS:-$(nproc)}
case "$PROCS" in
    ""|*[!0-9]*|0*) echo "MUTANTS_PROCS doit être un entier >= 1 (reçu : $PROCS)" >&2; exit 2 ;;
esac

total=0     # mutants planifiés (le rang #n de chaque ligne)
rangs=0     # lignes du journal : un mutant ou un refus de lecture, dans l'ordre des blocs
actifs=0    # mutants en cours

# Issue d'un rang : $SORTIE/r<rang>.out (sa sortie), $SORTIE/r<rang>.st (tue ou refus).
# $1 rang, $2 issue, $3 message : un refus constaté à la lecture des listes.
noter() {
    printf '%s\n' "$3" > "$SORTIE/r$1.out"
    echo "$2" > "$SORTIE/r$1.st"
}

# Joué dans un sous-processus, sortie dans $SORTIE/r<rang>.out.
# $1 rang, $2 numéro du mutant, $3 programme, $4 fichier (relatif à 4_Firmware/), $5 fonction (vide : aucune),
# $6 pourquoi, $7 original, $8 muté
jouer() {
    local rang=$1 num=$2 prog=$3 fichier=$4 fonction=$5 pourquoi=$6 avant=$7 apres=$8
    local dir="$SORTIE/m$((num - 1))" st="$SORTIE/r$rang.st" contenu reste n statut rc rc2 lieu=""
    local -a fns=()
    local nom deb fin k
    rm -rf "$dir"
    mkdir -p "$dir"
    cp -r "$F2/include" "$F2/sim" "$F2/components" "$dir/"
    if ! programme_sources "$dir" "$prog"; then
        echo "   REFUS  #$num programme inconnu de programmes.sh : $prog"; echo refus > "$st"; return
    fi
    programme_includes "$dir"
    programme_args "$dir" "$prog"
    if [ ! -f "$dir/$fichier" ]; then
        echo "   REFUS  #$num $fichier : fichier absent"; echo refus > "$st"; return
    fi
    case "$fichier" in
        *.c|*.h)
            rc=0; "$CODE_SEUL" < "$F2/$fichier" > "$dir/$fichier" || rc=$?
            if [ "$rc" -ne 0 ] && [ "$rc" -ne 3 ]; then
                echo "   REFUS  #$num $fichier : son code seul n'a pu être lu"; echo refus > "$st"; return
            fi
            rc=0; avant=$(printf '%s' "$avant" | "$CODE_SEUL") || rc=$?
            rc2=0; apres=$(printf '%s' "$apres" | "$CODE_SEUL") || rc2=$?
            if [ "$rc" -ne 0 ] || [ "$rc2" -ne 0 ]; then
                echo "   REFUS  #$num $fichier : le texte original ou muté contient un commentaire, qui n'est pas du code — $pourquoi"
                echo refus > "$st"; return
            fi
            if [ "$avant" = "$apres" ]; then
                echo "   REFUS  #$num $fichier : le texte muté ne change pas le code — $pourquoi"
                echo refus > "$st"; return
            fi
            if [ -z "$avant" ]; then
                echo "   REFUS  #$num $fichier : le texte original n'a pas de code — $pourquoi"
                echo refus > "$st"; return
            fi
            mapfile -t fns < <(awk "$FONCTIONS" "$dir/$fichier") ;;
        *)
            if [ -n "$fonction" ]; then
                echo "   REFUS  #$num $fichier : une fonction n'est nommée que dans un .c ou un .h ($fonction) — $pourquoi"
                echo refus > "$st"; return
            fi ;;
    esac
    if [ -n "$fonction" ]; then
        # l'étendue de la fonction : de sa ligne de définition à son accolade fermante (règles en tête de ce fichier)
        n=0
        for k in "${fns[@]}"; do
            read -r nom deb fin <<< "$k"
            [ "$nom" = "$fonction" ] && { n=$((n + 1)); lieu="$deb $fin"; }
        done
        if [ "$n" -ne 1 ]; then
            if [ "$n" -eq 0 ]; then
                echo "   REFUS  #$num $fichier : fonction inconnue : $fonction — $pourquoi"
            else
                echo "   REFUS  #$num $fichier : la fonction $fonction est définie $n fois — $pourquoi"
            fi
            echo refus > "$st"; return
        fi
        read -r deb fin <<< "$lieu"
        contenu=$(sed -n "${deb},${fin}p" "$dir/$fichier")
        lieu="dans la fonction $fonction"
    else
        contenu=$(<"$dir/$fichier")
        lieu="dans le fichier"
    fi
    reste=${contenu//"$avant"/}
    n=$(( (${#contenu} - ${#reste}) / ${#avant} ))
    if [ "$n" -ne 1 ]; then
        echo "   REFUS  #$num $fichier : le texte original apparaît $n fois $lieu (il en faut 1) — $pourquoi"
        echo refus > "$st"; return
    fi
    if [ -n "$fonction" ]; then
        { head -n $((deb - 1)) "$dir/$fichier"
          printf '%s\n' "${contenu/"$avant"/"$apres"}"
          tail -n +$((fin + 1)) "$dir/$fichier"; } > "$dir/mute"
        mv "$dir/mute" "$dir/$fichier"
    else
        # sans fonction, l'original ne doit pas commencer dans une fonction : il devrait la nommer
        k=$(( $(printf '%s' "${contenu%%"$avant"*}" | wc -l) + 1 ))   # la ligne où il commence
        for nom in "${fns[@]}"; do
            read -r nom deb fin <<< "$nom"
            if [ "$k" -ge "$deb" ] && [ "$k" -le "$fin" ]; then
                echo "   REFUS  #$num $fichier : le texte original est dans la fonction $nom, que le bloc doit nommer — $pourquoi"
                echo refus > "$st"; return
            fi
        done
        printf '%s\n' "${contenu/"$avant"/"$apres"}" > "$dir/$fichier"
    fi
    if ! gcc "${CFLAGS[@]}" "${INCLUDES[@]}" "${SOURCES[@]}" "${LIENS[@]}" -o "$dir/$prog" 2> "$dir/cc.log"; then
        echo "   REFUS  #$num $fichier : le mutant ne compile pas — $pourquoi"
        sed 's/^/          /' "$dir/cc.log" | head -n 5
        echo refus > "$st"; return
    fi
    statut=0
    timeout 120 "$dir/$prog" "${ARGS[@]}" > "$dir/run.log" 2>&1 || statut=$?
    if [ "$statut" -eq 1 ]; then
        echo "   tué    #$num $prog, $(grep -ac ECHEC "$dir/run.log") échec(s) :"
        grep -a ECHEC "$dir/run.log" | sed "s|^ *ECHEC $dir/sim/test/|          |" | cut -c 1-150
        echo tue > "$st"
    elif [ "$statut" -eq 0 ]; then
        echo "   SURVIT #$num $fichier — $pourquoi"
        echo refus > "$st"
    else
        echo "   REFUS  #$num $prog sort en $statut (ni vert ni une vérification échouée) — $pourquoi"
        echo refus > "$st"
    fi
}

# Planifie un mutant (arguments de jouer après le rang et le numéro), MUTANTS_PROCS au plus à la fois.
lancer() {
    while [ "$actifs" -ge "$PROCS" ]; do
        wait -n || true
        actifs=$((actifs - 1))
    done
    total=$((total + 1))
    ( jouer "$rangs" "$total" "$@"; rm -rf "$SORTIE/m$((total - 1))" ) > "$SORTIE/r$rangs.out" 2>&1 < /dev/null &
    actifs=$((actifs + 1))
    rangs=$((rangs + 1))
}

prog="" fichier="" fonction="" pourquoi="" avant="" apres="" dans_bloc=0
fin_bloc() {
    if [ "$dans_bloc" -eq 1 ]; then
        if [ -z "$prog" ] || [ -z "$avant" ]; then
            noter "$rangs" refus "   REFUS  bloc incomplet (@ et - requis) : $pourquoi"; rangs=$((rangs + 1))
        else
            lancer "$prog" "$fichier" "$fonction" "$pourquoi" "$avant" "$apres"
        fi
    fi
    prog="" fichier="" fonction="" pourquoi="" avant="" apres="" dans_bloc=0
}

for LISTE in "${LISTES[@]}"; do
while IFS= read -r ligne || [ -n "$ligne" ]; do
    case "$ligne" in
        "#"*) [ "$dans_bloc" -eq 0 ] && continue ;;
    esac
    if [ -z "$ligne" ]; then fin_bloc; continue; fi
    dans_bloc=1
    case "$ligne" in
        "@ "*)
            # @ <programme> | <fichier> [| <fonction>]
            prog=${ligne#@ }; prog=${prog%%|*}; prog=${prog%% }
            fichier=${ligne#*| }; fonction=""
            case "$fichier" in
                *" | "*) fonction=${fichier#*" | "}; fichier=${fichier%%" | "*} ;;
            esac ;;
        "= "*) pourquoi=${ligne#= } ;;
        "- "*) avant=${avant:+$avant$'\n'}${ligne#- } ;;
        "-") avant=${avant:+$avant$'\n'} ;;
        "+ "*) apres=${apres:+$apres$'\n'}${ligne#+ } ;;
        "+") apres=${apres:+$apres$'\n'} ;;
        *) noter "$rangs" refus "   REFUS  ligne inattendue : $ligne"; rangs=$((rangs + 1)) ;;
    esac
done < "$LISTE"
fin_bloc
done

wait
tues=0
refus=0
for ((r = 0; r < rangs; r++)); do
    [ -f "$SORTIE/r$r.out" ] && cat "$SORTIE/r$r.out"
    case $(cat "$SORTIE/r$r.st" 2>/dev/null || true) in
        tue) tues=$((tues + 1)) ;;
        refus) refus=$((refus + 1)) ;;
        *) echo "   REFUS  ligne $r du journal : le jeu du mutant n'a rendu aucune issue"; refus=$((refus + 1)) ;;
    esac
done
echo "mutants : $total, tués : $tues, refusés ou survivants : $refus"
[ "$total" -gt 0 ] && [ "$refus" -eq 0 ] && [ "$tues" -eq "$total" ]
