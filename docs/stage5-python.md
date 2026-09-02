# Stage 5 設計: Python バインディング

Stage 4 の核を Python から使えるようにする。到達点は「Python で書いたエージェントを
核に登録して実行し、ログを NumPy 配列として取り出し、同じシードで結果が再現する」こと。

前提は CLAUDE.md の要件 1–5。特に要件 5 の「同一時刻に起こす Python エージェント群に
観測を 1 回の呼び出しで配列として渡し、注文を 1 回で受け取る」が骨格。

---

## 0. 確定済みの方針

- **C++ 側は一括方式のみ。** 同一時刻の起床をまとめて 1 回 Python に渡す。
- **Python 側にラッパを置く。** `Agent` クラスを継承して 1 体ずつ書ける。書く側は一括方式を意識しない。
- **観測は最良気配のみ。** 深さ N の板は Stage 5 の範囲外。後から `Market::depth(n)` を足せる。
- **ログは NumPy 構造化配列。** `LogRecord` のレイアウトをそのまま `dtype` に写す。
- **実験メタデータはログの先頭。** シード、規則、エージェント構成を 1 箇所に置く。
- **`pip install` できる形。** scikit-build-core + pybind11。
- **依存追加(pybind11)は承認済み。**

---

## 1. パッケージ構成

```
python/
  pyproject.toml           # scikit-build-core
  src/lobcore/
    __init__.py            # 公開 API
    _core.pyi              # 型スタブ
    agent.py               # Agent 基底クラス、BatchAdapter
    log.py                 # ログの NumPy 変換、メタデータ
    experiment.py          # Experiment(設定 → 実行 → 結果)
  tests/
    test_binding.py        # pytest
bindings/
  module.cpp               # pybind11 モジュール定義
  batch_agent.hpp/cpp      # BatchPythonAgent(C++ 側の一括受け口)
```

C++ のビルドは既存の CMake に `LOBCORE_BUILD_PYTHON` オプションを足す形。
既定 OFF。CI では ON にして pytest も回す。

---

## 2. C++ 側: 一括受け口

### 2.1 BatchPythonAgent

核から見ると 1 つの `Agent` だが、内部で複数の Python エージェントを束ねる。

```cpp
class BatchPythonAgent : public Agent {
 public:
  BatchPythonAgent(pybind11::object step_fn, std::vector<AgentId> members);

  void on_wakeup(const KernelView& view, AgentContext& ctx) override;
  void on_notification(const NotificationPayload& n,
                       const KernelView& view, AgentContext& ctx) override;
 private:
  pybind11::object step_fn_;
  std::vector<AgentId> members_;
};
```

問題: 核は `AgentWakeup` を 1 体ずつディスパッチする。一括で渡すには、
**同一時刻の起床を核が集めてから Python を 1 回呼ぶ**必要がある。

### 2.2 核の変更: 同一時刻の起床をまとめる

`Kernel::dispatch` の `AgentWakeup` 処理を変える。

```
現在: AgentWakeup を 1 つ pop → agent.on_wakeup() → 次へ
変更: 同一 time の AgentWakeup を連続して pop し、agent_id を集める
      → BatchPythonAgent に属するものは 1 回の呼び出しにまとめる
      → C++ ネイティブの Agent は従来通り 1 体ずつ
```

`kind_order` で `AgentWakeup` は最後なので、同一時刻の起床は連続して並ぶ。
ヒープの先頭を見て、同じ時刻かつ `AgentWakeup` である限り pop し続ければ集まる。

**決定性への影響:** 集めた `agent_id` の順序は `seq` 順のまま。Python 側に渡す配列も
この順序。Python 側が順序通りに処理すれば、結果は 1 体ずつ呼んだ場合と同じ。

### 2.3 Python に渡すもの

```cpp
struct BatchObservation {
  Timestamp now;
  std::vector<AgentId> agent_ids;          // seq 順
  std::vector<MarketSnapshot> markets;     // 市場ごとの最良気配
};

struct MarketSnapshot {
  std::optional<Level> best_bid;
  std::optional<Level> best_ask;
};
```

自分の注文状態(`remaining`)は含めない。必要なら Python 側が
`market.remaining(order_id)` を個別に呼ぶ。頻度は低いはず。

### 2.4 Python から受け取るもの

```cpp
struct BatchAction {
  std::vector<OrderSubmission> orders;    // (agent_id, market_id, OrderMessage)
  std::vector<Timestamp> next_wakeups;    // agent_ids と同じ長さ。0 = 起きない
};
```

