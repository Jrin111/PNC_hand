# 左手三维触觉显示

本阶段：左手 47 区热力图、54 路诊断、现有机械手控制、独立模拟模式。
暂不接入触碰手势和相机关键点。

## 在没有硬件时运行

Mac/Windows 上先启动 Docker/OrbStack，在仓库根目录执行：

```bash
docker compose -f compose.demo.yaml up --build
```

这个入口只复制源码到 Linux Jazzy 镜像，不挂载硬件或旧目标产物；WebSocket
仅向本机开放。首次需要下载镜像和 ROS 依赖。本次本机验证用的独立测试容器已
占用 8765 端口，如要改用 Compose，先停止它：
`docker stop pnc-foxglove-jazzy-review`。Compose 配置已静态校验，以下相同的
依赖和构建流程已在 Jazzy 容器实际跑通；尚未重复构建 Compose 镜像。

在 **ROS 2 Jazzy Linux** 开发环境构建源码；不要使用仓库内的旧 `install/`
启动本次新增演示。该目录保留的是此前的 RZ/V2H 目标板产物。

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src --packages-up-to pnc_hand_demo \
  --build-base build --install-base install_local
source install_local/setup.bash
ros2 launch pnc_hand_demo hand_demo.launch.py
```

默认左手，模拟节点全部位于 ROS domain 77。这个 domain 应只用于模拟。
默认自动扫描触觉通道；手指由控制面板操作，不自动运动。

在 Foxglove Desktop：

1. 安装 `foxglove_inspire_hand_panels/renesasuxsst.foxglove-dexhand-panels-1.1.2.foxe`。
2. 导入 `pnc_left_hand_3d.json` 布局。
3. 通过 Foxglove WebSocket 连接 `ws://localhost:8765`，如果在另一台电脑运行 ROS，
   用那台电脑的 IP。
4. 右侧 **PNC Tactile Diagnostics** 显示 54 路数据。新鲜的模拟心跳到达后，
   手动启用 Simulation 输入，选择通道并调整强度；可以保留多个通道同时受力。
5. 左侧 47 区贴片随手指移动，并按各区强度独立变色。下方标签页保留关节控制、
   Inspire 内部力显示和夹爪控制；模拟环境中的真实力反馈显示 Unknown。

蓝→青→绿→黄→红代表相对触觉响应从弱到强。灰色代表缺失、无效或消息过期，
不是“零受力”。模拟范围为 0..1；不把模拟值或未标定 PNC 值标成牛顿。

`pnc_left_hand_preview.png` 是 CAD 贴片位置预览，颜色只是分区示意，不是实测数据。

## 接入真实手

这次已按原采集代码的 ROS 接口完成集成。真实数据链路是：

```text
9 颗 RAA 的 SPI 轮询 → 原硬件插件 read()
→ 54 通道 × raw_i/raw_q/value = 162 个状态接口
→ /tactile/tactile_hand_state_broadcaster/names  (control_msgs/msg/Keys)
  /tactile/tactile_hand_state_broadcaster/values (control_msgs/msg/Float64Values)
→ pnc_tactile_visualizer → /tactile/markers → foxglove_bridge → Foxglove 三维热力图
```

**PNC Tactile Diagnostics** 直接订阅同一组真实 `names`/`values`，显示每个通道的
I/Q/value，不依赖模拟节点。硬件采集正常后，经 bridge 即可看到通道数值变化；
三维表面准确对应触摸位置，还需要下述真实映射。模拟输入仅用于独立演示，真实
采集时不启动 `pnc_hand_demo`，也不需要勾选 Simulation。

热力图需要知道每个电气通道对应哪个手部区域，即“接线/通道映射”。配置在
`src/utils/pnc_tactile_visualizer/config/pnc_zones_left.json`。已有资料确认了每指
5 区、掌部 22 区；9 颗芯片到手指/掌区的完整线序尚未确认。

