/* Test du transport de l'app dans Node VM (sans navigateur ni carte) : DOM, port serie et
 * horloge simules. Chaque bloc ecrit un comportement attendu du transport, de la resynchronisation, du journal et du
 * decodage. Lancer : node test_app_vm.cjs <repertoire des traces de l'objectif>
 * (../run.sh le donne). */
const fs = require('fs'), vm = require('vm'), path = require('path'), assert = require('assert');
if (process.argv.length !== 3) { console.error('usage : node test_app_vm.cjs <repertoire des traces de l objectif>'); process.exit(2); }
const traces = process.argv[2];
const html = fs.readFileSync(path.join(__dirname, '..', 'emount-bench.html'), 'utf8');
const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
function setup() {
  let id = 0; const timers = new Map(), elements = new Map();
  const el = () => ({ textContent: '', value: '', checked: true, disabled: false, hidden: false, style: {}, classList: { add() {}, remove() {} }, addEventListener() {}, querySelector() { return el(); }, appendChild() {}, removeChild() {}, childElementCount: 0, click() {}, innerHTML: '' });
  const c = vm.createContext({ console, TextEncoder, TextDecoder, Date, Blob, URL, navigator: {}, confirm: () => true, Map, Set, Promise,
    document: { getElementById(k) { if (!elements.has(k)) elements.set(k, el()); return elements.get(k); }, createElement: el, querySelectorAll: () => [], addEventListener() {} },
    setTimeout(fn, ms) { timers.set(++id, { fn, ms }); return id; }, clearTimeout(i) { timers.delete(i); }, setInterval() { return ++id; }, clearInterval() {} });
  vm.runInContext(source, c); return { c, timers, elements, run: s => vm.runInContext(s, c) };
}
async function flush() { for (let i = 0; i < 12; i++) await Promise.resolve(); }
function expire(s) { const t = [...s.timers.entries()].find(([k, v]) => v.ms === 3000 || v.ms === 800); assert(t, 'un timeout arme'); s.timers.delete(t[0]); t[1].fn(); }
/* fait tomber les temporisations courtes (sleep) jusqu'a ce qu'il n'y en ait plus, sans toucher aux timeouts de commande */
async function fireSleeps(s) { for (let n = 0; n < 50; n++) { const t = [...s.timers.entries()].find(([k, v]) => v.ms < 800); if (!t) break; s.timers.delete(t[0]); t[1].fn(); await flush(); } }
const wait = ms => new Promise(r => global.setTimeout(r, ms));
/* une promesse du transport, ou undefined apres 50 ms : un test ne reste jamais pendu (node sortirait en 0 sans verdict) */
const got = p => Promise.race([p, wait(50)]);
/* apres un timeout, le transport envoie `v` avant la commande suivante ; on lui repond la version de la carte (1.0)
   puis on laisse passer le silence requis */
