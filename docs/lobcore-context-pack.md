# lobcore Context Pack

lobcore（LOB マッチングエンジン → 離散イベント市場シミュレータ核 → pybind11）のための文献・資料集。
タグの意味: [読む] = 人間が精読する / [参照] = 必要箇所を引く・エージェントの文脈に入れる / [データ] = 入手先。

エージェントに渡す優先順位は「仕様 > 設計の先例 > 論文」。
論文は人間用で、そこから抽出した決定だけを CLAUDE.md と設計文書に書く。
エージェントの文脈に丸ごと入れる価値があるのは §1 と §4 の資料のみ。
Stage 2・4 の設計要件（TCA 対応ログ / 注文駆動・時間駆動の Market / リプレイ入力 / 分離された乱数ストリーム）は CLAUDE.md の「先行固定する設計要件」で確定済み。§7 がその裏付け文献。

---

## 1. 中核 — Stage 1–3 の土台

### Gould, Porter, Williams, McDonald, Fenn, Howison — "Limit Order Books" (2013) [読む: §2 / 参照: 残り]
- Quantitative Finance 13(11), 1709–1742 / arXiv: https://arxiv.org/abs/1012.0349
- LOB の標準サーベイ。§2 が LOB の数学的定義（用語・優先ルール・状態）で、lobcore の仕様書の語彙をここに揃える。後半は実証的性質（stylized facts）とモデルのレビューで、Stage 4 以降のエージェント評価の物差し。

### WK Selph — "How to Build a Fast Limit Order Book" (2011) [読む]
- 原ブログは消滅。ミラー: https://gist.github.com/halfelf/db1ae032dc34278968f8bf31ee999a25
- アーカイブ: https://web.archive.org/web/20110314042933/http://howtohft.wordpress.com/
- 板のデータ構造設計の古典。価格レベル・注文・ID 索引の 3 構造と各操作の計算量要件を短くまとめている。Stage 1 の private を設計する前ではなく、std::map 実装で全テストを通した後に読むこと。

### Martin Fowler — "The LMAX Architecture" (2011) [読む]
- https://martinfowler.com/articles/lmax.html
- 単一スレッドのビジネスロジック + イベントソーシング + リプレイという構成の原典解説。Stage 2（追記専用ログ、状態ハッシュ一致、スナップショット）の設計思想はここから来ている。

---

## 2. 核（カーネル）設計の先例 — Stage 4 の設計文書用

比較すべき 5 点: (1) 時間モデル (2) エージェント呼び出し (3) 注文/約定の表現 (4) 決定論の保証 (5) ログ形式。
各先例を CLAUDE.md の要件 1–4 に照らして評価する: (1) は要件 2、(3) は要件 3、(4) は要件 4、(5) は要件 1 に対応。

### ABIDES — Byrd, Hybinette, Balch (2019) [参照]
- 論文: https://arxiv.org/abs/1904.12066 / エージェント構成の解説: https://arxiv.org/abs/1909.11650
- 原リポジトリ（更新停止）: https://github.com/abides-sim/abides
- 現行の開発元: https://github.com/jpmorganchase/abides-jpmc-public
- Kernel + レイテンシモデル付きメッセージング。メッセージ設計は ITCH/OUCH を模す。abides-core / abides-markets の分割が「核と市場モジュールの分離」の実例。

### MAXE — Belcak, Calliess, Zohren (2020) [参照]
- 論文: https://arxiv.org/abs/2008.07871 / リポジトリ: https://github.com/maxe-team/maxe
- C++ 核 + Python API。lobcore の到達点に最も近い。核は「時間を進めてメッセージを配送する」だけで、取引所もエージェントの一種。マッチング規則（価格時間優先 / プロラタ）が差し替え可能。保守状況は要確認。

### PAMS — Hirano, Takata, Izumi (2023) [参照]
- 論文: https://arxiv.org/abs/2309.10729 / リポジトリ: https://github.com/masanorihirano/pams / ドキュメント: https://pams.hirano.dev/
- Plham の設計思想を継ぐ Python 実装。時間モデルはステップ型（各ステップで一定数のエージェントを無作為に呼び出して発注させる）。Simulator / Runner / Session / Market / Agent / EventHook の分担を 5 点比較の基準列にする。

