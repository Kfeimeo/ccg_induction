# SCF v2.4 — Counterexample-Guided Closed-World Refinement

基于现有 v2.3.1 代码，**停止 `shared context -> merge candidate` 这一逻辑**，改成 closed-world contextual equivalence。

定义经验接受函数：

\[
Accept_D(s)=1 \iff s\in D,
\]

否则：

\[
Accept_D(s)=0.
\]

对当前候选字符串 \(u,v\)，定义 bounded empirical equivalence：

\[
u\equiv_D v
\iff
\forall (L,R)\in\mathcal C_D,\;
Accept_D(LuR)=Accept_D(LvR).
\]

这里没有频率、threshold 或 similarity score。

## 核心算法

实现 **partition refinement / counterexample-guided splitting**，不要继续沿用 v2.3 transactional merging 作为主算法。

对于一个 context：

\[
c=(L,R),
\]

定义测试：

\[
T_c(u)=\mathbf1[LuR\in D].
\]

对当前 block \(B\)，若：

\[
B_1=\{u\in B:T_c(u)=1\},
\qquad
B_0=B\setminus B_1
\]

都非空，则 \(c\) 是一个 distinguishing context，并执行：

\[
B\rightarrow B_1\sqcup B_0.
\]

反复寻找 splitter，直到 fixed point。

不需要枚举所有不存在的字符串。优先从 corpus 中已经观察到的 positive frames `(L,R)` 生成 tests，然后仅对当前 block 成员做 membership lookup：

```text
L + candidate + R in corpus?
```

可以选择优先处理最能 split 当前 block 的 context，例如最大化：

\[
\min(|B_0|,|B_1|),
\]

但该评分仅用于搜索效率，不属于 category definition。

## Important semantics

- `not observed` 在本版本中显式作为 closed-world negative。
- 这是 empirical language \(D\) 上的 equivalence，不声称等于真实自然语言 grammaticality。
- 随 corpus scale 增加，旧 negative 可能变 positive，因此不同 scale 的 partition 需要重新计算，不能把旧 split 当永久公理。
- `(ε,ε)` 只是一位 terminal test：
  - complete span → 1
  - non-complete span → 0
  - 它绝不能再生成 complete-sentence pairwise clique。

## Context universe

先实现一个明确、可审计的 bounded universe。

至少比较：

```text
A. all observed exact frames
B. internal frames only: L != ε and R != ε
C. all frames including sentence boundaries
```

不要引入 context abstraction。

candidate substring inventory 沿用当前 length 1..3 设置，除非工程上必须参数化。

## Synthetic oracle tests

新增 deterministic synthetic tests：

1. `dog/cat` 在所有 bounded contexts 上行为一致 → same class。
2. `Mary/swimming` 或等价构造：共享至少一个 positive context，但存在另一个 `(1,0)` distinguishing context → different classes。
3. 两个完整句子都满足 `(ε,ε)=1`，但其他 behavior 不同 → 不得因 terminal test merge。
4. observationally indistinguishable pair → 必须 merge。
5. 构造 `D_small ⊂ D_large`：
   - small corpus 因 closed-world false negative 把两个真实同类对象 split；
   - larger corpus 补上 missing positive 后允许它们 merge。
6. 与 brute-force full signature enumeration 比较，optimized refinement 输出必须完全一致。

## Real corpus experiment

使用 peS2o structured preprocessing，不再做新的 corpus ablation。

跑：

```text
1e5
2e5
4e5
1e6
```

如性能允许再扩。

报告：

```text
initial_objects
number_of_context_tests
number_of_effective_splitters
number_of_refinement_rounds
final_classes
largest_class_ratio
median/p95 class size
membership_queries
runtime / memory
partition change across scales
```

external diagnostics 继续使用 POS，仅 evaluation：

```text
within-class POS purity
pairwise same-POS precision
largest class examples
```

另外输出至少 20 个实际 distinguishing contexts：

```text
u
v
L
R
Accept(LuR)
Accept(LvR)
```

重点确认以前的：

```text
<num> / conclusions / introduction / short complete spans
```

不会再仅因 `(ε,ε)` 落入同一个类。

## Efficiency

membership lookup 应使用 hash/trie/index，不允许物化所有负例。

核心复杂度优化方向：

```text
positive frame -> splitter test
current block -> only query members of that block
cache membership results
avoid O(|objects|^2) pair generation
```

v2.3.1 的 empty-frame quadratic clique 应消失。

## Output

生成：

```text
SCF_V2_4_CLOSED_WORLD_REFINEMENT_REPORT.md
closed_world_scaling.csv
distinguishing_contexts.txt
class_examples.txt
oracle_comparison.txt
```

报告最后回答：

1. closed-world contextual signatures 是否消除了 v2.3 的 false-merge mechanism？
2. `(ε,ε)` hub 是否彻底消失？
3. 不使用任何统计频率时，是否形成 linguistically meaningful classes？
4. internal-only vs all-frame context universe 的差别是什么？
5. 随数据增加，主要现象是 false splits 被修复还是出现新的 distinctions？
6. optimized counterexample-guided refinement 是否与 brute-force signature partition 完全等价？

保持 deterministic，所有旧 tests 继续通过。