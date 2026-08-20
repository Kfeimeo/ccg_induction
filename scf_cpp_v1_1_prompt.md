# SCF v1.1：从硬约束改为证据最大化的无标签二叉结构发现
## C++20 增量开发任务说明

---

# 0. 任务性质

你正在接手一个已经完成 **SCF v1** 的 C++20 研究原型。

**不要重写整个项目。**
请在现有 v1 代码、数据结构、测试和命令行接口之上进行增量修改，完成 **SCF v1.1**。

v1 已经实现并验证了以下模块：

- token / string interning；
- finite observed string universe；
- `ContextTriple`；
- `ConcatTriple`；
- DSU / Union-Find；
- `ContextSubstitution`；
- `ConcatCongruence`；
- round-based fixed-point saturation；
- e-class 输出；
- union provenance；
- occurrence-level evidence projection；
- binary tree counting；
- crossing hard-constraint detection；
- forced span；
- CLI / synthetic corpus 测试。

目前 v1 的核心问题不是 equivalence saturation，而是：

> **错误地把 contextual substitutability 直接解释成 hard constituency constraint。**

v1.1 的目标是修正这一点。

---

# 1. 必须保持不变的第一阶段

第一阶段：

```text
Surface corpus
    ↓
Observed finite string universe
    ↓
Context Substitution
    ↓
Concat Congruence
    ↓
DSU fixed-point saturation
    ↓
final equivalence relation E*
```

原则上保持不变。

数学上：

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

以及：

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

- finite observed universe；
- strict global equivalence；
- DSU；
- `find(left), find(right)` 动态 canonicalization；
- round-based saturation；
- provenance；
- no dynamic Trie rewriting；
- no creation of unseen strings。

除非为了适配接口，否则不要重构该部分算法语义。

---

# 2. v1 暴露的问题

考虑：

```text
the dog runs
a cat runs
the dog sleeps
a cat sleeps
```

v1 正确发现：

```text
the dog ≡ a cat
runs ≡ sleeps
dog runs ≡ dog sleeps
cat runs ≡ cat sleeps
```

但是 v1 随后把所有这些 substitution occurrence 都设为 hard constituents，因此对于：

```text
the dog runs
```

同时要求：

```text
[0,2) = "the dog"
[1,3) = "dog runs"
```

两个 span crossing，导致：

```text
consistent_tree_count = 0
crossing_conflict = true
```

这不是 equivalence solver 的 bug。

真正错误的是推理：

\[
u\equiv_c v
\Rightarrow
u,v\text{ 必须是 constituents}.
\]

v1.1 必须改成：

\[
\boxed{
\text{substitution}
\Rightarrow
\text{constituency evidence}
}
\]

而不是 hard constraint。

---

# 3. v1.1 的核心原则

整个 pipeline 改为：

```text
Equivalence Saturation
        ↓
Direct Surface Substitution Witnesses
        ↓
Weighted Constituent Candidates
        ↓
Maximum-Evidence Binary Tree Solver
        ↓
Optimal Parse Forest
```

必须严格区分：

1. `String equivalence`
2. `Direct substitution witness`
3. `Constituent candidate`
4. `Hard constituent`
5. `Optimal tree`
6. `Forced span among optimal trees`

其中 corpus 自动产生的 substitution evidence **默认不是 hard constraint**。

---

# 4. 最关键的新概念：独立 surface witness

v1.1 不再简单使用：

```text
canonical context contains >1 yields
=> hard constituent
```

而要计算 **direct raw surface substitution witnesses**。

设 raw context：

\[
c=(L,R)
\]

这里的：

\[
L,R
\]

必须是原始 surface `StringId`。

**不得使用最终 DSU representative 代替 raw context。**

如果 raw corpus 中存在：

\[
L u R
\]

和：

\[
L v R
\]

且：

\[
u\neq v,
\]

则这个 raw context：

\[
c=(L,R)
\]

是 pair：

\[
(u,v)
\]

的一个 direct substitution witness。

定义：

\[
W(u,v)
=
\{
c:
L u R,\ L v R\in\mathcal D
\}.
\]

然后：

\[
support(u,v)=|W(u,v)|.
\]

这里的 context 必须按 **surface identity** 去重。

---

# 5. 为什么 witness 必须使用 raw surface context

以：

