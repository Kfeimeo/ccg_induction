请基于我提供的 `scf_cpp_v1_1_prompt.md`，在**现有 SCF v1 C++20 工程上增量实现 v1.1**。

不要重写已经正确工作的 equivalence saturation 模块。重点完成：

- raw surface substitution witness；
- pair-support 统计；
- occurrence-level constituency evidence；
- `max pair support` 评分；
- crossing evidence 从 hard conflict 改为竞争假设；
- maximum-evidence binary-tree DP；
- optimal tree count；
- forced spans among optimal trees；
- `simple.txt`、`deep.txt`、`cartesian.txt` 的新回归测试；
- 至少三轮有效传播的 fixed-point cascade 单元测试；
- provenance、README 与 `IMPLEMENTATION_NOTES.md` 更新。

严格遵守附件中的数学定义、非目标、测试和验收标准。若发现算法本身存在反例，不要擅自加入 heuristic 修补；保留最小失败测试并在 `IMPLEMENTATION_NOTES.md` 中说明。