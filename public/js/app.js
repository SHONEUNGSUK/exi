/* ══ ISO 15118-20 EXI Codec Web — app.js ══════════════════ */
'use strict';

const API = '';   // same origin

/* ── Tab navigation ───────────────────────────────────── */
document.querySelectorAll('.nav-btn[data-tab]').forEach(btn => {
  btn.addEventListener('click', () => {
    const t = btn.dataset.tab;
    document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
    document.querySelectorAll('.tab').forEach(s => s.classList.add('hidden'));
    btn.classList.add('active');
    document.getElementById('tab-' + t)?.classList.remove('hidden');
  });
});

/* ── Segmented control helper ─────────────────────────── */
function initSeg(id) {
  const seg = document.getElementById(id);
  seg.querySelectorAll('.seg-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      seg.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
    });
  });
}
initSeg('enc-align');
initSeg('dec-align');
initSeg('view-tog');

function getAlign(id) {
  return document.querySelector(`#${id} .seg-btn.active`)?.dataset.val || 'bit';
}

/* ── Health check ─────────────────────────────────────── */
const badge = document.getElementById('status-badge');
fetch('/api/health').then(r => r.json()).then(d => {
  if (d.status === 'ok' && d.codec === 'ready') {
    badge.textContent = '● Online'; badge.className = 'badge ok';
  } else {
    badge.textContent = '⚠ Codec missing'; badge.className = 'badge err';
  }
}).catch(() => { badge.textContent = '✗ Offline'; badge.className = 'badge err'; });

/* ── Load examples ────────────────────────────────────── */
const exSel = document.getElementById('ex-select');
fetch('/api/examples').then(r => r.json()).then(d => {
  d.examples.forEach(ex => {
    const o = document.createElement('option');
    o.value = ex.filename;
    o.textContent = ex.displayName + '  (' + ex.size + 'B)';
    exSel.appendChild(o);
  });
}).catch(() => {});

exSel.addEventListener('change', async () => {
  const f = exSel.value; if (!f) return;
  try {
    const r = await fetch('/api/example/' + f);
    document.getElementById('xml-in').value = await r.text();
    updateXmlMeta(); exSel.value = ''; toast('예제 로드: ' + f, 'info');
  } catch { toast('로드 실패', 'err'); }
});

/* ── XML input metadata ───────────────────────────────── */
const xmlIn   = document.getElementById('xml-in');
const xmlSize = document.getElementById('xml-size');
const xmlRoot = document.getElementById('xml-root');

function updateXmlMeta() {
  const v = xmlIn.value;
  xmlSize.textContent = fmtBytes(v.length);
  const m = v.match(/<([a-zA-Z][a-zA-Z0-9_:.-]*)\s/);
  xmlRoot.textContent = m ? m[1].replace(/^[a-z]+:/,'') : '';
}
xmlIn.addEventListener('input', updateXmlMeta);

document.getElementById('xml-upload').addEventListener('change', e => {
  const f = e.target.files[0]; if (!f) return;
  f.text().then(t => { xmlIn.value = t; updateXmlMeta(); });
});
document.getElementById('btn-xml-clear').addEventListener('click', () => {
  xmlIn.value = ''; updateXmlMeta(); resetEnc();
});

/* ── View toggle for encode output ───────────────────────*/
const togHex = document.getElementById('exi-hex');
const togBin = document.getElementById('exi-bin');
const togB64 = document.getElementById('exi-b64');
const viewTog = document.getElementById('view-tog');

viewTog.querySelectorAll('.seg-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const v = btn.dataset.v;
    togHex.classList.toggle('hidden', v !== 'hex');
    togBin.classList.toggle('hidden', v !== 'bin');
    togB64.classList.toggle('hidden', v !== 'b64');
  });
});

/* ═══════════════════════════════════════════════════════
 * ENCODE
 * ═══════════════════════════════════════════════════════ */
let lastExi = null;   // { base64, bytes }

document.getElementById('btn-encode').addEventListener('click', doEncode);
xmlIn.addEventListener('keydown', e => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') doEncode();
});

async function doEncode() {
  const xml = xmlIn.value.trim();
  if (!xml) { toast('XML을 입력하세요', 'err'); return; }
  showLoader('EXI 인코딩 중…');
  try {
    const alignment = getAlign('enc-align');
    const r = await fetch('/api/encode', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ xml, alignment })
    });
    const d = await r.json();
    if (!r.ok || !d.success) throw new Error(d.error || 'Encode failed');
    lastExi = { base64: d.exi_base64, bytes: d.exi_bytes };
    showEncResult(d);
    toast('인코딩 완료 — ' + d.stats.savedPercent + '% 압축', 'ok');
  } catch(e) { toast(e.message, 'err'); } finally { hideLoader(); }
}

