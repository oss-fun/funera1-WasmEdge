#!/bin/bash
realpath() {
  case "$1" in /*) ;; *) printf '%s/' "$PWD";; esac; echo "$1"
}
wasmedge=$(realpath ../../build/tools/wasmedge/wasmedge)


test-checkpoint() {
    dir=$1
    app=$2
    limit=$3

    pushd $dir

    # 1000回dispatchしたところでcheckpointする
    $wasmedge \
        --image-dir actual-images \
        --dispatch-limit $limit \
        $app

    # 実行が失敗すればfailed
    if [ $? != 0 ]; then
        echo "execution failure"
        rm ./actual-images/*
        exit 1
    fi

    # 出力されたimagesがexpect-imagesと同じ確認する
    diff=$(diff ./expect-images ./actual-images)
    status=$?
    if [ $status -eq 0 ]; then
        echo "Success" 
        rm ./actual-images/*
    else
        echo "Failed"
        diff ./expect-images ./actual-images

        rm ./actual-images/*
        exit 1
    fi

    popd
}


test-checkpoint "apps/binary-trees" "binary-trees.wasm" 1000
test-checkpoint "apps/n-body"       "n-body.wasm"       1000
test-checkpoint "apps/sqlite-bench" "sqlite-bench.wasm" 100000

echo OK