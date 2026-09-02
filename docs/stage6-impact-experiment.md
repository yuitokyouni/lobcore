# Stage 6 設計（草案）: 単一注文インパクトの反実仮想測定

Stage 5 までで「Python から再現可能な実験」が回る。Stage 6 の到達点は
**同一シード・同一背景市場のもとで、特定注文 1 本（または meta-order 1 件）を
除いた反実仮想実行と比較し、価格経路への影響を分解できること**。

実データでは同じ市場を 2 回走らせられない。lobcore の要件 4（乱数ストリーム分離）と
Stage 4 §2.4（反実仮想フィルタ）が正面から向き合う問い。

---

## 0. 背景文献

### 0.1 構造を持った人工市場（エージェント群）

ノイズトレーダーだけでは価格は乱歩するだけで、インパクト測定の「背景」にならない。
まず **fundamentalist / chartist / noise** が相互作用する板が必要。

| 文献 | 要点 | lobcore への示唆 |
|---|---|---|
| Chiarella & Iori (2002), *Quantitative Finance* | ダブルオークション ABM。chartist が fat tail・ボラクラスタの主因 | 最小構成の原型 |
| Chiarella, Iori & Perelló (2009), *JEDC* 33(3) | 効用最大化発注、異質リスク回避、異質時間軸 | Phase 5 の拡張目標 |
| Raberto et al. (2001), *Physica A* | 初期の financial ABM 系譜 | 比較用 |
| 水田孝信, *人工市場による市場制度の設計* (2024/2026 スライド・書籍) | 制度設計向け ABM。World Agent + Experimental Agent 構成 | §1.2 の実験枠組み |
| 和泉・Chiarella 系 効用関数発注 | 板寄せを含む価格決定（水田 2012 人工知能学会誌など） | 将来の発注サイズモデル |

### 0.2 反実仮想によるインパクト分解

