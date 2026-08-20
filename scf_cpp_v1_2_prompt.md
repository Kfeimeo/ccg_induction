# SCF v1.2：合成基准、Gold Evaluator 与结构可识别性实验框架
## C++20 增量开发任务说明

---

# 0. 任务性质

你正在接手一个已经完成 **SCF v1.1** 的 C++20 研究原型。

**不要重写整个项目。**
请在现有 v1.1 代码、数据结构、CLI、测试和 README 之上进行增量开发，完成 **SCF v1.2**。

v1.1 已经完成的核心能力包括：

- finite observed string universe；
- `ContextTriple` / `ConcatTriple`；
- DSU fixed-point equivalence saturation；
- raw surface substitution witness；
- pair-support；
- occurrence-level span evidence；
- maximum-evidence binary tree DP；
- optimal tree count；
- no tie-break ambiguity preservation；
- forced spans among optimal trees；
- `simple.txt` 唯一恢复；
- `deep.txt` 作为 non-identifiability case 输出 ambiguity；
- `cartesian.txt` equivalence 正常；
- hard constraints 与 corpus evidence 解耦。

v1.2 的目标不是继续大幅修改 SCF 核心 parser，而是建立一个可控、可复现、可批量运行的 **synthetic benchmark + gold evaluator + identifiability diagnostics**。

一句话目标：

> Build a controlled synthetic benchmark and gold evaluator for SCF, to measure when direct surface substitution evidence uniquely identifies unlabeled binary structure.

中文：

> 建立可控合成基准与 gold tree 评估器，系统测量 SCF 在不同语料覆盖率、递归深度、结构对称性和词汇歧义条件下，能否唯一识别无标签二叉结构。

---

# 1. v1.2 的核心定位

v1.2 的主线是：

```text
Synthetic grammar
    ↓
Synthetic corpus + gold binary trees
    ↓
SCF v1.1 parser
    ↓
Optimal parse forest
    ↓
Gold evaluator
    ↓
Identifiability report
```

不要把 v1.2 变成：

- CCG category induction；
- neural parser；
- probabilistic parser；
- full real-corpus benchmark；
- large-scale system optimization。

v1.2 的关键科学问题是：

\[
\boxed{
\text{在何种语料覆盖条件下，SCF 的 substitution evidence 能唯一恢复 gold bracketing？}
}
\]

---

# 2. v1.2 必须保持不变的部分

除非为了添加接口或修 bug，否则不要改变 v1.1 的核心算法语义。

## 2.1 Equivalence saturation 保持不变

仍然使用：

\[
E_{t+1}
=
EqClosure
\left(
E_t
\cup
Subst(E_t)
\cup
Concat(E_t)
\right)
\]

其中：

\[
Subst(E)
=
\{
(u,v):
L_1uR_1,L_2vR_2\in\mathcal D,\;
L_1 E L_2,\;
R_1 E R_2
\}
\]

\[
Concat(E)
=
\{
(u,v):
u=ab,\;
v=a'b',\;
aEa',\;
bEb'
\}.
\]

继续使用：

- finite observed string universe；
- strict global equivalence；
- DSU；
- round-based fixed-point；
- provenance；
- no dynamic Trie rewriting；
- no unseen string generation。

## 2.2 Evidence scoring 保持 v1.1 默认

默认仍然使用：

\[
score(o)
=
\max_{
v\neq u,\;
c_o\in W(u,v)
}
|W(u,v)|.
\]

其中 \(W(u,v)\) 必须来自 **direct raw surface contexts**，不得使用 final canonical context class。

## 2.3 Tree objective 保持 v1.1 默认

对 full binary tree：

\[
Score(T)
=
\sum_{[i,j)\in T}
Evidence(i,j).
\]

默认：

- leaf 不计分；
- root 不计分；
- no branching bias；
- tie 保留全部最优树；
- crossing evidence 不等于 conflict。

---

# 3. v1.2 主新增功能

v1.2 必须新增四个主模块：

```text
synthetic/
    grammar representation
    corpus generator
    gold tree generator

evaluation/
    gold tree parser
    span extractor
    optimal forest evaluator
    metrics aggregator

experiments/
    batch runner
    coverage sweep
    seed sweep
    report writer

diagnostics/
    ambiguity diagnostics
    collapse diagnostics
    failure examples
```

可以根据现有项目结构调整路径，但模块职责必须清晰。

---

# 4. 为什么 v1.2 主线使用 CFG-style 合成数据

当前 SCF v1.1 学的是：

\[
\text{surface substitution evidence}
\rightarrow
\text{unlabeled binary bracketing}
\]

不是：

\[
\text{word}
\rightarrow
\text{CCG category}
\]

也不是：

