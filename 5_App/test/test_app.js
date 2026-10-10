/* SOURCE: test de la page de banc dans un navigateur — une pageerror fait échouer le test (avant : écrasée par le process.exit final)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — joué par ../run.sh ; le faux port rend `v` comme une version de carte (1.0), `t` (ext=1 en tête),
 *         `DEBUG` ; la page lit i, r par leur lettre ; la marque sur `j` (lue, posée, rejointe par les boutons),
 *         `m<n>` un déplacement, `u…`, `cm`, `m`, `ms`, `mg`, `mx` inconnues du faux port ; la page n'en envoie aucune,
 *         ni h, c, n, w, l, d, DUMP, SNIFF, SDRIVE, que la carte ne sert pas (ticket #392) ; le démarrage de `b` occupé
 *         plus longtemps qu'une période de `t`, et l'état attendu plutôt qu'un délai fixe (la lecture de `t` à 1 Hz
 *         manquait la fenêtre de 0,8 s selon sa phase)
 *
 * Test de l'app dans un navigateur sans carte : un faux port Web Serial qui parle PROTOCOL.md
 * (lettres, Moonlite, labo). Verifie la connexion, le poll, les panneaux, un deplacement, une
 * ouverture, un démarrage asynchrone (b), la disparition de l'objectif.
 * Lancer : node test_app.js (Playwright + Chromium requis). */
const { chromium } = require('playwright');
const path = require('path');