| 文献 | 要点 | lobcore への示唆 |
|---|---|---|
| Vytelingum et al. (2025), [arXiv:2505.15296](https://arxiv.org/abs/2505.15296) | **baseline（注文なし）と counterfactual（注文あり）** を同一 RNG・同一 exogenous signal で実行。$\zeta = \zeta_{MR} + \zeta_{MI}$。$p_{MI}(t) = p_t - p_t^B$ | **方法論の直接的手本** |
| Vytelingum et al. (2025), [arXiv:2510.22206](https://arxiv.org/abs/2510.22206) | 上記 ABM を執行 RL の環境に。baseline 戦略生成 | 執行最適化は Phase 4 以降 |
| Bouchaud et al., *Trades, Quotes and Prices* (2018) / Tóth et al. (2011) | 平方根インパクト則 | サイズスイープのキャリブレーション目標（context pack §文献） |
| BruceBrasseur/market-impact-sim | noise + value + 1 impact order、baseline 比較 | 最小 PoC の参考実装 |
| Coletta et al. (2023) | 指値もインパクトあり（見せ玉分析） | 指値インパクト実験の動機（水田スライド §引用） |

### 0.3 実務（GPIF / NBIM）— 反実仮想とは別レイヤ

巨大運用者の公開資料は **「同一シードで注文 1 本を除く」** というシミュレーション反実仮想ではない。
ただし TCA・インパクトへの関心の構造は参考になる。

| 主体 | やっていること | lobcore との関係 |
|---|---|---|
| **GPIF** | 外部運用者に TCA・執行方針登録を義務化。自前は Bloomberg **BTCA** で実行分析。研究はイベントスタディ（保有開示の異常リターン） | 実績ベンチマーク比較。反実仮想シミュレーションではない |
| **NBIM** | **Implementation shortfall**（決定時刻価格 vs 約定価格）を明示コストとして計測・低減。電子取引・透明性 waiver・バッチオークションで自然流動性 | 指標定義（§3）の実務語彙。エンゲージメント効果は **対照群** による反実仮想（別問題） |
| **水田** | 執行アルゴ評価で World Agent（背景）+ Experimental Agent（戦略）。インパクト未考慮のバックテスト最適化はパラメータが定まらない、と指摘 | lobcore の Experimental + 反実仮想 pair が狙い |

---

## 1. 実験枠組み

### 1.1 World / Experimental の二層

水田・Vytelingum 系に合わせ、エージェントを二層に分ける。

```
World agents（背景市場）
  ├─ Fundamentalist  …  exogenous fundamental f_t への回帰
  ├─ Chartist          …  短期リターンのモメンタム / 逆張り
  ├─ Noise             …  スプレッド近傍のランダム指値
  └─ (optional) Market maker … 両側クォート

Experimental agent（測定対象）
  └─ Impact / Execution agent … 1 meta-order（単発 or TWAP 分割）
```

**World だけ**で Phase 1 を回し、板が「構造を持つ」ことを確認してから
Experimental を載せる。

### 1.2 反実仮想ペア

同一 `ExperimentMeta`（seed, rule, agent_config, end_time）で 2 回:

| 実行 | 内容 |
|---|---|
| **Baseline (B)** | Experimental agent の注文を **すべて抑制**（または agent 自体を無効化） |
| **Factual (F)** | Experimental agent が通常通り発注 |

lobcore 要件:

- `master_seed` 同一
- World 各 agent の `ctx.rng(component)` 列が F/B で一致（要件 4）
- Experimental を除いても World の起床スケジュールがずれない（component 分離）

Stage 4 §2.4 の **実行前フィルタ** で注文抑制する設計。イベントとしての Intervention は使わない。

---

## 2. フェーズ計画

### Phase 0 — 核の不足を埋める（最優先）

| 項目 | 内容 | 完了条件 |
|---|---|---|
| P0-1 | 注文抑制フィルタ（agent_id + order_id または submission 連番） | C++ テスト: 1 本抑制して他 agent の RNG 列不変 |
| P0-2 | Python `Experiment.run_counterfactual(suppress=...)` またはペア API | 同一 meta で F/B を 1 関数で返す |
| P0-3 | ログから mid 時系列抽出（`best_bid/ask` から `(bid+ask)/2`） | ユニットテスト |
| P0-4 | F/B ペアの `log` diff ユーティリティ | 抑制注文だけが消えることを確認 |

**既存:** Stage 5 の再現性テスト、component 分離設計。**未実装:** P0-1〜2。

### Phase 1 — 最小構造市場（MVP World）

Chiarella–Iori の **簡略版**（効用最大化は Phase 5 へ回す）。

```python
# イメージ（Python Agent）
class Fundamentalist(Agent):
    def on_wakeup(self, view, ctx):
        f = fundamental_oracle(view.now)  # exogenous, 全 agent 共通
        mid = quote_mid(view)
        if mid < f - band: ctx.submit(0, "buy", ...)
        elif mid > f + band: ctx.submit(0, "sell", ...)
        ctx.schedule_wakeup(view.now + delta)

class Chartist(Agent):
    def on_wakeup(self, view, ctx):
        # 直近 log からリターン符号 → 順張り指値
        ...

class NoiseTrader(Agent):
    def on_wakeup(self, view, ctx):
        r = ctx.rng(0)
        ...
```

**パラメータ（`agent_config` に記録）:**

- 各タイプの人数 \(N_f, N_c, N_n\)
- 起床間隔分布（component 0）
- 発注サイズ（component 2）、価格オフセット（component 1）
- fundamental の漂移（共有 exogenous 系列 \(f_t\)）

**合格基準（Phase 1 出口）:**

1. spread > 0 が定常的に存在
2. 取引量 > 0、価格が単純乱歩ではない（fundamental への弱いアンカー）
3. 10 シードで qualitative に同種の統計（平均 spread、ボラのオーダー）
4. `Experiment` 2 回再現（Stage 5 到達点の継続）

### Phase 2 — Experimental（単一インパクト注文）

1 体の `ImpactAgent` を追加:

- 時刻 \(t_0\) にサイズ \(Q\) の買い meta-order（最初は **1 本限り**）
- 約定方式: 最良 ask 付近の aggressive limit または複数 tick 内分割（Phase 2b）

ログから Experimental の `order_id`・Fill 系列を特定。

### Phase 3 — 反実仮想ペアと指標

F/B を走らせ、時系列 \(m_F(t), m_B(t)\)（mid）を計算。

**一次指標:**

| 記号 | 定義 |
|---|---|
| 瞬間インパクト | \(\Delta(t) = m_F(t) - m_B(t)\) |
| 実行窓平均 | \(\bar{\Delta}_{[t_0, t_1]} = \mathrm{mean}_{t \in [t_0,t_1]} \Delta(t)\) |
| 永続インパクト | \(\Delta_{\infty} = m_F(t_0 + W) - m_B(t_0 + W)\)（\(W\) = 十分な decay 窓） |
| Implementation shortfall（買い） | \(\mathrm{IS} = \frac{1}{Q}\sum_k p_k q_k - m_B(t_0)\) |

**二次指標（ログから直接）:**

- Fill 件数・板深さ（best qty）の F/B 差
- 他 World agent の Fill 価格分布の変化

**分解（Vytelingum 式の語彙）:**

- \(m_B(t)\) … マーケットリスクのみの参照経路
- \(m_F(t) - m_B(t)\) … インパクト成分（シミュレーションでは厳密に分離可能）

### Phase 4 — スイープと妥当性

| 実験 | 目的 |
|---|---|
| \(Q\) スイープ | 平方根則との乖離を定性的に確認 |
| 実行タイミング \(t_0\) スイープ | 板状態依存性 |
| `price_time` vs `pro_rata` | 配分規則がインパクト曲線に与える影響 |
| 指値 vs aggressive | Coletta 系の「指値インパクト」 |

### Phase 5 — フル C&I（効用最大化発注）

JEDC 2009 モデルに近づける。キャリブレーション目標:

- fat tail、ボラクラスタ（chartist 比率）
- order flow 統計
- Bouchaud 系メタオーダーインパクトスケーリング

---

## 3. 最初の PoC 実験（1 本）

**設定:**

```
seed = 42
end_time = 500_000（核の時刻単位。起床間隔と整合）
rule = price_time
World: N_f=20, N_c=30, N_n=50（要チューニング）
Experimental: 1 agent, t_0=200_000, Q=500, buy, aggressive
```

**手順:**

1. Phase 1 パラメータで World のみ burn-in（Experimental なし）→ spread/ボラ確認
2. F: World + Impact 実行
3. B: 同一 config、`suppress={impact_agent_id: all}` で実行
4. \(\Delta(t)\) をプロット。\(t_0\) 前後で \(\Delta \approx 0\)、実行中に \(\Delta > 0\)（買い）を確認
5. `write_log_file` で F/B ペアを保存（`agent_config` に suppress フラグも記録）

**成功:** \(t < t_0\) で \(\max|\Delta(t)| < \epsilon\)（Experimental 未介入）、\(t \in [t_0, t_1]\) で \(\bar{\Delta} > 0\)。

---

## 4. 実装優先順位（lobcore）

1. **C++ 注文抑制フィルタ** + `test_repro` に反実仮想 RNG テスト追加
2. **Python `Fundamentalist` / `Chartist` / `NoiseTrader`**（`python/src/lobcore/agents/` 新設）
3. **`ImpactExperiment`** ヘルパ（F/B ペア + mid 抽出 + \(\Delta(t)\)）
4. **可視化スクリプト**（`python/scripts/plot_impact.py` — 依存は matplotlib のみ提案）
5. Phase 5 以降: C++ エージェント化はボトルネックが見えてから

---

## 5. やらないこと（Stage 6 初期）

- 実データ LOBSTER/ITCH キャリブレーション（Stage 3 リプレイは別線）
- 深さ N 板の観測
- 多銘柄・多市場
- 執行 RL（Vytelingum 2025 の延長）
- GPIF/NBIM 実績データとの直接比較（指標定義の参照に留める）

---

## 6. 参考文献（リンク）

- Chiarella & Iori (2002): https://doi.org/10.1088/1469-7688/2/5/303
- Chiarella, Iori & Perelló (2009): https://doi.org/10.1016/j.jedc.2008.08.001
- Vytelingum et al. (2025) ABM liquidity: https://arxiv.org/abs/2505.15296
- Vytelingum et al. (2025) RL execution: https://arxiv.org/abs/2510.22206
- 水田孝信, 人工市場による市場制度の設計: https://mizutatakanobu.com/2026r.pdf
- GPIF Operation Policy: https://www.gpif.go.jp/en/info/operation_policy_20220601.pdf
- NBIM Investing in equities: https://www.nbim.no/contentassets/06d92cb612a84db39dba4aa34b9a7651/investing-in-equities_government-pension-fund-global_web.pdf