\[
\text{sentence}
\rightarrow
\text{CCG derivation}.
\]

所以 v1.2 主线使用 CFG-style / latent tree grammar synthetic data，原因是：

1. gold tree 明确；
2. 可控 coverage；
3. 可控递归深度；
4. 可控 structural symmetry；
5. 可控 lexical ambiguity；
6. 可以直接评估 unlabeled binary structure；
7. 不引入 CCG derivational ambiguity；
8. 不把 category induction 混入 tree induction。

v1.2 可以新增 **CCG-lite auxiliary generator**，但它只能作为副测试，不得取代 CFG-style 主 benchmark。

---

# 5. 合成数据格式要求

每个合成实验输出至少三个文件：

```text
corpus.txt
gold_spans.tsv
grammar.json
```

可选：

```text
gold_brackets.txt
metadata.json
```

## 5.1 corpus.txt

每行一个 tokenized sentence：

```text
c1 d1 e1 f1
c1 d1 e1 f2
...
```

这是 SCF parser 唯一可见的输入。

不得在 corpus 中包含 gold label、nonterminal 或 bracket。

## 5.2 gold_spans.tsv

每行一个 gold constituent span：

```text
sentence_id    begin    end    label
```

要求：

- `begin/end` 使用 half-open interval `[begin,end)`；
- `label` 只用于评估与 debug；
- SCF parser 不得读取 label；
- evaluator 可以选择忽略 label，只做 unlabeled span evaluation。

示例：

```text
0    0    2    A
0    2    4    B
0    0    4    S
```

注意：

- root `[0,n)` 可以保存；
- leaf 可以不保存；
- evaluator 默认计算 proper nontrivial span 时排除 leaf 和 root。

## 5.3 gold_brackets.txt

可选，每行一个 bracket tree：

```text
((c1 d1) (e1 f1))
```

如果同时提供 `gold_spans.tsv` 与 `gold_brackets.txt`，必须有测试验证二者一致。

## 5.4 grammar.json

记录生成配置，例如：

```json
{
  "grammar_name": "nested_ab_cd_ef",
  "seed": 42,
  "coverage": 0.25,
  "full_sentence_count": 81,
  "sampled_sentence_count": 20,
  "deduplicated": true,
  "nonterminals": ["S", "A", "B", "C", "D", "E", "F"],
  "rules": [
    {"lhs": "S", "rhs": ["A", "B"]},
    {"lhs": "A", "rhs": ["C", "D"]},
    {"lhs": "B", "rhs": ["E", "F"]},
    {"lhs": "C", "rhs": ["c1"]},
    {"lhs": "C", "rhs": ["c2"]}
  ]
}
```

No external JSON library is required; a simple deterministic JSON writer is sufficient.

---

# 6. Synthetic grammar representation

实现一个小型内部 grammar 表示，不需要完整 CFG parser。

建议结构：

```cpp
struct Rule {
    std::string lhs;
    std::vector<std::string> rhs;
};

struct Grammar {
    std::string name;
    std::string start_symbol;
    std::vector<Rule> rules;
};
```

第一版 synthetic grammars 可以硬编码在 C++ 中，也可以通过简单参数构造。
不要求实现通用 JSON grammar parser，但输出 `grammar.json` 必须可读。

---

# 7. Gold tree 表示

必须实现一个内部 gold tree 类型：

```cpp
struct GoldNode {
    std::string label;                 // nonterminal for internal node, terminal token for leaf
    std::vector<GoldNode> children;     // v1.2 benchmark should be binary or terminal
};
```

或者使用 ID-based representation。

要求：

- 所有 benchmark 主线 grammar 都应生成 binary gold tree；
- 若某个 grammar 产生 unary rule，生成器必须在输出 tree 前消除 unary 或将其标记为 unsupported；
- evaluator 默认只评估 binary span structure；
- v1.2 推荐完全避免 unary。

---

# 8. 必须实现的 synthetic grammars

v1.2 至少实现以下 grammar families。

---

## 8.1 `ab_cartesian`

结构：

\[
S \rightarrow A\ B
\]

\[
A \rightarrow a_1|a_2|\cdots|a_m
\]

\[
B \rightarrow b_1|b_2|\cdots|b_n
\]

生成句子：

```text
ai bj
```

gold tree：

```text
(ai bj)
```

用途：

- 验证 equivalence；
- 验证 trivial length-2 parsing；
- 验证 coverage 采样；
- 验证 basic generator/evaluator pipeline。

默认：

```text
m=3
n=3
```

---

## 8.2 `simple_np_vp`

默认直接生成：

```text
the dog runs
a cat runs
the dog sleeps
a cat sleeps
```

结构：

\[
S \rightarrow A\ B
\]

