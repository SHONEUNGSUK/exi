/**
 * server/app.js  –  ISO 15118-20 EXI Codec Web Server
 * Pure Node.js, zero npm dependencies.
 *
 * Deployment modes:
 *   Docker  → binaries pre-built in Stage 1, no gcc needed at runtime
 *   Local   → auto-builds codec if binaries are missing (requires gcc)
 */
'use strict';

const http   = require('http');
const fs     = require('fs');
const path   = require('path');
const os     = require('os');
const crypto = require('crypto');
const { spawnSync } = require('child_process');

/* ── Paths ─────────────────────────────────────────────── */
const ROOT      = path.resolve(__dirname, '..');
const CODEC_DIR = path.join(ROOT,  'codec');
const BUILD_DIR = path.join(CODEC_DIR, 'build');
const XSD_DIR   = path.join(CODEC_DIR, 'xsd', 'iso15118-2020');
const EXP_DIR   = path.join(CODEC_DIR, 'examples');
const ENC_BIN   = path.join(BUILD_DIR, 'exi_encoder_client');
const DEC_BIN   = path.join(BUILD_DIR, 'exi_decoder_client');
const LIB_SO    = path.join(BUILD_DIR, 'libexi15118.so');
const PUB_DIR   = path.join(ROOT, 'public');
const PORT      = process.env.PORT || 3000;

/* ── MIME types ────────────────────────────────────────── */
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.xml':  'text/xml; charset=utf-8',
  '.ico':  'image/x-icon',
  '.png':  'image/png',
  '.svg':  'image/svg+xml',
};

/* ── Codec readiness check ─────────────────────────────── */
function codecReady() {
  return fs.existsSync(ENC_BIN) &&
         fs.existsSync(DEC_BIN) &&
         fs.existsSync(LIB_SO);
}

/* ── Auto-build (local dev only, skipped in Docker) ────── */
if (!codecReady()) {
  // In Docker the binaries are COPY-ed in from the builder stage.
  // If they're missing here we're in a local dev environment → try to build.
  console.log('⚙  Codec binaries not found — attempting local build…');
  const buildSh = path.join(ROOT, 'scripts', 'build_codec.sh');
  if (!fs.existsSync(buildSh)) {
    console.error('✗  build_codec.sh not found. Cannot continue.');
    process.exit(1);
  }
  const r = spawnSync('bash', [buildSh], { stdio: 'inherit', timeout: 120_000 });
  if (r.status !== 0) {
    console.error('✗  Codec build failed (exit ' + r.status + ').');
    console.error('   Install gcc, make, zlib1g-dev and retry.');
    process.exit(1);
  }
}

if (!codecReady()) {
  console.error('✗  Codec binaries still missing after build attempt.');
  process.exit(1);
}

console.log('✓  Codec binaries ready');
console.log('   ENC:', ENC_BIN);
console.log('   DEC:', DEC_BIN);
console.log('   LIB:', LIB_SO);

/* ── Temp file helpers ─────────────────────────────────── */
function tmpFile(ext) {
  return path.join(os.tmpdir(),
    'exi_' + crypto.randomBytes(8).toString('hex') + ext);
}
function safeRm(f) {
  try { if (f && fs.existsSync(f)) fs.unlinkSync(f); } catch(_) {}
}