```text
the dog runs
a cat runs
the dog sleeps
a cat sleeps
```

为例。

对于：

```text
the dog
a cat
```

存在：

```text
(_, runs)
(_, sleeps)
```

两个不同 raw surface contexts。

所以：

\[
support(\text{the dog},\text{a cat})=2.
\]

虽然 saturation 最终可能得到：

```text
runs ≡ sleeps
```

从而两个 context 在 quotient algebra 中变成同一 canonical context class，

**但 tree evidence 中仍必须计为两个独立 raw witnesses。**

这是有意设计，而不是 bug。

原因：

> constituency evidence 需要衡量一组 surface substitutions 在多少个独立观察环境中重复出现，而不是 saturation 以后还剩多少 canonical context classes。

因此必须同时维护两套概念：

```text
canonical context
```

用于 equivalence saturation；

```text
raw surface context
```

用于 tree evidence。

不要混淆。

---

# 6. Derived equality 不自动产生 direct tree witness

如果 saturation 通过连锁更新得到：

```text
c1 ≡ c2
```

且：

```text
c1 -> {u, v}
c2 -> {v, z}
```

最终可以由 equivalence engine 推出：

```text
u ≡ z
```

这仍然是合法的结构等价推理。

但是：

```text
u ≡ z
```

**不得因此自动获得 direct substitution witness。**

除非 corpus 中确实存在某个 exact raw context：

```text
L u R
L z R
```

否则：

\[
W(u,z)
\]

不增加。

因此：

```text
equivalence evidence
```

和：

```text
constituency witness
```

必须彻底解耦。

---

# 7. Pair witness 的构建方式

不要做 sentence-pair O(N²) 比较。

继续使用已经存在的 raw `ContextTriple`。

首先按 exact surface key：

```cpp
RawContextKey {
    StringId left;
    StringId right;
};
```

group。

这里：

```cpp
left
right
```

不得调用 `dsu.find()`。

得到：

```text
raw context c
    -> distinct yields {u1, u2, ..., uk}
```

然后对这个 bucket 中的 distinct yields 生成 unordered pairs：

\[
(u_i,u_j),\quad i<j.
\]

对于每个 pair：

```text
pair (u_i, u_j)
    add witness c
```

同一个 raw context 对同一个 yield pair 最多贡献 1 个 witness。

重复句子 / occurrence frequency 不增加 support。

---

# 8. Pair key

定义 canonical surface pair：

```cpp
struct YieldPair {
    StringId first;
    StringId second;
};
```

要求：

```text
first < second
```

使：

```text
(u,v)
(v,u)
```

为同一个 key。

可实现 hash / ordering。

第一版数据规模较小，允许：

```text
sort + group
```

或：

```text
unordered_map<YieldPair, ...>
```

优先 correctness 和可解释性。

---

# 9. 建议的数据结构

增加类似：

```cpp
using RawContextId = std::uint32_t;

struct RawContext {
    StringId left{};
    StringId right{};
};

struct PairWitness {
    StringId first{};
    StringId second{};
    std::vector<RawContextId> contexts;
};
```

或者更紧凑的等价实现。

还需支持查询：

```cpp
std::size_t pair_support(StringId a, StringId b) const;
```

以及：

```cpp
std::span<const RawContextId>
pair_witnesses(StringId a, StringId b) const;
```

---

# 10. Occurrence-level constituent evidence

v1.1 仍然坚持：

> 不因为一个 surface yield 在某处得到 evidence，就把其所有 occurrence 自动设为 constituent candidate。

对具体 occurrence：

```cpp
Occurrence o;
```

设：

```text
u = o.yield
c = (o.left_context, o.right_context)
```

只有当存在某个：

\[
v\neq u
\]

满足：

\[
c\in W(u,v)
\]

时，该 occurrence 才有 direct constituent evidence。

即：

\[
Candidate(o)
\iff
\exists v\neq u:
c_o\in W(u,v).
\]

---

# 11. v1.1 的 evidence score

对 occurrence：

\[
o=(u,c_o)
\]

定义：

\[
score(o)
=
\max_{
v\neq u,\;
c_o\in W(u,v)
}
|W(u,v)|.
\]

如果不存在这样的：

\[
v,
\]

则：

\[
score(o)=0.
\]

这是 v1.1 的默认离散评分函数。

