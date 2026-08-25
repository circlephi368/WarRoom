# 战略地图管理器 (WarRoom)

> 一款基于 Qt6 的无限画布战略节点管理工具，用于整理复杂的逻辑关系、思维地图和战略规划。

![App Icon](WarRoom/resources/app_icon_128.png)

## 特性

### 🎨 无限画布交互
- **无限画布**：自由缩放、平移，支持鼠标滚轮缩放 + 中键/空格键平移
- **键盘平移**：支持方向键 / WASD 平移画布
- **背景样式**：支持点状网格、方格网格、自定义图片背景
- **相机动画**：平滑的相机动画过渡

### 🧩 节点系统
- **多种节点类型**：普通叶节点 (Leaf)、分组节点 (Group)
- **分组折叠**：分组节点可折叠，支持折叠后迷你图标/数量徽章/缩略色块三种显示模式
- **层级关系**：节点支持父子层级结构，自动管理层级坐标和深度排序
- **自定义颜色**：每个节点可独立设置颜色，支持颜色继承
- **尺寸调整**：节点可自由调整大小，支持拖拽缩放
- **优先级与标签**：每个节点可设置优先级（0-10）和标签（tags）
- **待办状态**：可将节点标记为待办/已完成，侧边栏集中管理待办列表
- **长文本内容**：节点支持 Markdown 格式的长文本正文与预览模式
- **置顶排序**：节点支持 Z 轴层级，可动态调整显示前后顺序

### 🔗 连线系统
- **6 种语义连线类型**：依赖、矛盾、转化、启发、否定、使用方法
- **灵活锚点**：节点锚点（带 4 边自动对齐）和自由画布锚点
- **路径路点**：支持自定义路点（waypoints），手动调整连线路径
- **连线标签**：每条连线可编辑文字标签，带半透明背景浮框
- **连线颜色**：每条连线可自定义颜色
- **锚点拖拽**：拖拽节点边缘锚点创建连线，拖到空白处自动新建节点并连接
- **右键菜单**：连线支持右键菜单，可编辑文字、清除文字、修改颜色、删除连线

### 🔌 节点模组系统（插件扩展）
内置模组机制，支持自定义节点的外观、交互、数据和编辑器：
- **ImageMod 图片模组**：拖放图片文件创建图片节点，自动管理相对路径
- **VideoMod 视频模组**：视频文件节点，提取首帧缩略图 + 播放图标
- **AnnotationMod 标注模组**：辅助类模组，用于节点标注
- **WebMod 网页模组**：嵌入网页内容的节点（基于 QWebEngine）
- **模组能力**：自定义渲染、右键菜单扩展、拖放支持、键盘/鼠标事件拦截、嵌入 Widget、存档资源打包
- **主/辅模组**：每个节点绑定一个主模组，可叠加多个辅助模组

完整模组开发指南：[`mod/MOD_DEVELOPMENT_GUIDE.txt`](mod/MOD_DEVELOPMENT_GUIDE.txt)

### ⚡ 操作与编辑
- **完整的 Undo/Redo**：所有节点/连线操作均支持撤销重做（命令模式实现）
  - 新增/删除/移动/缩放节点
  - 编辑节点文字
  - 修改节点颜色/连线颜色
  - 修改连线文字
  - 新增/删除连线
- **快捷操作**：拖拽节点、连线锚点、批量选择、分组收纳子节点
- **焦点指示器**：黄色直角三角形焦点指示，清晰显示当前操作焦点对象
- **右键菜单**：节点和连线均提供功能丰富的深色扁平右键菜单

### 💾 存档格式
- **JSON 格式**：所有数据以 JSON 序列化保存
- **两种存档方式**：
  - `.warroom` 单文件：所有内容在一个 JSON 文件中
  - `.warroom/` 文件夹存档：`board.json` + `mod_data/<modId>/` 资源目录，模组资源会被自动拷贝打包
- **自动识别加载**：`loadFromAuto` 自动识别单文件或文件夹存档
- **视图状态保存**：相机位置、缩放等级一并保存

### 🌍 战区 (Zone) / 侦察记录 / 时间轴（底层支持）
数据模型已预留：
- 战区 (WarZone)：可将节点划分到不同战区
- 侦察记录 (ScoutAction)：记录探索/侦察操作日志
- 时间轴 (TimelineEntry)：操作时间戳记录

> 注：这些功能在当前 UI 层尚未完全暴露，可通过扩展模组或自定义 UI 使用。

## 项目结构

