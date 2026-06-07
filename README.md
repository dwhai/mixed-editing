# MixedEditing

一个基于 Qt6 + FFmpeg 的桌面视频应用：既能浏览/播放在线视频，也能在时间线上进行多轨剪辑并导出成片。

## 项目简介

MixedEditing 使用 C++17 与 Qt6 构建，整体分为两大部分：

- **内容浏览与播放**：对接「开眼（Kaiyan）」开放接口，拉取首页信息流、分类、专题、排行榜与相关推荐，提供基于 FFmpeg 的自研播放器（音画同步、倍速、字幕、网络流直读）。
- **视频剪辑工作区**：以 SQLite 持久化工程数据，提供素材库、多轨时间线、合成预览与 MP4 导出，核心解码/合成/编码全部基于 FFmpeg。

界面采用统一的浅色主题，并在 macOS 上集成原生标题栏。

## 技术栈

- **编程语言**：C++17
- **UI 框架**：Qt6（Core / Gui / Widgets / Svg / Network / Multimedia / OpenGL / OpenGLWidgets / Sql）
- **音视频处理**：FFmpeg 7（libavcodec / libavformat / libavutil / libavfilter / libswscale / libswresample）
- **图片解码**：libwebp（开眼封面统一返回 webp，作为 QPixmap 解码回退路径）
- **数据持久化**：SQLite（通过 Qt Sql，启用 WAL + 外键，带版本迁移）
- **构建系统**：CMake 3.16+，使用 pkg-config 查找 FFmpeg / libwebp
- **平台支持**：macOS、Windows、Linux
- **开发环境**：CLion（推荐）

## 功能特性

### 内容浏览与播放

- 开眼接口对接：首页信息流（分页游标）、分类、专题、排行榜、视频详情、相关推荐
- 视频卡片瀑布流展示，webp 封面解码
- 自研 FFmpeg 播放器：
  - 多线程架构（解封装 / 视频解码 / 音频解码分线程）
  - 以音频为主时钟的音画同步，墙钟平滑兜底
  - OpenGL 控件渲染 YUV420P 帧
  - 倍速播放（atempo 保持音调）、音量控制、进度上报
  - 内嵌字幕解码与按时间显示
  - 本地文件与 http/https 网络流直接播放

### 视频剪辑

- 工程数据落库（工程 / 序列 / 轨道 / 片段 / 特效 / 转场 / 关键帧 / 文字 / 导出任务）
- 素材导入：探测媒体信息后写入素材库，支持本地与开眼来源
- 多轨时间线：视频/音频轨道，片段拖动、删除，胶片缩略条与音频波形（后台线程解码）
- 合成预览：按时刻取各可见轨片段，按图层序叠加（位置/缩放/不透明度），OpenGL 实时预览
- 播放头定位与逐帧步进（±1 帧）
- 导出为 MP4（H.264 视频 + AAC 音频），后台线程逐帧合成编码，进度回报、可取消

## 系统要求

### 必需依赖

- **CMake** 3.16 或更高
- **Qt6**：Core、Gui、Widgets、Svg、Network、Multimedia、OpenGL、OpenGLWidgets、Sql
- **FFmpeg 7**：libavcodec、libavformat、libavutil、libavfilter、libswscale、libswresample
- **libwebp**
- **pkg-config**（用于定位 FFmpeg 与 libwebp）
- **C++ 编译器**：支持 C++17
- **macOS 特定**：AppKit Framework（已自动配置）

## 构建指南

### 1. 克隆项目

```bash
git clone <repository-url>
cd MixedEditing
```

### 2. 安装依赖

#### macOS

```bash
brew install qt6 ffmpeg webp pkg-config cmake
```

