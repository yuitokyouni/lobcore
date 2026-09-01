# Stage 2 設計: 時刻とイベントログ

Stage 1 の板 (`OrderBook`) に時刻を導入し、その外側に追記専用のイベントログを置く。
到達点は「ログをリプレイした板の状態ハッシュが、元の実行と一致する」ことをテストで示すこと。

---

## 1. 時刻

```cpp
using Timestamp = std::int64_t;
```

単位は定義しない。日足なら 1 単位 = 1 日、レイテンシ実験なら 1 単位 = 1 ナノ秒。
実単位への変換は境界で 1 回だけ行う。価格をティック数で持つのと同じ考え方。

`Order` に発注時刻を足す。受付時刻は市場が付けるので引数で渡す。

```cpp
struct Order {
  OrderId   id;
  Side      side;
  Price     price;
  Qty       qty;
  Timestamp decided_at;   // エージェントが発注を決めた時刻
};

std::vector<Trade> OrderBook::add_limit(const Order& order, Timestamp received_at);
```

Stage 1 では `decided_at == received_at` になるが、フィールドは分けておく。

### 単調性

- `received_at` が直前の呼び出しより小さい場合は契約違反。拒否カウンタで数える (`non_monotonic_timestamp`)。約定も resting もせず空の trades を返す。
- `decided_at` の逆転は正常。レイテンシがあれば決定順と到着順は一致しない。
- `decided_at > received_at`（未来に決めた注文が過去に届く）は契約違反。同じカウンタで数える。

### 既存テストの扱い

`tests/test_book.cpp` の 14 本は本文を変更しない。ファイル先頭のヘルパだけを変える。

```cpp
Order buy(OrderId id, Price p, Qty q);   // 時刻を自動採番
Order sell(OrderId id, Price p, Qty q);
```

**採番カウンタは TEST_CASE ごとにリセットすること。** ファイルスコープの静的変数にすると、
Catch2 がテスト順を無作為化するため実行ごとに時刻の値が変わる。決定論を損なう。

時刻を明示するテストには別ヘルパを用意する。

```cpp
Order buy_at(OrderId id, Price p, Qty q, Timestamp decided_at);
```

---

## 2. 状態ハッシュ

```cpp
std::uint64_t OrderBook::state_hash() const noexcept;
```

板の中に実装する。public API として出す。Stage 4 で実験の再現性検証に使う。

### 対象

| 対象 | 含める | 備考 |
|---|---|---|
| 価格レベルの並びと各レベルの注文列 (id, qty, seq) | ○ | 板そのもの |
| `next_seq_` | ○ | 含めないと拒否された注文の有無が検出できない |
| `rejects_` | ○ | 実行の再現性として一致すべき |
| `locations_` | ✕ | 板から導出できるので冗長。§5 の整合性テストで別途検査 |

### 実装

FNV-1a を自前で書く。`std::hash` は実装依存で、Linux と macOS で値が異なりうるため使わない。
走査は `bids_` → `asks_` → `next_seq_` → `rejects_` の順。
`std::map` と `std::deque` の走査順は決定的なので、この順序が固定されていれば値も決定的になる。

---

## 3. イベントログ

### 置き場所

板の外に薄いラッパを置く。板は無変更。

```cpp
class LoggedBook {
 public:
  std::vector<Trade> add_limit(const Order& order, Timestamp received_at);
  bool cancel(OrderId id, Timestamp received_at);

  const std::vector<LogRecord>& log() const noexcept;
  const OrderBook& book() const noexcept;

 private:
  OrderBook book_;
  std::vector<LogRecord> log_;
};
```

受付時点の best bid / ask は `add_limit` を呼ぶ前に読む。
ログを取らない実験ではラッパを外すだけで済む。

### レコード

固定長。全イベント共通の 1 レイアウト。union は使わない。
使わないフィールドはゼロで埋める。空間効率は Stage 3 で測ってから考える。

```cpp
enum class EventKind : std::uint8_t { Add = 0, Fill, Cancel, Reject };

struct LogRecord {
  std::uint64_t seq;              // 受付順の連番。Fill には taker の seq が入る
  EventKind     kind;
  Side          side;
  RejectReason  reason;           // Reject 以外は None
  Timestamp     decided_at;
  Timestamp     received_at;      // Fill ではこれが約定時刻を意味する
  OrderId       order_id;         // Fill では taker
  OrderId       maker_id;         // Fill 以外は 0
  Price         price;            // Add は指値、Fill は約定価格
  Qty           qty;              // Add は発注数量、Fill は約定数量
  Price         best_bid_price;   // 受付時点
  Qty           best_bid_qty;     // 0 なら板が空
  Price         best_ask_price;
  Qty           best_ask_qty;     // 0 なら板が空
};
```

板が空の場合は数量 0 で表す。価格に番兵は置かない。

### 1 回の呼び出しが生むレコード

`add_limit` がスイープして 3 件約定した場合、レコードは 4 件。

```
Add  (seq=N)
Fill (seq=N)
Fill (seq=N)
Fill (seq=N)
```

`seq` は「何番目に受け付けた注文か」であり、ログの行番号とは別の概念。
Fill レコードの `seq` にはそれを引き起こした taker の `seq` が入る。

拒否された注文は Reject レコードを 1 件出す。`seq` は消費しない (板の `next_seq_` は進まない)。

---

## 4. リプレイ

ログから Add / Reject / Cancel を順に抜き出し、新しい板に流し込む。
Fill は Add の再実行で自然に再現されるため流さない。

Reject を流す理由: `state_hash()` に `rejects_` が含まれるため、
拒否も再現しないとハッシュが一致しない。Reject レコードには
拒否された注文の内容 (side / price / qty / decided_at / received_at) が
すべて記録されており、そのまま `add_limit` に流し直せば同じ理由で拒否される。

```cpp
OrderBook replay(const std::vector<LogRecord>& log);
```

---

## 5. テスト

`tests/test_book.cpp` に追加するもの:

- 受付時刻の単調性違反が拒否される (カウンタが増える、板が変わらない)
- `decided_at > received_at` が拒否される
- `decided_at` の逆転は受理される
- `locations_` の整合性: 板を走査して索引を再構築し、実際の `locations_` と一致する
- 同じ操作列を 2 つの板に流すと `state_hash()` が一致する
- 1 操作だけ違う操作列では `state_hash()` が異なる

新規ファイル `tests/test_log.cpp`:

- Add 1 件でレコードが 1 件出る
- スイープで Add 1 件 + Fill n 件が出る
- 拒否で Reject レコードが 1 件出る、`seq` が消費されない
- 受付時の best bid / ask がレコードに正しく入る
- **リプレイした板の `state_hash()` が元の板と一致する**

最後の 1 本が Stage 2 の到達点。

---

## 6. やらないこと

- ファイルへの書き出し (メモリ上の `std::vector<LogRecord>` に留める)
- スナップショット
- 深さ N の板状態記録 (best bid / ask の 1 段のみ)
- CSV への変換 (必要になったら後から足す。正本の設計に影響しない)
- 空間効率の最適化 (Stage 3 で測ってから)