它不是概率。

它表示：

> 当前 occurrence 所参与的 direct substitution relation 中，最强的一组替代关系在多少个独立 raw surface contexts 中重复出现。

---

# 12. 为什么使用 max，而不是 sum

第一版必须使用：

\[
\boxed{\max}
\]

不要使用：

\[
\sum_v support(u,v).
\]

原因：

- e-class / paradigm 大小时 sum 会天然偏大；
- 大 lexical class 会因为 alternatives 多而获得不合理优势；
- v1.1 首先希望衡量“最稳定的一条 substitution relation”；
- `max support` 更容易解释和单元测试。

未来版本可以实验：

```text
sum
top-k sum
log support
biclique size
MDL
```

但 v1.1 不做。

---

# 13. example：simple corpus

Corpus：

```text
the dog runs
a cat runs
the dog sleeps
a cat sleeps
```

应该有：

\[
W(\text{the dog},\text{a cat})
=
\{
(\epsilon,\text{runs}),
(\epsilon,\text{sleeps})
\}
\]

所以：

\[
support=2.
\]

因此 occurrence：

```text
[0,2) "the dog"
```

在：

```text
the dog runs
```

中 score：

```text
2
```

因为其 raw context：

```text
(_, runs)
```

属于该 witness set。

另一方面：

\[
W(\text{dog runs},\text{dog sleeps})
=
\{
(\text{the},\epsilon)
\}
\]

所以：

```text
[1,3) "dog runs"
```

score：

```text
1
```

于是：

```text
((the dog) runs)
```

应该优于：

```text
(the (dog runs))
```

---

# 14. Tree solver 的语义必须改变

v1 的 tree solver 输入是：

```text
hard spans
```

然后：

```text
crossing => inconsistent
```

v1.1 默认 pipeline 不再这样使用。

改成：

```text
span evidence score
```

然后求：

\[
\boxed{\text{maximum-evidence binary tree}}
\]

---

# 15. Tree scoring

对于 sentence：

\[
s=w_0\dots w_{n-1}
\]

每个 span：

\[
[i,j)
\]

有：

\[
e_s(i,j)\in\mathbb N.
\]

其中来自第 11 节的 occurrence evidence。

Tree score：

\[
Score(T)
=
\sum_{[i,j)\in T}
e_s(i,j).
\]

但是：

### 不计 leaf

如果：

\[
j-i=1
\]

则 tree score 强制：

\[
e_s(i,j)=0.
\]

原因：

所有 full binary trees 都包含 leaf。

### 不计 root

如果：

\[
[i,j)=[0,n)
\]

则 tree score 强制：

\[
0.
\]

原因：

所有 full binary trees 都包含 root。

因此真正参与比较的只有：

\[
\boxed{
2\le j-i<n
}
\]

的 proper nontrivial spans。

EvidenceBuilder 可以保留 leaf/root witness 供分析，但 TreeSolver 必须忽略其 score。

---

# 16. 为什么简单求和合理

对长度：

\[
n
\]

的 full binary tree，内部节点总数固定为：

\[
n-1.
\]

去除固定 root 后，每棵树都有相同数量的 proper internal constituents：

\[
n-2.
\]

所以：

\[
\sum e(i,j)
\]

不会因为某棵 full binary tree“节点更多”而天然占优。

---

# 17. Maximum-evidence CKY-style DP

定义：

```text
best[i][j]
```

为 span `[i,j)` 的最大 subtree evidence score。

叶：

\[
best[i,i+1]=0.
\]

非叶：

\[
best[i,j]
=
spanScore(i,j)
+
\max_{i<k<j}
\left[
best[i,k]+best[k,j]
\right].
\]

其中：

```text
spanScore(i,j) = evidence(i,j)
```

但如果：

```text
[i,j) == root
```

则：

```text
spanScore = 0
```

---

# 18. 必须保存全部最优 split

不能只保存：

```text
argmax k
```

而要保存：

```cpp
std::vector<std::uint16_t> optimal_splits;
```

所有满足最大值的：

\[
k
\]

都保留。

因为：

```text
tie == structural ambiguity
```

不得随便 tie-break。

---

# 19. Optimal tree count

DP cell 还需要：

```text
count[i][j]
```

表示达到：

```text
best[i][j]
```

的 subtree 数量。