`next_wakeups[i]` は `agent_ids[i]` の次回起床時刻。0 は「二度と起きない」。
`now` 以下の値は契約違反として拒否カウンタで数え、そのエージェントは起きない。

### 2.5 検査(C++ 側)

| 検査 | 違反時 |
|---|---|
| `next_wakeups.size() == agent_ids.size()` | 例外(構造的な誤り。実験を続ける意味がない) |
| `next_wakeups[i] > now` または `== 0` | 拒否カウンタ `invalid_wakeup`。そのエージェントは起きない |
| `orders[j].agent_id` が `agent_ids` に含まれる | 拒否カウンタ `order_from_sleeping_agent`。注文を捨てる |
| 注文の `qty > 0`、`price` が妥当 | 板が既に検査する。ここでは通す |

長さ不一致だけは例外にする。他は「アラートだけ出して止めるかは人間次第」の方針。

### 2.6 乱数ストリーム

Python 側が `np.random` を使うと要件 4 が壊れる。C++ の `Rng` を Python に出す。

```cpp
// バインディング
class PyRng {
 public:
  std::uint64_t next_u64();
  double uniform();                     // [0, 1)
  double normal(double mu, double sigma);
  double exponential(double rate);
  pybind11::array_t<double> uniform_array(std::size_t n);   // 一括
};

PyRng rng_for(AgentId agent, ComponentId component);   // 導出規則は Stage 4 §6
```

`normal` と `exponential` は C++ 側で `std::normal_distribution` 等を使う。
ただし分布の実装は標準ライブラリ依存で環境をまたいだ再現が保証されないため、
**Box-Muller と逆関数法を自前で書く。** 40 行程度。

---

## 3. Python 側: ラッパ

### 3.1 Agent 基底クラス

```python
class Agent:
    def on_wakeup(self, view: View, ctx: Context) -> None:
        raise NotImplementedError
    def on_notification(self, note: Notification, view: View, ctx: Context) -> None:
        pass   # 既定は無視
```

### 3.2 View と Context

```python
class View:
    now: int
    def market(self, market_id: int) -> MarketView: ...

class MarketView:
    best_bid: Level | None
    best_ask: Level | None
    def remaining(self, order_id: int) -> int | None: ...   # 境界を跨ぐ

class Context:
    def submit(self, market_id: int, side: str, price: int, qty: int,
               order_id: int | None = None) -> int: ...     # order_id を返す
    def cancel(self, market_id: int, order_id: int) -> None: ...
    def schedule_wakeup(self, t: int) -> None: ...
    def rng(self, component: int) -> Rng: ...
```

`submit` の `order_id` を省略した場合、ラッパが採番する。要件 3 で `OrderId` は
外部付番なので、Python 側が付番主体になる。エージェント ID と連番から導出して
衝突しないようにする。

### 3.3 BatchAdapter

C++ の一括呼び出しを受けて、各 `Agent` インスタンスの `on_wakeup` を順に呼ぶ。

```python
class BatchAdapter:
    def __init__(self, agents: list[Agent]):
        self._agents = agents

    def step(self, obs: BatchObservation) -> BatchAction:
        view = View.from_observation(obs)        # スナップショットを 1 回作る
        orders, wakeups = [], []
        for aid in obs.agent_ids:
            ctx = Context(agent_id=aid, now=obs.now)
            self._agents[aid].on_wakeup(view, ctx)
            orders.extend(ctx.orders)
            wakeups.append(ctx.next_wakeup or 0)
        return BatchAction(orders, wakeups)
```

`View` は `obs` から 1 回構築し、全エージェントに同じインスタンスを渡す。
要件 5 の「同時刻に全員同じ状態を観測する」がここで保たれる。

### 3.4 一括方式を直接書く道

`BatchAdapter` を使わず、`step` 関数を直接渡すこともできる。
速度が要る実験向け。

```python
def my_step(obs: BatchObservation) -> BatchAction:
    ...   # NumPy でベクトル化

kernel.add_python_agents(my_step, n_agents=1000)
```

---

## 4. ログ

### 4.1 NumPy 構造化配列

`LogRecord` のレイアウトをそのまま `dtype` に写す。

```python
LOG_DTYPE = np.dtype([
    ("seq", "u8"), ("kind", "u1"), ("side", "u1"), ("reason", "u1"),
    ("decided_at", "i8"), ("received_at", "i8"),
    ("order_id", "u8"), ("maker_id", "u8"),
    ("price", "i8"), ("qty", "i8"),
    ("best_bid_price", "i8"), ("best_bid_qty", "i8"),
    ("best_ask_price", "i8"), ("best_ask_qty", "i8"),
], align=True)
```

