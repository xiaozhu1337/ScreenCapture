# Linux 版本

Linux 版按仓库中的 Windows 实现对齐截图、贴图、设置及工具操作，使用 Qt 6、
LayerShellQt 和桌面门户适配当前 CachyOS / KDE Wayland x86_64。
Windows 的 Ling / Direct2D 实现与构建流程保持原样。

## 功能和操作

- 托盘左键截图，右键菜单与 Windows 一致，仅有“设置”和“退出”；普通首次启动截图，
  登录自启以托盘待命。升级保留原 YAML 中的启动偏好。
- 全屏框选、窗口吸附、拖动选区、八方向调整、方向键微调、鼠标放大镜和颜色值。
  右键或 `Esc` 退出，`F3` 贴图，`Enter` 或双击复制。
- 选区确认后进入独立贴图。支持同时保留多张贴图、拖动和以光标为中心滚轮缩放；
  主工具栏与样式栏悬浮在图片旁，右键取消当前工具或显示、隐藏工具栏。
- 矩形、椭圆、箭头、序号、画笔、文字、马赛克和橡皮擦，支持对应的颜色、
  粗细、填充、圆角、线型、字体与数字模式，并按工具分别记住样式。
  绘制时的 `Shift` 约束、文字编辑、撤销和重做按 Windows 的规则处理。
- 保存 PNG、复制图片后关闭当前截图或贴图；底图像素与界面缩放分开，输出保留原始像素。
  `Ctrl+S` 保存，`Ctrl+C` 复制，`Ctrl+Z` 撤销，`Ctrl+Shift+Z` 重做。
- 自动滚动长截图，显示实时预览，可停止后保存、复制或转为贴图。
- MP4 / GIF 录制，MP4 支持系统声音、麦克风及混音；开始前可选择格式和音频源，
  录制中可保存、复制文件或放弃。MP4 上限 120 分钟，GIF 上限 6 分钟。