叶：

\[
count[i,i+1]=1.
\]

对每个 optimal split：

\[
k
\]

贡献：

\[
count[i,k]\cdot count[k,j].
\]

所以：

\[
count[i,j]
=
\sum_{k\in OptimalSplits(i,j)}
count[i,k]count[k,j].
\]

root：

```text
optimal_tree_count = count[0][n]
```

必须检测 `uint64_t` overflow。

对于 v1.1 默认：

```text
n <= 10
```

理论上不会有问题，但代码仍应明确处理。

---

# 20. v1.1 中 crossing 的含义

这是一个关键语义变化。

若两个 candidate spans：

```text
[0,2)
[1,3)
```

crossing，

**不再是 constraint conflict。**

它们只是：

```text
competing constituency hypotheses
```

Binary tree 本身天然无法同时选择 crossing spans。

maximum-evidence solver 会自动比较：

```text
choose left span
vs
choose right span
```

所以默认 pipeline：

```text
crossing candidate evidence
```

不得导致：

```text
consistent=false
```

---

# 21. Hard constraints 仍然可以保留为底层 API

如果 v1 已经有：

```cpp
hard_spans
crossing_check
```

不要删除整个功能。

保留它用于：

- hand-written tests；
- future gold constraints；
- externally supplied axioms；
- debugging。

但是：

\[
\boxed{
\text{corpus substitution evidence 默认不得转成 hard span}
}
\]

CLI 正常运行时：

```text
hard_spans = empty
```

除非未来用户显式提供。

因此旧的：

```text
crossing hard spans => inconsistent
```

逻辑仍然正确，只是不再由 automatic corpus evidence 触发。

---

# 22. Forced spans 的定义改变

v1：

```text
forced = present in every tree satisfying hard constraints
```

v1.1：

\[
\boxed{
Forced(i,j)
\iff
[i,j)
\text{ 出现在所有 maximum-evidence trees 中}
}
\]

即：

\[
\forall T:
Score(T)=Score^*
\Rightarrow
[i,j)\in T.
\]

不要把次优树纳入 forced 计算。

---

# 23. 推荐的 forced-span 实现

因为 DP 已经保存：

```text
optimal_splits
```

可以直接在 packed optimal forest 上递归计算。

对 cell：

```text
[i,j)
```

定义：

```text
forced_set[i][j]
```

表示该 cell 的 **所有最优子树** 都包含的 spans。

叶：

```text
forced = {leaf}
```

或内部保留 leaf、输出时过滤。

对每个 optimal split `k`：

```text
alternative_forced(k)
    = { [i,j) }
      ∪ forced_set[i][k]
      ∪ forced_set[k][j]
```

然后：

```text
forced_set[i][j]
    = intersection over all optimal splits k
      of alternative_forced(k)
```

必须用 brute-force Catalan enumerator 验证。

---

# 24. Unique tree reconstruction

只有当：

```text
optimal_tree_count == 1
```

时才输出唯一 tree：

```text
tree=((the dog) runs)
```

如果：

```text
optimal_tree_count > 1
```

不要打印一个任意 tree 作为唯一结果。

可以输出：

```text
tree=<ambiguous>
```

并可选打印少量最优树用于 debug。

---

# 25. 输出术语修改

建议 v1.1 输出：

```text
EVIDENCE:
  [0,2) "the dog" score=2
    best_alternative="a cat"
    witness_contexts={
      (_, "runs"),
      (_, "sleeps")
    }

  [1,3) "dog runs" score=1
    best_alternative="dog sleeps"
    witness_contexts={
      ("the", _)
    }

OPTIMAL:
  best_score=2
  optimal_tree_count=1

FORCED_OPTIMAL:
  [0,2) "the dog"

tree=((the dog) runs)
```

---

# 26. `simple.txt` 的新预期行为

Corpus：

```text
the dog runs
a cat runs
the dog sleeps
a cat sleeps
```

v1.1 不得再输出：

```text
sentences_conflicted = 4
```

必须得到每句：

```text
hard_consistent = true
optimal_tree_count = 1
```

对：

```text
the dog runs
```

至少：

```text
[0,2) "the dog" score=2
[1,3) "dog runs" score=1
```

最终：

```text
best_score=2
tree=((the dog) runs)
```

同理：

