# SOURCE: la liste des programmes de test du firmware et de leurs sources, en un seul endroit
# AUTHOR: engineer
# DATE: 2026-09-27
# STATUS: actif — sourcé par run.sh (à la racine de 4_Firmware/) et par sim/test/mutants.sh ; ticket #528 : sim/stacks_sim.c
#
# run.sh compile les programmes depuis le dépôt, mutants.sh depuis une copie mutée de 4_Firmware/ :
# les deux lisent ici la même liste, sous la racine qu'on leur donne. Un programme ajouté ici est
# compilé de la même façon par les deux.

# programme_sources <racine firmware> <programme> : remplit SOURCES et LIENS (options d'édition de liens,
# après les sources) ; rend 1 si le programme est inconnu.
# test_session135, test_std et test_host135 sont liés avec --wrap : ils notent ce que la session fait sur le fil sans
# toucher à la PHY simulée (sim/test/test_session135.c, sim/test/test_std.c, sim/test/test_host135.c). Tout programme
# qui compile la couche HOTE (components/host/host.c) compile aussi components/led/led.c (`k`, le plafond de la LED)
# et est lié avec -lm (l'ouverture : log2, pow), et reçoit les marges de pile de `DEBUG` (bsk_host_stacks, bsk_host.h) :
# test_host pose les siennes, les autres compilent sim/stacks_sim.c.
programme_sources() {
    local r=$1
    local sim=("$r/components/phy/phy_common.c" "$r/sim/frame.c" "$r/sim/sources135.c" "$r/sim/lens135.c"
               "$r/sim/lens_sim_135.c" "$r/sim/phy_sim.c" "$r/sim/bench.c")
    local session=("$r/components/bench_core/bench_core.c" "$r/components/txn/txn.c"
                   "$r/components/session/session.c" "$r/components/session/lens_rx.c" "$r/components/session/still.c" "$r/components/session/motion.c" "$r/components/session/drive.c" "$r/components/session/ring.c" "$r/components/session/mark.c" "$r/components/session/cmd.c" "$r/components/session/init.c" "$r/components/session/restore.c" "$r/sim/store_sim.c" "$r/components/journal/journal.c" "$r/components/phy/phy_common.c"
                   "$r/sim/frame.c" "$r/sim/sources135.c")
    LIENS=()
    case $2 in
        test_lens135|replay|test_phy) SOURCES=("${sim[@]}" "$r/sim/test/$2.c") ;;
        test_bench_core) SOURCES=("$r/components/bench_core/bench_core.c" "$r/sim/test/$2.c") ;;
        test_lens_std) SOURCES=("$r/sim/lens_std.c" "$r/sim/test/$2.c") ;;
        test_led) SOURCES=("$r/components/led/led.c" "$r/sim/test/$2.c") ;;
        test_journal) SOURCES=("$r/components/journal/journal.c" "$r/components/phy/phy_common.c" "$r/sim/test/jdecode.c" "$r/sim/test/$2.c") ;;
        test_host) SOURCES=("$r/components/host/host.c" "$r/components/host/lens_names.c" "$r/components/led/led.c" "$r/components/journal/journal.c"
                            "$r/components/phy/phy_common.c" "$r/sim/test/$2.c")
            LIENS=(-lm) ;;
        test_host135)
            SOURCES=("${session[@]}" "$r/sim/lens135.c" "$r/sim/lens_sim_135.c" "$r/sim/phy_sim.c" "$r/components/host/host.c"
                     "$r/components/host/lens_names.c" "$r/components/led/led.c" "$r/sim/stacks_sim.c" "$r/sim/test/$2.c")
            LIENS=(-Wl,--wrap=bsk_phy_send -lm) ;;
        test_session_script)
            SOURCES=("${session[@]}" "$r/components/host/host.c" "$r/components/host/lens_names.c" "$r/components/led/led.c" "$r/sim/test/phy_script.c"
                     "$r/sim/stacks_sim.c" "$r/sim/test/$2.c")
            LIENS=(-lm) ;;
        test_session135)
            SOURCES=("${session[@]}" "$r/sim/lens135.c" "$r/sim/lens_sim_135.c" "$r/sim/phy_sim.c" "$r/sim/test/jdecode.c" "$r/sim/test/$2.c")
            LIENS=(-Wl,--wrap=bsk_phy_send,--wrap=bsk_phy_body_cs,--wrap=bsk_phy_rail,--wrap=bsk_phy_vd,--wrap=bsk_phy_lines,--wrap=bsk_phy_poll) ;;
        test_std)
            SOURCES=("${session[@]}" "$r/sim/lens_std.c" "$r/sim/lens_sim_std.c" "$r/sim/phy_sim.c" "$r/components/host/host.c"
                     "$r/components/host/lens_names.c" "$r/components/led/led.c" "$r/sim/test/jdecode.c" "$r/sim/stacks_sim.c"
                     "$r/sim/test/$2.c")
            LIENS=(-Wl,--wrap=bsk_phy_send,--wrap=bsk_phy_poll -lm) ;;
        *) return 1 ;;
    esac
}

# programme_args <racine firmware> <programme> : remplit ARGS, les arguments du programme. Les fichiers lus hors de
# 4_Firmware/sim/test sont dans TRACES (le répertoire des traces de l'objectif), PAGE (la page de banc, dont test_host lit
# les motifs) et VERSION (le CMakeLists.txt qui porte PROJECT_VER), que l'appelant a posés ; la liste des constats de
# test_std est prise sous la racine donnée, qu'une mutation peut viser.
programme_args() {
    case $2 in
        test_lens135|replay|test_session135) ARGS=("$TRACES") ;;
        test_host) ARGS=("$PAGE" "$VERSION") ;;
        test_std) ARGS=("$1/sim/test/constats_std.txt") ;;
        *) ARGS=() ;;
    esac
}

# programme_includes <racine firmware> : remplit INCLUDES.
programme_includes() {
    INCLUDES=(-I"$1/include" -I"$1/sim" -I"$1/components/phy")
}
