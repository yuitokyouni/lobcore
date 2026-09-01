# Stage 4 設計: 離散イベント市場シミュレータ核

Stage 1–3 の `OrderBook` を Market の一実装として取り込み、その外側に核を置く。
到達点は「エージェントを複数動かした実行が、同じシードで完全に再現する」ことをテストで示すこと。

前提は CLAUDE.md の要件 1–5。特に要件 5(Market と Agent の関係)がこの文書の骨格。

---

## 0. 確定済みの方針

- **イベント駆動。** 時刻を一定間隔で進めず、次のイベントの時刻まで飛ぶ。
- **自己スケジュール型。** 次回起床時刻はエージェントが返す。核が抽選しない。
- **Oracle は核に入れない。** 必要な実験では Agent として外部に置く。
- **残高・資金の検査は核でも Market でもしない。** Agent の設計領域。
- **遅延はエージェントごとの固定値。** 既定 1、必要なときだけ上書き(§5)。
- **配分規則は戦略オブジェクト。** 実行時に差し替える(§4.2)。
- **Intervention イベントは持たない。** 既存の仕組みで表現できる(§2.4)。

---

## 1. 核が持つ状態

```cpp
class Kernel {
 private:
  Timestamp now_ = 0;
  EventHeap heap_;
  std::uint64_t next_event_seq_ = 0;   // 同時刻の決定的順序づけ用

  std::vector<std::unique_ptr<Agent>>  agents_;    // 添字 = AgentId
  std::vector<std::unique_ptr<Market>> markets_;   // 添字 = MarketId

  LatencyModel  latency_;
  std::uint64_t master_seed_;
  EventLog      log_;
};
```

`AgentId` と `MarketId` は核が付番する密な整数。要件 3 の `OrderId`(外部付番)とは別物。

エージェントと市場は起動時に登録し、実行中に増減しない。動的な参加・退出が必要になったら、
「起床しないエージェント」で表現する。核の状態が実行中に変わらない方が、決定性の議論が単純になる。

---

## 2. イベントヒープ

### 2.1 イベントの定義

```cpp
struct OrderDelivery   { AgentId from; MarketId to; OrderMessage msg; };
struct MarketTimeEvent { MarketId market; MarketTimerId timer; };
struct Notification    { AgentId to; NotificationPayload payload; };
struct AgentWakeup     { AgentId agent; };

using EventBody = std::variant<OrderDelivery, MarketTimeEvent,
                               Notification, AgentWakeup>;

struct Event {
  Timestamp     time;
  std::uint8_t  kind_order;   // EventBody の index と一致させる
  std::uint64_t seq;          // 核が挿入時に振る
  EventBody     body;
};
```

`OrderMessage` の中身は要件 3 のメッセージ語彙(Add / Execute / Cancel / Delete / Replace)。

### 2.2 優先度

`(time, kind_order, seq)` の 3 段。すべて整数の比較で決まるので、
浮動小数点も壁時計も入らない。

`kind_order` は上の宣言順:

| 順 | 種別 | 理由 |
|---|---|---|
| 0 | OrderDelivery | 板を変える。時刻 t に届いた注文は t の板寄せに含まれる |
| 1 | MarketTimeEvent | 板を変える。板寄せ・清算 |
| 2 | Notification | 板を変えない。エージェントへの情報 |
| 3 | AgentWakeup | 板を読む。この段階では板が動かない |

**この順序が「同じ時刻に起こされたエージェントは全員同じ状態を観測する」を保証する。**
板を変えるイベント(0・1)を先に処理し切ってから、板を読むイベント(3)に入るため。

`OrderDelivery` の到着遅延を 1 以上に強制するのは、時刻 t の起床で出た注文が
t の観測に影響しないようにするため(要件 5)。核は配達時に `received_at > decided_at`
を検査する(Debug の assert とログ上の不変条件テスト)。

### 2.3 決定性

`seq` は核が挿入順に振る単調増加の整数。同じ `(time, kind_order)` のイベントは
挿入順に処理される。挿入順は決定的なので、全体の処理順が決定的になる。

`std::priority_queue` は同値の順序を保証しないが、`seq` を比較に含めるので問題にならない。

### 2.4 Intervention を持たない理由

要件 5 では「介入」をイベント種別の一つとして挙げていたが、実装しない。
想定していた 3 用途がすべて既存の仕組みで表現できるため。

| 用途 | 代替 |
|---|---|
| 反実仮想(注文を 1 本抑制) | 実行前に設定するフィルタ。実行中のイベントである必要がない |
| ショック注入(大口注文の投入) | 「時刻 t に注文を出すだけの Agent」を 1 体置く |
| サーキットブレーカー | Market の `on_time` で処理する。市場の機能 |

エージェント単位の取引停止は「そのエージェントを起こさない」で表現できる。

**CLAUDE.md の要件 5 から介入の記述を削る必要がある。** イベント種別集合とログ
レコード種別の対応表からも消える。

---

## 3. Agent のインターフェース