\[
A \rightarrow Det\ N
\]

\[
B \rightarrow V
\]

gold：

```text
((the dog) runs)
((a cat) runs)
((the dog) sleeps)
((a cat) sleeps)
```

用途：

- 回归 v1.1 `simple.txt`；
- 验证 left span evidence > suffix span evidence；
- full coverage 下 unique optimal 应为 100%。

---

## 8.3 `symmetric_abc`

结构上等价于完整：

\[
A \times B \times C
\]

句子：

```text
ai bj ck
```

不提供额外 context 打破歧义。

可以配置 gold tree：

```text
((A B) C)
```

或：

```text
(A (B C))
```

但 evaluator 必须显示：

```text
gold_in_argmax = true
optimal_tree_count = 2
exact_unique_match = false
```

用途：

- 测试 non-identifiability；
- 回归 `deep.txt` 类现象；
- 验证系统不强行 tie-break。

默认：

```text
|A|=2
|B|=2
|C|=2
```

---

## 8.4 `nested_balanced`

结构：

\[
S \rightarrow A\ B
\]

\[
A \rightarrow C\ D
\]

\[
B \rightarrow E\ F
\]

词汇：

\[
C\to c_i,\quad D\to d_j,\quad E\to e_k,\quad F\to f_l.
\]

句子：

```text
ci dj ek fl
```

gold：

```text
((ci dj) (ek fl))
```

用途：

- 测试多层结构；
- 测试 length 4 的 exact match；
- 观察 SCF 是否优先识别 `[0,2)` 与 `[2,4)` 而不是 crossing spans；
- 观察 coverage 变化。

默认：

```text
|C|=|D|=|E|=|F|=2
```

---

## 8.5 `right_branching`

结构：

\[
S \rightarrow A\ X
\]

\[
X \rightarrow B\ Y
\]

\[
Y \rightarrow C\ D
\]

gold：

```text
(A (B (C D)))
```

用途：

- 验证算法不默认 left-branching；
- 如果 evidence 不足，应该保留 ambiguity；
- 如果 context 设计足够，应恢复 right-branching。

---

## 8.6 `left_branching`

结构：

\[
S \rightarrow X\ D
\]

\[
X \rightarrow Y\ C
\]

\[
Y \rightarrow A\ B
\]

gold：

```text
(((A B) C) D)
```

用途：

- 验证算法不默认 right-branching；
- 与 `right_branching` 成对测试。

---

## 8.7 `ambiguous_lexicon`

构造一个 surface token 同时属于两个 latent classes。

例如：

\[
A \rightarrow x|a_1|a_2
\]

\[
B \rightarrow x|b_1|b_2
\]

但 \(A\) 与 \(B\) 出现在不同结构位置。

用途：

- 测试 strict global equivalence 的 failure mode；
- 检查 collapse ratio；
- 检查 largest e-class；
- 检查 gold F1 下降；
- 不要求 v1.2 解决。

此 grammar 的验收不是高准确率，而是 diagnostic 能发现异常。

---

# 9. Coverage sampling

每个 grammar generator 必须支持：

```text
coverage ∈ (0, 1]
seed
max_sentences
```

定义：

1. 先生成 full Cartesian language / full derivation set；
2. 按 seed 随机 shuffle；
3. 取前：

\[
\lceil coverage \cdot |\mathcal L| \rceil
\]

个句子；
4. 如果 `max_sentences` 指定，则再截断；
5. 输出 sampled corpus；
6. metadata 中记录 full count 与 sampled count。

默认 coverage grid：

```text
0.05
0.10
0.20
0.40
0.60
0.80
1.00
```

默认 seeds：

```text
1
2
3
4
5
```

---

# 10. Gold evaluator

新增 evaluator，可以作为：

```bash
scf_eval
```

或者合并进：

```bash
scf_cli --gold gold_spans.tsv --eval
```

推荐支持两种模式：

```bash
scf_cli --input corpus.txt --gold gold_spans.tsv --eval
```

以及 batch runner 中自动调用。

---

# 11. Gold span extraction

对每个 gold tree 提取 proper nontrivial spans：

\[
G_s
\]

默认排除：

- leaf `[i,i+1)`；
- root `[0,n)`。

需要可配置：

```text
include_root_in_eval=false
include_leaves_in_eval=false
```

默认均为 false。

---

# 12. Predicted spans

SCF v1.1 输出的是 optimal parse forest。

对每个 sentence 有最优树集合：

\[
\mathcal T_s^*.
\]

如果：

\[
|\mathcal T_s^*|=1
\]

则取唯一树 span set：

\[
P_s.
\]

如果：

\[
|\mathcal T_s^*|>1
\]