当前 `demo_channel` 只是模拟例子，实际 `channel` 都留空。硬件接好后逐区按压，
确认通道、贴片方向及实际位置，再填写 `channel`、设置 `mapping_verified=true`，
并以 `mapping_profile:=verified` 加载该文件。显示范围、方向/增益应根据实际
响应调整；牛顿标定另外进行。三维显示支持 `color_min`/`color_max` 和每区
`gain`/`offset`。诊断面板目前仍按 0..1 着色，真实数据可读，但超出该范围时
颜色会饱和，不能据此判断实际力度。详见
`src/utils/pnc_tactile_visualizer/README.md`。

当前真实 bringup 不会自动启动新增的 visualizer。先把当前源码中的新增包和更新
后的 bringup 安装到与 RZ/V2H Jazzy 匹配的新 overlay。仓库旧 `install/` 不包含
可视化包或本次新增的 `launch_foxglove` 开关，不能只更新扩展后继续依赖旧产物。
后续修复还改变了 `ssc_tactile_hand_ros2_control` 的 C++：芯片本轮采集失败时输出 NaN，
其他芯片继续更新。当前全部 9 个包已通过匹配 RZ/V2H Jazzy 的 xbuild 构建；应部署新 overlay，
其中也包含原 Inspire 串口与夹爪 profile 的后续修复。旧二进制仍会保留故障旧值。
下面是硬件部署完成后的四终端启动方式；串口、SPI/GPIO 接线、映射和量程必须先
按实物确认。

每个终端先执行，替换 overlay 路径。这里使用真实系统的 domain 0；全部节点保持
一致，并与 domain 77 的独立模拟源隔离：

```bash
source /opt/ros/jazzy/setup.bash
source /absolute/path/to/current_overlay/setup.bash
export ROS_DOMAIN_ID=0
```

终端 1：启动原左手机械手栈，提供关节状态和 URDF/TF；串口按实际修改。关闭该
launch 默认内置的 bridge，由终端 4 启动唯一的 bridge。

```bash
ros2 launch inspire_rh56e2_hand_bringup \
  inspire_rh56e2_hand_joint_position_control.launch.py \
  hand_side:=left use_mock_hardware:=false serial_port:=/dev/ttyUSB0 \
  launch_foxglove:=false
```

终端 2：启动原 SPI 触觉采集栈；确认安装的 `tactile_hand.yaml` 是本块开发板的
SPI/GPIO 和芯片顺序配置。

```bash
ros2 launch ssc_tactile_hand_bringup ssc_tactile_hand.launch.py namespace:=tactile
```

终端 3：启动真实数据的三维显示转换。映射文件须已核对全部 47 区；以下 0..1000
只是展示参数写法，必须替换成实测响应范围，并按需要设置映射文件中的 gain/offset。

```bash
ros2 run pnc_tactile_visualizer tactile_visualizer --ros-args \
  -p hand_side:=left -p mapping_profile:=verified \
  -p mapping_file:=/absolute/path/to/verified_zones.json \
  -p color_min:=0.0 -p color_max:=1000.0
```

终端 4：启动 bridge，显式允许模型实际使用的大小写 `.STL` 文件，并提供原控制
面板所需的发布/服务能力。先确保 8765 未被其他 bridge 占用。

```bash
ros2 run foxglove_bridge foxglove_bridge --ros-args \
  -p port:=8765 -p address:=0.0.0.0 \
  -p 'capabilities:=[clientPublish,parameters,parametersSubscribe,services,connectionGraph,assets]' \
  -p 'asset_uri_allowlist:=["^package://inspire_rh56e2_hand_description/meshes/(left|right)/[A-Za-z0-9_]+[.][sS][tT][lL]$"]'
```

Foxglove 使用现有扩展和左手布局，连接 `ws://<RZ/V2H的IP>:8765`。上述命令基于
当前源码接口；本次实际运行验收仍是模拟环境，不代表这四终端真实硬件流程已经验收。

