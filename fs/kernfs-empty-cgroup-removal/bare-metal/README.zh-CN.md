# 裸机实验索引

各子目录分别保存实验身份、结果和复核脚本，不把不同实验的样本合池。

| 目录 | 内容 |
| --- | --- |
| [original-regression](original-regression/README.zh-CN.md) | 最初的空 cgroup workload 研究：两轮精确回归对照、早期 root 复用原型、固定主线补测，共 603 份运行样本 |
| [inode-inited](inode-inited/README.zh-CN.md) | T.J. 首个补丁验证，以及另立身份的预热/间隔诊断 |
| [inode-requested](inode-requested/README.zh-CN.md) | 独立 inode 请求标记的补丁对照、正确性检查、内存代价和未覆盖项 |

第一个目录属于原来那条 workload 研究，但包含多次独立实验：root 复用改的是内核；
固定主线补测保持 SCX 关闭控制程序不变，换内核版本对照。它们不是新生成的 workload 候选，
也不能合称一次实验。

分别运行各目录的 `verify.py`；预热/间隔诊断另有自己的复核脚本。
目录整理不改实验数据或结论，也不影响已公开的固定 commit 链接。[English](README.md)。