不要随意选一棵作为 prediction。

此时 evaluator 需要计算 ambiguity-aware metrics。

---

# 13. 必须实现的 evaluation metrics

对每个实验输出 corpus-level summary。

## 13.1 `unique_optimal_rate`

\[
\frac{
\#\{s: |\mathcal T_s^*|=1\}
}{
\#\text{sentences}
}
\]

## 13.2 `ambiguous_optimal_rate`

\[
\frac{
\#\{s: |\mathcal T_s^*|>1\}
}{
\#\text{sentences}
}
\]

## 13.3 `exact_unique_match_rate`

只对唯一最优树判断是否：

\[
P_s=G_s.
\]

Corpus-level：

\[
\frac{
\#\{s: |\mathcal T_s^*|=1\land P_s=G_s\}
}{
\#\text{sentences}
}
\]

注意分母是所有句子，不是 unique 子集。

## 13.4 `exact_unique_match_given_unique`

\[
\frac{
\#\{s: |\mathcal T_s^*|=1\land P_s=G_s\}
}{
\#\{s: |\mathcal T_s^*|=1\}
}
\]

若 denominator 为 0，输出 `NA`。

## 13.5 `gold_in_argmax_rate`

定义：

\[
gold\_in\_argmax(s)
\iff
Score(T_{gold})=Score^*(s).
\]

即 gold tree 的 score 等于最优 score。

Corpus-level：

\[
\frac{
\#\{s: gold\_in\_argmax(s)\}
}{
\#\text{sentences}
}
\]

这是 v1.2 最重要的 ambiguity-aware 指标。

例如 `symmetric_abc` 中，即使最优树不唯一，也应有：

```text
gold_in_argmax_rate = 1.0
```

## 13.6 `mean_argmax_size`

\[
\frac{1}{N}
\sum_s
|\mathcal T_s^*|.
\]

## 13.7 `median_argmax_size`

中位数。

## 13.8 `mean_best_score`

平均：

\[
Score^*(s).
\]

## 13.9 `mean_gold_score`

平均 gold tree score。

## 13.10 `mean_margin`

定义：

\[
margin(s)
=
Score^*(s)-SecondBestScore(s).
\]

如果没有 second-best tree，输出 `NA`。

为了可聚合，建议同时输出：

- `mean_finite_margin`
- `zero_margin_rate`

其中：

\[
zero\_margin
\iff
|\mathcal T_s^*|>1.
\]

## 13.11 unlabeled F1 for unique predictions

对于 unique optimal 的句子：

\[
Precision_s=
\frac{|P_s\cap G_s|}{|P_s|}
\]

\[
Recall_s=
\frac{|P_s\cap G_s|}{|G_s|}
\]

\[
F1_s=
\frac{2PR}{P+R}.
\]

需要处理空 span set。

例如长度 2 的句子 proper span 为空：

- 如果 \(P_s=G_s=\emptyset\)，定义 F1=1；
- README 中必须明确。

输出：

```text
mean_unlabeled_precision_given_unique
mean_unlabeled_recall_given_unique
mean_unlabeled_f1_given_unique
```

---

# 14. Gold score 计算

必须实现：

```cpp
uint64_t score_tree(GoldTree tree, SpanScoreTable scores);
```

score 规则必须与 parser 完全一致：

- leaf 不计；
- root 不计；
- proper nontrivial span 加 evidence score。

然后：

```text
gold_in_argmax = (gold_score == best_score)
```

---

# 15. Second-best score

v1.2 需要输出 `second_best_score` 或至少 `margin`。

实现可以使用：

1. DP cell 保存 top-2 distinct scores；
2. 或对 n <= 10 枚举所有 Catalan trees 作 reference；
3. 或仅 batch evaluator 中用 brute force 对小句长求 second-best。

考虑 v1.2 默认 n <= 10，允许先实现 brute-force reference 用于 evaluator，但 README 必须注明不是大规模方案。

推荐：

- 主 TreeSolver 继续使用 DP；
- evaluator/debug 中添加 Catalan enumerator，用于：
  - gold-in-argmax validation；
  - second-best score；
  - tests；
  - small synthetic corpora。

---

# 16. Optimal forest gold membership

对于 n <= 10，最稳妥方式：

1. brute force 枚举所有 full binary trees；
2. 计算每棵树 score；
3. 找 best score；
4. 判断 gold tree score 是否等于 best；
5. 计算 argmax size；
6. 与 DP 输出的 `optimal_tree_count` 做一致性测试。

这能作为 v1.2 评估器的 correctness anchor。

主 parser 仍可使用 DP。

---

# 17. Sentence-level diagnostic output

对每个句子，必须可输出：