当前驱动源码已改为单芯片采集失败即输出 NaN，令对应区域变灰，其他芯片继续显示。
旧 `install/` 二进制仍有故障旧值冻结问题，必须部署新 xbuild 输出才能得到修复。
全局故障若导致停播，显示层依靠超时变灰；新鲜消息不等于全部芯片健康。
成功读取零 I 时的保留策略及恢复 tare 行为仍见 `PROJECT_STATUS.md`。

## 多个区域同时触摸

支持。54 是 **9 颗芯片 × 6 个电气槽**，三维图对应 **47 个物理区域**，二者不是
同一个数量。原采集代码在每轮 `read()` 中依次读取芯片，将各通道独立保存在
整帧中，再由 broadcaster 发布；诊断面板显示全部通道，三维图逐区独立计算颜色，
没有“只保留当前选中通道”或“只显示最大触点”的限制。

模拟时选中通道只是选择要修改的滑块目标。先把 ch0 设为 0.30，再把 ch1 设为
0.80，两个值会同时保留；这组操作已通过实际 Foxglove 界面和 ROS 数据验证。
持续按住多个真实区域也应在相邻的整帧中一起反映出来，但 SPI 是顺序轮询，各点
并非在同一瞬间采样。配置的 40 Hz / 25 ms 是采集周期目标，尚未在实机测量；
短于扫描周期的快速接触、恢复时延及多点压力响应仍需硬件验收。

## 验证

初始 1.1.1 版本已实际通过：ROS 模拟集成检查，以及仅暂停模拟数据源后的数据流超时
灰化/恢复检查。停止数据后约 0.66 秒，47 区全部变灰。5 项可视化核心测试、
8 项模拟模型测试和 9 项扩展测试通过。扩展已更新安装至 1.1.1。

Foxglove Desktop 的实际界面验收也已完成：54/54 数据接收、三维手模型与贴片
显示正常；面板清零及 ch0=0.30、ch1=0.80 的同时输入已从 ROS 侧核实，两个
掌区确实显示不同颜色。FIST/OPEN 命令、六关节模拟反馈和三维姿态变化正常。
1.1.1 修复了低高度关节面板的控件重叠，改为保持卡片高度并纵向滚动。
更新本地扩展后需刷新当前视图，以加载新代码并重新获得通道名称。

评审修复的扩展 1.1.2 已重新打包并安装到本机，12 项扩展测试、类型检查和构建通过；
安装的 JS 与包内 JS 一致，刷新 Foxglove 后加载新代码，异常长度帧显示为未知。
当前 smoke 只向 `/pnc_demo/tactile_values` 发送模拟数据，结束时只清零模拟触觉。
它被动核对关节反馈和 TF，已移除所有运动指令及主动运动验收；上述 FIST/OPEN 为历史 GUI 验证。
真实驱动的 NaN 修复和后续旧包修复均已完成目标交叉构建，记录见根目录 `PROJECT_STATUS.md`；
仍需匹配板端运行依赖、部署及上板验收。当前位置控制器没有“停止发命令 0.5 秒后自动停手”的能力。

```bash
PYTHONPATH=src/utils/pnc_tactile_visualizer python3 -m unittest discover \
  -s src/utils/pnc_tactile_visualizer/test
PYTHONPATH=src/utils/pnc_hand_demo python3 -m unittest discover \
  -s src/utils/pnc_hand_demo/test
# 另一个已 source 的终端，模拟演示运行期间：
ROS_DOMAIN_ID=77 python3 src/utils/pnc_hand_demo/test/demo_runtime_smoke.py
```

扩展重建：在 `foxglove_inspire_hand_panels` 内执行 `npm ci`、`npm test`、
`npm run typecheck`、`npm run package`。Linux 模拟构建不替代 RZ/V2H Jazzy
xbuild 或最终硬件验证。
