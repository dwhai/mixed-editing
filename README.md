# MixedEditing

一个基于 Qt6 开发的跨平台混合编辑器应用程序。

## 项目简介

MixedEditing 是一个使用 C++ 和 Qt6 框架构建的现代化桌面应用程序，支持跨平台运行（macOS、Windows、Linux）。项目采用深色主题，具有优雅的界面设计和良好的用户体验。

## 技术栈

- **编程语言**: C++11
- **UI 框架**: Qt6 (Core, Gui, Widgets, Svg)
- **构建系统**: CMake 3.16+
- **平台支持**: macOS, Windows, Linux
- **开发环境**: CLion (推荐)

## 功能特性

- ✅ 跨平台桌面应用支持
- ✅ 现代化深色主题界面
- ✅ 窗口自动居中显示
- ✅ macOS 原生标题栏集成
- ✅ SVG 矢量图标支持
- ✅ Qt UI 设计器集成

## 系统要求

### 必需依赖

- **CMake**: 版本 3.16 或更高
- **Qt6**: 包含以下模块
  - Qt6 Core
  - Qt6 Gui
  - Qt6 Widgets
  - Qt6 Svg
- **C++ 编译器**: 支持 C++11 标准
- **macOS 特定**: AppKit Framework (已自动配置)

## 构建指南

### 1. 克隆项目

```bash
git clone <repository-url>
cd MixedEditing
```

### 2. 安装依赖

#### macOS

使用 Homebrew 安装 Qt6:

```bash
brew install qt6
```

或从 [Qt 官网](https://www.qt.io/download) 下载 Qt6 开发工具包。

#### Ubuntu/Debian

```bash
sudo apt update
sudo apt install qt6-base-dev qt6-svg-dev cmake build-essential
```

#### Windows

从 [Qt 官网](https://www.qt.io/download) 下载并安装 Qt6，确保包含 Qt6 Tools 模块。

### 3. 配置和构建

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译项目
make -j$(nproc)  # Linux/macOS
# 或使用 make -j4 等指定线程数
```

### 4. 运行应用

```bash
# 在构建目录中
./MixedEditing
```

## 项目结构

```
MixedEditing/
├── CMakeLists.txt              # CMake 构建配置
├── main.cpp                    # 应用程序入口点
├── .gitignore                  # Git 忽略配置
├── README.md                   # 项目文档
├── widget/
│   ├── include/                # 头文件目录
│   │   ├── mainwindow.h        # 主窗口头文件
│   │   └── mac_titlebar.h      # macOS 标题栏头文件
│   ├── source/                 # 源代码目录
│   │   ├── mainwindow.cpp      # 主窗口实现
│   │   └── mac_titlebar.mm     # macOS 标题栏实现 (Objective-C++)
│   ├── ui/                     # UI 设计文件
│   │   └── mainwindow.ui       # Qt Designer UI 文件
│   └── resources/              # 资源文件
│       ├── icons.qrc           # Qt 资源配置文件
│       └── icons/              # SVG 图标目录
│           ├── help.svg
│           ├── message.svg
│           └── settings.svg
```

## 开发说明

### Qt 自动代码生成

项目启用了 Qt 的自动化工具：

- **AUTOMOC**: 自动生成 Meta-Object 代码
- **AUTOUIC**: 自动生成 UI 头文件
- **AUTORCC**: 自动生成资源代码

这些文件会在构建时自动生成，无需手动管理。

### macOS 特殊支持

在 macOS 平台上，项目集成了原生 AppKit 标题栏功能，通过 `mac_titlebar.mm` 文件实现。该文件仅在 macOS 平台编译。

### 深色主题

应用程序默认使用深色主题，通过以下代码设置：

```cpp
QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
```

## Git 配置

项目已配置 `.gitignore` 文件，自动忽略：

- 构建产物（`build/`, `cmake-build-*/`）
- Qt 自动生成文件（`*_autogen/`, `ui_*.h` 等）
- IDE 配置文件（`.idea/`, `.vscode/`）
- 编译中间文件（`*.o`, `*.obj`, `*.so` 等）
- 系统文件（`.DS_Store`）

## 许可证

本项目仅供学习和开发使用。

## 贡献

欢迎提交 Issue 和 Pull Request！

## 联系方式

- 开发者: Anlk
- 创建日期: 2026-06-02