```text
sentence_id
sentence
gold_tree
best_score
gold_score
second_best_score
margin
optimal_tree_count
gold_in_argmax
unique_optimal
exact_unique_match
gold_spans
forced_optimal_spans
missing_gold_spans_if_unique_wrong
extra_predicted_spans_if_unique_wrong
```

如果 ambiguous：

```text
tree=<ambiguous>
```

不得输出任意一棵作为 prediction，除非显式：

```text
--dump-one-optimal-tree-for-debug
```

而且必须标注它只是 debug sample。

---

# 18. Experiment batch runner

新增批量实验工具：

```bash
scf_experiment
```

或者在 `scf_cli` 中增加子命令：

```bash
scf_cli generate ...
scf_cli run-experiment ...
```

推荐最小接口：

```bash
scf_experiment \
  --grammar nested_balanced \
  --coverage-grid 0.05,0.10,0.20,0.40,0.60,0.80,1.00 \
  --seeds 1,2,3,4,5 \
  --output-dir results/nested_balanced
```

每个 run 生成：

```text
corpus.txt
gold_spans.tsv
grammar.json
scf_output.txt
metrics.json
sentence_metrics.tsv
saturation.csv
```

并生成总表：

```text
summary.csv
```

---

# 19. summary.csv 字段

至少包含：

```text
grammar
seed
coverage
full_sentence_count
sampled_sentence_count
distinct_strings
context_triples
concat_triples
final_eclasses
collapse_ratio
largest_eclass
largest_eclass_ratio
suspicious_collapse
successful_unions
unique_optimal_rate
ambiguous_optimal_rate
exact_unique_match_rate
exact_unique_match_given_unique
gold_in_argmax_rate
mean_argmax_size
median_argmax_size
mean_best_score
mean_gold_score
zero_margin_rate
mean_finite_margin
mean_unlabeled_precision_given_unique
mean_unlabeled_recall_given_unique
mean_unlabeled_f1_given_unique
unique_correct
unique_wrong
ambiguous_gold_included
ambiguous_gold_excluded
hard_inconsistent
```

---

# 20. JSON metrics output

每次 run 输出：

```text
metrics.json
```

包含：

```json
{
  "grammar": "nested_balanced",
  "seed": 1,
  "coverage": 0.4,
  "corpus": {
    "full_sentence_count": 81,
    "sampled_sentence_count": 33,
    "distinct_strings": 123,
    "context_triples": 456,
    "concat_triples": 789
  },
  "saturation": {
    "final_eclasses": 42,
    "collapse_ratio": 0.31,
    "largest_eclass": 9,
    "largest_eclass_ratio": 0.073,
    "suspicious_collapse": false
  },
  "parsing": {
    "unique_optimal_rate": 0.67,
    "gold_in_argmax_rate": 0.93,
    "mean_argmax_size": 1.8
  }
}
```

No external JSON library is required. A simple writer is enough.

---

# 21. Collapse diagnostics

v1.2 必须增强 collapse diagnostics，因为 strict global equivalence 可能在 ambiguous lexicon 或真实数据下失控。

## 21.1 Top e-classes

```text
top_eclasses.txt
```

按 size 降序输出前 20 个 e-class：

```text
EClass 17 size=42
  "..."
  "..."
```

## 21.2 Largest e-class ratio

\[
largest\_eclass\_ratio =
\frac{
\max_X |X|
}{
|\mathcal S|
}
\]

加入 metrics。

## 21.3 Suspicious collapse flag

如果：

```text
collapse_ratio > 0.8
```

或：

```text
largest_eclass_ratio > 0.25
```

输出：

```text
suspicious_collapse = true
```

阈值可以配置，但默认如上。

---

# 22. Failure diagnostics

若句子 unique optimal 但不等于 gold，输出：

```text
failure_examples.txt
```

每例包含：

```text
sentence_id
sentence
gold_tree
predicted_tree
gold_score
best_score
missing_gold_spans
extra_predicted_spans
span_evidence_table
```

若句子 ambiguous 且 gold 不在 argmax，也输出。

最多输出前 50 个。

---

# 23. Coverage curve expectations

对于不同 grammar，v1.2 不要求所有曲线单调，但必须能输出可分析数据。

尤其：

## `simple_np_vp`

预期较高 coverage 下：

```text
unique_optimal_rate -> 1
gold_in_argmax_rate -> 1
```

## `symmetric_abc`

即使 coverage=1：

```text
gold_in_argmax_rate = 1
unique_optimal_rate < 1
mean_argmax_size ≈ 2
```

## `nested_balanced`

应显示 coverage 对识别率的影响。

## `ambiguous_lexicon`

可能出现：

```text
collapse_ratio ↑
largest_eclass ↑
F1 ↓
```