/* ── Strip ANSI colour codes from CLI output ───────────── */
const RE_ANSI = /\x1b\[[0-9;]*m/g;
function stripAnsi(s) { return s.replace(RE_ANSI, ''); }

/* ── Run encoder binary ────────────────────────────────── */
function runEncode(xmlText, alignment) {
  const xf = tmpFile('.xml'), ef = tmpFile('.exi');
  try {
    fs.writeFileSync(xf, xmlText, 'utf8');
    const env = { ...process.env, LD_LIBRARY_PATH: BUILD_DIR };
    const r   = spawnSync(
      ENC_BIN, ['-s', XSD_DIR, '-a', alignment, xf, ef],
      { env, timeout: 15_000 }
    );
    if (r.status !== 0) {
      const msg = r.stderr ? stripAnsi(r.stderr.toString()) : '';
      throw new Error((msg.trim() || 'Encode failed') + ' (exit ' + r.status + ')');
    }
    const exiBuf = fs.readFileSync(ef);
    const out    = stripAnsi((r.stdout || '').toString());
    const rootM  = out.match(/Root\s*:\s*(\S+)/);
    const typeM  = out.match(/type=(0x[0-9A-Fa-f]+)/);
    const saved  = xmlText.length > 0
      ? Math.round((1 - exiBuf.length / xmlText.length) * 100) : 0;
    return {
      exiBuf,
      stats: {
        xmlBytes:     xmlText.length,
        exiBytes:     exiBuf.length,
        savedPercent: saved,
        rootElement:  rootM ? rootM[1] : null,
        msgType:      typeM ? typeM[1] : null,
      }
    };
  } finally { safeRm(xf); safeRm(ef); }
}

/* ── Run decoder binary ────────────────────────────────── */
function runDecode(exiBuf, alignment) {
  const ef = tmpFile('.exi'), xf = tmpFile('.xml');
  try {
    fs.writeFileSync(ef, exiBuf);
    const env = { ...process.env, LD_LIBRARY_PATH: BUILD_DIR };
    const r   = spawnSync(
      DEC_BIN, ['-s', XSD_DIR, '-a', alignment, ef, xf],
      { env, timeout: 15_000 }
    );
    if (r.status !== 0) {
      const msg = r.stderr ? stripAnsi(r.stderr.toString()) : '';
      throw new Error((msg.trim() || 'Decode failed') + ' (exit ' + r.status + ')');
    }
    const xmlText = fs.readFileSync(xf, 'utf8');
    const exp = exiBuf.length > 0
      ? (xmlText.length / exiBuf.length).toFixed(1) : '?';
    return {
      xml:   xmlText,
      stats: { exiBytes: exiBuf.length, xmlBytes: xmlText.length, expansion: exp }
    };
  } finally { safeRm(ef); safeRm(xf); }
}

/* ── Body reader ───────────────────────────────────────── */
function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', c => chunks.push(c));
    req.on('end',  () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

/* ── Multipart parser ──────────────────────────────────── */
function parseMultipart(buf, boundary) {
  const sep   = Buffer.from('\r\n--' + boundary);
  const parts = [];
  let pos = buf.indexOf('--' + boundary);
  while (pos !== -1) {
    const hdrEnd = buf.indexOf('\r\n\r\n', pos);
    if (hdrEnd === -1) break;
    const hdrStr = buf.slice(pos, hdrEnd).toString();
    const end    = buf.indexOf(sep, hdrEnd + 4);
    const body   = end === -1 ? buf.slice(hdrEnd + 4)
                              : buf.slice(hdrEnd + 4, end);
    const nameM  = hdrStr.match(/name="([^"]+)"/);
    const fnameM = hdrStr.match(/filename="([^"]+)"/);
    parts.push({
      name:     nameM  ? nameM[1]  : '',
      filename: fnameM ? fnameM[1] : null,
      data:     body,
      text:     body.toString('utf8')
    });
    pos = end === -1 ? -1 : end + sep.length;
    if (pos !== -1 && buf[pos] === 0x2d && buf[pos + 1] === 0x2d) break;
    if (pos !== -1) pos += 2;
  }
  return parts;
}

/* ── JSON response helper ──────────────────────────────── */
function sendJSON(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, {
    'Content-Type':                'application/json; charset=utf-8',
    'Content-Length':              Buffer.byteLength(body),
    'Access-Control-Allow-Origin': '*',
    'X-Content-Type-Options':      'nosniff',
  });
  res.end(body);
}

/* ── Static file helper ────────────────────────────────── */
function serveStatic(res, filePath) {
  const ext  = path.extname(filePath).toLowerCase();
  const mime = MIME[ext] || 'application/octet-stream';
  try {
    const data = fs.readFileSync(filePath);
    res.writeHead(200, {
      'Content-Type':   mime,
      'Content-Length': data.length,
      'Cache-Control':  'public, max-age=3600',
    });
    res.end(data);
  } catch {
    res.writeHead(404); res.end('Not found');
  }
}

/* ════════════════════════════════════════════════════════
 * HTTP Server
 * ════════════════════════════════════════════════════════ */
