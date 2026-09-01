# Stage 3: ベンチマークとプロファイリング

Stage 1–2 の `OrderBook` を固定し、参照実装との差分テストと Google Benchmark
による性能計測、Callgrind / Cachegrind によるプロファイリングを行った。
到達点は「何が速くないか」を数値と手順で残し、次段の判断材料にすること。

---

## 1. ビルドと実行

### テスト（Debug, ASan/UBSan）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### ベンチマーク（Release のみ意味がある）

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DLOBCORE_BUILD_BENCH=ON
cmake --build build-rel
./build-rel/bench/lobcore_bench
```

`bench/CMakeLists.txt` の `lobcore_bench` が Stage 3 の公式計測入口である。
壁時計の前後比較は **必ずこのバイナリ** で行う。

---

## 2. ベンチ 5 条件の定義と現状

いずれも `bench/bench_book.cpp` に定義。深い板は共通:

- `kDeepLevels = 1000`, `kOrdersPerLevel = 10`
- bid: 1000, 999, … / ask: 1001, 1002, …（best bid=1000, best ask=1001）

板の再構築は `benchmark::State::PauseTiming()` 内のみ。
**計測区間は注文処理だけ**。

| ベンチ | 内容 | 計測区間の注文数/回 |
|--------|------|---------------------|
| `BM_RestOnEmpty` | 空板へ非交差 Buy @ 100, qty=1 | 1,000 |
| `BM_RestOnDeepBook` | 深い板へ非交差 Buy @ 1000, qty=1 | 1,000 |
| `BM_CrossSingleLevel` | best ask を厚くしたうえで Buy @ 1001, qty=5（1 レベル完結） | 200 |
| `BM_CrossSweep` | Buy @ 2000, qty=50（複数レベルをスイープ、全量約定） | 20 |
| `BM_CancelDeepBook` | 深い板の注文 ID を shuffle して cancel | 1,000 |
| `BM_FlashCrash` | スイープで best を動かし、広い価格帯へ非交差指値を散らす（S3 crash 相当） | 500 |

`BM_FlashCrash` の 1 イテレーション（計測区間）:

- 3 回に 1 回: Buy @ 2000, qty=30（複数 ask レベルをスイープ）
- それ以外: 交互に Buy @ `500 + (i % 800)` / Sell @ `1500 + (i % 800)`, qty=1（空レベルが増える広帯域の resting）

上記 6 条件それぞれに **`Logged` 接尾辞** の対（`LoggedBook` 経由、ログは `std::vector<LogRecord>` 追記）がある。
計測区間の注文数・板形状は非 Logged 版と同一。

### 現状の数字（Release, repetitions=3 mean, min_time=0.01s, 2026-09 計測 @ struct-exp 着手前）

環境依存のため絶対値より **同一マシン・同一ビルドでの比較** を優先する。
計測は `./bench/run_bench_suite.sh ./build-rel/bench/lobcore_bench 0.01s`。

| ベンチ | mean (ns/iteration) | 備考 |
|--------|---------------------|------|
| `BM_RestOnEmpty` | 12,647,899 | |
| `BM_RestOnDeepBook` | 1,833,693,623 | 板再構築込みイテレーション |
| `BM_CrossSingleLevel` | 359,915,340 | avg fills/order = 5 |
| `BM_CrossSweep` | 37,419,148 | avg fills/order = 50 |
| `BM_CancelDeepBook` | 928,216 | |
| `BM_FlashCrash` | 695,908,439 | |
| `BM_RestOnEmptyLogged` | 13,005,323 | |
| `BM_RestOnDeepBookLogged` | 1,860,883,889 | |
| `BM_CrossSingleLevelLogged` | 371,992,446 | |
| `BM_CrossSweepLogged` | 36,918,915 | **§6 採否の主指標** |
| `BM_CancelDeepBookLogged` | 983,527 | |
| `BM_FlashCrashLogged` | 734,346,038 | |

（旧 Stage 3 単体 5 条件の数値は min_time・板規模が異なるため直接比較しない。）

付帯成果物:

- `ReferenceBook` + `tests/test_diff.cpp`（乱数ストリームで `OrderBook` と照合）
- `tests/test_move.cpp`（コピー・ムーブ・`replay` 返却値のライフタイム）
- `bench/cross_sweep_profile.cpp` + `bench/parse_callgrind.py`（プロファイル専用、後述）

---

## 3. Callgrind / Cachegrind の手順

### 3.1 使うハーネス

`cross_sweep_profile` は **Valgrind 開発ヘッダ**（Linux: `valgrind-dev`）が必要な
オプションターゲット。CI などヘッダが無い環境ではビルドされない。

| 用途 | ツール | ハーネス |
|------|--------|----------|
| 命令数・関数内訳 | Callgrind | `bench/cross_sweep_profile` |
| キャッシュミス（シミュレーション） | Callgrind + `--simulate-cache=yes` | 同上 |
| プロセス全体のキャッシュ | Cachegrind | 同上（板構築込みになる点に注意） |
| **壁時計の前後比較** | Google Benchmark | **`lobcore_bench` のみ** |

```bash
cmake --build build-rel --target cross_sweep_profile
valgrind --tool=callgrind --callgrind-out-file=build-rel/cg.out \
  ./build-rel/bench/cross_sweep_profile
