#!/usr/bin/env bash
set -euo pipefail

script_dir="$(realpath "$(dirname "$0")")"
hypjson_js="$1"
hypjson_wasm="$2"
hypjson_wasm_size=$(wc -c "${hypjson_wasm}" | cut -d ' ' -f 1)
output="$3"

(( $# == 3 )) || { >&2 echo "Usage: $0 hypjson.js hypjson.wasm packed_hypjson.js"; exit 1; }

# If this changes in an emscripten update, it's probably nothing to worry about,
# but we should double-check when it happens and adjust the tail command below.
[[ $(head -c 5 "${hypjson_js}") == "null;" ]] || { >&2 echo 'Expected hypjson.js to start with "null;"'; exit 1; }

echo "Packing $hypjson_js and $hypjson_wasm to $output."
(
    echo -n 'var Module = Module || {}; Module["wasmBinary"] = '
    echo -n '(function(source, uncompressedSize) {'
    # Note that base64DecToArr assumes no trailing equals signs.
    cpp "${script_dir}/base64DecToArr.js" | grep -v "^#.*"
    # Note that mini-lz4.js assumes no file header and no frame crc checksums.
    cpp "${script_dir}/mini-lz4.js" | grep -v "^#.*"
    echo 'return uncompress(base64DecToArr(source), uncompressedSize);})('
    echo -n '"'
    # We fix lz4 format settings, remove the 8 bytes file header and remove the trailing equals signs of the base64 encoding.
    lz4c --no-frame-crc --best --favor-decSpeed "${hypjson_wasm}" - | tail -c +8 | base64 -w 0 | sed 's/[^A-Za-z0-9\+\/]//g'
    echo '",'
    echo -n "${hypjson_wasm_size});"
    # Remove "null;" from the js wrapper.
    tail -c +6 "${hypjson_js}"
) > "$output"

echo "Testing $output."
echo "process.stdout.write(require('$(realpath "${output}")').wasmBinary)" | node | cmp "${hypjson_wasm}" && echo "Binaries match."
# Allow the wasm binary to be garbage collected after compilation.
echo 'Module["wasmBinary"] = undefined;' >> "${output}"