const server = http.createServer(async (req, res) => {
  const url    = new URL(req.url, 'http://localhost');
  const method = req.method.toUpperCase();
  const pname  = url.pathname;

  // CORS preflight
  if (method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin':  '*',
      'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type',
    });
    return res.end();
  }

  /* GET /api/health ──────────────────────────────────── */
  if (method === 'GET' && pname === '/api/health') {
    return sendJSON(res, 200, {
      status:    'ok',
      codec:     codecReady() ? 'ready' : 'missing',
      version:   'libexi15118 1.0.0',
      standard:  'ISO 15118-20:2022 / W3C EXI 1.0',
      timestamp: new Date().toISOString(),
    });
  }

  /* GET /api/examples ────────────────────────────────── */
  if (method === 'GET' && pname === '/api/examples') {
    try {
      const files = fs.readdirSync(EXP_DIR)
        .filter(f => f.endsWith('.xml'))
        .sort()
        .map(f => {
          const content = fs.readFileSync(path.join(EXP_DIR, f), 'utf8');
          const m = content.match(/<([a-zA-Z][a-zA-Z0-9_:.-]*)\s/);
          const root = m ? m[1].replace(/^[a-z]+:/, '') : f.replace('.xml', '');
          return { filename: f, displayName: root, size: content.length };
        });
      return sendJSON(res, 200, { examples: files });
    } catch (e) {
      return sendJSON(res, 500, { error: e.message });
    }
  }

  /* GET /api/example/:name ───────────────────────────── */
  if (method === 'GET' && pname.startsWith('/api/example/')) {
    const name = path.basename(pname);
    if (!name.endsWith('.xml'))
      return sendJSON(res, 400, { error: 'File must end with .xml' });
    const fp = path.join(EXP_DIR, name);
    if (!fs.existsSync(fp))
      return sendJSON(res, 404, { error: 'Example not found: ' + name });
    const data = fs.readFileSync(fp);
    res.writeHead(200, {
      'Content-Type':                'text/xml; charset=utf-8',
      'Content-Length':              data.length,
      'Access-Control-Allow-Origin': '*',
    });
    return res.end(data);
  }

  /* POST /api/encode ─────────────────────────────────── */
  if (method === 'POST' && pname === '/api/encode') {
    try {
      const buf = await readBody(req);
      const ct  = (req.headers['content-type'] || '').split(';')[0].trim();
      let xmlText, alignment = 'bit';

      if (ct === 'application/json') {
        const body = JSON.parse(buf.toString('utf8'));
        xmlText   = body.xml       || '';
        alignment = body.alignment || 'bit';
      } else if (ct === 'multipart/form-data') {
        const bnd   = (req.headers['content-type'] || '').match(/boundary=([^\s;]+)/)?.[1] || '';
        const parts = parseMultipart(buf, bnd);
        const xmlP  = parts.find(p => p.name === 'xml_file' || p.name === 'xml');
        const aliP  = parts.find(p => p.name === 'alignment');
        xmlText   = xmlP ? xmlP.text      : '';
        alignment = aliP ? aliP.text.trim() : 'bit';
      } else {
        xmlText = buf.toString('utf8');
      }

      if (!xmlText.trim())
        return sendJSON(res, 400, { error: 'No XML provided' });
      if (!['bit', 'byte', 'compress'].includes(alignment))
        alignment = 'bit';

      const { exiBuf, stats } = runEncode(xmlText, alignment);
      const hexStr = Array.from(exiBuf)
        .map(b => b.toString(16).padStart(2, '0').toUpperCase())
        .join(' ');

      return sendJSON(res, 200, {
        success:    true,
        exi_base64: exiBuf.toString('base64'),
        exi_hex:    hexStr,
        exi_bytes:  Array.from(exiBuf),
        stats,
      });
    } catch (e) {
      console.error('[/api/encode]', e.message);
      return sendJSON(res, 500, { error: e.message });
    }
  }

  /* POST /api/decode ─────────────────────────────────── */
  if (method === 'POST' && pname === '/api/decode') {
    try {
      const buf = await readBody(req);
      const ct  = (req.headers['content-type'] || '').split(';')[0].trim();
      let exiBuf, alignment = 'bit';

      if (ct === 'application/json') {
        const body = JSON.parse(buf.toString('utf8'));
        const b64  = body.exi_base64 || '';
        if (!b64) return sendJSON(res, 400, { error: 'No EXI data provided' });
        exiBuf    = Buffer.from(b64, 'base64');
        alignment = body.alignment || 'bit';
      } else if (ct === 'multipart/form-data') {
        const bnd   = (req.headers['content-type'] || '').match(/boundary=([^\s;]+)/)?.[1] || '';
        const parts = parseMultipart(buf, bnd);
        const exiP  = parts.find(p => p.name === 'exi_file');
        const aliP  = parts.find(p => p.name === 'alignment');
        exiBuf    = exiP ? exiP.data        : Buffer.alloc(0);
        alignment = aliP ? aliP.text.trim() : 'bit';
      } else {
        exiBuf = buf;
      }

      if (!exiBuf || exiBuf.length === 0)
        return sendJSON(res, 400, { error: 'Empty EXI input' });
      if (!['bit', 'byte', 'compress'].includes(alignment))
        alignment = 'bit';

      const { xml, stats } = runDecode(exiBuf, alignment);
      return sendJSON(res, 200, { success: true, xml, stats });

    } catch (e) {
      console.error('[/api/decode]', e.message);
      return sendJSON(res, 500, { error: e.message });
    }
  }

  /* Static files / SPA fallback ──────────────────────── */
  if (method === 'GET') {
    const fp = path.normalize(
      path.join(PUB_DIR, pname === '/' ? 'index.html' : pname)
    );
    // Security: prevent path traversal
    if (!fp.startsWith(PUB_DIR)) {
      res.writeHead(403); return res.end('Forbidden');
    }
    if (fs.existsSync(fp) && fs.statSync(fp).isFile()) {
      return serveStatic(res, fp);
    }
    // SPA fallback
    return serveStatic(res, path.join(PUB_DIR, 'index.html'));
  }

  res.writeHead(405); res.end('Method Not Allowed');
});

server.listen(PORT, '0.0.0.0', () => {
  console.log('');
  console.log('  ╔══════════════════════════════════════════════╗');
  console.log('  ║   ISO 15118-20 EXI Codec Web                 ║');
  console.log('  ║   http://0.0.0.0:' + PORT + '                       ║');
  console.log('  ║   Codec: ' + (codecReady() ? '✓ ready' : '✗ MISSING') + '                          ║');
  console.log('  ╚══════════════════════════════════════════════╝');
  console.log('');
});

server.on('error', e => { console.error('Server error:', e); process.exit(1); });
