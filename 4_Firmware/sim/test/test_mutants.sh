#!/usr/bin/env bash
# SOURCE: les règles de repérage en tête de sim/test/mutants.sh (le code seul, la fonction nommée par le bloc)
# AUTHOR: engineer
# DATE: 2026-10-02
# STATUS: actif — joué par 4_Firmware/run.sh (étape 5, avant les mutants)
# La preuve, sur une source jouet, que mutants.sh cherche le texte original sur le code seul, dans la fonction nommée.
#
# Joue une copie de sim/test/mutants.sh dans un arbre jouet (répertoire temporaire, rien n'est écrit dans le dépôt) :
# le script se place par son propre chemin, il y lit les programmes.sh, mutations.txt et mutations_std.txt du jouet.
# Le jouet : un() et deux() portent les deux mêmes lignes, x() deux paires jumelles que seul un commentaire distingue,
# trois() tient sur une ligne, BASE est une macro ; le programme imprime les quatre valeurs, ce qui dit laquelle est mutée.
# 1. une ancre de deux lignes de code, nommée dans un(), retrouve sa ligne, et son mutant est tué, quand les
#    commentaires de la source ont changé : un commentaire de fin de ligne réécrit, un commentaire de deux lignes inséré
#    entre les deux lignes de l'ancre, un `//` ajouté ; la même ancre, la source d'avant puis celle d'après ;
# 2. la même ancre nommée dans deux() mute deux() et laisse un() ; sans fonction, présente deux fois dans le fichier,
#    elle est refusée ;
# 3. une ancre qui n'apparaît qu'une fois dans le texte de x(), mais deux fois dans son code (un commentaire séparait ses
#    deux lignes de leurs jumelles), est refusée ;
# 4. une fonction inconnue du fichier est refusée ; une ancre dans une fonction, sans la nommer, est refusée ;
# 5. une macro, hors de toute fonction, se mute sans fonction ; une fonction d'une ligne se mute ; une ancre qui déborde
#    de l'accolade fermante de sa fonction n'y est pas trouvée, refusée ;
# 6. une ancre qui contient un commentaire est refusée ;
# 7. une mutation qui ne change qu'un commentaire, ou que des blancs de fin de ligne et des lignes vides, est refusée.
set -euo pipefail
ICI=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
J="$T/firmware"
mkdir -p "$J/include" "$J/components/jouet" "$J/sim/test"
cp "$ICI/mutants.sh" "$J/sim/test/mutants.sh"
: > "$J/sim/test/mutations_std.txt"
cat > "$J/sim/test/programmes.sh" <<'FIN'
programme_sources() {
    LIENS=()
    case $2 in
        test_jouet) SOURCES=("$1/components/jouet/jouet.c" "$1/sim/test/test_jouet.c") ;;
        *) return 1 ;;
    esac
}
programme_includes() { INCLUDES=(-I"$1/include"); }
programme_args() { ARGS=(); }
FIN
cat > "$J/sim/test/test_jouet.c" <<'FIN'
#include <stdio.h>
int un(void);
int x(void);
int deux(void);
int trois(void);
int main(void)
{
    if (un() == 1 && x() == 1 && deux() == 1 && trois() == 1) return 0;
    printf("  ECHEC jouet : un=%d x=%d deux=%d trois=%d (1 attendu)\n", un(), x(), deux(), trois());
    return 1;
}
FIN

# deux() porte les deux mêmes lignes que un() ; trois() tient sur une ligne ; BASE est hors de toute fonction
source_avant() {
    cat > "$J/components/jouet/jouet.c" <<'FIN'
#define BASE 1
int un(void);
int x(void);
int deux(void);
int trois(void);
int un(void)
{
    int a = 1;    /* commentaire d'avant */
    return a;
}
int x(void)
{
    int v = 0, w;
    w = 0;
    v = 1;
    w = 0;        /* un commentaire entre les deux lignes */
    v = 1;
    return v + w;
}
int deux(void)
{
    int a = 1;
    return a;
}
int trois(void) { return BASE; }
FIN
}

source_apres() {
    cat > "$J/components/jouet/jouet.c" <<'FIN'
#define BASE 1
int un(void);
int x(void);
int deux(void);
int trois(void);
int un(void)
{
    int a = 1;              /* commentaire réécrit, aligné ailleurs */
    /* un commentaire de deux lignes,
       inséré entre les deux lignes de l'ancre */
    return a;   // et un autre, en fin de ligne
}
int x(void)
{
    int v = 0, w;
    w = 0;
    v = 1;
    w = 0;        /* un commentaire entre les deux lignes */
    v = 1;
    return v + w;
}
int deux(void)
{
    int a = 1;
    return a;
}
int trois(void) { return BASE; }
FIN
}

