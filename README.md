# lobcore

[EN](README.en.md) | **JA**

**エージェントの判断が、注文・約定を通じて市場にどう現れるかを調べるための実験基盤。**

lobcore は、C++20 の指値注文板（Limit Order Book; LOB）マッチングエンジンと、離散イベント型の市場シミュレータ核、Python インターフェースを提供します。Python でエージェントの行動を書き、C++ 側で注文の到着・約定・取消を処理し、結果をイベントログとして取り出せます。

同じ設定とシードによる再実行や、特定エージェントの注文を抑制した比較を支援します。投資家の行動モデル、学習、実データによる較正、研究ごとの分析は、lobcore を利用する実験側で組み立てます。

> **既存実験を利用する方へ：2026-09-17 に Python の乱数ストリーム取得を修正しました。**
> `ctx.rng()` / `rng_for()` / `sentinel_rng()` の再取得で乱数列が先頭へ戻っていた問題と、過去の実験結果を再実行する必要がある範囲は、[修正記録](docs/python-rng-correction.md)を参照してください。

## 研究での使い方

エージェントの意思決定と、市場での注文処理を分けて扱います。

```text
実験側                         lobcore                         実験側
情報・価格履歴・保有状態 → 発注・取消 → 到着・マッチング → ログの集計・仮説の検証
ルール・学習済みモデル         市場ルールとイベント処理       価格・約定・流動性の分析
```

例えば、次のような実験の土台として使えます。

- 同じ背景設定で、ある参加者の注文がある場合とない場合の市場応答を比較する。
- 価格時間優先と数量比例配分で、注文の約定や価格形成がどう変わるかを調べる。
- 投資家の判断規則を差し替え、買い注文の補充・取消と価格変化の関係を調べる。

SG、ニューラルネット、LLM などの判断モデルを接続する研究は拡張用途です。これらのエージェントや、資金・在庫・損益の管理、実データに較正済みの市場は同梱していません。市場の統計的性質や仮説への妥当性は、各実験で検証します。

## 実装済みの機能

| 部品 | 内容 |
|---|---|
| 注文板 | 指値注文、部分約定、取消、最良気配、注文ごとの残数量。価格と数量は整数 |
| 配分規則 | 価格時間優先、同一価格での数量比例配分（pro-rata） |
| シミュレータ核 | 整数時刻のイベントキュー、エージェント起床、注文の遅延到着、市場の時刻イベント用インターフェース |
| Python エージェント | `Agent.on_wakeup(view, ctx)`、発注・取消・次回起床予約。同時刻の Python エージェントを一括処理 |
| 乱数 | マスターシードとエージェント・コンポーネント ID から独立したストリームを派生 |
| イベントログ | 注文・約定・取消・拒否を固定長レコードに記録。Python では NumPy 構造化配列として取得・保存 |
| 再現性と比較 | C++ のログリプレイと状態ハッシュ、ログのバイト比較、指定エージェントの注文抑制、ペア実行用ヘルパー |
| 検証 | 参照実装とのランダム注文列の差分テスト、C++／Python テスト、任意のベンチマーク |

Stage 1–5 の主要部品と、Stage 6 Phase 0 の比較実験用基盤を実装済みです。設計文書には将来の拡張も含まれるため、現在の範囲は本 README と公開 API を参照してください。

## Python で始める

必要なものは Python 3.10 以上、C++20 対応コンパイラ、CMake 3.21 以上です。NumPy と、ビルド用の scikit-build-core・pybind11 はインストール時に取得されます。初回の依存取得にはネットワーク接続が必要です。

以下はリポジトリ直下で実行します。

```sh
git clone https://github.com/yuitokyouni/lobcore.git
cd lobcore
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e "./python[test]"
python -m pytest python/tests -q
```

Windows PowerShell の仮想環境の有効化は `.venv\Scripts\Activate.ps1` です。現在の Python CI は Ubuntu・Python 3.12 で実行しています。

### 最小の約定例

二つのエージェントが一度ずつ注文を出します。価格は整数ティック、数量は整数単位です。

```python
from lobcore import Agent, Experiment


class PlaceOnce(Agent):
    def __init__(self, side, price, qty):
        self.side = side
        self.price = price
        self.qty = qty

    def on_wakeup(self, view, ctx):
        ctx.submit(0, self.side, self.price, self.qty)
        # 次の起床を予約しないので、一度だけ発注する。


result = Experiment(
    seed=42,
    agents=[PlaceOnce("sell", 101, 10), PlaceOnce("buy", 101, 4)],
    end_time=3,
    rule="price_time",  # "pro_rata" に変更可能
    strict=True,
).run()

fills = result.log[result.log["kind"] == 1]  # 1 = Fill
print(fills[["price", "qty"]])  # [(101, 4)]
```

初回起床は時刻 1、既定の注文到着遅延は 1 です。この例では時刻 2 に売り注文、買い注文の順で到着し、価格 101 で数量 4 が約定します。未約定の売り数量 6 は板に残ります。

`view.market(0)` で最良気配と注文の残数量を参照でき、`ctx.cancel()` で取消、`ctx.schedule_wakeup()` で次の行動時刻、`ctx.rng(component)` で乱数ストリームを指定できます。`strict=True` はエージェント内の例外を呼び出し側へ伝えます。既定の `False` では、例外を起こしたエージェントを停止して実行を続けます。

### 注文を抑制した比較

上の `PlaceOnce` を使い、買いエージェントの注文だけを抑制します。エージェント ID は登録順に 0 から割り当てられます。