```text
((a cat) runs)
((the dog) sleeps)
((a cat) sleeps)
```

---

# 27. `deep.txt` 的语义必须改变

Corpus：

```text
c1 d1 b1
c1 d1 b2
c1 d2 b1
c1 d2 b2
c2 d1 b1
c2 d1 b2
c2 d2 b1
c2 d2 b2
```

这是完整：

\[
C\times D\times B.
\]

对于：

```text
c1 d1 b1
```

左 span：

```text
[0,2) "c1 d1"
```

和右 span：

```text
[1,3) "d1 b1"
```

具有对称 substitution evidence。

因此：

\[
((CD)B)
\]

和：

\[
(C(DB))
\]

在这个 corpus 下不可区分。

正确结果：

```text
best_score(left_tree) == best_score(right_tree)
optimal_tree_count = 2
```

而不是：

```text
crossing_conflict=true
tree_count=0
```

proper `FORCED_OPTIMAL` 应为空。

---

# 28. `cartesian.txt` 的预期

Equivalence 输出不得退化。

应继续保持：

```text
{a1,a2,a3}
{b1,b2,b3}
{all ai bj}
{epsilon}
```

长度 2 每句：

```text
optimal_tree_count = 1
```

---

# 29. 新增 EvidenceBuilder 模块

建议新增：

```text
evidence_builder.hpp
evidence_builder.cpp
```

职责：

1. 从 raw `ContextTriple` 构造 exact raw context buckets；
2. 为每个 yield pair 建立 witness set；
3. 计算 pair support；
4. 对每个 occurrence 计算 score；
5. 保存 best alternatives；
6. 保存 supporting raw contexts；
7. 输出 debug / CSV。

---

# 30. 建议结构体

```cpp
struct RawContextKey {
    StringId left{};
    StringId right{};

    auto operator<=>(const RawContextKey&) const = default;
};

struct YieldPair {
    StringId first{};
    StringId second{};

    auto operator<=>(const YieldPair&) const = default;
};

struct SpanEvidence {
    OccurrenceId occurrence{};
    Span span{};
    StringId yield{};

    std::uint32_t score{};

    std::vector<StringId> best_alternatives;
    std::vector<RawContextKey> witnesses;
};
```

---

# 31. EvidenceBuilder 两阶段实现

## Phase 1：pair support

按 raw context：

```text
(L,R) -> distinct yields
```

对每个 raw bucket 生成 pair，并记录该 raw context。

最终：

```text
(the dog, a cat) -> support 2
(dog runs, dog sleeps) -> support 1
```

## Phase 2：occurrence score

对每个 occurrence：

```text
o=(u,c)
```

读取：

```text
raw_bucket[c]
```

枚举：

```text
v != u
```

查询：

```text
support(u,v)
```

仅当：

```text
c ∈ W(u,v)
```

时参与 max。

因此一个 occurrence 不能从它从未出现过的替换 context 中“借分”。

---

# 32. 不允许的错误 evidence 实现

不得：

1. 按 final canonical context 计算 witness 数；
2. 用 occurrence frequency 当 witness count；
3. 让重复句子增加 support；
4. 仅因为 `u ≡ v` 就增加 pair support；
5. 让一个 yield 在某处有 evidence 后，其所有 occurrence 自动继承 candidate status；
6. 对 alternatives 数量求和作为默认 score；
7. 把 root / leaf score 纳入 tree ranking；
8. crossing candidate 直接报 conflict。

---

# 33. 新增 CLI 输出

建议增加：

```text
--dump-witnesses
--dump-evidence
--dump-optimal-forest
```

统计：

```text
Evidence:
  raw_contexts = ...
  yield_pairs_with_support = ...
  candidate_occurrences = ...
  max_pair_support = ...
```

Parsing summary：

```text
Parsing:
  sentences = ...
  unique_optimal = ...
  ambiguous_optimal = ...
  hard_conflicted = ...
```

---

# 34. CSV 输出

新增：

## pair_witnesses.csv

```text
yield_a,yield_b,support
```

## span_evidence.csv

```text
sentence_id,begin,end,yield,score,best_alternative_count
```

## sentence_analysis.csv

```text
sentence_id,length,best_score,optimal_tree_count,candidate_span_count,forced_optimal_span_count,hard_consistent,unique_optimal
```

---

