# 公开跟踪状态：2026-10-19

本次核对 regzbot 的 [All 视图](https://linux-regtracking.leemhuis.info/regzbot/all/)
及各条目的独立页面。页面生成时间为2026-10-19 15:12:10（北京时间），以下日期也使用北京时间。
按报告者姓名及使用过的邮箱匹配到8条记录；这是跟踪条目数，不是8个独立的新问题。
本页更新公开跟踪状态，不重新计时，也不把早期源码审计改称为当前树核验。

## 跟踪关闭不等于补丁修复

`marked resolved` 可以来自人工结束跟踪。必须阅读理由；其中 `inconclusive`
表示未形成确定技术结论，不能列入“修复已合入”数量。

| 报告 | 跟踪记录与日期 | 应如何理解 |
| --- | --- | --- |
| [mprotect共享脏页切权限](../mm/mprotect-shared-dirty-toggle/) | [6月报告条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/7c9eeb21.1c04f.19efaacbb2f.Coremail.chengfenglin@stu.xmu.edu.cn/)：10月12日人工关闭，inconclusive | 跟踪者认为其他人未能复现，可能属于微基准问题；不是确认修复，也不是对本地测量的重新检验。 |
| [fsnotify并发增删watch](../fs/fsnotify-concurrent-inotify-watch-setup/) | [条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgd=RwMy=29OYG14b6kkoF1uBHz67jTGge5Nubb-XemMvBg@mail.gmail.com/)：10月12日人工关闭 | 理由是Jan Kara接受该可扩展性取舍、同时计划改善；不是已有完成的修复。 |
| [WAITV记账分配](../io_uring/io-uring-futex-waitv-accounted-allocation/) | [条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/20260830190717.1509012-1-lin2530632123@gmail.com/)：10月13日人工关闭，inconclusive | 跟踪者以缺少反馈、很可能是微基准问题为由停止跟踪。必要记账没有因此被判为错误。 |
| [MSG_RING SEND_FD安装](../io_uring/io-uring-msg-ring-send-fd-install/) | [条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgdmt0FQ=offsdfn+wEaDxbOFoAa6bi92X_vEo4S6aCZ56A@mail.gmail.com/)：10月13日人工关闭，inconclusive | 理由是微基准问题且已有开发者研究；有公开RFC和评审，但不能记为方案获采纳。 |
| [AppArmor AF_UNIX发送](../security/apparmor-af-unix-send-old-abi-6456cc/) | [条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgdnoz4NdqzOV9+SZaYTc+O_GkNNn5NVp+ZpHUQbp5h-QVQ@mail.gmail.com/)：10月13日人工关闭，inconclusive | 同样以微基准问题、开发者研究为由关闭；公开依据主要是维护者表示会调查，不是其已发布修复。 |
| [标量futex wait/wake](../io_uring/io-uring-futex-inflight-wait-wake/) | [条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgdn=R_qyUdE=j9za+vkmqcxacbP-84OHXF4nZ4ho9qRyVg@mail.gmail.com/)关联`73e701909747`及`4d327bbd1cd2`，标示主线v7.3-rc1及稳定分支回补 | 这是有对应修复提交的记录；不同于人工以inconclusive关闭。各测试收益仍限原测量范围，不等于历史慢差逐项归零。 |
| 5月旧mprotect报告 | [旧条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/662955ba.f499.19e3b2cf478.Coremail.23020251154299@stu.xmu.edu.cn/)没有关闭标记，最后活动5月26日 | 与6月报告分别登记；不能计为新增独立问题，也不表示近期仍在积极处理。 |
| 早期PAGEOUT／THP／no-swap报告 | [旧条目](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/3f4567fc.f496.19e3b2b297b.Coremail.23020251154299@stu.xmu.edu.cn/)：6月3日人工关闭 | 理由是报告者不再将其认作性能回归；不是补丁修复。 |

10月12日的两条关闭记录指向[这封跟踪邮件](https://lore.kernel.org/regressions/6a9e41a6-5acc-4e7b-8516-8c78935e5f6e@leemhuis.info/)，
10月13日的三条指向[另一封跟踪邮件](https://lore.kernel.org/regressions/45ee647a-6e60-42c2-a165-dc21258f8999@leemhuis.info/)。
本次理由直接核对自regzbot独立条目；两封关闭邮件原文访问受限，未据此补充页面没有记载的判断。

## “已有开发者研究”的具体证据

### MSG_RING：已有实现和评审，取舍尚未获认可

Uzair Beg于9月14日发布[三枚RFC的封面](https://lists.openwall.net/linux-kernel/2026/09/14/1077)，
明确引用本报告，依次尝试专用节点slab、缓存耗尽时批量补充、注册稀疏文件表时预填充。
该邮件明确说明所引实验由Chengfeng完成，不能将这些数字算作另一人的独立复现。

Gabriel Krisman Bertazi的[9月26日评审](https://lists.openwall.net/linux-kernel/2026/09/25/2034)
反对把扩大缓存作为通用方案：这些节点无法主动回收，大表但少量使用的程序会承担额外内存。
他要求解释超过128节点的实际工作集和安装成本是否会被后续I/O摊薄；若确有实际问题，
可考虑维持128项上限、在空缓存时批量补充，并希望不必另建专用slab。
这是已有代码及实质评审，不是补丁已接受，也不能由旧讨论推断作者现在仍在持续投入。

### AppArmor：维护者承诺调查；后来的原型来自本研究

John Johansen于8月6日在[公开回复](https://lists.ubuntu.com/archives/apparmor/2026-August/014884.html)
中表示需要调查额外开销，指出原提交似乎已经做了提前退出，预计有改善空间但不保证恢复原水平。
这支持“维护者已回应并计划分析”，不支持“维护者已有完成的修复”。

9月23日的[后续原型邮件](https://lists.openwall.net/linux-kernel/2026/09/22/2475)
来自Chengfeng，包含早退原型、有限正确性检查和另一路径代价。
本次核对的公开线程未显示维护者对此给出后续技术结论；不能把本研究的原型记成维护者的成果。

## 未被这次关闭记录覆盖的材料

不能概括为“除最近两封投稿外，其他报告都已有上游最终结果”。

- [tmpfs](../fs/tmpfs-flistxattr-small-list/)和[Btrfs](../fs/btrfs-remap-writeback-inhibition-v2/)
  保留既有主线修复证据；不因本次未匹配到独立regzbot条目而改变结论。
- [kernfs](../fs/kernfs-empty-cgroup-removal/)和[RT sysctl](../kernel/sched/sched-rt-sysctl-read-rebuild/)
  本次没有匹配到独立regzbot条目；既有回复／补丁测试不等于已采纳或明确拒绝，仍按各目录的有日期记录表述。
- [fair默认时间片](../kernel/sched/sched-fair-default-slice-noop/)及
  [换出回退页序](../mm/mm-swap-fallback-folio-order/)也没有匹配条目；其投稿、评审和合入状态应另查对应线程。

regzbot不是所有邮件和补丁的完整索引。跟踪处置、维护者技术结论、实际合入和实验结果分别记录，
不把缺少条目、停止跟踪或维护者愿意研究当作同一种“已解决”。