- 扫描二维码、中英文 OCR；OCR 使用专门 fork 的
  [ImageReader Linux 分支](https://github.com/xiaozhu1337/ImageReader/tree/linux-support)，
  保留 TinyOCR 和 PP-OCRv6 本地模型，以及 Windows 风格的图像、文字双面板，
  支持按识别框选择文字、左右面板联动和复制。
- 设置按 Windows 的侧栏、分页、菜单表格和开关布局实现，包含基础、快捷键、
  菜单和关于页面。语言、工具栏位置、截图菜单顺序、显隐和各工具样式即时保存。
  图标和翻译复用项目原有资源。

截图、OCR、二维码、长截图和录屏入口均来自同一全屏选区流程。
当前 KDE 上的窗口吸附通过 KWin 脚本取得窗口范围，保留小数坐标，支持普通窗口、
对话框、菜单、浮动工具窗和面板，并按前后遮挡顺序选择当前桌面与活动中的可见窗口。
窗口内部按钮、列表等子控件尚未接入无障碍接口识别；其他桌面可能无法提供窗口列表。

## 构建

需要 mise、Git、C++ 编译器、pkg-config，以及 Qt 6 Base / Wayland、OpenCV、
yaml-cpp、KGlobalAccel、LayerShellQt、PipeWire 开发文件。窗口探测回归测试另需
Qt 6 Qml 开发文件，用于执行生成的 KWin 脚本；主程序不依赖该模块。运行录制还需要 FFmpeg
和提供 PulseAudio 兼容接口的音频服务，音频源通过 `pactl` 查询。
当前机器已安装这些系统组件；mise 选用本机 xmake，管理 Python 和 ONNX Runtime。
OCR 构建配方目前覆盖 Linux x86_64，支持本机的 OpenCV 版本。

在项目根目录执行：

```sh
mise trust
HTTP_PROXY='http://127.0.0.1:7890' HTTPS_PROXY='http://127.0.0.1:7890' mise install
mise exec -- python 'Scripts/linux/build.py'
```

脚本检出固定版本的 ImageReader fork，构建 OCR 和截图程序，并执行下文所列测试。
外网下载使用 `http://127.0.0.1:7890`；后续构建复用 `build/deps` 缓存。
完整运行目录为 `build/linux/x86_64/release/`，需一并保留：

```text
ScreenCapture
ImageReader
models/PP-OCRv6_det_tiny.onnx
models/PP-OCRv6_rec_tiny.onnx
lib/libonnxruntime.so.1
```

Qt、OpenCV、PipeWire 和平台插件使用系统版本。这不是跨发行版的独立二进制包。

## 启动和配置

```sh
'./build/linux/x86_64/release/ScreenCapture'
'./build/linux/x86_64/release/ScreenCapture' --enter=tray
'./build/linux/x86_64/release/ScreenCapture' --enter=settings
'./build/linux/x86_64/release/ScreenCapture' --enter=ocr
'./build/linux/x86_64/release/ScreenCapture' --enter=long
'./build/linux/x86_64/release/ScreenCapture' --enter=video
'./build/linux/x86_64/release/ScreenCapture' --image '/absolute/path/image.png'
'./build/linux/x86_64/release/ScreenCapture' --output '/absolute/path/screenshot.png'
```

支持 `cap`、`pin`、`ocr`、`qr`、`long`、`video`、`tray` 和 `settings` 入口。
普通重复启动会把操作转发到常驻实例；`--output` 与 `--auto-quit=true` 使用独立进程。
`--auto-start=true` 以托盘待命模式启动，不发起截图请求。
复制时程序会维持 Wayland 剪贴板来源，避免关闭窗口后无法粘贴。

设置保存在 `$XDG_CONFIG_HOME/xland/ScreenCapture/config.yaml`，默认位置为
`~/.config/xland/ScreenCapture/config.yaml`，各字段均附有注释。已有配置会保留；
配置损坏时不直接覆盖原文件。登录自启写入
`~/.config/autostart/io.github.xland.ScreenCapture.desktop`。
默认全局截图快捷键为 `Ctrl+Alt+A`，贴图键为 `F3`；可在快捷键设置中修改。
全局键使用 KDE 的 KGlobalAccel，其他桌面可通过系统快捷键运行相应入口命令。

安装到用户目录无需 root：

```sh
mise exec -- python 'Scripts/linux/build.py' --install '/home/cat/.local'
```

安装包括主程序、OCR 程序、模型、ONNX Runtime 和桌面入口。

## Wayland 授权

截图使用桌面门户的 `interactive=false`，应用身份为
`io.github.xland.ScreenCapture`。当前 Qt 6.12 和 KDE 门户可以保存首次授权；
已授权后不会每次弹出 KDE 的范围、延迟和指针选择界面。
需要安装 `~/.local/share/applications/io.github.xland.ScreenCapture.desktop`，
使门户能识别应用，而非把终端识别为截图请求方。

当前用户可用以下系统命令明确保存截图许可：

```sh
busctl --user call \
  'org.freedesktop.impl.portal.PermissionStore' \
  '/org/freedesktop/impl/portal/PermissionStore' \
  'org.freedesktop.impl.portal.PermissionStore' \
  SetPermission sbssas screenshot true screenshot 'io.github.xland.ScreenCapture' 1 yes
```

程序自身不写入这项授权；撤销后截图会被拒绝。
录屏和自动滚动分别使用 ScreenCast、RemoteDesktop 门户和 PipeWire，
会由桌面请求屏幕共享、指针控制权限。截图许可不等同于这些权限。

## 验证与限制

构建脚本检查画布像素、缩放、标注、文字、马赛克、橡皮擦、历史记录，
托盘菜单、多贴图、选区、设置布局和即时保存、损坏配置保护、快捷键、
独立 OCR 的中英文识别、双面板选择、命令行和错误处理，
以及隔离 D-Bus 中的截图成功、取消、拒绝、异常响应和单实例转发。
窗口探测测试执行实际生成的 KWin 脚本，覆盖小数和负坐标、窗口类型、桌面与活动过滤、
遮挡顺序、悬停选区，以及无效响应、服务错误和超时回退。可在有可见窗口的 KDE 会话中
运行 `build/tests/release/ScreenCaptureWindowGeometryTests --probe` 检查真实窗口范围、悬停吸附和单击确认。
媒体测试使用真实 FFmpeg 验证 MP4、GIF 和音频编码，另检查滚动拼接、4K 匹配、
PipeWire 缓冲校验、背压恢复、录制时长、文件收尾和旧会话错误隔离。

离屏和模拟流测试不能代替真实共享授权、长页面滚动及音频设备录制。
当前单屏 KDE Wayland 已通过原生窗口探测、悬停吸附和单击确认检查；原生桌面完整测试
仍在贴图缩放往返的位置断言处失败。最后一次安装版门户截图因桌面锁屏返回黑帧，
未据此确认实际画面。多屏混合缩放、其他桌面、中文输入法和
跨应用剪贴板仍需实际环境验证。
Qt 与 Windows Direct2D 的字体栅格化、系统文件对话框和桌面授权界面由平台决定，
尚未与运行中的 Windows 程序做逐像素截图比对。

警告和错误异步写入可执行文件旁的 `logs/errors.log`；目录不可写时输出到 stderr。
OCR 临时图片使用用户缓存目录的私有临时目录；视频复制到剪贴板后保留对应文件，
以便剪贴板管理器继续访问。
