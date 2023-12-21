#!/bin/bash

BUILD_DIR="$(dirname "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)")"/build

mkdir -p "$BUILD_DIR"

if ! command -v pprof &>/dev/null; then
    TMP_DIR=/tmp/wasmedge
    
    mkdir -p "$TMP_DIR"

    pushd "$TMP_DIR" || exit

    git clone https://github.com/gperftools/gperftools
    pushd gperftools || exit
    
    git fetch --tags
    latestTag=$(git describe --tags "$(git rev-list --tags --max-count=1)")
    git checkout "$latestTag"
    echo "Installing gperftools $latestTag"

    ./autogen.sh
    ./configure

    make -j"$(nproc)"
    make install 

    popd || exit
    popd || exit
fi

pushd "$BUILD_DIR" || exit

wget -nc https://github.com/WasmEdge/www/raw/main/assets/public/20211011/wasmedge_quickjs.wasm
wget -nc https://raw.githubusercontent.com/WasmEdge/www/main/assets/public/20211011/index.js

popd || exit