# 35. 必须保留 saturation 统计

继续保留：

```text
round
classes
context_unions
concat_unions
largest_class
collapse_ratio
```

不得因为 v1.1 tree layer 改动而改变 equivalence diagnostics。

---

# 36. 必须新增：真实多轮 cascade test

新增一个 **人工 record-level 单元测试**，不要依赖自然 corpus 偶然触发。

直接构造 `ContextTriple` / `ConcatTriple`，使：

```text
round 1:
  base equality discovered

round 1 concat:
  creates equality A

round 2 context:
  A changes canonical context signature
  creates equality B

round 2 concat:
  creates equality C

round 3 context:
  creates equality D

round 4:
  no change
```

要求至少：

```text
>= 3 productive rounds
```

并验证最终 DSU classes 与人工推导一致。

---

# 37. 必须新增 witness tests

## Test A：两个独立 raw contexts

要求：

```text
support("the dog", "a cat") == 2
```

即使：

```text
runs ≡ sleeps
```

也仍必须是 2。

## Test B：单 context

```text
support("dog runs", "dog sleeps") == 1
```

## Test C：duplicate 不增 support

重复同一句不能增加 witness 数。

## Test D：derived equality 不等于 direct witness

若：

```text
u ≡ z
```

仅通过 saturation 间接得到，而不存在 exact raw context 同时包含 u 与 z：

```text
support(u,z) == 0
```

---

# 38. 必须新增 tree optimization tests

### 长度 3：左 evidence 更高

```text
score([0,2)) = 2
score([1,3)) = 1
```

要求：

```text
best_score = 2
optimal_tree_count = 1
tree = ((w0 w1) w2)
```

### 右 evidence 更高

```text
1 vs 3
```

要求右树唯一。

### 完全 tie

```text
2 vs 2
```

要求：

```text
optimal_tree_count = 2
proper forced spans = none
```

### 无 evidence

长度 3：

```text
optimal_tree_count = 2
```

长度 4：

```text
optimal_tree_count = 5
```

不得加入 left/right branching bias。

### root / leaf score 不影响 ranking

即使测试中人为给高分，也必须忽略。

---

# 39. 保留 hard constraint test

旧的显式：

```text
hard [0,3)
hard [2,5)
```

crossing 时仍应：

```text
hard_consistent=false
```

但 corpus evidence 默认不产生 hard span。

---

# 40. End-to-end：simple.txt

必须从 v1 的：

```text
sentences_conflicted = 4
```

变成：

```text
unique_optimal = 4
ambiguous_optimal = 0
hard_conflicted = 0
```

目标 trees：

```text
((the dog) runs)
((a cat) runs)
((the dog) sleeps)
((a cat) sleeps)
```

---

# 41. End-to-end：deep.txt

必须从：

```text
sentences_conflicted = 8
```

变成：

```text
unique_optimal = 0
ambiguous_optimal = 8
hard_conflicted = 0
```

每句：

```text
optimal_tree_count = 2
```

这是 non-identifiability 测试，不得人为恢复某个方向。

---

# 42. End-to-end：cartesian.txt

Equivalence 结果保持与 v1 一致。

每句长度 2：

```text
optimal_tree_count = 1
```

---

# 43. Provenance 改进

v1 的 equivalence proof 保留。

v1.1 额外为 evidence 提供：

```text
span
yield
score
best alternatives
raw witness contexts
```

例如：

```text
[0,2) "the dog"
score = 2
best alternative = "a cat"
witnesses:
  (<epsilon>, "runs")
  (<epsilon>, "sleeps")
```

---

# 44. e-class size 不等于 evidence

严禁：

```text
score(span) = eclass_size(yield)
```

或按 equivalent strings 数量评分。

Tree evidence 只能来自：

\[
\boxed{\text{direct repeated raw substitution witnesses}}
\]

---

# 45. Determinism

同一 corpus / config 必须得到完全相同：

- e-class partition；
- pair support；
- span score；
- best score；
- optimal tree count；
- forced optimal spans。

输出尽量排序，避免 `unordered_map` iteration order 影响 dump。

---

# 46. v1.1 默认配置

保留：

```text
max_sentence_length=10
lowercase=false
deduplicate_sentence_types=true
equivalence_mode=strict_global
global_occurrence_consistency=false
```

新增：

