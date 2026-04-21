# 一周可交付的 C++ DAG 任务调度器高层设计报告

## 执行摘要

本项目旨在为“有 C++ 基础、熟悉线程编程、会一点 Debian Linux”的应届生，设计一个**可在一周内形成闭环交付**、并能在面试中清晰讲述的 **DAG 任务调度器（本地工作流执行器）**。其核心价值在于：用一个“任务依赖图 + 并发执行 + 可恢复 + 可观测”的系统，将你的能力点（算法、系统设计、并发、工程质量、Linux 运维）组织成可验证的成果。设计上优先遵循 C++ Core Guidelines 对并发的关键建议：**避免数据竞争、减少可写共享、以任务而非线程思考**，使并发模型更可推理、更能被面试官认可。citeturn2search0turn6search6

默认假设：不做 GUI（CLI/配置文件即可）；目标公司类型未指定（设计尽量通用）；是否需要数据库持久化未指定（提供可选的事件日志/嵌入式数据库路径）。citeturn2search0turn7search2

## 项目目标与最终交付物

项目目标（高层）是构建一个可复用的“本地 DAG 工作流引擎”，其输入为任务与依赖关系定义，输出为可审计的执行结果与可观测数据。设计重点是让系统具备三类“面试可验证特质”：**正确性（依赖/状态机严谨）**、**并发安全性（无数据竞争/可检测）**、**工程可交付性（可测试/可部署/可观测）**。citeturn2search0turn6search6turn0search3

最终交付物（高层描述）建议包括：
- 一个可运行的调度器可执行程序（CLI 入口即可），能够加载 DAG、并发执行任务、生成执行报告与摘要指标，并支持错误/失败路径的清晰语义。citeturn2search0turn2search19  
- 一套可复现的测试与质量保障：单元测试框架（GoogleTest）与并发缺陷检测思路（TSan/ASan）能让“并发正确性”具备证据链。citeturn3search2turn0search3turn4search2  
- 一套在 Debian 上的运行与运维呈现（systemd 服务化）：面试时可直接展示“像真实服务一样运行、可查看状态与日志”。citeturn2search3turn2search19turn7search15  
- 一套可观测输出（日志 + 指标）：指标建议对齐 Prometheus/OpenMetrics 生态，便于讲“可观测性设计”。citeturn4search0turn3search4turn3search7  

## 核心模块与职责

下表给出推荐的核心模块边界（模块名 + 一句话职责），用于面试时讲“分层与职责单一”。

| 模块 | 一句话职责 |
|---|---|
| WorkflowSpec（工作流规格） | 描述任务节点、依赖边、资源需求与策略参数（重试/超时/优先级等），作为外部输入的领域模型。citeturn2search0 |
| DAGBuilder（依赖解析器） | 将输入规格解析为有向图，并在提交执行前完成结构合法性检查（含环检测）。citeturn9view1turn9view0 |
| Scheduler（调度器） | 在“依赖约束 + 资源约束 + 优先级”等规则下选择可运行任务，并将任务提交给执行层。citeturn2search0turn9view2 |
| Executor（执行器） | 管理线程池与任务执行生命周期，负责运行、取消、超时与回收结果。citeturn2search0turn6search16turn5search2 |
| StateStore（状态存储） | 维护任务状态机、运行历史与最终摘要；可选支持事件日志以便恢复/回放。citeturn7search2turn7search0 |
| Observer（可观测与报告） | 将运行过程输出为结构化日志与指标（吞吐、延迟、队列长度、失败率等），并生成执行报告。citeturn4search0turn3search11turn7search15 |

> 说明：模块划分强调“任务优先于线程”，把线程细节收敛在执行器内部，符合 C++ Core Guidelines 的并发设计取向。citeturn2search0

## 并发与线程模型设计思路

并发设计必须可讲清“为什么这样更安全、如何验证无竞态”。本项目建议采用“**单调度线程（或轻量调度循环）+ 线程池执行**”的结构，把复杂并发集中在少数关键共享结构上，并将共享写入最小化。citeturn2search0turn6search6

