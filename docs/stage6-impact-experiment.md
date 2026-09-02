# Stage 6 設計（草案）: 単一注文インパクトの反実仮想測定

Stage 5 までで「Python から再現可能な実験」が回る。Stage 6 の到達点は
**同一シード・同一背景市場のもとで、特定注文 1 本（または meta-order 1 件）を
除いた反実仮想実行と比較し、価格経路への影響を分解できること**。

実データでは同じ市場を 2 回走らせられない。lobcore の要件 4（乱数ストリーム分離）と
Stage 4 §2.4（反実仮想フィルタ）が正面から向き合う問い。

---

## 0. 背景文献（要約）

- **Vytelingum et al. (2025)** [arXiv:2505.15296](https://arxiv.org/abs/2505.15296): baseline / counterfactual を同一 RNG で走らせ $\zeta = \zeta_{MR} + \zeta_{MI}$ を分解。方法論の直接的手本。
- **Chiarella–Iori 系**: fundamentalist / chartist / noise で構造を持った背景板（Phase 1）。
- **GPIF / NBIM**: 実務 TCA・implementation shortfall。反実仮想シミュレーションではないが指標語彙の参照。
- **水田孝信**: World + Experimental 枠組み。執行評価でインパクト未考慮の最適化は定まらない、と指摘。

---

## 1. 実験枠組み

### 1.1 World / Experimental

```
World agents     … Fundamentalist / Chartist / Noise（+ optional MM）
Experimental     … 測定対象の Impact / Execution agent
```

### 1.2 反実仮想ペア

| 実行 | 内容 |
|---|---|
| **Baseline (B)** | `suppress_agent(id)` で Experimental の submit をすべて捨てる |
| **Factual (F)** | 通常実行 |

`on_wakeup` と `rng` は両実行で同一。捨てた注文はログに残さない（B は「注文が存在しなかった世界」）。

---

## 2. フェーズ計画

### Phase 0 — 核の不足を埋める ✅ 完了（PR #19）

| 項目 | 内容 | 完了条件 |
|---|---|---|
| P0-1 | `Kernel::suppress_agent(AgentId)` | `test_repro`: 抑制しても全 agent の rng 列不変。介入前 log 完全一致 |
| P0-2 | `Experiment.run_pair(suppress=...)` | F/B を 1 呼び出しで返す |
| P0-3 | `mid_series(log)` | pytest |
| P0-4 | `filter_log_exclude_*` / `log_before_time` | F の log から Experimental 由来を除き B と一致 |

**Phase 0 最重要テスト:** 介入時刻 $t_0$ より前は F/B の log が**バイト単位で一致**すること。
$\epsilon > 0$ の余地はない。ずれれば要件 4 が破れている。

### Phase 1 以降 — 実験側（別リポジトリ）

**Phase 1 以降の World エージェント・ImpactExperiment・可視化・PoC 実行は
lobcore には入れない。** 実験リポジトリで実装する（§4）。

Chiarella–Iori の簡略版（効用最大化は Phase 5 へ）。

```python
class Fundamentalist(Agent):
    def on_wakeup(self, view, ctx):
        f = fundamental_series(view.now)  # exogenous
        ...
```

**$f_t$ の生成（Stage 4 との整合）:** Oracle は核に入れない。$f_t$ は
**sentinel ストリーム**（`Kernel.sentinel_rng(component)`、`kSentinelAgentId` から導出）
で生成し、全 Fundamentalist が同じ系列を参照する。Fundamentalist を 1 体増減しても
$f_t$ がずれないようにする。

**Chartist の価格履歴:** 現在の `View` は最良気配のみ。Phase 1 では
**エージェント自身が起床のたびに mid を記録**して履歴を持つ（核変更なし）。
起床間隔が長い Chartist は粗い履歴になる — パラメータ設計に含める。
将来必要なら `BatchObservation` に直近 N mid を足す拡張。

Phase 1 パラメータ例: $N_f, N_c, N_n$、起床間隔、発注サイズ、chartist 履歴長。

### Phase 2 — Experimental（単一インパクト注文）

### Phase 3 — 指標

$\Delta(t) = m_F(t) - m_B(t)$。Implementation shortfall 等。

### Phase 4 — スイープ（サイズ、規則、平方根則照合）

---

## 3. 最初の PoC（Phase 3 以降・実験側）

**成功基準:** $t < t_0$ では F/B の log がバイト単位で一致（`log_before_time` + `logs_byte_equal`）。
$t \ge t_0$ では $\Delta(t) = m_F(t) - m_B(t)$ が Experimental 実行窓で非ゼロ。

---

## 4. 実装の分担

### 4.1 lobcore 側（完了）

反実仮想実験を **支援するインフラ**。モデル実装は置かない。

| 部品 | 役割 |
|---|---|
| `Kernel::suppress_agent` | 注文抑制フィルタ（Baseline = 注文が存在しなかった世界） |
| `Kernel::sentinel_rng` | exogenous 系列（$f_t$ 等）用ストリーム |
| `Experiment.run_pair` / `run(suppress=...)` | F/B ペア実行 |
| `lobcore.analysis` | `mid_series`・`logs_byte_equal`・`filter_log_exclude_*` 等のログ操作 |

```cpp
kernel.suppress_agent(agent_id);  // submit を捨てる。on_wakeup / rng は通常
kernel.sentinel_rng(component);   // exogenous f_t 用
```

```python
pair = Experiment(...).run_pair(suppress_agent_ids=[impact_agent_id])
from lobcore import log_before_time, filter_log_exclude_order_ids, mid_series
```

足りない機能が見つかったら lobcore に PR を出す。

### 4.2 実験側 — `financial-abm-lab` / `experiments/YH012`

**新規リポジトリは作らない。** 既存の研究モノレポ
[`financial-abm-lab`](https://github.com/yuitokyouni/financial-abm-lab) の研究ライン
**YH012** に置く（YH007–010 と同じ規約: `experiments/YH0xx/` に spec・スクリプト・レポートを同居）。

```
financial-abm-lab/
  experiments/YH012/          # 単一注文インパクト反実仮想（lobcore 利用）
    README.md
    specs/                    # プレレジ・HANDOFF
    agents.py                 # Fundamentalist, Chartist, NoiseTrader
    experiment.py             # ImpactExperiment（run_pair を使う側）
    plot.py
    configs/
      poc_seed42.yaml
    tests/                    # モデル実装のテスト。lobcore CI には入らない
  packages/                   # （必要なら再利用コアを昇格。初版は YH012 内で足りる）
```

依存: `pyproject.toml` から lobcore をローカル参照（例:
`lobcore @ file:///.../lobcore/python`）または editable install。

**Phase 1 以降はすべて YH012 側:**

- Fundamentalist / Chartist / NoiseTrader
- ImpactExperiment・可視化・PoC 実行
- モデル実装のテスト

lobcore にはモデル実装を入れない。本文書（`docs/stage6-impact-experiment.md`）は
「lobcore が反実仮想実験をどう支援するか」の設計として lobcore 側に残す。

YH012 の scaffold は lobcore 側の整理（本節）が済んでから、
`financial-abm-lab` 側に別途投げる。

---

## 5. 参考文献

- Vytelingum et al. (2025): https://arxiv.org/abs/2505.15296
- Chiarella, Iori & Perelló (2009): https://doi.org/10.1016/j.jedc.2008.08.001
- 水田孝信: https://mizutatakanobu.com/2026r.pdf