### exchange-core（Java） [参照]
- https://github.com/exchange-core/exchange-core
- イベントソーシング（ジャーナリング + リプレイ + スナップショット）、浮動小数点不使用、決定論的マッチング、板の Naive / Direct 二重実装、テスト分類（unit / integration / stress / integrity）。Stage 2–3 の要件定義として README を読む。Java コードは読まなくてよい。

### JAX-LOB — Frey et al. (2023) [参照]
- 論文: https://arxiv.org/abs/2308.13289 / リポジトリ: https://github.com/KangOxford/jax-lob
- 数千の板を GPU で並列処理する対極の設計（固定長配列、全分岐実行）。パラメータスイープが主目的化したときに C++ 逐次実装と比較検討する材料。

---

## 3. C++ 実装のリファレンス — Stage 1–3

### CppTrader（chronoxor） [参照]
- https://github.com/chronoxor/CppTrader
- マッチングエンジン・板・NASDAQ ITCH ハンドラ。市場マネージャを通常 / 最適化 / 積極最適化の 3 段で実装しベンチマークを並べる構成が Stage 3 の手本。
- 派生（学位論文: gperftools でのプロファイリング → データ構造置換 → スループット評価）: https://github.com/kasselouris/CppTrader

### Carl Cook — "When a Microsecond Is an Eternity: High Performance Trading Systems in C++" (CppCon 2017) [視聴]
- YouTube で題名検索。低レイテンシ C++ の入門講演。lobcore に全部は要らないが、Stage 3 で「何を測るか」の感覚を作る。

### ツール公式ドキュメント [参照]
- pybind11: https://pybind11.readthedocs.io/ （Stage 5。特に "Functions > Return value policies" と NumPy 連携の章）
- Catch2: https://github.com/catchorg/Catch2/tree/devel/docs
- Google Benchmark: https://github.com/google/benchmark/blob/main/docs/user_guide.md

---

## 4. データとプロトコル — 副案 B / Stage 3 の現実的注文流

### Nasdaq TotalView-ITCH 5.0 仕様書 [参照]
- https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf
- バイナリメッセージ仕様（ビッグエンディアン、ナノ秒タイムスタンプ、stock locate）。パーサを書くならこれが仕様書。エージェントに渡す場合は Add Order / Execute / Cancel / Delete / Replace の節だけ抜粋する。lobcore の内部メッセージ語彙はこの 5 種と LOBSTER の 7 イベント型を表現できる上位集合にする（要件 3）。

### サンプルデータ [データ]
- Nasdaq ITCH サンプルファイル: https://emi.nasdaq.com/ITCH （CppTrader のベンチマークが使っているのと同じ）
- LOBSTER（メッセージ + 板スナップショット形式、サンプルあり）: https://lobsterdata.com/
- Binance Spot WebSocket Streams（diff depth。公開・API キー不要）: developers.binance.com の Spot API ドキュメント内 "WebSocket Streams" の項

---

## 5. 日本語の文脈 — 研究としての位置づけ

### 水田孝信 — 講義資料「人工市場による市場制度の設計」(2026-05) [読む]
- https://www.docswell.com/s/mizutata/5JWX2M-2026-05-19-215749
- 人工市場研究のレビュー + 呼値（ティックサイズ）変更という実際の制度変更を題材にした介入設計の解説。lobcore が最終的に支えるべき研究の型がこれ。researchmap: https://researchmap.jp/mizutatakanobu

### JPX ワーキングペーパー [参照]
- https://www.jpx.co.jp/corporate/research-study/working-paper/
- 人工市場による制度分析の実例集。例: Vol.37 メイカー・テイカー制の市場間シェア分析、Vol.38 ショートサイドの市場非効率性。介入シナリオの品揃えとして眺める。

---

## 6. エージェントモデル側 — Stage 4–5 で使う（書誌のみ）

