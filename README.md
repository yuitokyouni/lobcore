# lobcore

単一銘柄・価格時間優先の板 (LOB) マッチングエンジン。
Stage 4 で離散イベント型の市場シミュレータ核、Stage 5 で pybind11 経由の Python モジュールに育てる。

## 構成

```
lobcore/
├── CMakeLists.txt          ライブラリ本体 + オプション
├── include/lobcore/
│   ├── types.hpp           Price / Qty / OrderId / Side   (整数ティック)
│   └── book.hpp            OrderBook の公開インターフェース (仕様はコメント参照)
├── src/
│   └── book.cpp            実装。今はスタブ。Stage 1 で全部書き換える
├── tests/
│   ├── CMakeLists.txt      Catch2 v3 を FetchContent で取得
│   └── test_book.cpp       Stage 1 の仕様。全部通したら Stage 1 完了
└── bench/
    ├── CMakeLists.txt      Google Benchmark を FetchContent で取得
    └── bench_book.cpp      Stage 3 用。既定では OFF
```

`include/lobcore/` という一段深い階層は `#include <lobcore/book.hpp>` と書くため。
将来 pybind11 モジュールが同じライブラリターゲット `lobcore` にリンクする。

## ビルド

### Visual Studio 2022
フォルダを「開く」→ CMake プロジェクトとして自動構成される。.sln は手で作らない。
.sln が欲しい場合はコマンドラインで生成する:

```
cmake -S . -B build -G "Visual Studio 17 2022"
```

→ `build/lobcore.sln`。ソリューション名 = `project()` の名前。

### コマンドライン (Windows / WSL / Linux 共通)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

初回は Catch2 の取得とビルドで数分かかる。
ベンチマークは Stage 3 に入ってから:

```
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DLOBCORE_BUILD_BENCH=ON
cmake --build build-rel
./build-rel/bench/lobcore_bench
```

Debug では GCC/Clang に限り ASan/UBSan が自動で入る (`LOBCORE_SANITIZE`)。
MSVC は対象外なので、サニタイザを使うなら WSL か clang-cl。

## 最初のセッションでやること

1. ビルドして `ctest` を走らせる。9 件中 8 件失敗するのが正常 (スタブなので)。
   通る 1 件は「空の板に best がない」で、スタブが nullopt を返すだけの空振り。
2. `book.hpp` の private にデータ構造を置き、`book.cpp` を書いて 9 件全部を通す。
   最初は `std::map` + `std::deque` で。最適化しない。cancel が線形探索でも構わない。
3. 通ったら `git init` してコミット。
4. 自分の `book.cpp` を読み返し、各操作 (add / cancel / best / remaining) の計算量を書き出す。
   これが Stage 3 で「何を速くするか」の出発点になる。

## 既に決めてあること (理由は types.hpp / book.hpp のコメント)

- 価格は整数ティック。double は使わない
- 約定価格は maker (板に載っていた側) の指値
- 到着順は add_limit の呼び出し順。壁時計は使わない
- OrderId は呼び出し側が付番する

## 自分で決めてテストを足すこと

- 重複 OrderId、qty <= 0、cancel 後の ID 再利用

## ステージ

- [ ] Stage 1: 指値・取消のマッチング。test_book.cpp 全通過
- [ ] Stage 2: 追記専用イベントログ + 決定論的リプレイ (状態ハッシュ一致)
- [ ] Stage 3: 参照実装 (素朴な O(n)) との差分テスト、乱数注文列、Google Benchmark で設計選択を実測
- [ ] Stage 4: 優先度付きキューによる離散イベントスケジューラ
- [ ] Stage 5: pybind11 で Python 公開 (ステップ単位のバッチ渡し)

## 名前を変えたい場合

`lobcore` は 4 箇所で使っている。全部そろえること。
`project()` 名 / ライブラリターゲット名 / `namespace lobcore` / `include/lobcore/` ディレクトリ名。
Python モジュール名にもなるので、小文字・ハイフンなし・Python 識別子として有効なものにする。
