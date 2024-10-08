apps/{app}の下に、各アプリケーションの.wasmと型スタックテーブルとdisapatch countが1000のときのimageが入っている

### checkpoint
- checkpointのテスト
    - あるdumpファイルを用意しておく
    - そのdumpファイルと同じ位置で止めて、同じファイルが生成されるかを確認する

### restore
- restoreのテスト
    - 適当な位置でcheckpointする
    - dumpされたファイルをrestoreして、すぐにdumpさせる。
    - 2つのdumpファイルが同じことを確認する