```
.
├── main.cpp                      # 入口
├── WarRoom.rc                    # 资源（图标）
├── myQtproject.vcxproj           # Visual Studio 工程
├── myQtproject.qrc               # Qt 资源
├── myQtproject.ui                # UI 布局
├── resource.h
│
├── core/                         # 核心数据层（与 UI 解耦）
│   ├── command/                  # 命令系统（Undo/Redo）
│   │   ├── undo_manager.h/cpp
│   │   ├── add_node_command      # 新增节点
│   │   ├── delete_node_command   # 删除节点
│   │   ├── move_node_command     # 移动节点
│   │   ├── resize_node_command   # 缩放节点
│   │   ├── edit_node_command     # 编辑节点文本
│   │   ├── set_node_color_command
│   │   ├── add_link_command
│   │   ├── delete_link_command
│   │   └── set_link_label_command
│   │
│   ├── serialization/            # JSON 序列化
│   └── warroom/                  # 数据模型
│       ├── warroom_types.h       # Uuid、Color、Point2D 等基础类型
│       ├── war_room_model.h/cpp  # 核心文档模型
│       ├── war_node.h            # 节点结构
│       ├── war_link.h            # 连线结构
│       ├── war_zone.h            # 战区结构
│       └── scout_action.h
│
├── ui/                           # Qt UI 层
│   ├── WarRoomMainWindow.h/cpp   # 主窗口
│   ├── warroomview.h             # 视图（QGraphicsView 子类）
│   ├── NodeGraphicsItem.h/cpp    # 节点图元
│   ├── LinkGraphicsItem.h/cpp    # 连线图元
│   ├── ConnectionAnchor.h/cpp    # 连接锚点
│   ├── TempConnectionItem.h/cpp  # 拖拽创建连线时的临时连线
│   ├── LinkCreationManager.h/cpp # 连线创建流程管理器
│   ├── HighlightOverlay.h        # 焦点指示 + 节点高亮叠加层
│   ├── CustomTitleBar.h          # 自定义标题栏
│   ├── CustomSidebar.h           # 左侧自定义侧边栏
│   ├── TodoSidebar.h             # 待办侧边栏
│   ├── CustomTextEdit.h/cpp      # 自定义文本编辑
│   ├── ColorPickerDialog.h/cpp   # 颜色选择器
│   ├── CameraAnimator.h/cpp      # 相机动画
│   └── WindowHelper.h
│
├── mod/                          # 模组系统
│   ├── NodeMod.h                 # 模组基类（~30 个虚函数接口）
│   ├── ModManager.h              # 模组管理器（单例）
│   ├── KeyBinding.h              # 键位绑定系统
│   ├── MOD_DEVELOPMENT_GUIDE.txt # 模组开发指南
│   └── builtin/                  # 内置模组
│       ├── BuiltinMods.h         # 模组注册入口
│       ├── ImageMod.h            # 图片节点
│       ├── VideoMod.h            # 视频节点
│       ├── WebMod.h              # 网页节点
│       └── AnnotationMod.h       # 标注辅助模组
│
├── nlohmann/                     # JSON 库（header-only）
│   ├── json.hpp
│   └── json_fwd.hpp
│
├── resources/                    # 资源文件
│   ├── app_icon.ico              # Windows 应用图标（多尺寸）
│   ├── app_icon.png              # 256x256 PNG 图标
│   ├── app_icon_16/32/48/64/128/256.png
│   └── gen_icon.py               # 图标生成脚本（Python Pillow）
│
└── test_main.cpp                 # 单元测试入口
```

## 构建

### 环境要求
- **操作系统**：Windows 10 / 11
- **IDE**：Visual Studio 2022（MSVC 工具集）
- **Qt 版本**：Qt 6.x （推荐 6.5+）
  - 必需模块：`QtCore`、`QtGui`、`QtWidgets`
  - 可选模块：`QtMultimedia`（视频模组需要）、`QtWebEngineWidgets`（网页模组需要）
- **C++ 标准**：C++17 或更高

### 构建步骤
1. 确保已安装 Qt Visual Studio Tools 扩展，或正确配置了 Qt 头文件和库路径
2. 打开 [myQtproject.vcxproj](myQtproject.vcxproj)
3. 检查项目属性 → Qt 安装路径，指向本地 Qt6 路径
4. 选择 Release | x64 配置
5. 点击生成

### 其他说明
- JSON 序列化库使用 [nlohmann/json](https://github.com/nlohmann/json)（header-only，已内置，无需额外安装）
- 项目当前仅提供 Visual Studio 工程文件 (`.vcxproj`)，未提供 CMake

## 快速上手

1. 启动程序后，会出现一块空画布
2. 在画布上右键 → **新增节点**，或直接拖放图片/视频文件到画布（会自动创建对应模组节点）
3. **拖动节点**：鼠标拖拽
4. **创建连线**：鼠标悬停节点，四周边缘出现锚点，拖拽锚点到目标节点（或空白处新建节点）
5. **编辑节点**：双击节点重命名标题；右键节点可编辑长文本、设置颜色、切换预览模式、标记待办、调整层级等
6. **编辑连线**：右键连线可编辑标签文字、修改颜色、删除
7. **视图操作**：滚轮缩放，中键拖拽（或空格键+左键）平移画布，方向键 / WASD 平移
8. **撤销/重做**：`Ctrl+Z` 撤销，`Ctrl+Y` 重做
9. **保存**：`Ctrl+S`，保存为 `.warroom` 文件

## 开源协议

MIT License. 参见 `LICENSE` 文件。

## 开发扩展

- 想要自定义节点模组？请阅读 [`mod/MOD_DEVELOPMENT_GUIDE.txt`](mod/MOD_DEVELOPMENT_GUIDE.txt)
- 想要修改/新增 Undo/Redo 命令？参考 `core/command/` 目录下的现有命令模式
- 想要新增连线类型？在 `core/warroom/war_link.h` 的 `LinkType` 枚举中添加，并更新 UI 渲染

## 鸣谢

- [Qt](https://www.qt.io/) - 跨平台 UI 框架
- [nlohmann/json](https://github.com/nlohmann/json) - 现代 C++ JSON 库