```text
tree_objective=max_direct_substitution_support
evidence_aggregation=max_pair_support
count_raw_surface_contexts=true
score_leaves=false
score_root=false
```

---

# 47. 暂时不要加入 min-support threshold

即使：

```text
support=1
```

也保留 evidence。

让 tree objective 比较：

```text
2 vs 1
```

而不是人工删掉 1。

---

# 48. 不要使用频次和 branching bias

不得使用：

- sentence frequency；
- token frequency；
- left-branching preference；
- right-branching preference；
- balanced-tree preference；
- span length heuristic。

完全 tie 必须保持 ambiguity。

---

# 49. README 必须更新

新增至少三节：

## Contextual equivalence is not constituency

\[
u\equiv v
\not\Rightarrow
u,v\text{ 必然是 constituent}.
\]

## Repeated substitutability as evidence

定义：

\[
W(u,v)
\]

与：

\[
support(u,v).
\]

## Structural identifiability

解释：

```text
deep.txt
```

为什么左右 bracket 在该 corpus 中不可区分，以及：

```text
optimal_tree_count > 1
```

为什么是正确结果。

---

# 50. IMPLEMENTATION_NOTES.md 更新

说明：

1. v1 hard projection 为什么被废弃；
2. raw context 与 canonical context 的区别；
3. pair witness 数据结构；
4. occurrence score；
5. maximum-evidence DP；
6. optimal tree count；
7. forced-optimal span；
8. non-identifiability；
9. 当前 objective 局限；
10. future work。

---

# 51. v1.1 不解决的问题

暂不实现：

- lexical ambiguity model；
- context-sensitive e-class；
- probabilistic confidence；
- biclique / Formal Concept Analysis；
- MDL；
- DreamCoder；
- neural guidance；
- semantic constraints；
- long-sentence scaling；
- external sort；
- parallel pair generation；
- learned aggregation；
- CCG / labels。

---

# 52. 验收命令

至少运行：

```bash
ctest --test-dir build --output-on-failure
```

以及：

```bash
scf_cli --input data/synthetic/cartesian.txt --stats --dump-classes --dump-witnesses --dump-evidence --dump-trees

scf_cli --input data/synthetic/simple.txt --stats --dump-classes --dump-witnesses --dump-evidence --dump-trees

scf_cli --input data/synthetic/deep.txt --stats --dump-classes --dump-witnesses --dump-evidence --dump-trees
```

---

# 53. 最终预期 summary

### cartesian

```text
equivalence unchanged from v1
unique_optimal = 9
ambiguous_optimal = 0
hard_conflicted = 0
```

### simple

```text
support(the dog, a cat) = 2
support(dog runs, dog sleeps) = 1
support(cat runs, cat sleeps) = 1

unique_optimal = 4
ambiguous_optimal = 0
hard_conflicted = 0
```

### deep

```text
unique_optimal = 0
ambiguous_optimal = 8
hard_conflicted = 0
optimal_tree_count = 2 per sentence
```

---

# 54. 最终数学语义

v1.1 不再主张：

\[
\text{substitutability}=\text{constituency}.
\]

而是：

\[
\boxed{
\text{repeated direct substitutability}
\rightarrow
\text{constituency evidence}
}
\]

并在所有 full binary trees 中求：

\[
\boxed{
T^*
=
\arg\max_T
\sum_{[i,j)\in T}
Evidence(i,j)
}
\]

如果：

\[
|\arg\max_T|=1,
\]

则当前 evidence objective 唯一识别结构。

如果：

\[
|\arg\max_T|>1,
\]

则结构在当前纯符号证据下 underdetermined。

只有显式 hard constraints 无模型时才叫 inconsistent。

**crossing evidence 本身不是 inconsistency。**

---

# 55. 实施原则

优先级：

```text
correctness
determinism
inspectability
minimal delta from v1
```

如果发现本说明存在算法反例：

1. 保留最小失败测试；
2. 不偷偷加 heuristic；
3. 在 `IMPLEMENTATION_NOTES.md` 中说明；
4. 给出建议修改；
5. 将建议与 v1.1 既定语义分开。

v1.1 的研究目标是：

> **在完全没有语言学标签、概率模型和神经网络的情况下，直接的跨句 surface substitution evidence 能在多大程度上唯一确定无标签二叉结构。**