```cpp
class Agent {
 public:
  virtual ~Agent() = default;

  // 起床。観測して行動を決める。
  virtual void on_wakeup(const KernelView& view, AgentContext& ctx) = 0;

  // 自分の注文に関する事実の通知(約定、取消受理、拒否)。
  virtual void on_notification(const NotificationPayload& n,
                               const KernelView& view,
                               AgentContext& ctx) = 0;
};
```

### 3.1 観測 — KernelView

```cpp
class KernelView {
 public:
  Timestamp now() const noexcept;

  const Market& market(MarketId id) const;   // 板の読み取り
  std::size_t   market_count() const noexcept;
};
```

同期読み取り。核が起こした時刻の状態をその場で読む(要件 5)。
`const` 参照なので、観測経路から板を変えられない。

**自分の注文の状態は `Market::remaining(OrderId)` で引く。** 核が
「エージェントごとの注文一覧」を持つ設計にはしない。持たせると核と市場で
二重管理になり、同期漏れの経路が増える。必要なら Agent が自分で覚える。

### 3.2 行動 — AgentContext

```cpp
class AgentContext {
 public:
  Timestamp now() const noexcept;
  AgentId   id()  const noexcept;

  Rng& rng(ComponentId c);                             // §6

  void submit(MarketId to, const OrderMessage& msg);   // 注文・取消・訂正
  void schedule_wakeup(Timestamp t);                   // 自己スケジュール
};
```

戻り値ではなく `ctx` への push にする。1 回の起床で複数の市場に複数の注文を出せる。

`submit` は即座に配達されず、`decided_at = now()` として遅延分を足した時刻の
`OrderDelivery` イベントとしてヒープに積まれる。

`schedule_wakeup(t)` は `t > now()` を要求する。同時刻の再入を禁じないと
無限ループが書けてしまう。

### 3.3 起床しなかった場合

`on_wakeup` で `schedule_wakeup` を呼ばなければ、そのエージェントは二度と起きない。
これで「途中で退出する参加者」が表現できる。

---

## 4. Market のインターフェース

```cpp
class Market {
 public:
  virtual ~Market() = default;

  // 注文駆動
  virtual void on_order(const OrderMessage& msg, Timestamp received_at,
                        MarketContext& ctx) = 0;

  // 時間駆動(板寄せ、清算)
  virtual void on_time(MarketTimerId timer, Timestamp t, MarketContext& ctx) = 0;

  // 観測(const)
  virtual std::optional<Level> best_bid() const = 0;
  virtual std::optional<Level> best_ask() const = 0;
  virtual std::optional<Qty>   remaining(OrderId id) const = 0;
  virtual std::uint64_t        state_hash() const noexcept = 0;
};
```

```cpp
class MarketContext {
 public:
  Timestamp now() const noexcept;

  void notify(AgentId to, const NotificationPayload& n);   // 約定通知など
  void schedule_timer(MarketTimerId timer, Timestamp t);   // 板寄せの予定
  void emit(const LogRecord& rec);                         // 要件 1

  const KernelView& view() const noexcept;  // 他市場の参照(要件 2)
};
```

`notify` も遅延付きで配達される。

### 4.1 ContinuousMarket

`OrderBook` を包んで `Market` を実装する。ザラバ(連続約定)の市場。

```cpp
class ContinuousMarket : public Market {
 private:
  OrderBook book_;
  std::unique_ptr<AllocationRule> rule_;
};
```

### 4.2 配分規則

要件 2 の「配分規則(価格時間優先 / プロラタ)は差し替え可能な部品にする」を満たす。

```cpp
class AllocationRule {
 public:
  virtual ~AllocationRule() = default;
  // 反対側の価格レベルに対し、taker の残量をどう配分するかを決める
  virtual void allocate(PriceLevel& level, Qty taker_qty,
                        std::vector<Fill>& out) = 0;
};
```

実装は `PriceTimePriority`(既定)と、後から `ProRata` など。

**テンプレート引数ではなく戦略オブジェクト(仮想関数)にする。** 実行時に
設定から選べる方が実験基盤として使いやすい。Stage 3 で fill 単価が 380 ns
だったので、仮想関数呼び出しの数 ns は誤差の範囲。

上のインターフェースは方針を示す仮のもの。`PriceLevel` を規則に露出させるか、
`OrderBook` の内部構造をどこまで見せるかは実装時に詰める(§10)。

---

## 5. 遅延モデル

エージェントごとの固定値。既定 1、必要なときだけ上書き。

```cpp
class LatencyModel {
 public:
  Timestamp order_delay(AgentId from, MarketId to) const;
  Timestamp notify_delay(MarketId from, AgentId to) const;

  void set_agent_delay(AgentId a, Timestamp d);   // 上書き

 private:
  Timestamp default_delay_ = 1;
  std::unordered_map<AgentId, Timestamp> overrides_;
};
```

実験の大半は既定値のまま使う。HFT と一般投資家を混ぜる実験でだけ、
該当エージェントに小さい値を入れる。

`order_delay` は 1 以上を返す。0 以下は設定時と呼び出し時に検査する(§2.2)。