# jouer <attendu : 0 ou 1> <motif attendu dans la sortie> <description> : joue la copie sur le mutations.txt du jouet
echecs=0
jouer() {
    local st=0
    # traces, page, version : le programme jouet n'en lit aucun (programme_args lui en donne zéro)
    MUTANTS_PROCS=1 bash "$J/sim/test/mutants.sh" "$T" "$T" "$T" > "$T/sortie" 2>&1 || st=$?
    if [ "$st" -eq "$1" ] && grep -qF -- "$2" "$T/sortie"; then
        echo "   $3 : « $2 »"
    else
        echo "   ÉCHEC $3 : sortie $st (attendu $1), « $2 » attendu dans :"
        sed 's/^/      /' "$T/sortie"
        echecs=$((echecs + 1))
    fi
}

# mute <fonction ou vide> <original> <muté> : un bloc d'une ligne de code dans mutations.txt
mute() {
    { echo "@ test_jouet | components/jouet/jouet.c${1:+ | $1}"
      echo "= une mutation du jouet"
      printf -- '- %s\n' "$2"
      printf -- '+ %s\n' "$3"; } > "$J/sim/test/mutations.txt"
}

cat > "$J/sim/test/mutations.txt" <<'FIN'
@ test_jouet | components/jouet/jouet.c | un
= deux lignes de code, des commentaires autour et entre elles
-     int a = 1;
-     return a;
+     int a = 1;
+     return a + 1;
FIN
source_avant
jouer 0 "un=2 x=1 deux=1 trois=1" "1. la source d'avant : l'ancre trouvée dans un, le mutant tué"
source_apres
jouer 0 "un=2 x=1 deux=1 trois=1" "1. les commentaires changés : la même ancre trouvée dans un, le mutant tué"

sed -i 's/jouet\.c | un$/jouet.c | deux/' "$J/sim/test/mutations.txt"
jouer 0 "un=1 x=1 deux=2 trois=1" "2. le même texte, présent dans un et deux, nommé dans deux : deux muté, un intact"
sed -i 's/jouet\.c | deux$/jouet.c/' "$J/sim/test/mutations.txt"
jouer 1 "le texte original apparaît 2 fois dans le fichier (il en faut 1)" "2. le même texte sans fonction : refusé"

cat > "$J/sim/test/mutations.txt" <<'FIN'
@ test_jouet | components/jouet/jouet.c | x
= deux lignes que seul un commentaire distinguait de leurs jumelles : une fois dans le texte, deux dans le code
-     w = 0;
-     v = 1;
+     w = 0;
+     v = 2;
FIN
jouer 1 "le texte original apparaît 2 fois dans la fonction x (il en faut 1)" "3. une ancre ambiguë dans sa fonction : refusée"

mute quatre "    int a = 1;" "    int a = 2;"
jouer 1 "fonction inconnue : quatre" "4. une fonction inconnue du fichier : refusée"
mute "" "    return v + w;" "    return v;"
jouer 1 "le texte original est dans la fonction x, que le bloc doit nommer" "4. une ancre dans une fonction, sans la nommer : refusée"

mute "" "#define BASE 1" "#define BASE 2"
jouer 0 "un=1 x=1 deux=1 trois=2" "5. hors de toute fonction (une macro), sans fonction : trouvée dans le fichier, le mutant tué"
mute trois "int trois(void) { return BASE; }" "int trois(void) { return BASE + 1; }"
jouer 0 "un=1 x=1 deux=1 trois=2" "5. une fonction d'une ligne : l'ancre trouvée, le mutant tué"
cat > "$J/sim/test/mutations.txt" <<'FIN'
@ test_jouet | components/jouet/jouet.c | un
= une ancre qui déborde de l'accolade fermante de sa fonction
-     return a;
- }
- int x(void)
+     return a + 1;
+ }
+ int x(void)
FIN
jouer 1 "le texte original apparaît 0 fois dans la fonction un (il en faut 1)" "5. une ancre qui déborde de sa fonction : refusée"

mute x "    w = 0;        /* un commentaire entre les deux lignes */" "    w = 1;        /* un commentaire entre les deux lignes */"
jouer 1 "le texte original ou muté contient un commentaire" "6. une ancre qui contient un commentaire : refusée"
mute un "    return a;" "    return a; // muté"
jouer 1 "le texte original ou muté contient un commentaire" "7. un texte muté qui n'ajoute qu'un commentaire : refusé"

cat > "$J/sim/test/mutations.txt" <<'FIN'
@ test_jouet | components/jouet/jouet.c | un
= seuls des blancs de fin de ligne et une ligne vide changent
-     return a;
+     return a;   
+
FIN
jouer 1 "le texte muté ne change pas le code" "7. un texte muté qui n'ajoute que des blancs de fin et une ligne vide : refusé"

[ "$echecs" -eq 0 ] || { echo "test_mutants : $echecs échec(s)"; exit 1; }
echo "test_mutants : le repérage des mutants se fait sur le code seul, dans la fonction nommée"