线程池与任务执行
- 以“任务（task）”为一等公民：调度器产生可运行任务，执行器将任务映射到线程池工作线程；面试讲述时强调“线程是实现细节，任务是业务抽象”，这是 C++ Core Guidelines 明确倡导的思路。citeturn2search0  
- 线程生命周期管理强调可控与可推理：标准库线程语义（如 join 的同步关系、joinable 状态等）可作为你阐述线程安全边界的依据；避免让业务逻辑散落在线程创建/销毁细节中。citeturn6search4turn6search0turn6search16  
- 取消/停止语义可选参考 C++20 的 `std::jthread` 及 stop 机制概念：它强调自动回收与协作式停止，便于讲“资源回收与优雅停止”。citeturn0search5turn0search9  

同步策略与共享状态治理
- 遵循“减少可写共享”的原则：共享数据越少，锁越少；锁越少，死锁与竞态面越小，这是并发工程中最具性价比的正确性策略。citeturn2search0  
- 锁的使用以 RAII 方式表达：用作用域锁语义避免遗漏解锁，并在需要同时持有多把锁时采用带死锁规避语义的机制（标准库也强调 `std::lock` 具备死锁避免算法；`scoped_lock` 提供 RAII 包装）。citeturn6search5turn6search1turn6search13  
- 条件变量用于“线程睡眠/唤醒”而非忙等：其等待语义会原子释放互斥量并在唤醒后重新获得互斥量，同时需要面对虚假唤醒并以条件谓词重检，这些语义细节来自标准库文档，是你解释“为什么不会丢信号/为何要循环检查”的依据。citeturn5search9turn5search2  

内存可见性与竞态定义（面试高频）
- 明确“数据竞争是未定义行为”：C++ 的多线程内存模型规则指出，若发生数据竞争（未通过原子或 happens-before 关系约束的冲突访问），程序行为未定义；因此“跑得过”不是正确性证据。citeturn6search6  
- 原子变量用于跨线程发布/可见性：`std::atomic` 访问可建立线程间同步，内存序（`std::memory_order`）描述原子操作周围的排序约束；面试时可用“发布-获取（release-acquire）”等概念解释“写入何时对其他线程可见”。citeturn6search2turn0search1  

竞态检测与验证思路
- ThreadSanitizer（TSan）是并发项目的“证据链工具”：官方文档明确其用于检测数据竞争，且由编译器插桩与运行时库组成；你可以把它作为“并发正确性的自动化验证手段”写入项目质量说明。citeturn0search3turn0search15  

## 调度语义与算法思路：任务状态机、依赖解析与调度

这一部分是面试讲述的主线：**先定义语义（状态机与失败处理），再谈算法（拓扑与调度），最后谈扩展（优先级/资源约束）**。

任务模型与状态机（高层）
建议把任务抽象为“具有输入/输出与副作用边界的可执行单元”，并用状态机保证可解释性与可恢复性。状态机不需要复杂，但必须有清晰的失败与重试语义。

推荐生命周期（概念层）：`Pending → Ready → Running → (Succeeded | Failed | TimedOut | Canceled)`，并允许 `Failed/TimedOut → Retrying → Ready` 的回边（受策略控制）。状态机的价值在于：任何时刻系统都能说明“任务为何处于当前状态、下一步可能是什么”。citeturn2search0  

重试、超时、幂等性（面试可讲点）
- 重试策略应与幂等性绑定：重试本质上会重复执行任务，因此需区分“天然幂等任务”与“需要补偿/去重的任务”；否则重试可能造成重复副作用。citeturn7search2turn7search6  
- 超时策略强调“协作式停止”: 在 C++ 并发语义中，强杀线程通常不可取；更常见方式是让任务在可检查点响应停止信号（可借助 C++20 stop token 概念解释）。citeturn0search5turn0search9  