python3 bench/parse_callgrind.py build-rel/cg.out
```

キャッシュ付き Callgrind（**計測区間と一致**するのはこちら）:

```bash
valgrind --tool=callgrind --simulate-cache=yes \
  --callgrind-out-file=build-rel/cg-cache.out \
  ./build-rel/bench/cross_sweep_profile
callgrind_annotate --show=Ir,D1mr,DLmr --tree=none build-rel/cg-cache.out
```

### 3.2 `cross_sweep_profile` の計測区間

- `CALLGRIND_START/STOP` は **CrossSweep 20 注文 × 50 rep** のみ
- 板構築（`make_deep_book`）は区間外
- 注文内容は `BM_CrossSweep` と同一（Buy @ 2000, qty=50 × 20）

### 3.3 `cross_sweep_profile` の壁時計は使わない

このバイナリをそのまま native 実行すると、**板構築 + ホットループ** の合計時間になる。
Callgrind が数えるのはホットループだけなので、

- Callgrind で Ir が 30% 減っても
- `cross_sweep_profile` の壁時計はほぼ不変

という乖離が起きる。**性能の前後比較にこの壁時計を使ってはならない。**

### 3.4 パーサ

`bench/parse_callgrind.py` は Callgrind の function セクションを
`allocator` / `locations_` / `add_limit_body` 等にバケット分けする。
`add_limit` inclusive に対する allocator 比率の推移追跡に使った。

---

## 4. 試して戻した最適化

### 4.1 std::pmr プール（revert 済み）

**内容:** `std::map` / `std::deque` / `unordered_map` を `std::pmr` + pool に変更。

**結果（CrossSweep ハーネス, Callgrind 計測区間）:**

| 指標 | 非 pmr | pmr |
|------|--------|-----|
| Program total Ir | 37.6M | 35.0M |
| allocator % (add_limit inclusive) | 57.7% | 55.3% |
| glibc malloc 経路 | ~51% | ~4%（pool に移行） |

glibc → pool へのコスト移動であり、**allocator 支配率はほぼ横ばい**。
さらに copy/move が O(1) から O(n) になり、要件 4（反実仮想・replay の値返し）に不利。
placement-new による `operator=` 再構築はメンバ追加のたびに更新が必要で UAF 再発リスクもある。

**判断:** revert。ReferenceBook・ベンチ・`test_move.cpp` は残した。

### 4.2 locations_ 遅延削除（revert 済み）

**内容:** 約定で maker を消すとき `locations_.erase()` しない。`cancel` / `remaining`
が板を確認し、無効ヒントを遅延削除。

**結果（eager erase `a3cc4d8` vs lazy `c694dc7`, クリーンビルド）:**

| 指標 | eager | lazy | 変化 |
|------|-------|------|------|
| Callgrind Ir（計測区間） | 37.6M | 23.3M | −38% |
| D1mr（`--simulate-cache=yes`, 計測区間） | 174,350 | 84,596 | −51% |
| DLmr（同上） | 145,864 | 59,184 | −59% |
| `BM_CrossSweep` mean | 369,873 ns | 375,378 ns | **+1.5%** |
| `BM_CancelDeepBook` mean | 512,950 ns | 526,291 ns | **+2.6%** |

CrossSweep ホットパスには効かず、Cancel がわずかに悪化。

**判断:** revert。命令数・シミュレーション上のミスは減るが実時間に乗らない。

---

## 5. 教訓: 命令数と実時間の乖離

Stage 3 で確認したこと:

1. **Callgrind の Ir だけを最適化指標にしない。**
   遅延削除では Ir −38%・D1mr −51% でも `BM_CrossSweep` は横ばい〜微悪化だった。

2. **プロファイル用ハーネスの壁時計と Callgrind の計測区間を混同しない。**
   `cross_sweep_profile` の native 時間は板構築込み。Callgrind はスイープ 20 注文のみ。

3. **Cachegrind をプロセス全体で見るとホットループの変化が埋もれる。**
   全体 Ir は板構築が支配的で ±2% 程度にしか見えない。
   ホットループのキャッシュは **`callgrind --simulate-cache=yes`** の計測区間で見る。

4. **残ボトルネックは `std::map` + `std::deque` のノード追跡。**
   各 fill で反対側の best レベルへポインタを辿るコストが支配的で、
   `locations_.erase` 削除はクリティカルパスを短くしない（メモリ待ちが既に律速）。

---

## 6. 構造変更実験計画（ドラフト）

Stage 4（市場核・`ContinuousMarket`・`AllocationRule`・ログ replay・再現性テスト）が
`main` に入った時点で、次の性能改善は **板の内部表現の置き換え** に絞れる。
§4 の pmr と遅延削除は Ir やシミュレーション上のキャッシュミスを動かしても
`lobcore_bench` の壁時計は動かなかった。残ボトルネックは allocator と
`std::map` / `std::deque` のノード追跡であり、ここを変えない限り実時間は律速し続ける。

本節は **実装着手前の実験計画** である。2026-09 に承認済み。

### 6.1 仮説

| ID | 仮説 | 根拠 |
|----|------|------|
| H1 | 価格レベルと注文を **連続メモリ**（単一バッファまたは SoA）に載せ替えると、`BM_CrossSweep` / `BM_CrossSingleLevel` の壁時計が改善する | Callgrind で allocator ≈ 58%、`locations_` ≈ 15%。各 fill で反対側 best レベルへポインタを辿るコストが支配的 |
| H2 | H1 が成立しても `BM_RestOnEmpty` / `BM_CancelDeepBook` は横ばい〜微改善に留まる可能性がある | 深い板・スイープ以外は map 探索や cancel 経路が別律速 |
| H3 | ログ書き込みを含めたベンチでも H1 の改善が残る | Stage 2 以降の本番経路は `BookEventLogWriter` 経由。板だけ速くてもログ追記が律速なら意味が薄い |

**採用判断は H3 を満たす計測でのみ行う。** Callgrind Ir の改善だけでは採用しない（§5 教訓）。

### 6.2 現行ボトルネック（ベースライン）

Callgrind（CrossSweep 計測区間, eager erase, `main` @ Stage 3 計測時）:

| バケット | `add_limit` inclusive に占める割合 |
|----------|-----------------------------------|
| allocator | ≈ 58% |
| `locations_` | ≈ 15% |
| `add_limit` body (exclusive) | ≈ 19% |

壁時計ベースライン（§2 の 5 条件, Release, 同一マシン比較用）:

| ベンチ | mean (ns/iteration) |
|--------|---------------------|
| `BM_RestOnEmpty` | 39,741 |
| `BM_RestOnDeepBook` | 543,658 |
| `BM_CrossSingleLevel` | 401,250 |
| `BM_CrossSweep` | 380,640 |
| `BM_CancelDeepBook` | 511,837 |

構造変更 PR では **変更前後を同一マシン・同一 `lobcore_bench` バイナリ** で再計測し、
表を更新する。絶対値より差分を記録する。

### 6.3 前提: ベンチが本番経路を通すこと

現行 `bench/bench_book.cpp` は素の `OrderBook` のみを計測している。
`LoggedBook` / `BookEventLogWriter` / `LogRecord` 追記は含まれない。

**構造変更実験の着手条件:**

1. `lobcore_bench` に **ログ書き込み付き** のベンチを追加する（既存 5 条件と対になる命名。
   例: `BM_CrossSweepLogged`）。計測区間の注文数・板形状は既存と同一に保つ。
2. **広帯域の価格変動** を含むベンチ `BM_FlashCrash`（および `BM_FlashCrashLogged`）を追加する。
   調査文書 §1 結論 2 のとおり、価格レベル配列は価格が動くと空レベルの走査で性能が崩れる
   （参照: static 6.99 → flash-crash 0.35 M/s）。現行 5 条件は深い板が固定価格帯にあり
   best がほとんど動かないため、この弱点が見えない。案 A では **ビットマップ等による次レベル探索**
   が前提になり、本シナリオで劣化がないことを C6 で確認する（§6.6）。
3. ログは `std::vector<LogRecord>` への追記とする（ファイル I/O は含めない）。
   ディスクは別問題として Stage 5 以降で切り出す。
4. 参照実装との差分テスト（`test_diff.cpp`）は **引き続き素の `OrderBook`** で走らせる。
   構造変更後も `OrderBook` の公開 API とセマンティクスは不変。

ログ付きベンチと `BM_FlashCrash` が入るまで、構造変更の採否判断は行わない。

### 6.4 候補する構造（実装案は 1 つずつ）

一度に複数を混ぜない。各案は独立 PR とし、却下されたら revert して次へ。

| 案 | 概要 | 主に効かせたいベンチ | リスク |
|----|------|----------------------|--------|
| A | 価格レベル配列 + レベル内 FIFO キュー（連続 `Order` スロット、free list）。**次レベル探索にビットマップ等が前提** | CrossSweep, CrossSingleLevel | 価格が広く動くと空レベル走査で崩れる（調査 §1 結論 2）。`OrderId` 検索・cancel の O(?) 設計。copy/move のコスト |
| B | SoA（price / qty / seq / side を別配列）+ 価格インデックス | 同上 | 配分規則（ProRata）との接続。可読性低下 |
| C | best レベルへのキャッシュ（bid/ask 先頭ポインタ）を A/B と併用 | RestOnDeepBook, Cross* | キャッシュ整合のバグ。単独では allocator 問題は残る |

案 A を第一候補とする。B・C は A の結果を見てから。

**スコープ外（本実験ではやらない）:**

- 浮動小数点の導入、価格・数量の非整数化
- `tests/test_book.cpp` の仕様変更
- 配分規則アルゴリズムの変更（FIFO / ProRata の結果は不変であること）
- マルチ銘柄・`Kernel` 統合ベンチ（板単体が速くなってから）

### 6.5 実験手順（1 案あたり）

1. **赤:** 構造変更後も `ctest` 全通過（特に `test_diff`, `test_move`, `test_pro_rata`, `test_repro`）。
2. **計測（必須）:** Release で `lobcore_bench` を 3 回実行し、5 条件 + ログ付き 5 条件 + `BM_FlashCrash` / `BM_FlashCrashLogged` の mean を記録。
3. **計測（任意・参考）:** `cross_sweep_profile` + Callgrind Ir / `--simulate-cache=yes`。
   採否には使わないが、Ir が大きく動いて壁時計が動かない場合は §5 と同型の失敗として記録。
4. **文書化:** 本節のベースライン表を更新し、PR 説明に before/after を貼る。

### 6.6 合格基準（採用）

すべてを満たすこと:

| # | 条件 |
|---|------|
| C1 | `ctest`（Debug, ASan/UBSan）全通過 |
| C2 | `ReferenceBook` 差分テストが新実装でも一致 |
| C3 | **`lobcore_bench` の壁時計** で、ログ付き `BM_CrossSweepLogged` の mean がベースライン比 **≥ 5% 改善**（同一マシン、3 回 mean） |
| C4 | ログ付き `BM_CrossSingleLevelLogged` も **横ばい以上**（悪化 ≤ 2%）。C3 だけが良く他が壊れる案は不採用 |
| C5 | `test_move.cpp`（copy/move/replay 返却）が通過。反実仮想・replay の前提を壊さない |
| C6 | **`BM_FlashCrashLogged`** も **横ばい以上**（悪化 ≤ 2%）。案 A（価格レベル配列）は空レベル走査で広帯域の価格変動に弱いため、固定深さ板だけの合格は不十分 |

C3 の **5%** は、Stage 3 の同一マシン再計測で run 間のばらつきがおおむね **1–3%** だったことから、
ノイズと区別できる最小水準として採用する。改善が 3–4% で再現性が高い場合は人間判断でよいが、
**1–2% の揺らぎは採用しない**（§4.2 の lazy erase が +1.5% 悪化した先例）。

### 6.7 却下・revert 基準

- C1–C2 のいずれか失敗 → 即 revert。仕様合わせのためのテスト改変はしない。
- Callgrind Ir が大きく改善しても C3 未達 → revert（§5 教訓どおり）。
- C3 達成でも C4 で Cancel が **> 5% 悪化** → revert または案の縮小。cancel は実験で重要な経路。
- copy/move が O(n) 化し `test_move` や replay 性能要件に抵触 → revert。

### 6.8 実装順序（承認後）

```
[1] ログ付きベンチ + BM_FlashCrash（bench のみ、挙動変更なし）
[2] 案 A 実装 + 計測 + PR
[3] 案 A 不採用なら revert → 案 B を同手順で
[4] 採用案を reference 実装のまま残す（削除しない）
```

Stage 5（Python / Config / 実験メタデータのログヘッダ）は本実験と並行してよいが、
**構造変更の採否は Stage 5 の有無に依存しない。**

### 6.9 記録テンプレート（PR 用）

```markdown
## 構造変更: 案 A

### 壁時計 (lobcore_bench, Release, 3-run mean)
| ベンチ | before (ns) | after (ns) | delta |
|--------|-------------|------------|-------|
| BM_CrossSweepLogged | | | |
| ... | | | |

### 正しさ
- [ ] ctest Debug 全通過
- [ ] test_diff / test_pro_rata / test_repro

### Callgrind（参考）
- Ir: ... → ... (計測区間のみ。採否には不使用)
```

---

## 7. Stage 3 の区切り

- 参照実装との差分テスト、5 条件ベンチ、プロファイル手順を整備した
- pmr と遅延削除は計測に基づき revert し、理由をコミットログに残した
- 「何が効かなかったか」と「何がまだ遅いか」を文書化した
- 構造変更の実験計画は §6 にまとめた（実装は計画承認・ログ付きベンチ追加後）
