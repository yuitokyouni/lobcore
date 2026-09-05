# LogRecord の生バイト再現性 — 2026-09-06 調査結果

**原因は A: C++ LogRecord のパディングが決定的に初期化されていなかったこと。**
観測エージェントを追加しない2実行でも同じ問題が再現し、パディングだけのゼロ初期化で解消した。
今回の比較では、観測頻度による市場状態・注文ログフィールドの変化（B）は認められなかった。
調査時点では修正をローカルに留め、Phase 3 の複数 seed 平均を保留した。
その後、保存・読込の往復と全比較テストの追加確認を条件に、ユーザーから commit・push の指示を受けた。

## 保存してから比較した再現実験

YH012 の seed=13、end_time=30,000、同じ背景パラメータを使用。
観測なし2回、ID=100 の読み取り専用観測を毎整数時刻に行う実行、
時刻1・25,000にだけ行う実行を、それぞれ独立した Python プロセスで実行した。
観測は注文・取消を出さず、乱数を消費しない。

`Kernel.log_bytes()` の返却値を比較・変換の前に `native.raw` へ保存した。
その後 `write_log_file` の出力も保存し、ファイルのログ本体が native.raw と全バイト一致することを確認した。
前回保存できなかった2回目のバッファも、今回は保存している。

| 段階 | 比較 | フィールド・状態ハッシュ | 生バイト |
|---|---|---|---|
| 修正前 | 観測なし × 2 | 一致 | 不一致 |
| 修正前 | 毎時刻観測 vs 2回観測 | 一致 | 不一致 |
| memset のみ | 毎時刻観測 vs 2回観測 | 修正前とも一致 | 完全一致 |
| 最終修正 | 観測なし × 2、毎時刻、2回の全4実行 | 修正前とも一致 | 全4実行で完全一致 |

各実行は **70,715レコード、6,788,640バイト**。
修正前の各比較は **11,774バイト**が異なり、差異はすべてレコード内オフセット11に限定された。
最初の差異は前回と同じ **レコード272、受付時刻68、Fill、オフセット11**。
観測なし2回では該当バイトが `0x35` / `0x03`、観測頻度比較では `0x43` / `0x5b` だった。

全10実行（修正前4、memsetのみ2、最終4）で共通:

- state_hash: `1162569896189705366`
- フィールドだけを詰めて計算した SHA-256:
  `f6aeda60924e77a00f8e43a0e10019069b2d212512c00811961e73665312f95c`

memsetのみ・最終修正の計6実行ではパディング非ゼロ件数が **0**、ログ本体の SHA-256 は共通:
`2f8af89ca4f019bd70e152462fec8585de6f1082dab7e5fd6b193380886ece80`。

追加の切り分けとして、修正前バッファの**診断用コピーだけ**のオフセット11〜15をゼロにすると、
修正後のログ本体と完全一致した。元バッファは書き換えておらず、
この正規化を「修正前の生バイト一致」の証明や合否判定に使ってはいない。

## シリアライズ経路と修正

`LogRecord` は96バイト・8バイト整列。kind/side/reason の後、
オフセット11〜15に5バイトのパディングがあり、末尾の追加パディングはない。

[src/log.cpp](../src/log.cpp) の生成箇所はもともとすべて `LogRecord ...{};` だった。
しかし、この集成体のフィールド初期化だけではオブジェクト表現全体のゼロを保証できず、
今回の Release バイナリではパディングに残存値が入った。
[bindings/module.cpp](../bindings/module.cpp) は `vector<LogRecord>` のストレージ全体を
`reinterpret_cast<const char*>` で Python bytes にコピーしていたため、それも外へ出ていた。
`write_log_file` に渡る前の native.raw にすでに差があり、Python の保存処理が今回の発生源ではない。

ローカル修正:

1. **全6生成箇所**で、フィールド設定前に `std::memset(&record, 0, sizeof(record))`。
   これだけの段階を別ビルドで保存・実行し、問題の解消を確認した。
2. C++→Python の書き出しも、ゼロで満たした96バイト領域へ14フィールドを個別にコピーする方式に変更。
   構造体コピーがパディングを保持することに依存しない。サイズ・整列・既存フィールド位置は維持。
3. Python の取得・読込・抽出・非連続配列の保存で、構造化フィールドのコピーにパディング保持を任せず、
   レコードの全バイトを保持する経路に変更。取得・読込結果は従来どおり書換可能。
   保存済みの古い非ゼロパディングを勝手にゼロへ直す処理は入れていない。
4. `logs_byte_equal` を dtype・shape・`tobytes()` の比較に変更。
   以前は名前に反して `np.array_equal` によるフィールド比較だった。
   YH012 の検証コード・テストも、新しい比較関数でパディング差の位置を報告できるよう整合させた。

マッチング、乱数、観測エージェントの振る舞い、ImpactAgent、t0、市場パラメータは変更していない。

## state_hash と過去の検証の扱い

[OrderBook::state_hash](../src/book.cpp) は価格・数量・ID・連番・拒否カウンタ等を個別に混ぜ、
`LogRecord` のオブジェクト表現を読まない。[log_hash](../src/log.cpp) もフィールドごとの計算。
ログのパディングだけを意図的に `0xA5` に変更したテストで、log_hash と replay 後の state_hash が
不変であることを確認した。実際の seed 13 の修正前後でも state_hash は不変だった。