function showEncResult(d) {
  // stats
  const s = d.stats;
  document.getElementById('enc-stats').classList.remove('hidden');
  document.getElementById('enc-empty').style.display = 'none';
  document.getElementById('sv-xml').textContent = fmtBytes(s.xmlBytes);
  document.getElementById('sv-exi').textContent = fmtBytes(s.exiBytes);
  document.getElementById('sv-pct').textContent = s.savedPercent + '%';
  document.getElementById('sv-msg').textContent =
    (s.rootElement || '—') + (s.msgType ? ' ' + s.msgType : '');

  // hex view
  togHex.innerHTML = buildHexView(d.exi_bytes);
  togHex.classList.remove('hidden');
  togBin.classList.add('hidden');
  togB64.classList.add('hidden');

  // other views
  togBin.textContent = d.exi_bytes.map(b => b.toString(2).padStart(8,'0')).join(' ');
  togB64.textContent = wordWrap(d.exi_base64, 76);

  // reset view seg
  viewTog.querySelectorAll('.seg-btn').forEach(b => b.classList.remove('active'));
  viewTog.querySelector('[data-v="hex"]').classList.add('active');

  document.getElementById('btn-copy-exi').disabled = false;
  document.getElementById('btn-dl-exi').disabled   = false;
}

function resetEnc() {
  document.getElementById('enc-stats').classList.add('hidden');
  document.getElementById('enc-empty').style.display = '';
  togHex.classList.add('hidden'); togBin.classList.add('hidden'); togB64.classList.add('hidden');
  document.getElementById('btn-copy-exi').disabled = true;
  document.getElementById('btn-dl-exi').disabled   = true;
  lastExi = null;
}

document.getElementById('btn-copy-exi').addEventListener('click', () => {
  if (!lastExi) return;
  const hex = lastExi.bytes.map(b => b.toString(16).padStart(2,'0').toUpperCase()).join(' ');
  navigator.clipboard.writeText(hex).then(() => toast('Hex 복사됨', 'ok'));
});

document.getElementById('btn-dl-exi').addEventListener('click', () => {
  if (!lastExi) return;
  const blob = b64Blob(lastExi.base64, 'application/octet-stream');
  dlBlob(blob, 'output.exi');
  toast('output.exi 다운로드', 'ok');
});

/* ═══════════════════════════════════════════════════════
 * DECODE
 * ═══════════════════════════════════════════════════════ */
const exiIn   = document.getElementById('exi-in');
const exiSize = document.getElementById('exi-size');
const exiMagic = document.getElementById('exi-magic');
let lastXml = '';

function updateExiMeta() {
  const v = exiIn.value.trim();
  if (!v) { exiSize.textContent = '0 B'; exiMagic.textContent = ''; return; }
  try {
    const bin = atob(v);
    exiSize.textContent = fmtBytes(bin.length);
    const ok = bin.charCodeAt(0)===0x24 && bin.charCodeAt(1)===0x45 &&
               bin.charCodeAt(2)===0x58 && bin.charCodeAt(3)===0x49;
    exiMagic.textContent = ok ? '✓ EXI' : '⚠ ?';
    exiMagic.style.color  = ok ? 'var(--green)' : 'var(--yellow)';
  } catch { exiSize.textContent = '?'; exiMagic.textContent = '⚠ bad base64'; }
}
exiIn.addEventListener('input', updateExiMeta);

document.getElementById('exi-upload').addEventListener('change', e => {
  const f = e.target.files[0]; if (!f) return;
  const rd = new FileReader();
  rd.onload = ev => {
    exiIn.value = btoa(String.fromCharCode(...new Uint8Array(ev.target.result)));
    updateExiMeta(); toast(f.name + ' 로드됨', 'info');
  };
  rd.readAsArrayBuffer(f);
});

document.getElementById('btn-exi-clear').addEventListener('click', () => {
  exiIn.value = ''; updateExiMeta(); resetDec();
});

// Drop zone
const dz = document.getElementById('drop-zone');
dz.addEventListener('dragover', e => { e.preventDefault(); dz.classList.add('over'); });
dz.addEventListener('dragleave', () => dz.classList.remove('over'));
dz.addEventListener('drop', e => {
  e.preventDefault(); dz.classList.remove('over');
  const f = e.dataTransfer.files[0]; if (!f) return;
  const rd = new FileReader();
  rd.onload = ev => {
    exiIn.value = btoa(String.fromCharCode(...new Uint8Array(ev.target.result)));
    updateExiMeta(); toast(f.name + ' 드롭됨', 'info');
  };
  rd.readAsArrayBuffer(f);
});
dz.addEventListener('click', () => document.getElementById('exi-upload').click());

document.getElementById('btn-decode').addEventListener('click', doDecode);

async function doDecode() {
  const b64 = exiIn.value.trim();
  if (!b64) { toast('EXI 데이터를 입력하세요', 'err'); return; }
  showLoader('XML 디코딩 중…');
  try {
    const alignment = getAlign('dec-align');
    const r = await fetch('/api/decode', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ exi_base64: b64, alignment })
    });
    const d = await r.json();
    if (!r.ok || !d.success) throw new Error(d.error || 'Decode failed');
    lastXml = d.xml;
    showDecResult(d);
    toast('디코딩 완료', 'ok');
  } catch(e) { toast(e.message, 'err'); } finally { hideLoader(); }
}