```python
def make_experiment():
    # 履歴や在庫を持つモデルでも比較できるよう、毎回新しい agent を作る。
    return Experiment(
        seed=42,
        agents=[PlaceOnce("sell", 101, 10), PlaceOnce("buy", 101, 4)],
        end_time=3,
        strict=True,
    )


factual = make_experiment().run()
baseline = make_experiment().run(suppress_agent_ids=[1])

print((factual.log["kind"] == 1).sum())   # 1
print((baseline.log["kind"] == 1).sum())  # 0
```

抑制対象も通常どおり起床し、乱数を利用できますが、そのエージェントの注文送信は市場へ届けません。これは特定の一注文を除く操作ではなく、指定エージェントの送信を抑制する操作です。

`Experiment.run_pair(suppress_agent_ids=[...])` もありますが、二つの実行で同じ Python agent／step オブジェクトを再利用します。履歴・在庫・学習状態を持つモデルでは、上のように新しいオブジェクトでそれぞれ実行してください。

乱数ストリームの分離だけで、介入後のすべての乱数呼び出しや行動時刻が自動的に対応するわけではありません。反実仮想実験では、外生系列・初期状態・介入前ログの一致と、介入後に変化させる範囲を実験側で定義します。LLM 等の外部サービスを使う場合も、応答の保存・再利用を含めた再現条件は実験側で管理します。

## C++ のビルドとテスト

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

初回は Catch2 を取得します。GCC／Clang の Debug ビルドでは ASan／UBSan が既定で有効です。C++ CI は Ubuntu と macOS で実行しています。

Visual Studio 2022 で複数構成のビルドを使う場合：

```sh
cmake -S . -B build-vs -G "Visual Studio 17 2022"
cmake --build build-vs --config Debug
ctest --test-dir build-vs -C Debug --output-on-failure
```

### ベンチマーク

```sh
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DLOBCORE_BUILD_BENCH=ON
cmake --build build-rel
./build-rel/bench/lobcore_bench
```

Google Benchmark を取得してビルドします。性能比較には Release を使い、コンパイラ・実行環境・ワークロードを併記してください。測定設計は [Stage 3](docs/stage3-benchmark.md) にまとめています。

| CMake オプション | 既定値 | 用途 |
|---|---|---|
| `LOBCORE_BUILD_TESTS` | `ON` | C++ テスト |
| `LOBCORE_BUILD_BENCH` | `OFF` | ベンチマーク |
| `LOBCORE_BUILD_PYTHON` | `OFF` | Python 拡張 |
| `LOBCORE_SANITIZE` | `ON` | GCC／Clang の Debug サニタイザ |

Python パッケージのインストールでは、Python 拡張を有効にし、C++ テスト・ベンチマーク・サニタイザを無効にしてビルドします。

## モデルの前提と現在の制約

- **マッチング：** 約定価格は板に載っていた注文（maker）の指値です。時間優先は到着処理順で決まり、壁時計を使いません。pro-rata は同一価格で残数量に比例して配分し、端数は最大剰余法、同点は到着順で処理します。
- **時間：** シミュレーション時刻は整数です。秒・ミリ秒などとの対応は実験で決めます。同時刻の起床では同じ板状態を観測し、注文は正の遅延を経て到着します。エージェント別の遅延設定は C++ API にあり、現行 Python API には公開していません。
- **注文と市場：** 実装済みの注文メッセージは指値と取消、市場は連続取引です。成行専用注文型、IOC、バッチオークション、ダークプール、ITCH／LOBSTER 入力アダプタは未実装です。
- **観測：** Python の公開観測は最良気配と注文残数量です。深さ N の板や口座状態は含みません。約定通知の接続も、現行の連続市場と Python エージェントでは未整備です。
- **ログ：** 気配はイベント適用前の受付時点を記録します。`mid_series()` はその気配から系列を作り、片側だけ存在するときはその価格を返します。常に両側気配の中間値や約定後価格になるわけではありません。
- **複数市場：** 核には複数の市場を登録できますが、各板は単一銘柄で、現行ログには市場 ID がありません。複数市場の統合分析や多資産の資金・決済管理には追加実装が必要です。

## 構成と関連文書

```text
include/lobcore/        注文板・配分規則・ログの公開ヘッダ
include/lobcore/kernel/ イベント、市場、エージェント、乱数、遅延
src/                   C++ 実装と参照注文板
bindings/              pybind11 バインディング
python/src/lobcore/     Agent、Experiment、ログ保存、分析ヘルパー
tests/                 C++ の仕様・差分・再現性テスト
python/tests/          Python API・ログ・乱数のテスト
bench/                 ベンチマーク
docs/                  設計と調査記録
```

- [注文板の契約](include/lobcore/book.hpp)と[実行可能な仕様](tests/test_book.cpp)
- [イベントログとリプレイ](docs/stage2-event-log.md)
- [離散イベント核の設計](docs/stage4-kernel.md)
- [Python インターフェース](docs/stage5-python.md)
- [反実仮想実験の基盤と分担](docs/stage6-impact-experiment.md)
- [ログのバイト再現性の修正記録](docs/log-padding-investigation.md)
- [文献・背景資料](docs/lobcore-context-pack.md)

研究モデルや個別実験は [financial-abm-lab](https://github.com/yuitokyouni/financial-abm-lab) 側で管理します。Stage 6 の設計では、lobcore が比較実験の基盤を担い、YH012 がエージェントモデル・インパクト評価・可視化を担う構成です。