既存 C++ `test_repro` は `operator==`・log_hash・state_hash・RNG列で検証しており、
パディングの偶然の一致に依存したテストではなかった。
一方で生バイト比較そのものは欠けていたので、同一 seed 再実行、F/B の介入前、
抑制注文を除いたログの比較に `memcmp` を追加した。

**過去に実際の生バイト比較が真だった場合、フィールド一致の証拠まで失われるわけではない。**
今回の欠陥は、フィールド・状態が一致していても生バイト比較を失敗させ得る。
フィールド比較だけを「生バイト一致」と呼んでいたヘルパーの検証不足は、それとは分けて是正した。

既存の seed=42・Q=200・end_time=50,000 の F/B ペアも回帰確認した。
両バッファを先に保存してから厳密比較し、以下を確認:

- 介入前 **20,570レコード、1,974,720バイトが一致**。
- 介入前 SHA-256 は過去と同じ
  `194108d4de64c6928660a0d04ca2a5b845c473a1210d9e9c9762f7508c711cc2`。
- F/B 各ログの**本体全体**が過去の保存ログと生バイト一致。新しい診断用メタデータのヘッダは比較対象外。
- F: 43,993レコード、9,032約定、state_hash=`1925857981061294172`。
- B: 43,858レコード、8,917約定、state_hash=`15247905874259579476`。

これは既存ケースの回帰確認であり、保留した39 seed 平均を再開したものではない。

## 検証と再現方法

- 追加 Python 回帰テストは修正前に3件失敗、6件通過。修正後は全件通過。
- 初回修正後 Python: **60件通過**（lobcore と YH012 の既存・追加テストすべて）。
- 最終 C++ Debug: **79件通過、ASan/UBSan 有効、ビルド警告0件**。
- 新規 C++ テストで各ログ種類のゼロパディング、パディング変更時のハッシュ非干渉を検証。
- `git diff --check` と対象 Python ファイルの ruff は通過。

### commit 前の追加確認

`test_log_file_roundtrip_preserves_every_record_byte` は、レコードごとに異なる非ゼロ値を
5バイトのパディングへ入れ、通常・間引き・逆順・空配列で `write_log_file` → `read_log_file` を実行する。
保存ファイルのログ本体と読み戻した配列の両方を、元の **96 × レコード数バイト** と直接比較する。
`logs_byte_equal` にも合格すること、元配列が変更されないこと、メタデータが往復することを確認する。
実際の Experiment が生成したゼロパディングのログでも往復を直接バイト比較する。

両リポジトリの `logs_byte_equal` 呼び出しを全検索した。
介入前の比較、注文除外後の比較、ファイル再読込の比較は全バイト一致を要求し、
パディングだけを変更したケースはフィールド一致を維持したまま同関数が False を返すことを要求する。
過去のテストをフィールド比較へ戻して通す変更はしていない。

追加確認後は **Python 62件通過、C++ Debug ASan/UBSan 79件通過**。
実行ログは `precommit_python_tests.log` と `precommit_cpp_tests.log` に保存した。

この Mac の AppleClang 15 付属 ASan は、テスト開始前に
`sanitizer_malloc_mac.inc:189` で異常終了した。
同じ症状に対して新しいLLVMを使う対処が[一次資料](https://github.com/contentauth/c2pa-cpp/blob/main/README.md#sanitizer-test-builds-fail-on-macos)にも記載されている。
Homebrew LLVM 23.1.0 を導入して Debug 検証を完了した。標準コンパイラの PATH は変更していない。
Catch2 3.8.1 の `__COUNTER__` 拡張警告は、Catch2 が提供する `CATCH_CONFIG_NO_COUNTER=ON` で回避した。
テスト・sanitizer の無効化やリポジトリ依存の追加・更新は行っていない。

```bash
# lobcore ディレクトリで実行。Catch2 ソースは初回 CMake 構成で取得済み。
cmake -S . -B build/llvm-debug \
  -DCMAKE_BUILD_TYPE=Debug -DLOBCORE_SANITIZE=ON \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=/Users/hasegawayuito/dev/lobcore/build/_deps/catch2-src \
  -DCATCH_CONFIG_NO_COUNTER=ON
cmake --build build/llvm-debug -j 4
ctest --test-dir build/llvm-debug --output-on-failure -j 4

# financial-abm-lab-main ディレクトリで実行。
.venv/bin/python -m pytest ../lobcore/python/tests experiments/YH012/tests -q
```

元バッファ・修正前/ゼロ初期化のみ/最終のソース・バイナリ・比較結果・テストログは、ローカルの
`financial-abm-lab-main/experiments/YH012/artifacts/repro_padding_20260906/` に保存した。
入口は `comparisons.json`、`validation_manifest.json`、`before/comparison.json`。

調査で使用した lobcore の基点は `4fb83dcb2c0d17cc5239816606d2ec4cc0e3fabf`、FAL は
`cbbaea0dde2479b52e4805bfcf3eb70949531eb0`。
未 commit ビルドを基点コミットそのものと混同しないよう、各実行に拡張バイナリの SHA-256 を保存し、
最終検証にはソース・差分の SHA-256 も添えた。通常の40桁コミット値だけでは今回のローカルビルドを識別できない。

Phase 3 再開時の基準は YH012 spec.md に記録した:
**固定時刻 t0 に best_ask がない seed を除外**する。現在の候補0〜39では1件該当し適格39件。
番号・Δの符号による選別はせず、「t0に売り板がある背景市場」で条件付けた測定とする。