function showDecResult(d) {
  const s = d.stats;
  document.getElementById('dec-stats').classList.remove('hidden');
  document.getElementById('dec-empty').style.display = 'none';
  document.getElementById('dv-exi').textContent = fmtBytes(s.exiBytes);
  document.getElementById('dv-xml').textContent = fmtBytes(s.xmlBytes);
  document.getElementById('dv-exp').textContent = s.expansion + 'x';

  const out = document.getElementById('xml-out');
  out.innerHTML = hlXml(d.xml);
  out.classList.remove('hidden');
  document.getElementById('btn-copy-xml').disabled = false;
  document.getElementById('btn-dl-xml').disabled   = false;
}

function resetDec() {
  document.getElementById('dec-stats').classList.add('hidden');
  document.getElementById('dec-empty').style.display = '';
  document.getElementById('xml-out').classList.add('hidden');
  document.getElementById('btn-copy-xml').disabled = true;
  document.getElementById('btn-dl-xml').disabled   = true;
  lastXml = '';
}

document.getElementById('btn-copy-xml').addEventListener('click', () => {
  navigator.clipboard.writeText(lastXml).then(() => toast('XML 복사됨', 'ok'));
});
document.getElementById('btn-dl-xml').addEventListener('click', () => {
  dlBlob(new Blob([lastXml], { type:'text/xml' }), 'decoded.xml');
  toast('decoded.xml 다운로드', 'ok');
});

/* ═══════════════════════════════════════════════════════
 * XML Syntax Highlight (dependency-free)
 * ═══════════════════════════════════════════════════════ */
function hlXml(xml) {
  // escape HTML first
  let s = xml.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
  // declaration
  s = s.replace(/(&lt;\?xml[^?]*\?&gt;)/g, '<span class="xd">$1</span>');
  // closing tags
  s = s.replace(/(&lt;\/[a-zA-Z][a-zA-Z0-9:._-]*&gt;)/g, '<span class="xt">$1</span>');
  // xmlns attributes
  s = s.replace(/(xmlns(?::[a-zA-Z0-9_-]*)?)=(&quot;[^&]*&quot;)/g,
      '<span class="xn">$1</span>=<span class="xv">$2</span>');
  // normal attributes
  s = s.replace(/([a-zA-Z][a-zA-Z0-9:._-]*)=(&quot;[^&]*&quot;)/g,
      '<span class="xa">$1</span>=<span class="xv">$2</span>');
  // opening tag names
  s = s.replace(/&lt;([a-zA-Z][a-zA-Z0-9:._-]*)/g,
      '&lt;<span class="xt">$1</span>');
  return s;
}

/* ═══════════════════════════════════════════════════════
 * Hex view builder
 * ═══════════════════════════════════════════════════════ */
function buildHexView(bytes) {
  const COLS = 16;
  let html = '';
  for (let i = 0; i < bytes.length; i += COLS) {
    const row = bytes.slice(i, i + COLS);
    const off = i.toString(16).padStart(4,'0').toUpperCase();
    const hex = row.map(b => b.toString(16).padStart(2,'0').toUpperCase()).join(' ');
    const asc = row.map(b => (b>=32&&b<127) ? String.fromCharCode(b) : '.').join('');
    html += `<span class="hrow"><span class="hoff">${off}</span><span class="hbyt">${hex.padEnd(COLS*3-1)}</span>  <span class="hasc">${escHtml(asc)}</span></span>\n`;
  }
  return html;
}

/* ═══════════════════════════════════════════════════════
 * Utilities
 * ═══════════════════════════════════════════════════════ */
function fmtBytes(n) {
  if (n == null) return '—';
  if (n < 1024) return n + ' B';
  return (n/1024).toFixed(1) + ' KB';
}
function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}
function wordWrap(s, w) {
  const re = new RegExp(`.{1,${w}}`, 'g');
  return (s.match(re) || []).join('\n');
}
function b64Blob(b64, mime) {
  const bin = atob(b64);
  const arr = new Uint8Array(bin.length);
  for (let i=0;i<bin.length;i++) arr[i]=bin.charCodeAt(i);
  return new Blob([arr], { type: mime });
}
function dlBlob(blob, name) {
  const url = URL.createObjectURL(blob);
  const a = Object.assign(document.createElement('a'), { href:url, download:name });
  document.body.appendChild(a); a.click();
  setTimeout(() => { URL.revokeObjectURL(url); a.remove(); }, 500);
}

/* ── Toast ────────────────────────────────────────────── */
let _tt;
const toastEl = document.getElementById('toast');
function toast(msg, type='info') {
  clearTimeout(_tt);
  toastEl.textContent = msg;
  toastEl.className   = 'toast ' + type;
  _tt = setTimeout(() => { toastEl.className = 'toast hidden'; }, 3200);
}

/* ── Loader ───────────────────────────────────────────── */
const loaderEl  = document.getElementById('loader');
const loaderMsg = document.getElementById('loader-msg');
function showLoader(msg='처리 중…') { loaderMsg.textContent=msg; loaderEl.classList.remove('hidden'); }
function hideLoader() { loaderEl.classList.add('hidden'); }