这不是 v1.2 失败，而是 diagnostic 目标。

---

# 24. CCG-lite auxiliary generator

v1.2 可以实现一个非常受限的 CCG-lite generator，作为副测试。

它不能成为主 benchmark。

## 24.1 支持的 categories

只支持：

```text
NP
N
S
NP/N
S\NP
(S\NP)/NP
```

## 24.2 支持的 rules

只支持：

- forward application；
- backward application。

禁止：

- type raising；
- composition；
- coordination；
- extraction；
- polymorphic category；
- punctuation；
- modifiers；
- semantic terms。

## 24.3 Lexicon 示例

```text
the : NP/N
a   : NP/N
dog : N
cat : N
john : NP
mary : NP
runs : S\NP
sleeps : S\NP
likes : (S\NP)/NP
sees  : (S\NP)/NP
```

生成：

```text
the dog runs
john sees mary
the cat likes john
```

## 24.4 Gold tree

CCG-lite generator 必须将 derivation 投影为 ordinary binary bracket。

但 README 必须明确：

> This is not full CCG induction. It is only a bracketing sanity check for application-only CCG fragments.

## 24.5 验收

CCG-lite 输出只要求 pipeline 能运行，并报告 metrics。
不要用它替代 CFG-style benchmark。

---

# 25. Real-data smoke test

v1.2 可以实现一个低优先级真实数据预处理工具：

```bash
scf_prepare_text --input raw.txt --output corpus.txt
```

功能：

- split lines / simple sentence split；
- ASCII punctuation strip 可选；
- lowercase 可选；
- whitespace tokenization；
- filter length <= max_len；
- deduplicate；
- drop empty lines；
- optionally drop sentences with digits / rare symbols。

默认：

```text
max_len=10
lowercase=true
strip_punctuation=true
deduplicate=true
```

真实数据 v1.2 只做 smoke test，不作为成功标准。

输出：

```text
real_smoke_report.txt
```

包含：

```text
input_sentence_count
kept_sentence_count
filtered_long
filtered_symbols
distinct_tokens
distinct_strings
collapse_ratio
largest_eclass
unique_optimal_rate
ambiguous_optimal_rate
top_eclasses
example_trees
```

不要声称真实语料 parse accuracy，除非提供 gold。

---

# 26. v1.2 不应实现的内容

不要实现：

- full CCG induction；
- CCG category learning；
- neural scorer；
- probabilistic model；
- EM；
- MDL objective；
- DreamCoder；
- biclique mining as main algorithm；
- real Treebank reader；
- PTB official evaluation；
- tokenizer dependency；
- parallelization；
- external sort；
- Trie optimization；
- GPU；
- web download；
- automatic dataset acquisition。

---

# 27. 必须新增的测试

## 27.1 Generator tests

- `ab_cartesian` 生成句子数正确；
- `nested_balanced` full Cartesian count 正确；
- coverage sampling deterministic under seed；
- coverage=1 输出 full set；
- sampled corpus 无重复；
- gold span 与 bracket 一致；
- root span 正确；
- proper spans 正确。

## 27.2 Evaluator tests

- gold tree span extraction；
- exact match；
- F1；
- empty proper span case；
- gold score；
- gold-in-argmax；
- argmax size；
- margin；
- unique vs ambiguous cases。

## 27.3 Parser/evaluator integration tests

### simple

```text
unique_optimal_rate = 1
exact_unique_match_rate = 1
gold_in_argmax_rate = 1
```

### symmetric_abc

With coverage=1:

```text
gold_in_argmax_rate = 1
ambiguous_optimal_rate > 0
```

If full symmetry is exactly preserved, expect:

```text
unique_optimal_rate = 0
mean_argmax_size = 2
```

### nested_balanced

For full coverage, expected behavior must be explicitly documented after first empirical run.

If SCF cannot uniquely identify all structures, do not fudge tests.
Set tests to check consistency of metrics and gold-in-argmax rather than impossible exact recovery.

### ambiguous_lexicon

Must produce diagnostic output and not crash.

## 27.4 Regression tests

All v1.1 tests must still pass:

- `cartesian.txt`;
- `simple.txt`;
- `deep.txt`;
- witness tests;
- tree optimization tests;
- hard crossing constraint tests;
- multi-round cascade test.

---

# 28. Handling non-identifiability

Evaluator must treat non-identifiability as a first-class outcome.

Do not mark ambiguous outputs as simple failures.

For each sentence, classify:

```text
UNIQUE_CORRECT
UNIQUE_WRONG
AMBIGUOUS_GOLD_INCLUDED
AMBIGUOUS_GOLD_EXCLUDED
HARD_INCONSISTENT
```