async function answerSync(s) { s.run("onLine('1.0')"); await wait(110); await fireSleeps(s); await flush(); }
let fails = 0; const check = (c, m) => { console.log((c ? '  ok   ' : '  FAIL ') + m); if (!c) fails++; };
(async () => {
  { const s = setup(); s.run("writer={write:async()=>onLine('1.0')}");
    check(await s.run("ask('v',true)") === '1.0', 'reponse immediate recue (attente installee avant l ecriture)'); }
  { const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}");
    const a = s.run("ask('f',true)"), b = s.run("ask('e',true)"); await flush();
    check(s.run('sent.length') === 1, 'deux demandes : une seule en vol');
    s.run("onLine('123')"); check(await a === '123', 'f servie'); await flush();
    check(s.run('sent.length') === 2, 'e part apres f'); s.run("onLine('n')"); check(await b === 'n', 'e servie'); }
  { /* timeout de f, puis la tardive « 16384 » ne doit PAS etre prise pour la reponse a e.
       Apres un timeout le transport se resynchronise (`v` -> 1.3, puis silence) AVANT e. */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}"); const a = s.run("ask('f',true)"); await flush(); expire(s); check(await a === null, 'timeout de f -> null');
    const b = s.run("ask('e',true)"); await flush();
    check(s.run('sent.length') === 2 && s.run('sent[1]') === 'v\n', 'apres un timeout, un `v` de resynchronisation part avant e');
    s.run("onLine('16384')"); await flush();
    check(s.run("waiter!==null&&waiter.cmd==='v'"), 'la tardive 16384 est ignoree (forme invalide pour v), la resynchronisation attend toujours');
    await answerSync(s);
    check(s.run('sent.length') === 3 && s.run('sent[2]') === 'e\n', '1.0 recu puis silence -> e part');
    s.run("onLine('n')"); check(await b === 'n', 'puis la vraie reponse n arrive a e'); }
  { /* deux goto de meme forme : f20000 expire, son « ok » tardif ne doit pas servir f21000 */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}"); const a = s.run("ask('f20000',true)"); await flush(); expire(s); check(await a === null, 'f20000 expire');
    const b = s.run("ask('f21000',true)"); await flush(); s.run("onLine('ok')"); await flush();
    check(s.run("waiter!==null&&waiter.cmd==='v'") && s.run("sent.indexOf('f21000\\n')") === -1, 'le ok tardif de f20000 est ignore, f21000 n est pas encore parti');
    await answerSync(s);
    check(s.run("sent[sent.length-1]") === 'f21000\n', 'f21000 part apres la resynchronisation');
    s.run("onLine('ok')"); check(await b === 'ok', 'et recoit SON ok'); }
  { /* carte muette pendant la resynchronisation (L09-11) : e partait quand meme, sur un canal non
       resynchronise, et pouvait recevoir la tardive d'une autre commande. Attendu : e rend null sans partir, la file
       n'est pas bloquee, et la resynchronisation est retentee avant la commande suivante. */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}"); const a = s.run("ask('f',true)"); await flush(); expire(s); await a;
    const b = s.run("ask('e',true)"); await flush(); expire(s); await flush(); expire(s); await flush(); expire(s); await flush();
    check(s.run("sent.filter(x=>x==='v\\n').length") === 3 && s.run("sent.indexOf('e\\n')") === -1, '3 `v` sans reponse -> e ne part pas (' + s.run("sent.join('|')").replace(/\n/g, '') + ')');
    check(await got(b) === null, 'e non envoyee -> null, la file n est pas bloquee');
    const c = s.run("ask('f',true)"); await flush();
    check(s.run("sent.filter(x=>x==='v\\n').length") === 4 && s.run("sent[sent.length-1]") === 'v\n', 'la commande suivante (f) retente la resynchronisation avant de partir');
    await answerSync(s);
    check(s.run("sent[sent.length-1]") === 'f\n', 'resynchronisee, f part');
    s.run("onLine('16384')"); check(await got(c) === '16384', 'et recoit sa reponse'); }
  { /* L09-12 : l'ecriture de f echoue APRES son echeance ; son catch effacait l'attente de la commande suivante (le `v` de
       la resynchronisation), dont l'echeance ne trouvait plus rien : la pompe restait suspendue. */
    const s = setup(); s.run("globalThis.sent=[]; globalThis.rej=null; writer={write:b=>{ const c=new TextDecoder().decode(b); sent.push(c); return c==='f\\n'?new Promise((r,j)=>{ rej=j; }):Promise.resolve(); }}");
    const a = s.run("ask('f',true)"); await flush(); expire(s); check(await got(a) === null, 'f expire, son ecriture toujours en cours');
    const b = s.run("ask('e',true)"); await flush();
    check(s.run("waiter!==null&&waiter.cmd==='v'"), 'le v de resynchronisation attend sa reponse');
    s.run("rej(new Error('ecriture bloquee'))"); await flush();
    check(s.run("waiter!==null&&waiter.cmd==='v'"), 'l echec de l ecriture de f n efface pas l attente du v');
    await answerSync(s);
    check(s.run("sent[sent.length-1]") === 'e\n', 'la pompe continue : e part');
    s.run("onLine('n')"); check(await got(b) === 'n', 'et recoit sa reponse'); }
  { /* sous un journal soutenu (LOG ON/ALL), les lignes « * » tenaient
       le silence de resynchronisation ouvert ; la boucle tombait sur son plafond (5 x 120 ms) et la
       commande partait sans silence. PROTOCOL.md § 6 : `v`, puis 100 ms sans ligne de reponse. */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}");
    const a = s.run("ask('f',true)"); await flush(); expire(s); await a;
    const b = s.run("ask('e',true)"); await flush();
    s.run("onLine('1.0')"); await flush();
    const journal = () => s.run("onLine('* lens pos=5')");
    journal(); await fireSleeps(s);                                         /* 50 attentes tombent, bien moins de 100 ms reelles apres 1.3 */
    check(s.run("sent[sent.length-1]") === 'v\n', 'moins de 100 ms apres 1.0, e n est pas parti (pas de plafond qui passe outre le silence)');
    for (let n = 0; n < 25 && s.run("sent[sent.length-1]") !== 'e\n'; n++) { journal(); await wait(20); await fireSleeps(s); } /* journal a ~50 lignes/s, sans reponse, 500 ms au plus */
    check(s.run("sent[sent.length-1]") === 'e\n', 'sous un journal soutenu, e part apres 100 ms sans ligne de reponse');
    s.run("onLine('n')"); check(await Promise.race([b, wait(50)]) === 'n', 'et recoit sa reponse'); }
  { /* `q` n'avait aucune forme de reponse ; toute ligne
       arrivee pendant son attente (une tardive « 16384 » de f, par exemple) lui etait prise */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}");
    const q = s.run("ask('q',true)"); await flush();
    s.run("onLine('16384')"); await flush();
    check(s.run("waiter!==null&&waiter.cmd==='q'"), 'une ligne hors forme (16384) n est pas la reponse a q');
    s.run("onLine('er busy boot')"); check(await Promise.race([q, wait(50)]) === 'er busy boot', 'q recoit sa reponse de PROTOCOL.md (er busy boot)'); }
  { /* « Stop » passait dans la meme file que le poll, derriere toute
       requete en attente. q passe devant, attend la requete en vol, et apres une echeance respecte
       la resynchronisation (PROTOCOL.md § 6) avant de partir. */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}");
    const f = s.run("ask('f',true)"), e = s.run("ask('e',true)"), t = s.run("ask('t',true)"); await flush();
    const q = s.run("ask('q',true)"); await flush();
    check(s.run('sent.join()') === 'f\n', 'q attend la requete en vol (une seule en vol)');
    s.run("onLine('16384')"); check(await got(f) === '16384', 'f recoit sa reponse'); await flush();
    check(s.run('sent[sent.length-1]') === 'q\n', 'q part avant e et t, deja en attente');
    s.run("onLine('ok')"); check(await got(q) === 'ok', 'q recoit son ok'); await flush();
    check(s.run('sent[sent.length-1]') === 'e\n', 'puis la file reprend dans l ordre (e)');
    s.run("onLine('n')"); await got(e); await flush(); s.run("onLine('ext=1')"); check(await got(t) === 'ext=1', 'puis t');
    /* apres une echeance : le h expire, q arrive pendant la resynchronisation d'un poll deja en attente */
    const h = s.run("ask('h',true)"); await flush(); expire(s); await got(h);
    const f2 = s.run("ask('f',true)"); await flush();
    check(s.run('sent[sent.length-1]') === 'v\n', 'apres l echeance de h, v de resynchronisation');
    const q2 = s.run("ask('q',true)"); await flush();
    s.run("onLine('ok')"); await flush();                                   /* le ok tardif de h */
    check(s.run("waiter!==null&&waiter.cmd==='v'") && s.run('sent[sent.length-1]') === 'v\n', 'le ok tardif de h n est pas pris, q n est pas parti');
    await answerSync(s);
    check(s.run('sent[sent.length-1]') === 'q\n', '1.0 puis silence : q part, avant le f en attente');
    s.run("onLine('ok')"); check(await got(q2) === 'ok', 'q recoit SON ok'); await flush();
    check(s.run('sent[sent.length-1]') === 'f\n', 'puis f');
    s.run("onLine('16384')"); check(await got(f2) === '16384', 'f servie'); }
  { /* L09-10 : q passait devant la file, mais les deplacements deja en attente partaient juste apres lui, et le moteur
       repartait. Attendu : q retire de la file les deplacements en attente (leur promesse rend null), pas le reste. */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}");
    const f = s.run("ask('f',true)"), m1 = s.run("ask('f20000',true)"), m2 = s.run("ask('f+100',true)"), m3 = s.run("ask('jg',true)"), e = s.run("ask('e',true)"); await flush();
    const q = s.run("ask('q',true)"); await flush();
    check(await got(m1) === null && await got(m2) === null && await got(m3) === null, 'q : f20000, f+100 et jg en attente rendent null');
    s.run("onLine('16384')"); check(await got(f) === '16384', 'la requete en vol (f) recoit sa reponse'); await flush();
    check(s.run('sent[sent.length-1]') === 'q\n', 'q part');
    s.run("onLine('ok')"); check(await got(q) === 'ok', 'q recoit son ok'); await flush();
    check(s.run('sent[sent.length-1]') === 'e\n', 'puis e, qui n est pas un deplacement');
    s.run("onLine('n')"); check(await got(e) === 'n', 'e servie');
    check(!s.run("sent.some(c=>/^(f20000|f\\+100|jg)\\n$/.test(c))"), 'aucun deplacement retire n est parti (' + s.run("sent.join('|')").replace(/\n/g, '') + ')'); }
  { /* face a une carte muette, la page restait « connectee » ~89 s
       (8 essais de `v`, chacun precede d'une resynchronisation), puis lancait le poll quand meme.
       Attendu : 4 echeances de `v` (12 s : v, puis la resynchronisation de 3 v ; le second `v` ne part pas sur un canal
       non resynchronise, L09-11), carte muette au journal, deconnexion ; puis reconnexion. */
    const s = setup(); const txt = k => s.elements.get(k) ? s.elements.get(k).textContent : undefined;
    const port = repond => { s.c.repond = repond; s.run("globalThis.sent=[]; globalThis.rel=null; navigator.serial={requestPort:async()=>({open:async()=>{},close:async()=>{},writable:{getWriter:()=>({write:async b=>{ const c=new TextDecoder().decode(b); sent.push(c); const r=repond[c]; if(r) Promise.resolve().then(()=>onLine(r)); },releaseLock(){}})},readable:{getReader:()=>({read:()=>new Promise(r=>{rel=r;}),cancel:async()=>{ if(rel) rel({done:true}); },releaseLock(){}})}})}"); };
    port({});
    const c = s.run('connect()'); await flush(); await flush();
    check(txt('connTxt') !== 'connecté', 'tant que `v` n a pas repondu, la page ne se dit pas connectee');
    for (let n = 0; n < 4; n++) { await fireSleeps(s); expire(s); await flush(); await flush(); }
    await got(c);
    check(s.run("lines.some(l=>l.includes('carte muette'))") && txt('connTxt') === 'déconnecté' && s.elements.get('btnConnect').disabled === false, '4 echeances de v (12 s) -> « carte muette », deconnexion, bouton Connecter rendu');
    check(s.run("sent.every(x=>x==='v\\n')") && s.run('sent.length') === 4, 'rien d autre que 4 `v` n est parti (ni t ni poll) (' + s.run('sent.length') + ')');
    port({ 'v\n': '1.0', 'DEBUG\n': 'ok reset=brownout dropped=0 move_rc=-', 't\n': 'ext=1 present=0 boot_state=off busy=- power_off=1 last_op=-:ok mf=- oss=-' });
    const c2 = s.run('connect()'); await flush(); await flush(); await got(c2);
    check(txt('connTxt') === 'connecté' && s.run("sent.join('')") === 'v\nDEBUG\nt\n', 'reconnexion a une carte qui repond : v, DEBUG, t, connectee, sans retard');
    /* `v` est la version de la carte, la cause du redemarrage vient de DEBUG, l'etat de `t` (ext, power_off) */
    check(s.run("lines.some(l=>l.endsWith('[app] carte : firmware 1.0'))"), 'v lu comme la version du firmware de la carte');
    check(txt('s_board') === 'fw 1.0 · reset brownout · boot off', 'la ligne de la carte : v, reset de DEBUG, boot_state de t (« ' + txt('s_board') + ' »)');
    check(txt('s_caps') === 'lot étendu (ext=1)' && txt('s_power') === 'alims coupées (p0) : p1 ou b pour rétablir', 'ext lu au lieu de caps, power_off'); }
  { /* Audit R5 : l'ouverture du port peut redemarrer la carte ; les lignes de la ROM de l'ESP32-S3 (son demarrage
       ordinaire) et des octets hors ASCII arrivent apres l'envoi du premier `v`, pendant l'attente de sa reponse. La
       page tient deja la propriete, sans code de plus : une ligne hors de la forme de `v` (expectFor, `<maj>.<min>`)
       n'est jamais prise pour sa reponse, et un `v` perdu passe par la resynchronisation. Joue par connect() et
       readLoop() de la page, octets bruts : 1) la ROM puis la reponse ; 2) la ROM, dont la derniere ligne, sans fin,
       se colle a la reponse : `v` expire, la resynchronisation la rattrape. Dans les deux cas, la connexion aboutit. */
    const ROM = ['ESP-ROM:esp32s3-20210327\r\n', 'Build:Mar 27 2021\r\n', 'rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n',
                 'Saved PC:0x40378ad6\r\n', 'SPIWP:0xee\r\n', 'mode:DIO, clock div:1\r\n', 'load:0x3fce3810,len:0x178c\r\n',
                 'entry 0x403c9900\r\n'];
    const carte = (s, colle) => {
      s.c.ROM = ROM; s.c.colle = colle;
      s.run("globalThis.sent=[]; globalThis.chunks=[]; globalThis.wake=null; globalThis.nv=0;"
        + "globalThis.push=b=>{ chunks.push(typeof b==='string'?new TextEncoder().encode(b):b); if(wake){ const w=wake; wake=null; w(); } };"
        + "globalThis.repond=c=>{ if(c==='v\\n'&&nv++===0){ ROM.forEach(push); push(new Uint8Array([0x00,0xFF,0xFE,0x1B,0x0D,0x0A])); push(colle?'entry 0x40':''); push('1.0\\n'); }"
        + "  else if(c==='v\\n') push('1.0\\n'); else if(c==='DEBUG\\n') push('ok reset=usb dropped=0 move_rc=-\\n');"
        + "  else if(c==='t\\n') push('ext=1 present=0 boot_state=off busy=- power_off=0 last_op=-:ok mf=- oss=-\\n'); };"
        + "navigator.serial={requestPort:async()=>({open:async()=>{},close:async()=>{},"
        + "writable:{getWriter:()=>({write:async b=>{ const c=new TextDecoder().decode(b); sent.push(c); repond(c); },releaseLock(){}})},"
        + "readable:{getReader:()=>({read:async()=>{ while(!chunks.length) await new Promise(r=>{wake=r;}); return {value:chunks.shift(),done:false}; },"
        + "cancel:async()=>{ if(wake) wake(); },releaseLock(){}})}})}");
    };
    { const s = setup(); const txt = k => s.elements.get(k) ? s.elements.get(k).textContent : undefined;
      carte(s, false);
      const c = s.run('connect()'); for (let n = 0; n < 20; n++) await flush(); await got(c);
      check(txt('connTxt') === 'connecté' && s.run("sent.join('')") === 'v\nDEBUG\nt\n',
            'R5 : la ROM puis 1.0 pendant l attente de v : connectee, v, DEBUG, t (' + s.run("sent.join('|')").replace(/\n/g, '') + ')');
      check(s.run("lines.filter(l=>l.includes('(réponse inattendue à « v », ignorée)')).length") === 9 && s.run('L.ver') === '1.0'
            && s.run("lines.some(l=>l.endsWith('[app] carte : firmware 1.0'))") && txt('s_board').startsWith('fw 1.0 · reset usb'),
            'R5 : les 8 lignes de la ROM et les octets hors ASCII ignores, aucune prise pour une reponse ; la version lue est 1.0'); }
    { const s = setup(); const txt = k => s.elements.get(k) ? s.elements.get(k).textContent : undefined;
      carte(s, true);
      const c = s.run('connect()'); for (let n = 0; n < 20; n++) await flush();
      check(txt('connTxt') !== 'connecté' && s.run("waiter!==null&&waiter.cmd==='v'"), 'R5 : « entry 0x401.0 », hors forme, n est pas la reponse a v ; v attend');
      expire(s); for (let n = 0; n < 20; n++) await flush();
      await wait(110); await fireSleeps(s); for (let n = 0; n < 20; n++) await flush(); await got(c);
      check(txt('connTxt') === 'connecté' && s.run("sent.join('')") === 'v\nv\nv\nDEBUG\nt\n' && s.run('L.ver') === '1.0'
            && s.run("lines.filter(l=>l.includes('(réponse inattendue à « v », ignorée)')).length") === 10,
            'R5 : v expire, la resynchronisation (v) puis le second v aboutissent : connectee (' + s.run("sent.join('|')").replace(/\n/g, '') + ')'); }
  }
  { /* L09-13 : « Connecter » n'etait desactive qu'apres requestPort et open : deux clics ouvraient deux ports */
    const s = setup(); s.run("globalThis.asked=0; navigator.serial={requestPort:async()=>{ asked++; return {open:()=>new Promise(()=>{}),close:async()=>{}}; }}");
    s.run('connect()'); check(s.elements.get('btnConnect').disabled === true, 'Connecter desactive des le clic');
    s.run('connect()'); await flush();
    check(s.run('asked') === 1, 'deux clics pendant l ouverture : un seul port demande (' + s.run('asked') + ')'); }
  { /* L09-19 : une deconnexion pendant refreshAll de la connexion ; connect lancait quand meme startPoll */
    const s = setup();
    s.run("globalThis.sent=[]; navigator.serial={requestPort:async()=>({open:async()=>{},close:async()=>{},writable:{getWriter:()=>({write:async b=>{ const c=new TextDecoder().decode(b); sent.push(c); const r={'v\\n':'1.0','DEBUG\\n':'ok reset=poweron dropped=0 move_rc=-'}[c]; if(r) Promise.resolve().then(()=>onLine(r)); },releaseLock(){}})},readable:{getReader:()=>({read:()=>new Promise(()=>{}),cancel:async()=>{},releaseLock(){}})}})}");
    const c = s.run('connect()'); for (let n = 0; n < 4; n++) await flush();
    check(s.run("sent[sent.length-1]") === 't\n', 'connect en est a refreshAll (t en attente)');
    await s.run('disconnect()'); await got(c);
    check(s.run('pollTimer') === null, 'deconnecte pendant refreshAll : le poll n est pas lance'); }
  { /* L09-19 : un rappel du poll de l'ancienne connexion continuait apres la deconnexion (pollSlow, busyPoll) */
    const s = setup(); s.run("globalThis.ticks=[]; setInterval=fn=>{ ticks.push(fn); return ticks.length; }");
    s.run("globalThis.asked=[]; globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b)),releaseLock(){}}; port={close:async()=>{}}; L.present=true; slowTick=4; startPoll(); const real=ask; ask=(c,q)=>{ asked.push(c); return real(c,q); }");
    const tick = s.run('ticks[0]()'); await flush();
    check(s.run("sent.join('')") === 'f\n', 'le poll de la premiere connexion lit f');
    await s.run('disconnect()'); await got(tick); await flush();
    check(s.run("asked.join()") === 'f', 'apres la deconnexion, le rappel de l ancienne connexion ne demande plus rien (' + s.run("asked.join()") + ')');
    check(s.elements.get('s_state').textContent === '—', 'ni ne touche l etat affiche (« ' + s.elements.get('s_state').textContent + ' »)'); }
  { /* deconnexion pendant une demande -> la demande rend null, la reconnexion peut envoyer */
    const s = setup(); s.run("writer={write:async()=>{},releaseLock(){}}; port={close:async()=>{}}"); let done = null; s.run("ask('f',true)").then(v => done = v); await flush();
    await s.run('disconnect()'); await flush(); check(done === null, 'deconnexion : la demande en cours rend null');
    s.run("globalThis.sent=[]; writer={write:async b=>sent.push(b)}; port={}; ask('v',true)"); await flush();
    check(s.run('sent.length') === 1, 'apres reconnexion, la nouvelle demande part'); }
  { /* e en erreur -> mouvement inconnu (null), pas « arrete » */
    const s = setup(); s.run("writer={write:async()=>onLine('er link lens')}; L.present=true; L.moving=true");
    await s.run('pollFast()'); check(s.run('L.moving') === null, 'e en erreur -> moving = null (inconnu), pas false'); }
  { const s = setup(); s.run("L.present=true;L.ext=true;L.min=100;L.max=500;globalThis.replies=['ext=1 present=0'];ask=async()=>replies.shift();");
    await s.run('refreshAll()'); check(s.run('L.present') === false && s.run('L.min') === null, 'objectif absent : bornes effacees'); }
  { /* un '#' dans une ligne non Moonlite ne bloque rien ; un GV de 2 octets est livre */
    const s = setup(); s.run("connGen=1; port={}; globalThis.stream=[new TextEncoder().encode('* note #1\\nok\\n')]; reader={read:async()=>stream.length?{value:stream.shift(),done:false}:{done:true},cancel:async()=>{},releaseLock(){}}; globalThis.answer=null; waiter={cmd:'x',gen:1,expect:null,moon:false,resolve:l=>answer=l,cancel(){}}; port=null");
    await s.run('readLoop(1)'); check(s.run('answer') === 'ok', 'ligne avec # puis reponse : la reponse est livree');
    const s2 = setup(); s2.run("connGen=1; globalThis.stream=[new TextEncoder().encode('10')]; reader={read:async()=>stream.length?{value:stream.shift(),done:false}:{done:true},cancel:async()=>{},releaseLock(){}}; globalThis.answer=null; waiter={cmd:':GV#',gen:1,expect:null,moon:true,moonFixed:2,resolve:l=>answer=l,cancel(){}}; port=null");
    await s2.run('readLoop(1)'); check(s2.run('answer') === '10', ':GV# -> les 2 octets « 10 » sont livres sans terminateur');
    const s3 = setup(); s3.run("connGen=1; globalThis.stream=[new TextEncoder().encode('4E20#')]; reader={read:async()=>stream.length?{value:stream.shift(),done:false}:{done:true},cancel:async()=>{},releaseLock(){}}; globalThis.answer=null; waiter={cmd:':GP#',gen:1,expect:null,moon:true,moonFixed:0,resolve:l=>answer=l,cancel(){}}; port=null");
    await s3.run('readLoop(1)'); check(s3.run('answer') === '4E20#', ':GP# -> « 4E20# » sans LF est livre'); }
  { /* un 0x05 de 97 octets (FE 24-105) se decode, et des fragments se reassemblent.
       97 est la mise en page lue, pas une table d'objectif */
    const s = setup();
    const payload = new Array(97).fill(0); payload[0] = 0x05; payload[25] = 0xF0; payload[26] = 0x00; payload[27] = 0xF0; payload[28] = 0x00; /* offsets 24-27 apres le type */
    const len = 5 + 97 + 3; const bytes = [0xF0, len & 255, len >> 8, 1, 7, ...payload, 0, 0, 0x55];
    let ck = 0; for (let i = 1; i < len - 3; i++) ck = (ck + bytes[i]) & 0xFFFF; bytes[len - 3] = ck & 255; bytes[len - 2] = ck >> 8;
    s.c.bytes = bytes;
    const f = s.run("decodeFrame(bytes,'L2B')");
    check(f && !f.err && f.table === 'samyang' && f.msgs.length === 1 && f.msgs[0].fields && f.msgs[0].fields.focal_mm === '24.0', '0x05 de 97 octets : pave par la mise en page lue, focale decodee');
    const hexs = bytes.map(b => b.toString(16).padStart(2, '0').toUpperCase());
    const l1 = '* rx 1000 +0/' + len + ' ' + hexs.slice(0, 64).join(' '), l2 = '* rx 1000 +64/' + len + ' ' + hexs.slice(64).join(' ');
    s.c.l1 = l1; s.c.l2 = l2;
    s.run("addLine('rx', l1)"); check(s.run('frags.size') === 1, 'premier fragment garde en attente');
    s.run("addLine('rx', l2)"); check(s.run('frags.size') === 0, 'second fragment : trame reassemblee et decodee'); }
  { /* la generation change PENDANT l'attente de read() : les octets de l'ancienne lecture ne sont pas livres */
    const s = setup(); s.run("connGen=1; port={}; globalThis.answer=null; waiter={cmd:'f',gen:1,expect:null,moon:false,resolve:l=>answer=l,cancel(){}}; globalThis.rel=null; reader={read:()=>new Promise(r=>{rel=r;}),cancel:async()=>{},releaseLock(){}}; port=null");
    const loop = s.run('readLoop(1)'); await flush();
    s.run("connGen=2; waiter={cmd:'e',gen:2,expect:null,moon:false,resolve:l=>answer='NEW:'+l,cancel(){}}"); /* reconnexion pendant la lecture */
    s.run("rel({value:new TextEncoder().encode('16384\\n'),done:false})"); await loop;
    check(s.run('answer') === null, 'octets lus apres un changement de generation -> jamais livres a la nouvelle attente'); }
  { /* LOG ON pendant :GV# : « * rx … » puis « 10 » -> l evenement est journalise, la reponse est « 10 » */
    const s = setup(); s.run("connGen=1; globalThis.stream=[new TextEncoder().encode('* rx 1000 F0 08 00 01 02 10 00 13 00 55\\n1'), new TextEncoder().encode('0')]; reader={read:async()=>stream.length?{value:stream.shift(),done:false}:{done:true},cancel:async()=>{},releaseLock(){}}; globalThis.answer=null; waiter={cmd:':GV#',gen:1,expect:null,moon:true,moonFixed:2,resolve:l=>answer=l,cancel(){}}; port=null");
    await s.run('readLoop(1)'); check(s.run('answer') === '10', ':GV# avec un evenement devant, en deux morceaux -> « 10 »');
    const s2 = setup(); s2.run("connGen=1; globalThis.stream=[new TextEncoder().encode('*'), new TextEncoder().encode(' lens pos=5\\n4E20#')]; reader={read:async()=>stream.length?{value:stream.shift(),done:false}:{done:true},cancel:async()=>{},releaseLock(){}}; globalThis.answer=null; waiter={cmd:':GP#',gen:1,expect:null,moon:true,moonFixed:0,resolve:l=>answer=l,cancel(){}}; port=null");
    await s2.run('readLoop(1)'); check(s2.run('answer') === '4E20#', ':GP# avec un prefixe « * » fragmente entre deux lectures -> « 4E20# »'); }
  { /* :GP# expire (0,8 s), sa reponse tardive « 4000# » n'a pas de fin
       de ligne ; elle se collait a la ligne suivante : la resynchronisation recevait « 4000#1.3 »,
       hors forme, et `v` expirait ; une ligne « * … » qui suivait etait perdue de la meme facon. */
    const lu = (s, morceaux) => { s.c.morceaux = morceaux; s.run("connGen=1; globalThis.stream=morceaux.map(x=>new TextEncoder().encode(x)); reader={read:async()=>stream.length?{value:stream.shift(),done:false}:{done:true},cancel:async()=>{},releaseLock(){}}; globalThis.answer=null; waiter={cmd:'v',gen:1,expect:expectFor('v'),moon:false,resolve:l=>answer=l,cancel(){}}; port=null"); return s.run('readLoop(1)'); };
    const s = setup(); await lu(s, ['4000#1.3\n']);
    check(s.run('answer') === '1.3', '« 4000# » tardive puis « 1.3 » : la resynchronisation recoit 1.3');
    const s2 = setup(); await lu(s2, ['4000#', '* lens pos=5\n', '1.3\n']);
    check(s2.run("lines.some(l=>l.slice(l.indexOf(' ')+1)==='* lens pos=5')") && s2.run('answer') === '1.3', '« 4000# » tardive puis une ligne « * » : la ligne est journalisee telle quelle'); }
  { /* Moonlite muettes : envoyees sans attente (pas de faux timeout) */
    const s = setup(); s.run("globalThis.sent=[]; writer={write:async b=>sent.push(new TextDecoder().decode(b))}");
    const r = s.run("ask(':SN 0BB8#',true)"); await flush();
    check(await r === '' && s.run('waiter===null') && s.run('sent[0]') === ':SN 0BB8#', ':SN# part sans attente ni timeout'); }
  { /* last_op=…:er:"raison avec espaces" est lu en entier ; une valeur entiere entre guillemets sans les guillemets */
    const s = setup(); s.c.line = 'ext=1 present=1 boot_state=fault busy=- power_off=0 last_op=boot:er:"pas de telemetrie 0x06 stable" mf=- oss=- q="a b"';
    const o = s.run('parseKV(line)');
    check(o.last_op === 'boot:er:"pas de telemetrie 0x06 stable"' && o.q === 'a b' && o.boot_state === 'fault' && o.oss === '-', 'raison avec espaces lue en entier, valeur entre guillemets sans les guillemets'); }
  { /* la page n'attend plus les champs retires de `t` ; elle lit i, n, r, a par leur lettre ; la
       marque par j, le role de la bague dans t (ring=), plus aucun u. */
    const s = setup(); const txt = k => s.elements.get(k) ? s.elements.get(k).textContent : undefined;
    s.run("globalThis.sent=[]; L.ver='1.0'; L.reset='poweron'; globalThis.rep={t:'ext=1 present=1 boot_state=ready busy=- power_off=0 last_op=boot:ok mf=1 oss=0 ring=focus',i:'SAMYANG AF 135mm F1.8',n:'er nocap',w:'er nocap',j:'16340',r:'13878-30733',a:'1.8-22',o:'1.8',l:'er nocap focal',d:'-',f:'20000',e:'n'}; ask=async c=>{ sent.push(c); return rep[c]; };");
    await s.run('refreshAll(false)');
    check(txt('s_id') === 'SAMYANG AF 135mm F1.8' && s.run('L.mark') === 16340 && s.run('L.min') === 13878 && s.run('L.max') === 30733,
          'identite par i, marque par j, bornes par r');
    check(txt('s_t').endsWith(' ring=focus') && txt('s_board') === 'fw 1.0 · reset poweron · boot ready · dernière op. boot:ok' && txt('s_state') === '· prêt',
          'role de la bague lu dans t (« ' + txt('s_t') + ' ») ; ligne de la carte (« ' + txt('s_board') + ' »)');
    check(!s.run("sent.some(c=>/^(u.*|m.*|cm)$/.test(c))"), 'ni u…, ni m…, ni cm dans la lecture de l etat');
    /* L09-16 : n, w, l (er nocap) et d (toujours -) ne sont pas servis : la page ne les scrute plus */
    check(!s.run("sent.some(c=>/^[nwld]$/.test(c))"), 'ni n, ni w, ni l, ni d dans la lecture de l etat (' + s.run("sent.join(' ')") + ')');
    check(s.run("sent.filter(c=>c==='t').length") === 2 && !s.run("sent.some(c=>/^[A-Z]/.test(c))"), 't lu par refreshAll et pollSlow, aucune commande de labo dans le poll');
    s.run("rep.r='er link limits'; rep.j='-'"); await s.run('pollSlow()');
    check(s.run('L.min') === null && s.run('L.mark') === null && s.run("lines.some(l=>l.endsWith('[app] bornes inconnues (er link limits)'))"), 'bornes et marque perdues lues par r et j, au poll'); }
  /* ce que la page affiche : un etat scripte, `ask` remplace par les reponses de `rep` (PROTOCOL.md § 2, § 5) */
  const etat = () => {
    const s = setup(); const txt = k => s.elements.get(k) ? s.elements.get(k).textContent : undefined;
    s.run("globalThis.sent=[]; L.ver='1.0'; globalThis.rep={t:'ext=1 present=1 boot_state=ready busy=- power_off=0 last_op=boot:ok mf=- oss=- ring=-',i:'SAMYANG AF 135mm F1.8',j:'16340',r:'13878-30733',a:'1.8-22',o:'2.8',f:'20000',e:'n',g:'ok'}; ask=async c=>{ sent.push(c); return rep[c]; };");
    return { s, txt };
  };
  { /* L09-14 : t porte boot_state=fault ; le statut affichait « prêt » et les panneaux restaient actifs */
    const { s, txt } = etat();
    s.run("rep.t='ext=1 present=1 boot_state=fault busy=- power_off=0 last_op=boot:er:\"lost\" mf=- oss=- ring=-'; rep.f='er fault'; rep.r='er fault'");
    await s.run('refreshAll(true)');
    check(txt('s_state') === '· FAULT : lost', 'fault : le statut le dit, avec sa raison (« ' + txt('s_state') + ' »)');
    check(s.run('L.min') === null && s.run('L.pos') === null && txt('s_focus') === '—', 'fault : ni bornes ni position');
    s.run('sent.length=0'); await s.run('pollFast()');
    check(s.run('sent.length') === 0, 'fault : le poll rapide ne lit ni f ni e');
    s.run("rep.t='ext=1 present=1 boot_state=ready busy=- power_off=0 last_op=boot:ok mf=- oss=- ring=-'; rep.f='20000'; rep.r='13878-30733'"); await s.run('pollSlow()');
    check(txt('s_state') === '· prêt' && s.run('L.min') === 13878 && s.run('L.pos') === 20000, 'sortie de fault (b) : relu, pret (« ' + txt('s_state') + ' »)'); }
  { /* L09-15 : un deplacement fini en STALLED n'etait pas signale. A la fin d'un deplacement lance par la page (e repasse
       a n), la page lit g et signale toute autre reponse qu'ok, au journal et dans le panneau Focus ; jamais DEBUG. */
    const { s, txt } = etat();
    await s.run('refreshAll(false)');
    s.run("rep['f+100']='ok'; rep.e='y'"); await s.run("$('btnFp100').onclick()"); await s.run('pollFast()');
    check(!s.run("sent.includes('g')"), 'en mouvement (e=y) : g n est pas lu');
    s.run("rep.e='n'; rep.g='stall'"); await s.run('pollFast()');
    check(s.run("sent.filter(c=>c==='g').length") === 1 && !s.run("sent.includes('DEBUG')"), 'e repasse a n : g lu une fois, pas DEBUG');
    check(s.run("lines.some(l=>/\\[app\\] .*déplacement.*stall/.test(l))") && /stall/.test(txt('s_move')), 'stall signale au journal et dans le panneau Focus (« ' + txt('s_move') + ' »)');
    await s.run('pollFast()'); check(s.run("sent.filter(c=>c==='g').length") === 1, 'g n est lu qu a la fin du deplacement de la page');
    /* l'arret non confirme arrive apres (PROTOCOL.md § 3 : trois 0x1C, 4,5 s) : la page relit g a 1 Hz, quelques secondes */
    s.run("rep.g='unconfirmed'"); await s.run('pollSlow()');
    check(/unconfirmed/.test(txt('s_move')) && s.run("lines.some(l=>/\\[app\\] .*unconfirmed/.test(l))"), 'stall puis unconfirmed : signale (« ' + txt('s_move') + ' »)');
    /* un deplacement qui arrive : rien a signaler, le panneau s'efface */
    s.run("rep.jg='ok'; rep.e='n'; rep.g='ok'"); const n = s.run('lines.length'); await s.run("$('btnMarkGo').onclick()"); await s.run('pollFast()');
    check(s.run("sent.filter(c=>c==='g').length") === 3 && txt('s_move') === '' && !s.run("lines.slice(" + n + ").some(l=>/\\[app\\] .*déplacement/.test(l))"), 'jg arrive (g=ok) : rien de signale, le panneau efface');
    /* un deplacement refuse n'est pas suivi */
    s.run("rep['f-10']='er range limits'"); await s.run("$('btnFm10').onclick()"); await s.run('pollFast()');
    check(s.run("sent.filter(c=>c==='g').length") === 3, 'un deplacement refuse : g n est pas lu'); }
  { /* L09-20 : objectif retire, bornes inconnues, o en erreur : marque, ouverture et curseurs gardaient d'anciennes
       valeurs, et ±⅓ calculait depuis une ouverture perimee */
    const { s, txt } = etat();
    await s.run('refreshAll(false)');
    check(s.run('L.fnum') === 2.8 && s.run('L.mark') === 16340 && txt('s_fnum') === 'f/2.8', 'etat de depart : f/2.8, marque 16340');
    s.run("rep.o='er nocap ap'"); await s.run('pollSlow()');
    check(s.run('L.fnum') === null && txt('s_fnum') === 'f/—' && txt('irisRangeVal') === 'f/—', 'o en erreur : ouverture inconnue, « f/— » (« ' + txt('irisRangeVal') + ' »)');
    s.run('sent.length=0'); await s.run("$('btnIp').onclick()"); await s.run("$('btnIm').onclick()");
    check(!s.run("sent.some(c=>/^a/.test(c))"), '±⅓ sans ouverture lue : rien n est envoye (' + s.run("sent.join(' ')") + ')');
    s.run("rep.o='2.8'; rep.r='er link limits'"); await s.run('pollSlow()');
    check(s.run('L.min') === null && s.elements.get('scMark').style.display === 'none', 'bornes inconnues : la marque n est plus placee sur le curseur');
    s.run("rep.r='13878-30733'; rep.a='er nocap ap'"); await s.run('refreshAll(false)');
    check(s.run('L.fmin') === null && txt('s_irange') === '—', 'plage inconnue (a) : « — »');
    s.run('sent.length=0'); await s.run("$('btnIopen').onclick()");
    check(!s.run("sent.some(c=>/^a/.test(c))"), 'pleine ouverture sans plage lue : rien n est envoye (' + s.run("sent.join(' ')") + ')');
    s.run("rep.a='1.8-22'"); await s.run('refreshAll(false)');
    s.run("rep.t='ext=1 present=0 boot_state=off busy=- power_off=0 last_op=boot:ok mf=- oss=- ring=-'"); await s.run('pollSlow()');
    check(s.run('L.mark') === null && s.run('L.fnum') === null && s.run('L.fmin') === null && s.run('L.pos') === null, 'objectif retire : marque, ouverture, plage, position oubliees');
    check(txt('s_fnum') === 'f/—' && txt('focusRangeVal') === '—' && txt('irisRangeVal') === 'f/—' && txt('s_irange') === '—' && txt('s_focus') === '—' && s.elements.get('scMark').style.display === 'none',
          'objectif retire : « — » partout (' + ['s_fnum', 'focusRangeVal', 'irisRangeVal', 's_irange', 's_focus'].map(txt).join(' | ') + ')'); }
  { /* L09-17 : a la deconnexion, un glissement en cours (pointerdown sans change) est termine */
    const s = setup(); s.run("writer={write:async()=>{},releaseLock(){}}; port={close:async()=>{}}; focusDragging=true; irisDragging=true");
    await s.run('disconnect()');
    check(s.run('focusDragging') === false && s.run('irisDragging') === false, 'deconnexion : plus de glissement en cours (focus, ouverture)'); }
  { /* L09-21 : stopsFor ajoutait fmin mais jamais fmax, et FSTOPS s'arretait a f/32. Les crans du firmware (table
       `thirds` de host.c, en f/ x10), recopies a la main : la page doit tous les connaitre. */
    const s = setup();
    const firmware = [1, 1.1, 1.2, 1.4, 1.6, 1.8, 2, 2.2, 2.5, 2.8, 3.2, 3.5, 4, 4.5, 5, 5.6, 6.3, 7.1, 8, 9, 10, 11, 13, 14, 16, 18, 20, 22, 25, 29, 32, 36, 40, 45];
    s.c.fw = firmware;
    check(s.run('fw.every(f=>FSTOPS.includes(f))'), 'les crans de la page couvrent ceux du firmware (manquent : ' + s.run("fw.filter(f=>!FSTOPS.includes(f)).join(' ')") + ')');
    check(s.run('stopsFor(2,45).join()') === '2,2.2,2.5,2.8,3.2,3.5,4,4.5,5,5.6,6.3,7.1,8,9,10,11,13,14,16,18,20,22,25,29,32,36,40,45', 'f/2-45 : le curseur atteint f/45 (' + s.run('stopsFor(2,45).join()') + ')');
    check(s.run('stopsFor(1.8,23).join()') === '1.8,2,2.2,2.5,2.8,3.2,3.5,4,4.5,5,5.6,6.3,7.1,8,9,10,11,13,14,16,18,20,22,23', 'f/1.8-23 (hors cran) : fmax ajoute en dernier (' + s.run('stopsFor(1.8,23).join()') + ')');
    check(s.run('stopsFor(1.8,22).join()') === '1.8,2,2.2,2.5,2.8,3.2,3.5,4,4.5,5,5.6,6.3,7.1,8,9,10,11,13,14,16,18,20,22', 'f/1.8-22 : inchange'); }
  { /* L09-22 : une plage reduite a une valeur (min = max, un seul cran) : division par zero dans le placement des reperes */
    const s = setup();
    s.run("globalThis.made=[]; document.createElement=()=>{ const e={style:{},textContent:''}; made.push(e); return e; }");
    s.run('L.min=1000; L.max=1000; L.mark=1000; L.fmin=2.8; L.fmax=2.8; applyLimits()');
    const left = s.elements.get('scMark').style.left;
    check(/^\d+(\.\d+)?%$/.test(left), 'bornes 1000-1000, marque 1000 : position CSS finie (« ' + left + ' »)');
    check(s.run('STOPS.join()') === '2.8' && s.run('made.length') > 0 && s.run("made.every(e=>/^\\d+(\\.\\d+)?%$/.test(e.style.left))"),
          'plage f/2.8-2.8, un seul cran : reperes en position finie (' + s.run("made.map(e=>e.style.left).join(' ')") + ')'); }
  { /* L09-18 : LAYOUT n'etait mis a jour que si le decodage etait affiche : un 0x0A vu case decochee ne l'oubliait pas, et
       le decodage reactive pavait avec une mise en page perimee. Trames : le 0x05 de 97 octets du 135 (dump05:5) et le
       0x0A de la carte (full:38), lues en place ; un 0x05 de 109 octets (celle d'avant tout 0x0A) ecrit a la main. */
    const s = setup();
    const tr = f => fs.readFileSync(path.join(traces, f), 'utf8').split('\n');
    s.c.l05 = '* rx 1000 ' + tr('sy135-2026-09-20-dump05.txt')[4].trim();
    s.c.l0a = tr('sy135-2026-09-20-full.txt')[37].replace(/^.*?(\* tx )/, '$1').trim();
    const p = new Array(109).fill(0); p[0] = 0x05; const f109 = [0xF0, 117, 0, 1, 7, ...p, 0, 0, 0x55]; let ck = 0; for (let i = 1; i < 114; i++) ck = (ck + f109[i]) & 0xFFFF; f109[114] = ck & 255; f109[115] = ck >> 8;
    const hx = f109.map(x => x.toString(16).padStart(2, '0').toUpperCase());
    s.c.l109a = '* rx 3000 +0/117#4 ' + hx.slice(0, 64).join(' '); s.c.l109b = '* rx 3000 +64/117#4 ' + hx.slice(64).join(' ');
    s.run("addLine('rx', l05)"); check(s.run('LAYOUT[5]') === 97, 'le 0x05 du 135 fixe la mise en page (97)');
    s.run("$('fFrames').checked=false"); s.run("addLine('tx', l0a)"); s.run("$('fFrames').checked=true");
    check(s.run('LAYOUT[5]') === undefined, 'un 0x0A vu case decochee oublie la mise en page');
    s.run("addLine('rx', l109a)"); s.run("addLine('rx', l109b)");
    check(/05:status-main/.test(s.run('lastEl.innerHTML')) && !/non pavé/.test(s.run('lastEl.innerHTML')), 'decodage reactive : le 0x05 de 109 octets pave d apres une mise en page juste'); }
  { /* L09-18 : LAYOUT, REFS et les fragments survivaient a la deconnexion : la connexion suivante decodait contre eux */
    const s = setup(); s.run("writer={write:async()=>{},releaseLock(){}}; port={close:async()=>{}}");
    s.c.l = '* tx 70000 F0 1D 00 01 10 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 CE 02 55'; s.run("addLine('tx', l)");
    s.c.l = '* rx 70001 +0/105#9 F0 69 00 01 07 05'; s.run("addLine('rx', l)");
    s.run('LAYOUT[5]=97');
    check(s.run('REFS.size') === 1 && s.run('frags.size') === 1, 'une reference LOG ALL et un fragment en cours');
    await s.run('disconnect()');
    check(s.run('REFS.size') === 0 && s.run('frags.size') === 0 && s.run('Object.keys(LAYOUT).length') === 0, 'deconnexion : references, fragments et mise en page oublies');
    s.c.l = '* tx 70017 ~03 s12'; s.run("addLine('tx', l)");
    check(/référence inconnue/.test(s.run('lastEl.innerHTML')), 'apres reconnexion, une difference n est pas reconstruite contre une reference de la connexion precedente'); }
  { /* L09-23 : l'URL de l'export (un Blob de tout le journal) n'etait jamais liberee */
    const s = setup();
    s.run("globalThis.made=[]; globalThis.revoked=[]; globalThis.clicked=0; URL={createObjectURL:()=>'blob:export-1', revokeObjectURL:u=>revoked.push(u)}; document.createElement=()=>{ const e={style:{},click(){ clicked++; }}; made.push(e); return e; }");
    s.run("addLine('ev','une ligne')"); s.run("$('btnExport').onclick()");
    check(s.run('clicked') === 1 && s.run('made[made.length-1].href') === 'blob:export-1', 'Exporter : un lien vers le Blob, clique');
    await fireSleeps(s);
    check(s.run('revoked.join()') === 'blob:export-1', 'apres le telechargement, l URL est liberee (revokeObjectURL)'); }
  { /* deux trames de meme sens/taille/milliseconde : la premiere a perdu un morceau, la seconde arrive entiere */
    const s = setup();
    const mk = seq => { const payload = new Array(97).fill(0); payload[0] = 0x05; payload[25] = 0xF0; payload[27] = 0xF0; const len = 105; const b = [0xF0, len & 255, len >> 8, 1, seq, ...payload, 0, 0, 0x55]; let ck = 0; for (let i = 1; i < len - 3; i++) ck = (ck + b[i]) & 0xFFFF; b[len - 3] = ck & 255; b[len - 2] = ck >> 8; return b; };
    const hx = b => b.map(x => x.toString(16).padStart(2, '0').toUpperCase());
    const A = hx(mk(1)), B = hx(mk(2));
    s.c.a1 = '* rx 1000 +0/105#7 ' + A.slice(0, 64).join(' ');           /* le +64 de la trame 7 est perdu */
    s.c.b1 = '* rx 1000 +0/105#8 ' + B.slice(0, 64).join(' '); s.c.b2 = '* rx 1000 +64/105#8 ' + B.slice(64).join(' ');
    s.run("addLine('rx', a1)"); s.run("addLine('rx', b1)"); check(s.run('frags.size') === 2, 'deux trames distinctes par leur numero (#7, #8), pas melangees');
    s.run("addLine('rx', b2)"); check(s.run('frags.size') === 1 && s.run("[...frags.keys()][0]") === 'rx:#7', 'la trame 8 est reassemblee (seq=2), la 7 reste incomplete');
    /* ancien firmware sans #id : un nouveau +0 remplace l assemblage incomplet au lieu de le completer */
    s.run('frags.clear()');
    s.c.l1 = '* rx 2000 +0/105 ' + A.slice(0, 64).join(' '); s.c.l2 = '* rx 2000 +0/105 ' + B.slice(0, 64).join(' '); s.c.l3 = '* rx 2000 +64/105 ' + B.slice(64).join(' ');
    s.run("addLine('rx', l1)"); s.run("addLine('rx', l2)");
    check(s.run('frags.size') === 1 && s.run("[...frags.values()][0].bytes[4]") === 2, 'un nouveau debut remplace l assemblage incomplet (seq du second)');
    s.run("addLine('rx', l3)"); check(s.run('frags.size') === 0, 'la seconde trame se termine correctement'); }
  { /* le 0x05 d'un 135 en flux (97 octets, apres le 0x0A de la carte)
       etait marque « table sony » : sa longueur etait une constante (109, celle d'avant tout 0x0A).
       Trames reelles des traces de l'objectif (argument), lues en place ; le 0x0A est celui de la carte (full:38). */
    const s = setup();
    const tr = f => fs.readFileSync(path.join(traces, f), 'utf8').split('\n');
    const octets = l => l.trim().split(/\s+/).map(x => parseInt(x, 16));
    const d05 = tr('sy135-2026-09-20-dump05.txt'), full = tr('sy135-2026-09-20-full.txt');
    s.c.f05 = octets(d05[4]); s.c.f06 = octets(d05[10]);
    s.c.f0a = octets(full[37].replace(/^.*\* tx \d+ /, ''));
    const a = s.run("decodeFrame(f05,'L2B')");
    check(a && a.ckOk && a.table === 'samyang' && a.msgs.length === 1 && a.msgs[0].type === 5 && a.msgs[0].payload.length === 96, '0x05 du 135 en flux (dump05:5, 97 octets) pave, pas « table sony »');
    const b = s.run("decodeFrame(f06,'L2B')");
    check(b && b.ckOk && b.table === 'samyang' && b.msgs.length === 1 && b.msgs[0].fields.position === 14623, '0x06 du 135 (dump05:11, 40 octets) pave, position 14623');
    /* le 0x06 suivi d'un accuse 1D (§ 1.7.4) : l'accuse reste un message, il n'allonge pas le 0x06 */
    const ack = s.c.f06.slice(0, -3).concat([0x1D, 0x00]); ack[1] = ack.length + 3; let ck = 0; for (let i = 1; i < ack.length; i++) ck = (ck + ack[i]) & 0xFFFF; s.c.fack = ack.concat([ck & 255, ck >> 8, 0x55]);
    const c = s.run("decodeFrame(fack,'L2B')");
    check(c && c.ckOk && c.table === 'samyang' && c.msgs.map(m => m.type).join() === '6,29', '0x06 + accuse 1D 00 -> deux messages, 06 puis 1D');
    /* la mise en page suit le 0x0A : apres un 0x0A vu, un 0x05 de 109 octets (celle d'avant tout 0x0A) pave aussi */
    s.run("decodeFrame(f0a,'B2L')");
    const p = new Array(109).fill(0); p[0] = 0x05; const f109 = [0xF0, 117, 0, 1, 7, ...p, 0, 0, 0x55]; ck = 0; for (let i = 1; i < 114; i++) ck = (ck + f109[i]) & 0xFFFF; f109[114] = ck & 255; f109[115] = ck >> 8; s.c.f109 = f109;
    const d = s.run("decodeFrame(f109,'L2B')");
    check(d && d.ckOk && d.table === 'samyang' && d.msgs.length === 1 && d.msgs[0].payload.length === 108, 'apres un 0x0A, un 0x05 de 109 octets pave (la longueur n est pas une constante)'); }
  { /* trois noms faux ou sans reference dans le journal decode */
    const s = setup();
    check(s.run('NAMES[0x1B]') === 'iris-move', '0x1B deplace l iris sur le 135 (§ 1.8), plus « focus-by-distance »');
    check(s.run('NAMES[0x02]') === 'refus', '0x02 = refus (Tamron § 3.1), plus « status2 »');
    check(s.run('NAMES[0x1F]') === 'msg1F', '0x1F sans role etabli = msg1F, plus « af-hunt »'); }
  { /* 0x07 reel du FE 24-105 G : LensType2 = 32805 -> nom ExifTool dans le journal */
    const s = setup(); s.c.b = [0xF0,0x2B,0x00,0x02,0x00,0x07,0x01,0x07,0x60,0x01,0x00,0x03,0x01,0x17,0xA0,0x25,0x80,0,0,0,0,0x60,0x92,0x86,0x5E,0,0,0,0,0,0,0,0x42,0x0D,0x03,0,0,0,0,0,0x25,0x04,0x55];
    const f = s.run("decodeFrame(b,'L2B')");
    check(f && f.ckOk && f.msgs[0].fields && f.msgs[0].fields.lens_id === 32805 && f.msgs[0].fields.name === 'Sony FE 24-105mm F4 G OSS' && f.msgs[0].fields.fw === '3.01', '0x07 du 24-105 : lens_id=32805, « Sony FE 24-105mm F4 G OSS », fw 3.01'); }
  { /* LOG ALL du firmware, une trame entiere puis ses differences (l'extrait de test_journal.c:t_all_form,
       trames de la capture du Sony) ; les sommes attendues calculees a la main depuis celle de la capture (CE 02 avec 10). */
    const s = setup();
    const bytes = l => l.split(' ').map(x => parseInt(x, 16));
    const ref = () => s.run("REFS.get('tx:3')&&REFS.get('tx:3').bytes.map(x=>x.toString(16).padStart(2,'0').toUpperCase()).join(' ')");
    s.c.l = '* tx 70000 F0 1D 00 01 10 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 CE 02 55'; s.run("addLine('tx', l)");
    check(ref() === 'F0 1D 00 01 10 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 CE 02 55', 'la ligne entiere est la reference du 0x03 emis');
    s.c.l = '* tx 70017 ~03 s12'; s.run("addLine('tx', l)");
    check(ref() === 'F0 1D 00 01 12 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 D0 02 55', '~03 s12 -> numero 12, somme D0 02');
    s.c.l = '* tx 70034 ~03'; s.run("addLine('tx', l)");
    check(ref() === 'F0 1D 00 01 14 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 D2 02 55', '~03 seul -> le pas de 2, numero 14');
    s.c.l = '* tx 70051 ~03 11=10'; s.run("addLine('tx', l)");
    check(ref() === 'F0 1D 00 01 16 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 10 00 02 00 02 01 00 00 00 E4 02 55', '11=10 -> l octet 17 de la trame, numero 16, somme E4 02');
    check(/seq=22/.test(s.run('lastEl.innerHTML')) && /03:body-control/.test(s.run('lastEl.innerHTML')), 'la trame reconstruite est decodee comme une ligne entiere');
    s.c.l = '* lost 1'; s.run("addLine('ev', l)"); s.c.l = '* tx 70068 ~03'; s.run("addLine('tx', l)");
    check(/référence inconnue/.test(s.run('lastEl.innerHTML')) && s.run("REFS.size") === 0, 'apres * lost, une difference sans reference n est pas reconstruite');
    s.c.l = '* rx 70100 ~05 60=01'; s.run("addLine('rx', l)");
    check(/référence inconnue/.test(s.run('lastEl.innerHTML')), 'une difference d un type jamais vu entier : inconnue, sans exception'); }
  { /* « decoder les trames » decochee puis recochee sous LOG ALL : les references suivent quand meme, une
       difference n'est jamais reconstruite contre une reference perimee (sommes calculees a la main, CE 02 avec 10). */
    const s = setup();
    const ref = () => s.run("REFS.get('tx:3')&&REFS.get('tx:3').bytes.map(x=>x.toString(16).padStart(2,'0').toUpperCase()).join(' ')");
    s.c.l = '* tx 70000 F0 1D 00 01 10 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 CE 02 55'; s.run("addLine('tx', l)");
    s.run("$('fFrames').checked=false");
    s.c.l = '* tx 70017 ~03 s12'; s.run("addLine('tx', l)");
    s.c.l = '* tx 70034 ~03 11=10'; s.run("addLine('tx', l)");
    check(s.run('lastEl.innerHTML').indexOf('dec') < 0, 'decochee, rien de decode n est affiche');
    s.run("$('fFrames').checked=true");
    s.c.l = '* tx 70051 ~03'; s.run("addLine('tx', l)");
    check(ref() === 'F0 1D 00 01 16 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 10 00 02 00 02 01 00 00 00 E4 02 55' && /seq=22/.test(s.run('lastEl.innerHTML')),
          'recochee, la difference suivante reconstruite contre la trame precedente (numero 16, offset 11 a 10, somme E4 02)');
    /* une trame en morceaux (le 0x05 de 97 octets du 135) decochee : reassemblee quand meme, sa reference suivie */
    const p = new Array(97).fill(0); p[0] = 0x05; const fr = [0xF0, 105, 0, 1, 7, ...p, 0, 0, 0x55]; let ck = 0; for (let i = 1; i < 102; i++) ck = (ck + fr[i]) & 0xFFFF; fr[102] = ck & 255; fr[103] = ck >> 8;
    const hx = fr.map(x => x.toString(16).padStart(2, '0').toUpperCase());
    s.run("$('fFrames').checked=false");
    s.c.l = '* rx 70060 +0/105#9 ' + hx.slice(0, 64).join(' '); s.run("addLine('rx', l)");
    s.c.l = '* rx 70060 +64/105#9 ' + hx.slice(64).join(' '); s.run("addLine('rx', l)");
    s.run("$('fFrames').checked=true");
    s.c.l = '* rx 70077 ~05 60=01'; s.run("addLine('rx', l)");
    check(s.run("REFS.get('rx:5').bytes[66]") === 1 && s.run("REFS.get('rx:5').bytes[4]") === 7 && /move=1/.test(s.run('lastEl.innerHTML')),
          'un 0x05 en morceaux pendant que la case est decochee sert de reference ensuite (offset 60 -> octet 66, move=1)'); }
  console.log(fails + ' echec(s)');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