`align=True` で C++ の構造体パディングと一致させる。
一致することを C++ 側の `static_assert(sizeof(LogRecord) == ...)` と
Python 側のテストの両方で固定する。

取り出しは `kernel.log()` が `np.ndarray` を返す。コピーで良い。
ゼロコピーは実験が終わってから取り出すので不要。

### 4.2 メタデータ

ログの先頭に実験の設定を置く。

```python
@dataclass
class ExperimentMeta:
    master_seed: int
    allocation_rule: str          # "price_time" | "pro_rata"
    n_agents: int
    n_markets: int
    end_time: int
    lobcore_version: str
    agent_config: dict            # 自由形式。JSON にできるもの
```

ファイルに書く時は、メタデータを JSON ヘッダとして先頭に置き、その後に
バイナリレコードを並べる。読む側はヘッダ長を先頭 8 バイトから取る。

```
[8 bytes: header_len][header_len bytes: JSON][records...]
```

メモリ上では `kernel.meta()` と `kernel.log()` を別々に返す。

---

## 5. Experiment

設定から実行、結果取り出しまでを 1 つにまとめる。

```python
exp = Experiment(
    seed=42,
    markets=[ContinuousMarket(rule="pro_rata")],
    agents=[NoiseTrader() for _ in range(100)] + [MarketMaker()],
    end_time=1_000_000,
)
result = exp.run()
result.log          # np.ndarray
result.meta         # ExperimentMeta
result.state_hash   # 全市場の state_hash
```

同じ `Experiment` を 2 回 `run()` して `state_hash` と `log` が一致することが
Stage 5 の到達点。

---

## 6. 検査(Python 側)

C++ 側の検査(§2.5)と重複するが、Python 側でも早期に捕まえる。

- `Context.schedule_wakeup(t)` で `t <= now` なら即座に `ValueError`
- `Context.submit` で `qty <= 0` なら即座に `ValueError`
- `on_wakeup` が例外を投げたら、そのエージェントを停止して警告(実験は続ける)

例外を投げる方が良いか、警告して続けるかは、開発中と本番で違う。
`Experiment(strict=True)` で切り替えられるようにする。既定は `strict=False`。

---

## 7. テスト

`python/tests/test_binding.py`:

- `OrderBook` を Python から叩いて Stage 1 の基本仕様が通る
- `LOG_DTYPE` のサイズが C++ の `sizeof(LogRecord)` と一致
- Python `Agent` を 1 体登録して実行、注文が板に届く
- 同一時刻に複数の Python エージェントが起き、全員同じ `View` を見る
- `schedule_wakeup(now)` が拒否される
- `rng(component)` が同じシードで同じ列を返す
- **同じ `Experiment` を 2 回実行して `log` と `state_hash` が一致する**(到達点)
- 規則を `pro_rata` に変えると `log` が変わる
- `BatchAdapter` 経由と `step` 直接渡しで同じ結果になる

C++ 側 `tests/test_batch.cpp`:

- `Kernel` が同一時刻の `AgentWakeup` を正しく集める
- `BatchPythonAgent` 無しでも既存 69 本が通る(核の変更が既存に影響しない)

---

## 8. ビルド

```toml
[build-system]
requires = ["scikit-build-core", "pybind11"]
build-backend = "scikit_build_core.build"

[project]
name = "lobcore"
requires-python = ">=3.10"
dependencies = ["numpy>=1.24"]
```

`pip install -e .` で開発モード。`pip install .` で通常インストール。
wheel の配布は範囲外。

CI に Python ジョブを足す。`ubuntu-24.04` のみ。macOS は既存の C++ ジョブで足りる。

---

## 9. 未決定

- **`on_notification` の一括化。** 起床と同じく同一時刻の通知をまとめるか、
  1 体ずつ呼ぶか。通知は起床より頻度が低いはずなので、初版は 1 体ずつで良い
- **`remaining()` の境界コスト。** 頻繁に呼ぶエージェントがいると遅い。
  必要なら `BatchObservation` に自分の注文一覧を含める拡張を後から足す

---

## 10. やらないこと

- 深さ N の板(最良気配のみ)
- wheel の配布(`pip install .` で足りる)
- ゼロコピーのログ取り出し
- Python エージェントの並列実行
- Python から `Market` を実装する(市場は C++ のみ)