Corpus summary should count each class.

Suggested fields:

```text
unique_correct
unique_wrong
ambiguous_gold_included
ambiguous_gold_excluded
hard_inconsistent
```

This is more informative than only exact match.

---

# 29. Gold-in-argmax implementation detail

For small n <= 10, implement:

```text
gold_score = score(gold_tree)
best_score = solver.best_score
gold_in_argmax = (gold_score == best_score)
```

This is sufficient even if argmax forest not explicitly enumerated.

But tests should verify that `optimal_tree_count` from DP equals brute-force argmax count for small cases.

---

# 30. Best margin

To compute `second_best_score`, recommended simple implementation:

For n <= 10:

1. enumerate all full binary trees;
2. score all;
3. sort distinct scores descending;
4. best = first;
5. second = second if exists;
6. margin = best - second.

If only one distinct score, set:

```text
second_best_score = NA
margin = NA
all_trees_tied = true
```

For reporting, also output:

```text
zero_margin_rate = optimal_tree_count > 1
```

---

# 31. Brute-force Catalan enumerator

Add a brute-force tree enumerator for n <= 10 for testing/evaluation.

It should return compact span-set trees, not pointer-heavy objects.

Possible representation:

```cpp
struct BinaryTree {
    std::vector<Span> spans;   // include proper spans; optional root
    std::string bracket;       // optional debug
};
```

Use dynamic recursion:

```text
Trees(i,j) =
  if j=i+1: leaf
  else for k in (i+1..j-1):
      combine Trees(i,k) and Trees(k,j)
```

For n=10 Catalan(9)=4862, acceptable.

Do not use this enumerator in large future modes, but v1.2 default synthetic max length can stay <= 10.

---

# 32. Reproducibility

Every generated dataset must record:

- grammar name;
- grammar parameters;
- seed;
- coverage;
- full sentence count;
- sampled sentence count;
- code version if available;
- timestamp optional。

If timestamp is included, it must not affect deterministic data generation.

---

# 33. CLI proposal

At minimum provide:

## Generate

```bash
scf_generate \
  --grammar nested_balanced \
  --coverage 0.4 \
  --seed 42 \
  --output-dir data/generated/nested_balanced_cov040_seed42
```

Outputs corpus/gold/metadata.

## Run with eval

```bash
scf_cli \
  --input data/generated/.../corpus.txt \
  --gold data/generated/.../gold_spans.tsv \
  --stats \
  --eval \
  --dump-evidence \
  --dump-trees
```

## Batch experiment

```bash
scf_experiment \
  --grammar nested_balanced \
  --coverage-grid 0.05,0.10,0.20,0.40,0.60,0.80,1.00 \
  --seeds 1,2,3,4,5 \
  --output-dir results/nested_balanced
```

If separate executables are inconvenient, implement subcommands in one executable:

```bash
scf_cli generate ...
scf_cli parse ...
scf_cli experiment ...
```

Choose the approach that minimally disrupts existing code.

---

# 34. Output directory layout

For batch runs:

```text
results/
  nested_balanced/
    summary.csv
    cov_0.05_seed_1/
      corpus.txt
      gold_spans.tsv
      grammar.json
      metrics.json
      sentence_metrics.tsv
      saturation.csv
      top_eclasses.txt
      failure_examples.txt
    cov_0.05_seed_2/
      ...
```

---

# 35. README updates

README must add:

1. v1.2 purpose;
2. why synthetic benchmark first;
3. why CFG-style data is mainline;
4. why CCG-lite is only auxiliary;
5. definition of gold-in-argmax;
6. difference between exact unique match and ambiguity-aware success;
7. non-identifiability interpretation;
8. how to run generator;
9. how to run evaluator;
10. how to run batch experiment;
11. how to interpret summary.csv;
12. known limitations.

---

# 36. IMPLEMENTATION_NOTES.md updates

Add:

1. generator architecture;
2. grammar families implemented;
3. gold tree/span format;
4. evaluator design;
5. brute-force enumerator role;
6. metric definitions;
7. ambiguity classification;
8. coverage sampling method;
9. collapse diagnostics;
10. CCG-lite limitations;
11. real-data smoke-test limitations;
12. current performance limits.

---

# 37. Acceptance criteria

v1.2 is complete only if all conditions below hold.

## 37.1 Build/test

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

passes.

## 37.2 Existing v1.1 behavior unchanged

The previous v1.1 synthetic files still behave as expected:

### cartesian

```text
unique_optimal = 9
ambiguous_optimal = 0
hard_conflicted = 0
```

### simple

```text
unique_optimal = 4
ambiguous_optimal = 0
hard_conflicted = 0
trees:
  ((the dog) runs)
  ((a cat) runs)
  ((the dog) sleeps)
  ((a cat) sleeps)
```

