# ╔══════════════════════════════════════════════════════════╗
# ║  ISO 15118-20 EXI Codec Web — Dockerfile                 ║
# ║  Multi-stage: Stage 1 compiles C codec,                  ║
# ║  Stage 2 is minimal Node.js runtime (no gcc).            ║
# ╚══════════════════════════════════════════════════════════╝

# ── Stage 1: Build the C EXI codec library ─────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        gcc make zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY codec/ ./codec/

RUN cd codec && \
    make clean && \
    make V=1 && \
    echo "── Compiled files ──" && \
    ls -lh build/

# ── Stage 2: Minimal Node.js runtime ───────────────────────
FROM node:20-slim

# Only zlib runtime (no build tools)
RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends zlib1g && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Web server source
COPY server/      ./server/
COPY public/      ./public/
COPY package.json ./

# Codec XSD schemas & example messages
COPY codec/xsd/      ./codec/xsd/
COPY codec/examples/ ./codec/examples/

# Pre-compiled binaries from builder (avoid gcc at runtime)
RUN mkdir -p ./codec/build
COPY --from=builder /build/codec/build/libexi15118.so     ./codec/build/libexi15118.so
COPY --from=builder /build/codec/build/exi_encoder_client ./codec/build/exi_encoder_client
COPY --from=builder /build/codec/build/exi_decoder_client ./codec/build/exi_decoder_client

# Create libexi15118.so.1 as a copy (avoid symlink issues)
RUN cp ./codec/build/libexi15118.so ./codec/build/libexi15118.so.1

# Verify everything is in place before starting
RUN node -e " \
  const fs = require('fs'); \
  const files = [ \
    'codec/build/libexi15118.so', \
    'codec/build/exi_encoder_client', \
    'codec/build/exi_decoder_client', \
    'codec/xsd/iso15118-2020/V2GCI_DC.xsd', \
    'public/index.html', \
    'server/app.js' \
  ]; \
  files.forEach(f => { \
    if (!fs.existsSync('/app/' + f)) { \
      console.error('MISSING: ' + f); process.exit(1); \
    } \
    console.log('  ✓  ' + f); \
  }); \
  console.log('All required files present.'); \
"

ENV NODE_ENV=production
ENV PORT=3000

EXPOSE 3000

HEALTHCHECK --interval=30s --timeout=10s --start-period=20s --retries=3 \
  CMD node -e "\
    require('http').get( \
      'http://localhost:' + process.env.PORT + '/api/health', \
      r => process.exit(r.statusCode === 200 ? 0 : 1) \
    ).on('error', () => process.exit(1))"

CMD ["node", "server/app.js"]
