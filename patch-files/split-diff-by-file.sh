#!/bin/bash
PATCH=$1
OUTDIR=${2:-split_patches}

mkdir -p "$OUTDIR"

csplit -sz "$PATCH" '/^diff --git /' '{*}' -f "$OUTDIR/part-"

# 各ファイルの先頭行を見て適切なファイル名で保存
for f in "$OUTDIR"/part-*; do
    # ファイル名を抽出（最初の diff 行から）
    fname=$(grep -m1 '^diff --git' "$f" | awk '{print $3}' | sed 's|a/||' | tr '/' '_')
    if [[ -n "$fname" ]]; then
        mv "$f" "$OUTDIR/$fname.patch"
    else
        rm "$f"  # 不要な空ファイルなど
    fi
done
