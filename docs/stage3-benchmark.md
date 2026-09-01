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

### 現状の数字（Release, 3 回 mean, 2026-09 計測）

環境依存のため絶対値より **同一マシン・同一ビルドでの比較** を優先する。

| ベンチ | mean (ns/iteration) | 備考 |
|--------|---------------------|------|
| `BM_RestOnEmpty` | 39,741 | items/s ≈ 25.2M |
| `BM_RestOnDeepBook` | 543,658 | |
| `BM_CrossSingleLevel` | 401,250 | avg fills/order = 5 |
| `BM_CrossSweep` | 380,640 | avg fills/order = 50 → **≈19.0 μs/order** |
| `BM_CancelDeepBook` | 511,837 | |

付帯成果物:

- `ReferenceBook` + `tests/test_diff.cpp`（乱数ストリームで `OrderBook` と照合）
- `tests/test_move.cpp`（コピー・ムーブ・`replay` 返却値のライフタイム）
- `bench/cross_sweep_profile.cpp` + `bench/parse_callgrind.py`（プロファイル専用、後述）

---

## 3. Callgrind / Cachegrind の手順

### 3.1 使うハーネス

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

## 6. 残っているボトルネックと Stage 4 以降

Callgrind（CrossSweep 計測区間, eager erase 現行実装）の目安:

- allocator ≈ 58% of `add_limit` inclusive
- `locations_` ≈ 15%
- `add_limit` body exclusive ≈ 19%

allocator チューニング（pmr・遅延削除）では実時間が動かなかった。
次に効きそうなのは **データ構造そのもの**（単一バッファ、SoA、価格レベルの連続配置など。
WK Selph の指摘と同型）だが、Stage 4 の市場核・複数銘柄・配分規則の実験規模が
見えてから判断する。**Stage 3 では構造変更は行わない。**

---

## 7. Stage 3 の区切り

- 参照実装との差分テスト、5 条件ベンチ、プロファイル手順を整備した
- pmr と遅延削除は計測に基づき revert し、理由をコミットログに残した
- 「何が効かなかったか」と「何がまだ遅いか」を文書化した
- 構造変更は Stage 4 のスコープ確定後に再検討する
