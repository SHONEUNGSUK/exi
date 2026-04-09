#!/usr/bin/env bash
# scripts/build_codec.sh — compile libexi15118.so from source
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CODEC_DIR="$SCRIPT_DIR/../codec"
BUILD_DIR="$CODEC_DIR/build"

echo "=== Building ISO 15118-20 EXI Codec ==="
echo "    Source  : $CODEC_DIR"
echo "    Output  : $BUILD_DIR"
echo ""

# Check for gcc
if ! command -v gcc >/dev/null 2>&1; then
  echo "ERROR: gcc not found. Install build-essential."
  exit 1
fi

cd "$CODEC_DIR"
make clean
make

echo ""
echo "=== Build complete ==="
ls -lh "$BUILD_DIR/libexi15118.so"
ls -lh "$BUILD_DIR/exi_encoder_client"
ls -lh "$BUILD_DIR/exi_decoder_client"
echo ""
echo "Running test suite..."
make test
echo ""
echo "Codec ready for web server."
