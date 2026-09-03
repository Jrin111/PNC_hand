# PNC Hand 独立工程评审入口

更新时间：2026-09-03。本文用于让 Claude 或其他工程师独立检查方案与实现；
正文中的“已报告验证”是开发交接记录，不能代替评审者自己的代码核对或实机证据。
用户仅要求评审时，先进行只读审查，不自动修改源码、替换 `install/`、部署 DTB 或发送机械手命令。
用户已授权修复/实现时，由 Main Claude 按任务范围执行关键修改，不重复申请已有授权。
主对话与 subagent 分工、适量验证及状态维护遵循 [CLAUDE.md](../CLAUDE.md)。

## 1. 先确认评审版本

- 仓库：[Jrin111/PNC_hand](https://github.com/Jrin111/PNC_hand)。
- Foxglove 工作基线：`3ecf1d6891a4dd8057e88c578c4c514a67e420c0`。
- 首次完整实现提交：`e5c43263a4517076c60ad469f3b99fc8256eb4e2`。
- 工作分支：`codex/pnc-foxglove-offline`；[PR #1](https://github.com/Jrin111/PNC_hand/pull/1)。
- 先核实 PR 当前状态和实际 HEAD；后续文档/修复可能继续提交，不能假设 `main` 已包含本功能。

在仓库根目录只读查看：

```bash
git status --short
git branch --show-current
git log -1 --format='%H %s'
git diff 3ecf1d6891a4dd8057e88c578c4c514a67e420c0 e5c43263a4517076c60ad469f3b99fc8256eb4e2 --stat
gh pr view 1 --repo Jrin111/PNC_hand --json state,baseRefName,headRefName,headRefOid,url
```

没有 `gh` 时直接查看 PR 页面。评审结论注明所读提交，并区分基线已有问题与本次新增问题。

## 2. 用户目标与范围

- 最终目标：**RZ/V2H 直接通过共享 SPI 和 9 根独立 GPIO 片选采集 9 颗 RAA2S4704，
  用户触摸左手机械手时，Foxglove 在三维手表面按位置和相对响应强弱着色。**
- 保留既有 Inspire RH56E2 串口运动控制、关节状态、URDF/TF 和控制面板；ESP32 不参与触觉采集。
- 9 × 6 = **54 个电气通道槽**；当前表面模板为 **47 个物理区**，即掌部 22 区＋每指 5 区。
  未映射的 7 个槽不能凭数量推断为损坏、备用或额外触摸区。
- 多个区域应能持续同时受触并独立显示；SPI 顺序扫描意味着各通道采样时刻不同，
  不能把整帧发布解释为 54 路同步采样，也不能保证捕获比扫描周期更短的接触。
- 硬件前提供独立模拟，验证软件消息、颜色、运动反馈和 TF 链路。
- 触碰手势识别、触摸自动触发动作、相机关键点叠加继续延期。

## 3. 读哪些代码

先读 [README](../README.md)、[项目状态](../PROJECT_STATUS.md) 和
[Foxglove 使用说明](../foxglove/README.md)，再沿数据流核对下表。

| 路径（相对仓库根目录） | 重点 |
| --- | --- |
| `src/robots/ssc_tactile_hand_ros2_control/src/spi_transport.cpp` | GPIO/CS、SPI 请求与响应、echo/CRC、测量等待与流水线 |
| `src/robots/ssc_tactile_hand_ros2_control/src/ssc_tactile_hand_hardware_interface.cpp` | `read()`、通道重排、EMA/tare、失败保留与恢复、耗时统计 |
| `src/robots/ssc_tactile_hand_bringup/{launch,config,urdf}` | 设备路径、9 根 CS 顺序、162 状态接口、40 Hz 配置、Jazzy 启动 |
| `src/utils/state_interfaces_broadcaster/src/state_interfaces_broadcaster.cpp` | `names/values` 顺序、类型、QoS、发布时戳及缺失值行为 |
| `src/utils/pnc_tactile_visualizer/pnc_tactile_visualizer/{core,node}.py` | 按名称解码、每区颜色、NaN/超时、TF 贴片、映射检查 |
| `src/utils/pnc_tactile_visualizer/config/pnc_zones_left.json` | 47 区几何、真实 `channel` 与示例 `demo_channel` 的区别 |
| `src/utils/pnc_hand_demo/{launch,pnc_hand_demo,test}` | 模拟隔离、162 接口、54 值输入、故障模拟和动作测试边界 |
| `foxglove/foxglove_inspire_hand_panels/src/{data,converters,panels}` | 原生话题接入、数值/颜色、选中通道与其他通道保留、模拟输入门控 |
| `src/robots/inspire_rh56e2_hand_bringup/{launch,config}` | 原运动栈复用、bridge 开关、仅模拟反馈变更 |
| `platform/rzv2h/{dts,patches,dtb}` | 引脚复用与电压假设、目标板型、源文件/DTB 对应性 |

相对上述基线，本次**未修改原 SPI 采集、机械手驱动或 broadcaster 的 C++**。
运动 bringup 只改位置控制 launch 的 `launch_foxglove` 开关（默认仍为 true），
以及 `controller_manager_mock.yaml` 仅发布 `position`，不伪造模拟测速/测力。
新增两个 Python ROS 包，更新 TypeScript Foxglove 扩展、布局、模拟构建入口和文档。
不要把这一差异范围理解为原 C++ 已经通过实机验收。

## 4. 真实链路与模拟替换点

```text
真实：RZ/V2H SPI + GPIO CS → 原采集 read() → /tactile/controller_manager
       → state_interfaces_broadcaster → names / values
模拟：pnc_hand_demo 的 synthetic source ──────────────┘（同类型、同名称接口）
       → pnc_tactile_visualizer → /tactile/markers → foxglove_bridge → 3D 表面贴片
       → foxglove_bridge → PNC Tactile Diagnostics（直接读 names / values）

运动：原 Inspire 串口硬件／模拟 GenericSystem → 根 /controller_manager
       → 关节状态 + robot_state_publisher 的 URDF/TF → 模型和贴片随手指运动
```

- 名称话题：`/tactile/tactile_hand_state_broadcaster/names`，`control_msgs/msg/Keys`。
  broadcaster 使用 depth 1、transient-local；visualizer 订阅 reliable/transient-local。
- 数值话题：同路径 `/values`，`control_msgs/msg/Float64Values`；162 项是每槽的
  `raw_i/raw_q/value`。broadcaster 使用 SystemDefaultsQoS，visualizer 使用 best-effort。
  应检查目标 RMW 的实际端点 QoS、names 晚加入和重连，而不只检查类型名。
- 新显示代码按 `raaN_chM/value` 找值，不依赖写死的数组序号；核对名称变更、长度错误及重复名称处理。
- 默认真实映射 `unmapped`；现有配置实际 `channel` 全空、`mapping_verified=false`。
  `verified` 模式要求填写全部 47 个有效且唯一的对应通道。`demo` 映射只是演示例子。
- 三维颜色支持 `color_min/color_max`、逐区 `gain/offset`；诊断卡目前固定按 0..1 着色。
  两者都不是牛顿。模拟 I/Q 是占位数据，不能代表 ADC 物理响应或力标定。
- 模拟默认 domain 77，真实流程示例 domain 0。真实显示不需要启动模拟源；仅运行一个 bridge。
  模拟输入需显式启用及新鲜心跳；domain 与心跳门控不是硬件健康证据。

真实模式当前采用四终端组合启动，不是一个总 launch。完整命令、overlay、左手 TF、
bridge 的大写 `.STL` 资源规则见 [Foxglove 使用说明的真实接入部分](../foxglove/README.md#接入真实手)。
这组真实命令经过源码接口核对，尚未用整套实物运行验收。

## 5. 本次最小构建与部署范围

| 包/产物 | 本次需要处理什么 |
| --- | --- |
| `pnc_tactile_visualizer` | 真实热力图必需；新增 ament Python 包，安装入口、配置、包索引及目标 ROS/Python 依赖 |
| `inspire_rh56e2_hand_bringup` | 更新 launch/config 安装文件，获得 `launch_foxglove`；本次无该包 C++ 重编内容 |
| `pnc_hand_demo` | 仅硬件前演示需要；Python 包，真实模式不启动 |
| `foxglove_bridge` | 目标环境已有兼容 Jazzy 版本可复用；缺失时补齐对应架构/发行版版本 |
| 原采集/运动/broadcaster/description/dexhand_utils | 在匹配目标 ABI 和依赖的前提下复用既有输出；不是无条件承诺任意镜像可运行 |
| `.foxe` 扩展 | 电脑侧 npm 构建/安装，当前 1.1.1；不需要 RZ/V2H 交叉编译 |

因此本次可视化改动没有要求重编自研 C++ 插件，但新增包和更新配置仍需正确安装到目标 overlay。
仓库旧 `install/` 不包含新增可视化包及本次 bringup 修改；不要仅更新 `.foxe` 就认为板端已更新。
目标要求 RZ/V2H AArch64 + ROS 2 Jazzy；原触觉包还使用 libgpiod 1.6 API，拒绝直接用 2.x 构建。
部署时也要核对目标 libgpiod 运行库等依赖，而不只检查插件文件是否存在。
若评审后接受 C++ 故障处理修改，应使用匹配 Jazzy sysroot 的 xbuild 重编相关依赖并更新产物。
桌面/Linux 容器的构建成功不能代替目标 ABI、Python 安装路径和启动环境验证。

## 6. 已报告验证与复现入口

以下来自实现提交的验收记录；本评审文档编写本身没有重新运行这些测试。
历史 ROS/GUI 手工验收的完整原始日志未归档在仓库中，记录不构成独立证明，评审者可以要求复核。

- 5 项 visualizer 核心测试、8 项模拟模型测试、9 项扩展测试，合计 22 项通过；TypeScript 检查与打包通过。
- Linux/AArch64 Jazzy 容器构建六包模拟依赖集合；没有重新构建 RZ/V2H 真实 SPI 插件或覆盖旧目标产物。
- ROS 模拟集成检查：162 keys、47 TF 贴片、多通道不同颜色、NaN 故障/恢复、六关节位置反馈及贴片随动。
- 实际 Foxglove GUI：54/54 数据，ch0=0.30 与 ch1=0.80 同时保留并着不同色，FIST/OPEN 驱动模拟手。
- 仅暂停模拟源后约 0.66 秒全部贴片变灰，恢复源后恢复显示；这不验证单颗真实芯片冻结的检测。
- Compose 配置静态校验通过；同类依赖/构建流程在独立容器跑通，尚未单独重建整个 Compose 镜像。

纯逻辑测试可从仓库根目录复现（npm 依赖未安装时先在扩展目录执行 `npm ci`）：

```bash
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=src/utils/pnc_tactile_visualizer python3 -m unittest discover -s src/utils/pnc_tactile_visualizer/test
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=src/utils/pnc_hand_demo python3 -m unittest discover -s src/utils/pnc_hand_demo/test
npm --prefix foxglove/foxglove_inspire_hand_panels test
npm --prefix foxglove/foxglove_inspire_hand_panels run typecheck
```

ROS 构建、演示和 GUI 步骤见 [Foxglove 使用说明](../foxglove/README.md)。下面的可选检查**会发布模拟触觉和关节命令**，
仅做初次只读评审时不执行；需要复现时，仅对已确认独立 domain、mock-only URDF 的模拟系统运行：

```bash
ROS_DOMAIN_ID=77 python3 src/utils/pnc_hand_demo/test/demo_runtime_smoke.py
```

## 7. 请优先反驳或确认的风险

| 触发条件/边界 | 当前行为或待核实点 |
| --- | --- |
| 某芯片读取失败、其他芯片继续更新 | 失败芯片保留旧有限值，整条 values 流仍有新时戳；三维节点的 0.5 秒、诊断面板的 2 秒超时均只检测整条流停止，不能证明各芯片正常 |
| 读取成功但 `raw_i=0` | 原处理逻辑保留上一非零滤波值；应核实零值在传感器协议中的含义，不能直接解释为松手或无触摸 |
| `auto_tare=true` 且芯片恢复时仍被按住 | 恢复路径会重新 tare，可能把持续触摸当作新零点；需确定保留基线还是显式空载去皮 |
| 多颗芯片故障，尤其 `recovery_interval_frames=1` | 顺序选择恢复可能不公平；默认 40 不能证明所有配置正确 |
| `read()` 内发生同步恢复 | 成功走完 `initialize_chip()` 的固定等待合计已达 60 ms，另有传输/tare；不能保证恢复周期 25 ms，正常采集 40 Hz 也尚无目标板测量 |
| 实际响应大于 1、为负或方向相反 | 诊断颜色固定 0..1 可能饱和；3D 必须配置实际量程/方向，不可套用模拟值 |
| 直接使用示例映射或现有 CAD | 颜色位置可能错误；新增 PNC 载体未包含在原 CAD 中，装配方向和贴片坐标未实物核对 |
| BSP/板型或启动 DTB 与记录不同 | SPI/GPIO 编号、复用、电气状态和 ABI 可能不同，不能直接复用设备路径和 DTB 假设 |

硬件命名尤其要核实：说明称 **SPI6**，仓库 DTS 节点是 **`&spi0`**，Linux 默认路径是
**`/dev/spidev1.0`**。这三个名字属于不同层级，当前材料不足以证明它们在最终板上必然一一对应。
配置的 `/dev/gpiochip1`、9 个 offset、片选空闲 HIGH/选中 LOW、native SS 状态均需目标确认。
DTB 变更包含 WS125 的 SDHI0 VccQ 固定 1.8 V、释放 PA0 和 P70/P71 等假设，
须对照最终板型、电路和 BSP，不能把现有 DTB 当作所有 RZ/V2H 板型的通用文件。

## 8. 软件收尾与硬件验收的分界

硬件前可推进：定义每芯片有效性/年龄契约及失败显示，确定恢复 tare 策略，统一诊断/3D 量程配置，
改进恢复公平性与时序，整合真实启动入口，补有针对性的故障测试，并准备匹配目标的构建产物。
是否修改应由独立评审结论驱动；这些尚未完成，不能以模拟通过替代。

必须有实物/目标信息：最终接线与装配、每区通道对应、真实零点/极性/量程、SPI echo/CRC、电气 CS，
短时及多点触摸、故障恢复、完整帧耗时和机械手运动同时运行。验收记录应包括 Git 提交、
板型/BSP/内核/ROS 版本、DTB/库哈希、命令/参数及测量结果，而不只写“运行正常”。

现有记录引用的 NanoSen 手册、Gerber、原装配/CAD 资料并非都完整保存在本仓库；最终接线表、
装配数据、最终 BSP 版本和实测性能仍缺失。需要外部资料时明确列出缺什么，不能凭文档推断实物。

## 9. 给 Claude 的评审要求

请独立判断：这套实现能否在完成哪些具体条件后实现用户目标？哪些是已实现源码，哪些是模拟证据，
哪些结论仍未经硬件验证？允许推翻本文或已有项目状态的任何判断，以当前代码和可复现证据为准。

在评审任务中先只读输出：按 P0–P3 排序的具体问题，每项包含**文件与行号、触发条件、影响、复现方法、
最小修复建议和验证方式**；区分基线已有问题、本次引入问题及尚缺证据的疑点。
特别检查真实接口/QoS、并发触摸、陈旧值、tare/恢复时序、模拟隔离、映射量程和部署产物一致性。
最后给出“可继续软件联调／目标板部署前必须修复／只能等待硬件验证”的清单。
未发现可证实缺陷时明确说明，不为凑数量提出泛化重构。仅要求评审时不自动修改或合并代码；
如果用户已经要求修复，则按已有授权推进，不将本文的评审流程变成重复确认门槛。