### deep

```text
unique_optimal = 0
ambiguous_optimal = 8
hard_conflicted = 0
optimal_tree_count = 2 per sentence
tree=<ambiguous; no tie-break>
```

## 37.3 Generator works

At least these commands work:

```bash
scf_generate --grammar ab_cartesian --coverage 1.0 --seed 1 --output-dir data/generated/ab

scf_generate --grammar symmetric_abc --coverage 1.0 --seed 1 --output-dir data/generated/sym

scf_generate --grammar nested_balanced --coverage 0.4 --seed 1 --output-dir data/generated/nested
```

Each output dir contains:

```text
corpus.txt
gold_spans.tsv
grammar.json
```

## 37.4 Evaluator works

At least:

```bash
scf_cli --input data/generated/ab/corpus.txt --gold data/generated/ab/gold_spans.tsv --eval --stats

scf_cli --input data/generated/sym/corpus.txt --gold data/generated/sym/gold_spans.tsv --eval --stats
```

produce metrics.

## 37.5 Batch runner works

At least:

```bash
scf_experiment \
  --grammar simple_np_vp \
  --coverage-grid 0.2,0.6,1.0 \
  --seeds 1,2 \
  --output-dir results/simple_np_vp
```

produces:

```text
summary.csv
```

with required fields.

## 37.6 Ambiguity is correctly reported

For `symmetric_abc` coverage=1, output must indicate structural ambiguity rather than forcing a branch.

Expected:

```text
gold_in_argmax_rate = 1.0
ambiguous_optimal_rate > 0
```

## 37.7 No hidden tie-break

Tests must fail if the parser silently chooses a left/right tree under tied evidence.

## 37.8 Metrics deterministic

Running the same experiment twice with the same seed must produce identical:

```text
corpus.txt
gold_spans.tsv
metrics.json
summary.csv row
```

excluding optional timestamp fields.

---

# 38. Suggested implementation order

## Milestone 1：Gold tree/span infrastructure

- `GoldNode`;
- span extraction;
- bracket parser or writer;
- span TSV writer/reader;
- tests.

## Milestone 2：Synthetic generator

- `ab_cartesian`;
- `simple_np_vp`;
- `symmetric_abc`;
- `nested_balanced`;
- deterministic coverage sampling;
- metadata writer;
- tests.

## Milestone 3：Evaluator

- gold score;
- exact unique match;
- gold-in-argmax;
- F1;
- ambiguity classification;
- sentence metrics;
- tests.

## Milestone 4：Brute-force enumerator

- enumerate binary trees for n <= 10;
- score all trees;
- argmax count validation;
- second-best/margin;
- tests.

## Milestone 5：Batch runner

- coverage grid;
- seed grid;
- output dirs;
- summary.csv;
- metrics.json.

## Milestone 6：Diagnostics

- top eclasses;
- suspicious collapse;
- failure_examples;
- sentence-level debug.

## Milestone 7：Optional CCG-lite and real smoke

- CCG-lite generator;
- real text preparation;
- README caveats.

---

# 39. Important edge cases

## 39.1 Length 1

No binary tree internal structure.

Evaluation should not crash.

## 39.2 Length 2

Only one binary tree.

Proper nontrivial span set is empty if root excluded.

If gold also has no proper span, F1 can be defined as 1.

## 39.3 All trees tied

If all scores are 0:

```text
optimal_tree_count = Catalan(n-1)
gold_in_argmax = true
exact_unique_match = false unless n <= 2
```

This is not an error.

## 39.4 Gold tree malformed

If gold spans do not form a valid projective binary tree, evaluator should report a clear error.

## 39.5 Sentence mismatch

If `corpus.txt` sentence count and `gold_spans.tsv` sentence IDs mismatch, fail loudly.

---

# 40. Research interpretation required in reports

Experiment report should explicitly distinguish:

```text
UNIQUE_CORRECT
UNIQUE_WRONG
AMBIGUOUS_GOLD_INCLUDED
AMBIGUOUS_GOLD_EXCLUDED
HARD_INCONSISTENT
```

Do not reduce everything to “accuracy”.

This project studies identifiability.
Ambiguity with gold included is a meaningful result, not simply failure.

---

# 41. Final warning

Do not optimize away ambiguity.

Do not add hidden biases.

Do not use gold labels during parsing.

Do not use grammar nonterminals as input to SCF.

Do not make CCG-lite the main benchmark.

Do not claim real-data parsing accuracy without gold.

v1.2 should make the research question measurable:

\[
\boxed{
\text{When does surface substitution evidence determine unlabeled binary structure, and when is structure underdetermined?}
}
\]
