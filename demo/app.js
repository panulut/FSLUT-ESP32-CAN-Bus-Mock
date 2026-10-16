const $ = id => document.getElementById(id);
const token = document.querySelector('meta[name="demo-token"]').content;
let sequence = 0, state = null, rows = [], busy = false, toastTimer;
const fmt = n => Number(n).toLocaleString('fi-FI');
const escape = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

function toast(text, error = false) {
  $('toast').textContent = text;
  $('toast').className = 'toast' + (error ? ' error' : '');
  $('toast').hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $('toast').hidden = true, 3500);
}
async function send(...commands) {
  if (busy) return;
  busy = true;
  try {
    const response = await fetch('/api/command', {
      method: 'POST', headers: {'Content-Type':'application/json','X-Demo-Token':token},
      body: JSON.stringify({commands})
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Komento epäonnistui');
    await refresh();
  } catch (error) { toast(error.message, true); }
  finally { busy = false; }
}
function renderLog() {
  const filter = $('log-filter').value;
  const visible = rows.filter(e => filter === 'all' || e.kind === filter ||
    (filter === 'reply' && e.kind === 'command'));
  if (!visible.length) {
    $('log').innerHTML = '<div class="empty-log"><span class="empty-icon">⇄</span><b>Väylä odottaa ensimmäistä viestiä</b><span>Paina Käynnistä tai kokeile tallenteen toistoa.</span></div>';
  } else {
    const atBottom = $('log').scrollHeight - $('log').scrollTop - $('log').clientHeight < 60;
    $('log').innerHTML = visible.map(e => {
      const isFrame = e.kind === 'tx' || e.kind === 'rx';
      const error = e.kind === 'reply' && e.text.startsWith('ERR');
      return `<div class="log-row ${escape(e.kind)} ${error ? 'error' : ''}"><span class="time">${(e.time_ms/1000).toFixed(3)} s</span><span class="direction">${e.kind === 'command' ? 'CMD' : e.kind === 'reply' ? (error ? 'ERR' : 'INFO') : e.kind.toUpperCase()}</span>${isFrame ? `<span>${escape(e.id)}</span><span>${e.dlc}</span><span title="${escape(e.data)}">${escape(e.data)}${e.rtr ? ' RTR' : ''}</span>` : `<span class="event-text">${escape(e.text)}</span>`}</div>`;
    }).join('');
    if (atBottom || visible.length < 15) $('log').scrollTop = $('log').scrollHeight;
  }
  $('log-count').textContent = `${fmt(visible.length)} tapahtumaa · viimeiset 400`;
}
function render(s) {
  state = s;
  $('connection').textContent = 'Yhteys simulaatioytimeen';
  $('connection').classList.remove('offline');
  $('sent').textContent = fmt(s.sent); $('rx').textContent = fmt(s.rx); $('failed').textContent = fmt(s.failed);
  $('clock').textContent = (s.time_ms / 1000).toFixed(1).replace('.', ',') + ' s';
  $('mode-label').textContent = `${s.mode} · ${s.running ? 'lähetys käynnissä' : 'lähetys pysäytetty'}`;
  $('run-label').textContent = s.fault ? 'Väylävirhe' : s.running ? 'Simulaatio käynnissä' : 'Pysäytetty';
  $('run-label').parentElement.className = 'runtime' + (s.fault ? ' fault' : s.running ? ' running' : '');
  if (document.activeElement !== $('mode')) $('mode').value = s.mode;
  const synthetic = s.profile === 'synthetic';
  $('vehicle-profile').classList.toggle('active', !synthetic);
  $('synthetic-profile').classList.toggle('active', synthetic);
  $('signals').hidden = !synthetic;
  $('profile-note').textContent = synthetic ? 'Testiprofiili: RPM +600/s ja SOC −0,5 %/s AUTO-tilassa. Auton profiilia ei muuteta.' : 'Profiilin vaihto nollaa testiväylän laskurit ja pysäyttää lähetyksen.';
  for (const signal of s.signals) {
    if (signal.name === 'RPM') { $('rpm-value').textContent = fmt(signal.value); $('rpm-bar').style.width = `${signal.value / 6000 * 100}%`; }
    if (signal.name === 'SOC') { $('soc-value').textContent = signal.value.toFixed(1).replace('.', ',') + ' %'; $('soc-bar').style.width = `${signal.value}%`; }
    if (signal.name === 'NMT_STATE' && document.activeElement !== $('nmt')) $('nmt').value = String(signal.value);
  }
  $('messages').innerHTML = s.messages.map(m => `<tr><td>${escape(m.id)}</td><td>${m.dlc}</td><td>${escape(m.data)}</td><td>${m.interval} ms${m.enabled ? '' : ' · pois'}</td></tr>`).join('');
  const nmt = s.messages.find(m => m.id === '0x701');
  if (nmt && document.activeElement !== $('interval')) $('interval').value = String(nmt.interval);
  $('progress-fill').style.width = `${(s.complete ? s.count : s.position) / s.count * 100}%`;
  $('progress-label').textContent = `${s.complete ? s.count : s.position} / ${s.count} viestiä${s.complete ? ' · valmis' : ''}`;
  $('loop').checked = s.loop; $('ack').checked = s.ack; $('fault-box').hidden = !s.fault;
  $('start').textContent = s.running ? '▶ Käynnissä' : '▶ Käynnistä';
  if (s.events.length) {
    rows.push(...s.events); rows = rows.slice(-400);
    const reply = s.events.filter(e => e.kind === 'reply').at(-1);
    if (reply) { $('last-reply').textContent = reply.text; $('last-reply').classList.toggle('error', reply.text.startsWith('ERR')); }
    renderLog();
  }
  sequence = Math.max(sequence, s.sequence);
}
let refreshing = false;
async function refresh() {
  if (refreshing) return;
  refreshing = true;
  try {
    const response = await fetch(`/api/state?since=${sequence}`);
    if (!response.ok) throw new Error('Tilatietoja ei saatu');
    const s = await response.json();
    if (s.error) throw new Error(s.error);
    render(s);
  } catch (error) {
    $('connection').textContent = 'Yhteys katkennut'; $('connection').classList.add('offline');
    $('run-label').textContent = 'Ei yhteyttä';
  } finally { refreshing = false; }
}

$('start').onclick = () => send('START');
$('stop').onclick = () => send('STOP');
$('pause').onclick = () => send('PAUSE');
$('mode').onchange = () => send('STOP', `MODE ${$('mode').value}`);
$('nmt').onchange = () => send(`SET_SIGNAL NMT_STATE ${$('nmt').value}`);
$('interval').onchange = () => send(`INTERVAL 0x701 ${$('interval').value}`);
$('loop').onchange = () => send(`LOOP ${$('loop').checked ? 'ON' : 'OFF'}`);
$('ack').onchange = () => send(`@ACK ${$('ack').checked ? 'ON' : 'OFF'}`);
$('recover').onclick = () => send('RECOVER');
$('inject-rx').onclick = () => send('@RX');
$('replay').onclick = () => send('STOP', 'MODE PLAYBACK', 'RESTART', 'PLAY');
$('resume').onclick = () => send('PLAY');
document.querySelectorAll('[data-profile]').forEach(button => button.onclick = () => send(`@PROFILE ${button.dataset.profile.toUpperCase()}`));
document.querySelectorAll('[data-command]').forEach(button => button.onclick = () => send(button.dataset.command));
$('log-filter').onchange = renderLog;
$('clear-log').onclick = () => { rows = []; renderLog(); };
$('command-form').onsubmit = event => { event.preventDefault(); const text = $('command').value.trim(); if (text) { send(text); $('command').value = ''; } };
refresh(); setInterval(refresh, 180);