(async () => {
  const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || require('fs').readdirSync('/opt/pw-browsers').filter(d=>d.startsWith('chromium-')).map(d=>'/opt/pw-browsers/'+d+'/chrome-linux/chrome')[0] });
  const page = await browser.newPage();
  // Toute exception JS non rattrapée dans la page est un échec, même si les vérifications
  // fonctionnelles passent : on l'affiche tout de suite et on la compte à la fin (le
  // process.exit final décide seul du code de sortie, un process.exitCode posé ici serait écrasé).
  const pageErrors = [];
  page.on('pageerror', e => { console.log('PAGE ERROR', e.message); pageErrors.push(e); });
  await page.addInitScript(() => {
    /* ---- simulateur de carte ---- */
    /* boot : `b` rejoue un démarrage, busy=boot pendant 10 réponses à `e` (5 Hz, 2 s au moins) : plus long que la période
       de `t` (1 s), que la page lit pour l'afficher ; à 4 (0,8 s), la lecture de `t` tombait ou non dans la fenêtre selon
       sa phase */
    const S = { present: true, pos: 16384, min: 13723, max: 30988, moving: 0, fnum: 1.8, busy: '-', target: null, mark: null, limitsAfter: 13723, g: '-', stallNext: false, fault: false };
    window.__sim = S;
    function reply(cmd) {
      const c = cmd[0];
      if (cmd.startsWith(':')) { const m = cmd.slice(1, -1); if (m === 'GP') return S.pos.toString(16).toUpperCase().padStart(4, '0') + '#'; if (m === 'GI') return S.moving ? '01#' : '00#'; if (m === 'GV') return '10'; return null; }
      if (/^[A-Z]/.test(cmd)) { if (cmd === 'DEBUG') return 'ok reset=poweron dropped=0 move_rc=-'; if (cmd === 'LOG ON') return 'ok log=1 all=0'; return 'er nocap'; }
      if (c === 'v') return '1.0';
      if (c === 't') return `ext=1 present=${S.present ? 1 : 0} boot_state=${!S.present ? 'off' : S.fault ? 'fault' : S.busy === 'boot' ? 'powering' : 'ready'} busy=${S.busy} power_off=0 last_op=${S.fault ? 'boot:er:"lost"' : 'boot:ok'} mf=- oss=- ring=-`;
      if (c === 'g') return S.g;                     /* le résultat du dernier déplacement suivi (PROTOCOL.md § 2) */
      if (c === 'p') return cmd.length === 1 ? '1 1' : 'ok';
      if (c === 'b') { S.busy = 'boot'; S.busyN = 10; S.limitsAfter = 13700; return 'ok'; }
      if (!S.present) return 'fread cs'.includes(c) ? 'nc' : 'er nolens';
      if (S.fault) return c === 'e' ? 'n' : 'er fault';   /* PROTOCOL.md § 7 : en fault, e = n */
      if (c === 'm' && !/^m\d/.test(cmd)) return 'er nocap'; /* m<n> seul, un déplacement comme f<n> */
      if (c === 'f' || c === 'm') {
        if (cmd.length === 1) return String(S.pos);
        if (S.busy !== '-') return 'er busy ' + S.busy;
        let t = cmd[1] === '+' || cmd[1] === '-' ? S.pos + parseInt(cmd.slice(1), 10) : parseInt(cmd.slice(1), 10);
        if (isNaN(t)) return 'er range 65535';
        S.target = Math.max(S.min, Math.min(S.max, t)); S.moving = 3; return 'ok';
      }
      if (c === 'e') { if (S.moving) { S.moving--; if (!S.moving && S.target !== null) { if (S.stallNext) S.g = 'stall'; else { S.pos = S.target; S.g = 'ok'; } S.stallNext = false; S.target = null; } return 'y'; } if (S.busy !== '-') { if (--S.busyN <= 0) { S.busy = '-'; S.min = S.limitsAfter; } return 'y'; } return 'n'; }
      if (c === 'r') return `${S.min}-${S.max}`;
      if (c === 'a') { if (cmd.length === 1) return '1.8-22'; const v = parseFloat(cmd.slice(1)); S.fnum = cmd[1] === '+' || cmd[1] === '-' ? S.fnum + v : v; return 'ok'; }
      if (c === 'o') return S.fnum.toFixed(1);
      if (c === 'i') return 'Samyang AF 135mm f/1.8-22 fw1.05';
      if (c === 'j') { /* la marque */
        if (cmd === 'j') return S.mark === null ? '-' : String(S.mark);
        if (cmd === 'js') { S.mark = S.pos; return 'ok'; }
        if (cmd === 'jx') { S.mark = null; return 'ok'; }
        if (cmd === 'jg') { if (S.mark === null) return 'er range nomark'; S.target = S.mark; S.moving = 3; return 'ok'; }
        return 'er nocap';
      }
      if (c === 'u') return 'er nocap';
      if (c === 'q') { if (S.moving) { S.moving = 0; S.target = null; S.g = 'aborted'; } return 'ok'; }
      return 'er nocap cmd';
    }
    const enc = new TextEncoder(), dec = new TextDecoder();
    let ctrl = null; let inbuf = '';
    const readable = new ReadableStream({ start(c) { ctrl = c; } });
    const writable = new WritableStream({ write(chunk) { inbuf += dec.decode(chunk); let i; while ((i = inbuf.search(/[\n#]/)) >= 0) { const cmd = inbuf.slice(0, i + (inbuf[i] === '#' ? 1 : 0)); inbuf = inbuf.slice(i + 1); if (!cmd || cmd === '#') continue; const r = reply(cmd); window.__sent = (window.__sent || []).concat(cmd); if (r !== null && ctrl) setTimeout(() => ctrl.enqueue(enc.encode(r + (r.endsWith('#') || r === '10' ? '' : '\n'))), 2); } } });
    const port = { open: async () => {}, close: async () => {}, readable, writable };
    Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => port }, configurable: true });
  });
  await page.goto('file://' + path.resolve(__dirname, '..', 'emount-bench.html'));
  let fails = 0;
  const check = (c, m) => { console.log((c ? '  ok   ' : '  FAIL ') + m); if (!c) fails++; };
  await page.click('#btnConnect');
  await page.waitForTimeout(1500);
  check(await page.textContent('#connTxt') === 'connecté', 'connexion');
  if (fails) { console.log(await page.textContent('#log')); }
  check((await page.textContent('#s_id')).includes('Samyang AF 135mm'), 'identite affichee (i)');
  check((await page.textContent('#s_board')).startsWith('fw 1.0 · reset poweron · boot ready'), 'la carte, v et DEBUG (« ' + (await page.textContent('#s_board')) + ' »)');
  check(await page.textContent('#s_caps') === 'lot étendu (ext=1)', 'ext lu au lieu de caps');
  if (fails) { console.log(await page.textContent('#log')); console.log(await page.evaluate(() => JSON.stringify(window.__sent))); }
  check(await page.textContent('#s_focus') === '16384', 'position lue par f');
  check(await page.textContent('#scMin') === '13 723 (proche)' || (await page.textContent('#scMin')).startsWith('13'), 'bornes lues par r');
  check(!(await page.isDisabled('#focusRange')), 'curseur focus actif (goto + bornes)');
  check(await page.textContent('#s_fnum') === 'f/1.8', 'ouverture courante par o');
  /* L09-16 : ce que la carte ne sert pas n'est plus sur la page */
  for (const id of ['btnHome', 'btnCalib', 'btnDump', 'dumpType', 'btnSniffOn', 'btnSniffOff', 'btnSdriveScan', 'btnSdriveStop', 'sdriveState', 's_mods', 's_fw', 's_focal', 's_dist'])
    check(await page.$('#' + id) === null, 'retiré de la page : #' + id);
  check(await page.$('button[data-cmd]') === null, 'retirés de la page : DUMP et le profil SDRIVE (data-cmd)');
  await page.click('#btnFp100');
  await page.waitForTimeout(1200);
  check(await page.textContent('#s_focus') === '16484', 'f+100 puis poll : position suivie');
  await page.fill('#focusTarget', '20000'); await page.click('#btnFgoto');
  await page.waitForTimeout(1200);
  check(await page.textContent('#s_focus') === '20000', 'aller a 20000');
  await page.click('#btnMarkSet'); await page.waitForTimeout(1200);
  check(await page.evaluate(() => window.__sim.mark) === 20000 && await page.evaluate(() => document.getElementById('scMark').style.display) === '',
        'Marquer -> js, la marque 20000 relue par j et affichee');
  await page.click('#btnFp100'); await page.waitForTimeout(1200);
  await page.click('#btnMarkGo'); await page.waitForTimeout(1200);
  check(await page.textContent('#s_focus') === '20000', 'Aller a la marque -> jg, de retour en 20000');
  /* L09-15 : un déplacement de la page fini bloqué (g = stall) est signalé dans le panneau Focus ; lu sans l'attendre
     (null s'il n'existe pas) */
  const sMove = () => page.evaluate(() => { const e = document.getElementById('s_move'); return e ? e.textContent : null; });
  check(await sMove() === '', 'deplacements arrives : rien de signale');
  await page.evaluate(() => { window.__sim.stallNext = true; });
  await page.click('#btnFp100'); await page.waitForTimeout(1200);
  check(String(await sMove()).includes('stall') && (await page.textContent('#log')).includes('[app] fin du déplacement : stall'),
        'bloque (g = stall) : signale au panneau Focus et au journal (« ' + (await sMove()) + ' »)');
  /* Un Stop de l'humain (q) pendant un déplacement de la page : g = aborted, un arrêt demandé, pas un échec (PROTOCOL.md
     § 2, ligne q) — au journal une ligne d'événement, aucune d'erreur, et le panneau Focus dit « arrêté ». Le Stop part
     une fois le déplacement accepté par le faux port (sinon q le retire de la file). */
  const moveLines = () => page.$$eval('#log .l', ls => ls.filter(l => l.textContent.includes('[app] fin du déplacement')).map(l => l.className + ' | ' + l.textContent));
  const before = (await moveLines()).length;
  await page.click('#btnFp100');
  await page.waitForFunction(() => window.__sim.moving > 0, null, { timeout: 2000 }).catch(() => {});
  await page.click('#btnStop'); await page.waitForTimeout(1200);
  const stopLines = (await moveLines()).slice(before);
  check(await page.evaluate(() => window.__sim.g) === 'aborted' && stopLines.length === 1 && /\bev\b/.test(stopLines[0]) && !/\berr\b/.test(stopLines[0]),
        'arrete (g = aborted) : une ligne d\'evenement au journal, pas d\'erreur (' + JSON.stringify(stopLines) + ')');
  check(String(await sMove()).includes('arrêté') && !String(await sMove()).includes('aborted'),
        'arrete (g = aborted) : le panneau Focus dit « arrêté » (« ' + (await sMove()) + ' »)');
  /* L'arrêt non confirmé (aborted passé à unconfirmed, relu pendant la fenêtre de l'arrêt) reste une erreur. */
  await page.evaluate(() => { window.__sim.g = 'unconfirmed'; }); await page.waitForTimeout(1500);
  const unconf = (await moveLines()).slice(before + 1);
  check(unconf.length === 1 && /\berr\b/.test(unconf[0]) && unconf[0].includes('unconfirmed') && String(await sMove()).includes('unconfirmed'),
        'aborted puis unconfirmed : signale en erreur (' + JSON.stringify(unconf) + ', « ' + (await sMove()) + ' »)');
  check(await page.evaluate(() => window.__sent.filter(c => c === 'DEBUG').length) === 1 && await page.evaluate(() => window.__sent.includes('g')),
        'la fin se lit par g ; DEBUG une fois, a la connexion seulement');
  await page.click('#btnIp'); await page.waitForTimeout(1500);
  check((await page.textContent('#s_fnum')).startsWith('f/2'), '+1/3 : f/2 rapporte par o');
  /* L09-17 : pointerdown sans change (relâché sans bouger, pointercancel) laissait le glissement ouvert, et le curseur ne
     suivait plus la position */
  await page.dispatchEvent('#focusRange', 'pointerdown'); await page.dispatchEvent('#focusRange', 'pointerup');
  await page.evaluate(() => { window.__sim.pos = 21000; }); await page.waitForTimeout(700);
  check(await page.$eval('#focusRange', e => e.value) === '21000', 'focus relache sans bouger : le curseur suit la position (' + await page.$eval('#focusRange', e => e.value) + ')');
  await page.dispatchEvent('#focusRange', 'pointerdown'); await page.dispatchEvent('#focusRange', 'pointercancel');
  await page.evaluate(() => { window.__sim.pos = 21500; }); await page.waitForTimeout(700);
  check(await page.$eval('#focusRange', e => e.value) === '21500', 'focus annule (pointercancel) : le curseur suit la position (' + await page.$eval('#focusRange', e => e.value) + ')');
  await page.dispatchEvent('#irisRange', 'pointerdown'); await page.dispatchEvent('#irisRange', 'pointerup');
  await page.evaluate(() => { window.__sim.fnum = 4; }); await page.waitForTimeout(1500);
  check(await page.textContent('#irisRangeVal') === 'f/4', 'ouverture relachee sans bouger : le curseur suit l ouverture rapportee (« ' + await page.textContent('#irisRangeVal') + ' »)');
  await page.evaluate(() => { window.__sim.pos = 20000; });       /* la position que :GP# relit plus bas (4E20#) */
  await page.click('#btnReboot');
  const vu = sel => page.waitForFunction(t => document.getElementById('s_state').textContent.includes(t), sel, { timeout: 4000 }).then(() => true, () => false);
  check(await vu('occupé : boot'), 'b : demarrage, etat occupe via t');
  await page.waitForFunction(() => document.getElementById('s_state').textContent.includes('prêt') && document.getElementById('scMin').textContent.replace(/\s/g, '').startsWith('13700'), null, { timeout: 5000 }).catch(() => {});
  check((await page.textContent('#s_state')).includes('prêt') && (await page.textContent('#scMin')).replace(/\s/g,'').startsWith('13700'), 'fin du demarrage : bornes relues');
  await page.evaluate(() => { window.__sim.present = false; });
  await page.waitForTimeout(1500);
  check((await page.textContent('#s_state')).includes('absent'), 'objectif disparu : etat absent');
  check(await page.isDisabled('#btnFp100'), 'boutons focus desactives sans objectif');
  await page.evaluate(() => { window.__sim.present = true; });
  await page.waitForTimeout(1500);
  check((await page.textContent('#s_state')).includes('prêt'), 'objectif revenu');
  /* L09-14 : en fault, le statut le dit avec sa raison, et les panneaux qui ont besoin d'un objectif prêt sont inactifs */
  await page.evaluate(() => { window.__sim.fault = true; });
  await page.waitForTimeout(1500);
  check(await page.textContent('#s_state') === '· FAULT : lost', 'fault : statut et raison (« ' + (await page.textContent('#s_state')) + ' »)');
  check(await page.isDisabled('#btnFp100') && await page.isDisabled('#focusRange') && await page.isDisabled('#btnMarkGo') && await page.isDisabled('#irisRange') && await page.isDisabled('#btnIp'),
        'fault : panneaux Focus et Ouverture inactifs');
  await page.evaluate(() => { window.__sim.fault = false; });
  await page.waitForTimeout(1500);
  check((await page.textContent('#s_state')).includes('prêt') && !(await page.isDisabled('#btnFp100')), 'sortie de fault : pret, panneaux actifs');
  await page.fill('#cliIn', ':GP#'); await page.press('#cliIn', 'Enter'); await page.waitForTimeout(500);
  const logTxt = await page.textContent('#log');
  check(/4E20#/.test(logTxt), 'Moonlite :GP# depuis la ligne de commande -> 4E20#');
  const sent = (await page.evaluate(() => window.__sent)) || [];
  const star = sent.filter(c => !/^[a-z:A-Z]/.test(c));
  check(star.length === 0, 'rien d\'autre que des lettres, Moonlite et majuscules n\'est parti (' + sent.length + ' commandes)');
  const retired = sent.filter(c => /^(u.*|cm|m|ms|mg|mx)$/.test(c));
  check(retired.length === 0 && sent.includes('j') && sent.includes('js') && sent.includes('jg'),
        'ni u…, ni cm, ni m, ms, mg, mx ; la marque par j, js, jg (' + retired.join(' ') + ')');
  const dead = sent.filter(c => /^([hcnwld]|DUMP.*|SNIFF.*|SDRIVE.*)$/.test(c));
  check(dead.length === 0, 'ni h, c, n, w, l, d, DUMP, SNIFF, SDRIVE : non servis par la carte (' + dead.join(' ') + ')');
  check(pageErrors.length === 0, 'aucune erreur JavaScript dans la page (' + pageErrors.length + ' pageerror)');
  for (const e of pageErrors) console.log('       ' + (e.stack || e.message).split('\n').join('\n       '));
  console.log(fails + ' echec(s)');
  await browser.close();
  process.exit(fails ? 1 : 0);
})();
