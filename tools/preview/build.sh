#!/bin/sh
# Builds the settings page's live preview: src/c/main.c as WebAssembly (with preview.c standing
# in for the Pebble SDK), and the digit fonts, into src/pkjs/preview-data.js.
# Run it after changing src/c/main.c or the fonts, and commit the result: the Pebble build
# (CloudPebble) only bundles the file. Needs clang with the wasm32 target, wasm-ld and node.
set -e
cd "$(dirname "$0")"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
# The message keys main.c reads, for pebble.h and preview.c.
grep -o 'MESSAGE_KEY_[A-Za-z0-9]*' ../../src/c/main.c | sort -u | sed 's/MESSAGE_KEY_\(.*\)/K(\1)/' > "$out/keys.h"
clang --target=wasm32 -std=c11 -Oz -ffreestanding -nostdlib -fno-builtin -Wall -Wno-unused-function \
  -Wno-unused-parameter -Wno-return-type -I "$out" -I . \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined -Wl,--strip-all \
  -o "$out/preview.wasm" preview.c
node -e '
  const fs = require("fs"), b64 = (p) => fs.readFileSync(p).toString("base64");
  const fonts = ["iceberg", "bezel"].map((n) => b64("../../resources/fonts/" + n + ".bin"));
  fs.writeFileSync("../../src/pkjs/preview-data.js",
    "// Made by tools/preview/build.sh: do not edit. The watch face code as WebAssembly, and the\n" +
    "// digit fonts (in RESOURCE_ID order), for the live preview on the settings page.\n" +
    "module.exports = " + JSON.stringify({ wasm: b64(process.argv[1]), fonts: fonts }, null, 1) + ";\n");
' "$out/preview.wasm"
ls -l "$out/preview.wasm" ../../src/pkjs/preview-data.js