- Chiarella & Iori, "A simulation analysis of the microstructure of double auction markets", Quantitative Finance 2(5), 2002 — 最小構成の LOB 型 ABM の古典。移植第一号の候補。
- Cont, "Empirical properties of asset returns: stylized facts and statistical issues", Quantitative Finance 1(2), 2001 — stylized facts の標準リスト。シミュレータ出力の検証項目表として使う。
- Speculation Game（Katahira & Chen）一連 — 修論の移植対象。手元の文献をそのまま設計文書に添付する。
- 水田・和泉・八木・吉村「人工市場を用いた値幅制限・空売り規制・アップティックルールの検証と最適な制度の設計」電気学会論文誌C 133(9), 2013 — 日本語での介入検証研究の型。

---

## 7. 先行固定した要件の裏付け — CLAUDE.md「先行固定する設計要件」1–4 に対応

### 要件 1: 再実行なしで TCA が出せるイベントログ
- Perold, "The Implementation Shortfall: Paper versus Reality", Journal of Portfolio Management 14(3), 1988 — 実装ショートフォールの原典。ログに何が要るか（決定時刻の価格・受付時刻の価格・約定価格）はこの分解から逆算する。
- Almgren & Chriss, "Optimal Execution of Portfolio Transactions", Journal of Risk 3(2), 5–39, 2001 — 執行アルゴをエージェントとして載せるときの基準モデル。
- Tóth, Lempérière, Deremble, de Lataillade, Kockelkoren, Bouchaud, "Anomalous Price Impact and the Critical Nature of Liquidity in Financial Markets", Physical Review X 1, 021006, 2011 / Bouchaud, Bonart, Donier, Gould, *Trades, Quotes and Prices*, Cambridge University Press, 2018 — 平方根インパクト則。ABM 型バックテストのキャリブレーション目標。
- LOBSTER のメッセージ形式（§4。7 種のイベント型）— ログのスキーマの先例。

### 要件 2: 注文駆動と時間駆動を両方受ける Market
- Budish, Cramton, Shim, "The High-Frequency Trading Arms Race: Frequent Batch Auctions as a Market Design Response", Quarterly Journal of Economics 130(4), 2015 — 時間駆動で清算する市場の代表例。
- 水田・小杉・楠本・松本・和泉・八木・吉村, "Effects of Price Regulations and Dark Pools on Financial Market Stability: An Investigation by Multiagent Simulations", Intelligent Systems in Accounting, Finance and Management 23(1–2), 97–120, 2016 — 複数市場（lit + dark）と参照価格の先例。
- MAXE（§2）— 価格時間優先 / プロラタの差し替え。PAMS の Session（区間ごとに約定の可否を切り替える設計）— 時間駆動の最小例。
- CME Group "Matching Algorithms"（FIFO / Pro-Rata / Allocation などの配分規則の一覧。サイト内検索で到達）

### 要件 3: リプレイ入力
- §4 の ITCH 5.0 仕様書と LOBSTER。内部メッセージ語彙は ITCH の Add / Execute / Cancel / Delete / Replace の上位集合にする。
- ABIDES（§2）の ITCH / OUCH を模したメッセージ設計。

### 要件 4: 分離された乱数ストリーム
- Salmon, Moraes, Dror, Shaw, "Parallel Random Numbers: As Easy as 1, 2, 3", SC '11, 2011 — カウンタ型乱数（Random123）。値が (シード, ストリーム ID, 引き番号) の関数で決まるので、要件 4 を最も素直に満たす。
- O'Neill, PCG: https://www.pcg-random.org/ — ストリーム分割できる小型生成器。std::mt19937_64 + std::seed_seq でエージェント ID から派生させる方法でも可。

---

## 使い方の指針

1. 人間の精読は 3 本だけ: Gould §2、WK Selph、Fowler LMAX。残りは辞書として引く。
2. claude.ai プロジェクトのナレッジに入れるのはこのファイル + lobcore の README + 設計文書。論文 PDF を丸ごと入れない（薄めるだけ）。
3. コーディングエージェントの文脈に入れるのは CLAUDE.md + tests/test_book.cpp + 必要時に §1・§4 の該当箇所。
4. Stage 4 設計時に §2 の 4 プロジェクトについて 5 点比較表を作り、それを設計文書の第 1 章にする。
5. Stage 2・4 に入る前に §7 を読み、CLAUDE.md の要件 1–4 をそれぞれ実行可能なテストに落とす（例: 要件 1 はログ再生の状態ハッシュ一致、要件 4 は注文一つを除いた実行で他エージェントの乱数列が不変）。