確率分布から引く形にする拡張は、`LatencyModel` を仮想化して後から足せる。
Stage 4 の初版では固定値のみ。

---

## 6. 乱数ストリーム

要件 4 を満たす具体的な方式。

### 6.1 導出

```cpp
struct StreamKey {
  std::uint64_t agent_id;      // Agent 以外は sentinel
  std::uint64_t component_id;  // 用途ごと
};

Rng make_rng(std::uint64_t master_seed, StreamKey key);
```

`master_seed` と `key` を SplitMix64 で混ぜて 4 × `uint64` の初期状態を作り、
xoshiro256** に渡す。両方とも自前で書ける規模(合計 40 行程度)で、依存追加にならない。

`std::mt19937_64` は状態が 2.5 KB あり、エージェント 1000 体で 2.5 MB になる。
xoshiro256** は 32 B。

`std::seed_seq` は使わない。導出規則が実装に依存し、環境をまたいだ再現が保証されない。

### 6.2 コンポーネント分離

エージェントは用途ごとに別のストリームを引く。

```
component 0: 次回起床間隔
component 1: 発注価格
component 2: 発注数量
component 3: 売買方向
...
```

これがないと、反実仮想実験で「注文を 1 本出さない」ようにしただけで
次回起床時刻までずれる。分離しておけば、行動の一部を変えても
スケジュールは同じまま保たれる。

Market が確率的な規則を持つ場合(呼値のランダム化など)も、
`agent_id = sentinel` の別ストリームとして同じ導出規則で受ける。

### 6.3 保証される性質

- エージェントを 1 体増減しても、他のエージェントのストリームは変わらない
  (ID から導出しているので、逐次的な引き当てがない)
- 注文を 1 本除いても、他のエージェントの乱数引きはずれない
- 同じ `master_seed` と同じ構成なら、実行が完全に再現する

---

## 7. 実行ループ

```cpp
void Kernel::run(Timestamp until) {
  while (!heap_.empty()) {
    const Event& e = heap_.top();
    if (e.time > until) break;
    now_ = e.time;
    dispatch(e);          // variant の種別ごとに型付きメンバ関数へ
    heap_.pop();
  }
}
```

終了条件は 3 通り。

- 時刻 `until` に到達
- ヒープが空になった(全エージェントが起床を予約しなくなった)
- 処理イベント数が上限に達した(暴走検出)

3 番目は実験の安全装置として入れる。

---

## 8. ログとの対応

| ヒープのイベント | ログレコード |
|---|---|
| OrderDelivery(新規) | Add + Fill 0 件以上、または Reject |
| OrderDelivery(取消) | Cancel、または Reject |
| MarketTimeEvent | 時刻レコード(kind 追加) + Fill 0 件以上 |
| AgentWakeup | 記録しない |
| Notification | 記録しない |

起床と通知を記録しないのは、決定論的スケジュールと要件 4 により再現できるため。
`EventKind` は Stage 2 で 4 値だったが、ここで 1 値追加になる(時刻レコード)。
Stage 2 の設計文書に「kind を 4 値で閉じない」と書いた通り。

**リプレイの意味が Stage 4 で変わる。** Stage 2 のリプレイは「ログから板を再構成する」
だったが、Stage 4 では「同じシードで実行し直すと同じログが出る」も検証できる。
後者の方が強い。両方テストする。

---

## 9. テスト

到達点は次の 1 本。

```cpp
TEST_CASE("same seed reproduces identical log and market state") {
  // エージェント複数、市場 1 つ、固定シードで実行
  // 2 回実行して、ログのハッシュと全市場の state_hash() が一致
}
```

その他:

- 同時刻に起こされたエージェントが全員同じ板を観測する
- 時刻 t の起床で出た注文が、時刻 t の他エージェントの観測に影響しない
- `schedule_wakeup(now())` が拒否される
- `set_agent_delay(a, 0)` が拒否される
- エージェントを 1 体増やしても、既存エージェントの乱数列が変わらない
- 反実仮想: 1 エージェントの注文を 1 本だけ抑制した実行で、
  他エージェントの乱数列が同一
- 配分規則を差し替えても、`PriceTimePriority` では既存の板の挙動が変わらない
- Stage 2 のリプレイが Stage 4 のログでも動く

---

## 10. 未決定

- **Config を持つか。** マスターシード、エージェント構成、市場構成、終了条件、
  遅延の上書きを 1 つの構造体にまとめるか、`Kernel` のコンストラクタ引数で受けるか
- **`AllocationRule` の具体的なインターフェース。** `OrderBook` の内部構造を
  どこまで規則に露出させるか。実装時に詰める

---

## 11. やらないこと

- 並行実行(単一スレッド。実取引所も同型 — stage3-book-layout-survey.md §4.1)
- 動的なエージェント・市場の追加削除
- 資金・残高の管理(Agent の領域)
- Oracle(必要なら Agent として外部に置く)
- Intervention イベント(§2.4)
- 確率分布による遅延(固定値のみ。後から拡張可能)
- 注文の永続化・ネットワーク層