依赖解析与拓扑调度（Kahn 思路）
依赖解析完成后，调度问题的核心是：在保持依赖约束的前提下，尽可能让“就绪任务”并发运行。  
- 基础拓扑排序可采用 Kahn 思路：维护入度、把入度为 0 的节点加入队列，处理节点时降低后继入度并产生新的零入度节点。citeturn9view0  
- 环检测与失败快速定位：若队列耗尽但仍有节点未处理，则存在环；这为输入校验提供明确判据，也便于把“环路径”作为用户可理解的错误返回。citeturn9view1  
- 复杂度叙事：Kahn 的处理过程可达到线性级别（常表述为 O(V+E)），这使得调度开销可控，面试讲述更有底气。citeturn9view2turn9view0  
- Kahn 算法的历史归因可引用课程讲义对其源头的说明（Arthur Kahn 1962）。citeturn8view1turn2search5  

优先级与资源约束（概念扩展）
在“就绪任务集合”中引入调度策略即可扩展能力边界，而无需推倒重来：
- 优先级：就绪队列可按优先级/截止期（deadline）选择任务；在面试中可讨论公平性（避免低优先级饥饿）与吞吐的权衡。citeturn2search0  
- 资源约束：将“可并行度”从固定线程数扩展为多维资源（如 CPU 核数配额、独占资源锁、I/O 限流），调度器在“依赖满足 + 资源可用”时才发放执行权；这是从玩具走向工程的关键叙事。citeturn2search0  

## 可靠性、可观测性与工程实践建议

错误处理与恢复策略（高层）
并发调度器的可靠性核心在于：失败可解释、恢复可验证。
- 失败传播：明确当某任务失败时，其下游依赖任务的语义（跳过、标记失败、或继续执行可独立分支）。这决定了用户对系统的“心理模型”。citeturn2search0  
- 回滚/补偿：对有副作用的任务，建议从设计层引入“补偿动作（compensation）”的概念，而不是假设能回滚一切；这与事件驱动/事件溯源常见的思路一致（用事件描述变化而非直接覆盖状态）。citeturn7search2turn7search6  
- 持久化事件日志（可选）：把关键状态变化写入“追加式日志”，使得崩溃恢复可通过重放恢复系统状态；该思想与 Event Sourcing 的“事件序列可重建状态”一致。citeturn7search2turn7search6  
- 数据库持久化（可选）：若希望更“工程化”，可采用嵌入式数据库（如 SQLite）并理解 WAL（Write-Ahead Logging）作为保障一致性与崩溃恢复的通用机制；SQLite 与 PostgreSQL 文档均强调 WAL 的核心理念是“先记录日志再落数据”。citeturn7search0turn7search1  

可观测性与性能评估要点
建议把可观测性当作交付物的一部分，而不是后补；Prometheus 文档也强调应广泛、系统化地埋点，并把指标与代码靠近以便定位问题。citeturn4search0  
- 指标（metrics）：对齐 Prometheus 数据模型，输出延迟、吞吐（任务/秒）、队列长度、并发度、失败率、重试次数等；指标类型（Counter/Gauge/Histogram/Summary 等）可参考 Prometheus 对指标类型的定义。citeturn3search7turn4search4turn3search11  
- 日志（logging）：在 Debian 上运行时，可利用 systemd-journald 进行结构化日志收集与索引；systemd 文档明确 journald 负责收集与存储日志数据，并提供查询接口。citeturn7search15turn7search11  
- 基准与压测（benchmarking）：基准不要求复杂，但要能复现并解释瓶颈（如调度开销、锁竞争、任务分发延迟）；“先度量再优化”的叙事会显著提升可信度。citeturn2search0  