或从 [Qt 官网](https://www.qt.io/download) 下载 Qt6 开发工具包。

#### Ubuntu/Debian

```bash
sudo apt update
sudo apt install cmake build-essential pkg-config \
  qt6-base-dev qt6-svg-dev qt6-multimedia-dev \
  libavcodec-dev libavformat-dev libavutil-dev \
  libavfilter-dev libswscale-dev libswresample-dev \
  libwebp-dev
```

> 注：FFmpeg 需为 7.x，发行版自带版本较旧时建议自行编译或使用第三方源。

#### Windows

从 [Qt 官网](https://www.qt.io/download) 安装 Qt6，并准备 FFmpeg 7 与 libwebp 开发库，确保 `pkg-config` 能在 `PKG_CONFIG_PATH` 中找到对应的 `.pc` 文件。

### 3. 配置和构建

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

### 4. 运行应用

```bash
# 在构建目录中
./MixedEditing
```

## 项目结构

```
MixedEditing/
├── CMakeLists.txt              # CMake 构建配置（Qt6 + FFmpeg + libwebp）
├── main.cpp                    # 应用入口：打开数据库、居中显示主窗口
├── README.md                   # 项目文档
├── computer_submit_line.sh     # 代码行数统计脚本
├── docs/
│   ├── 剪辑功能路线图.md        # 剪辑功能分阶段规划
│   ├── 数据库设计.md            # 剪辑数据库表结构设计
│   └── 导出设计.md              # 时间线合成与 MP4 导出设计
└── src/
    ├── widget/                 # UI 层
    │   ├── include/ source/    # 主窗口、首页、视频列表/卡片、播放器对话框、
    │   │                       # 剪辑窗口 EditorWindow、时间线 TimelineView、
    │   │                       # OpenGL 视频控件、顶栏、主题、通用组件
    │   └── source/mac_titlebar.mm  # macOS 原生标题栏（Objective-C++）
    ├── ffmpeg/                 # 音视频引擎
    │   ├── MediaPlayer         # 多线程播放器（音画同步）
    │   ├── MediaProbe          # 媒体信息探测
    │   ├── ClipSource          # 片段解码（剪辑用，独立解码游标）
    │   ├── CompositionEngine   # 时间线合成引擎
    │   ├── Exporter            # 分段并行 MP4 导出（H.264 + AAC）
    │   └── AudioOutput         # QAudioSink 音频输出
    ├── network/                # 开眼 API 接口与数据模型
    │   ├── VideoAPI            # Feed/分类/专题/排行/详情/相关推荐
    │   └── models/             # Author/Tag/Provider/Consumption/VideoData…
    ├── db/                     # 剪辑数据层
    │   ├── Database            # SQLite 连接、迁移、事务、UUID/时间戳工具
    │   ├── DbModels            # 工程/序列/轨道/片段等模型
    │   └── Repositories        # 各实体仓库
    └── resources/
        └── icons.qrc / icons/  # SVG 图标资源
```

## 架构说明

### 播放器线程模型

`MediaPlayer` 采用解封装 / 视频解码 / 音频解码三线程协作，以音频时钟为主时钟做音画同步，GUI 定时器按主时钟从帧队列取帧送显。解码引擎不直接依赖 UI，到点的帧通过信号发出由 OpenGL 控件接收。

### 剪辑数据与合成

剪辑工程全部落库到 SQLite（详见 `docs/数据库设计.md`）。`CompositionEngine` 在给定时刻取所有可见视频轨命中的片段，按图层序叠加位置/缩放/不透明度，输出 YUV420P 帧供预览或导出。

### 导出渲染

导出（`Exporter`）作为 worker 跑在独立线程，与主线程预览完全隔离：

- **快照式隔离**：主线程在起线程前把时间线（轨道/片段/素材路径）快照进 `Request`，worker 运行期间不访问数据库。
- **分段并行编码**：把整个帧区间切成多个连续区间并行编码到临时 MP4，每段自带独立 `CompositionEngine` 顺序合成。强制无 B 帧（`pts==dts`），最终用 stream-copy 拼接并重写全局递增时间戳，拼接近乎零成本。
- **编码器探测**：优先 macOS 硬件 `h264_videotoolbox`（试开探测可用性），失败回退 `libx264`。
- **音频预混**：所有音频片段重采样到 48k 立体声后按时间线叠加成整轨缓冲，编码为 AAC 并与视频按时间手动交错写入。
- **可观测/可取消**：原子计数 + 轮询上报进度，取消通过原子标志在各检查点生效，失败时清理半成品。

完整设计见 `docs/导出设计.md`。

### Qt 自动代码生成

启用 AUTOMOC / AUTORCC，构建时自动处理 Meta-Object 与资源代码。

### 主题与 macOS 支持

默认浅色主题，由 `main.cpp` 设置 `Qt::ColorScheme::Light`，视觉样式统一由 `Theme` 提供。macOS 上通过 `mac_titlebar.mm` 集成原生标题栏（仅在该平台编译）。

## Git 配置

`.gitignore` 已忽略：

- 构建产物（`build/`、`cmake-build-*/`）
- Qt 自动生成文件（`*_autogen/`、`ui_*.h` 等）
- IDE 配置文件（`.idea/`、`.vscode/`）
- 编译中间文件（`*.o`、`*.obj`、`*.so` 等）
- 系统文件（`.DS_Store`）

## 许可证

本项目仅供学习和开发使用。

## 贡献

欢迎提交 Issue 和 Pull Request。

## 联系方式

- 开发者：Anlk
- 创建日期：2026-06-02