工程实践（构建/测试/CI/静态分析/Sanitizer/部署）
这部分决定你简历上的“工程成熟度”像不像真实团队项目。
- 构建系统：建议使用现代 CMake 的 target 模式；`target_link_libraries` 的文档强调依赖的“使用需求（usage requirements）”可传播到依赖方，适合构建可复用库与可执行程序的分层。citeturn3search3  
- 依赖管理：`FetchContent` 允许在配置阶段拉取依赖，使测试框架等依赖更易复现。citeturn2search2  
- 配置复现：CMake Presets 的官方文档指出其用于共享常用配置、支持 CI/团队协作。citeturn4search1  
- 单元测试：GoogleTest 的 CMake Quickstart 适合快速形成可跑的测试体系；这为“正确性可验证”提供基础。citeturn3search2  
- 静态分析：clang-tidy 文档明确其用于发现典型编程错误、接口误用与可由静态分析推断的缺陷；在并发项目中用于提前发现危险模式很有价值。citeturn4search3  
- Sanitizer：AddressSanitizer 用于内存错误检测；ThreadSanitizer 用于数据竞争检测，二者都是并发项目的高性价比质量工具。citeturn4search2turn0search3  
- Debian 部署：systemd 单元文件是描述服务行为的配置载体；Debian wiki 提供服务单元文件与目录的基本实践，系统文档也解释了 unit file 的角色与目录分层。citeturn2search3turn2search19  

## 面试讲解要点

面试叙事建议遵循“问题—抽象—权衡—验证—演进”的结构，避免陷入实现细枝末节。

设计取舍怎么讲
- “为何是 DAG 调度器”：它把并发能力放在正确的抽象层（任务而非线程），与 C++ Core Guidelines 的并发建议一致，并且算法点（拓扑与环检测）能快速建立专业可信度。citeturn2search0turn9view0  
- “为何选择 Kahn”：可讲清楚入度队列、环检测判据与线性复杂度，且课程讲义明确给出“处理不完则有环”的判定，非常适合口述。citeturn9view1turn9view2  
- “并发模型为何这样组织”：强调把共享写入集中到少数模块、减少锁粒度复杂度；同时用条件变量/原子定义清晰的 happens-before 关系来保证可见性。citeturn2search0turn0search1turn5search9  

并发难点怎么讲
- 数据竞争是未定义行为：并发 bug 不能靠“跑通”证明正确，必须靠内存模型与工具验证；TSan 能提供数据竞争报告，是你“并发质量闭环”的核心证据。citeturn6search6turn0search3  
- 锁与死锁：当需要多把锁时，强调采用带死锁规避语义的标准做法；并通过 RAII 锁控制作用域，降低遗漏与异常路径风险。citeturn6search5turn6search13  

优化方向怎么讲（不需要实现，只要思路）
- 性能方向：减少锁竞争（分离只读与写入路径、缩小临界区）、提升就绪队列操作效率、降低调度开销。citeturn2search0  
- 可扩展方向：引入资源约束、优先级策略与更完善的恢复/回放机制；指标与日志对齐 Prometheus/OpenMetrics 生态，便于平台化。citeturn3search4turn4search0  
- 可靠性方向：引入事件日志与重放（event sourcing / append-only），使失败与恢复可审计，并可选用 WAL 思想增强崩溃恢复的可信度。citeturn7search2turn7search1  

## 最终结果

最终交付物应达到以下“可验证状态”，以支撑投递与面试展示：

- **可运行性**：能加载工作流规格并完成一次 DAG 调度执行，输出完整的执行报告（成功、失败、跳过、超时等结果可解释）。citeturn2search0turn9view0  
- **可测试性**：具备单元测试体系（GoogleTest），并能用并发检测工具（TSan）对关键并发路径做自动化验证，形成“并发正确性证据链”。citeturn3search2turn0search3  
- **可部署性**：在 Debian 上可作为 systemd 服务运行，服务行为与日志可通过 systemd/journald 体系被观察与管理。citeturn2search3turn7search15  
- **可观测性**：输出关键指标并对齐 Prometheus/OpenMetrics 的语义（至少具备吞吐、延迟、失败率、队列长度等），同时输出结构化日志便于追踪任务执行链路。citeturn3search7turn3search4turn4search0  
- **面试可讲点**：能清晰讲述“任务优先于线程”的并发设计取舍、拓扑排序与环检测的算法依据、数据竞争未定义行为的风险与工具验证方法、以及从 MVP 到工程化演进的路线。citeturn2search0turn6search6turn0search3turn9view1