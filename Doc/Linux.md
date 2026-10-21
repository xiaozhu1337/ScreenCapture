# Linux 版本

Linux 版使用 Qt 6 原生窗口，通过 xdg-desktop-portal 截图，适用于当前的
CachyOS / KDE Wayland x86_64，也可在提供截图门户的 X11 桌面使用。
Windows 版仍使用原有 Ling / Direct2D 实现和构建流程。

## 已支持

- 桌面门户截图，包括系统提供的区域、窗口或全屏选择。
- 打开图片、拖动框选、裁剪，保存与复制整图或选区。
- 画笔、矩形、椭圆、箭头、文字、颜色和线宽调整、标注撤销与重做。
- PNG 保存、图片剪贴板、置顶钉图、系统托盘和二维码识别。
- 中英文 OCR：使用专门 fork 并移植的 [ImageReader](https://github.com/xiaozhu1337/ImageReader/tree/linux-support)，
  保留原 TinyOCR 引擎和 PP-OCRv6 模型，本地推理，无需上传图片。

滚动长截图、GIF/MP4 录屏、自动窗口吸附、马赛克、橡皮擦、标注对象编辑、
取景放大/拾色、自由缩放、应用内全局快捷键配置和多语言设置尚未移植。
Linux 界面目前使用中文。这里列出的功能范围与 Windows 版不同。

## 构建

需要 mise、Git、C++ 编译器、pkg-config，以及本机的 Qt 6 Base、Qt 6 Wayland、
OpenCV 开发文件和 xmake。当前机器已经安装这些系统组件；mise 选用本机
xmake 以匹配系统 ncurses，并管理 Python 与 ONNX Runtime 的固定版本。
OpenCV 4 和 5 均可使用。当前下载配方和 OCR 推理运行库仅覆盖 Linux x86_64。

在项目根目录执行：

```sh
mise trust
HTTP_PROXY='http://127.0.0.1:7890' HTTPS_PROXY='http://127.0.0.1:7890' mise install
mise exec -- python 'Scripts/linux/build.py'
```

脚本下载固定提交的 ImageReader fork，再构建 OCR 与截图程序，并执行画布、
中英文识别、错误输入和导出检查。网络下载使用 `http://127.0.0.1:7890` 代理。
首次构建需要联网；后续构建复用 `build/deps` 缓存。

完整运行目录是 `build/linux/x86_64/release/`：

```text
ScreenCapture
ImageReader
models/PP-OCRv6_det_tiny.onnx
models/PP-OCRv6_rec_tiny.onnx
lib/libonnxruntime.so.1
```

不要只复制 ScreenCapture。Qt、OpenCV 和 Wayland 平台插件来自系统；
这不是跨发行版的独立二进制包。

## 使用与安装

```sh
'./build/linux/x86_64/release/ScreenCapture'
'./build/linux/x86_64/release/ScreenCapture' --enter=ocr
'./build/linux/x86_64/release/ScreenCapture' --enter=tray
'./build/linux/x86_64/release/ScreenCapture' --image '/absolute/path/image.png'
'./build/linux/x86_64/release/ScreenCapture' --output '/absolute/path/screenshot.png'
```

正常启动会显示桌面提供的截图界面，完成后进入编辑窗口。`--enter=ocr` 在截图后
直接识别文字；也可在编辑窗口框选后点击“文字识别 OCR”，只识别当前选区。
右键清除选区。没有选区时，保存、复制、OCR 和二维码识别使用整张图片。
底图像素坐标与窗口缩放分离，导出保持原图像素尺寸。

常用按键：`Ctrl+N` 新截图、`Ctrl+C` 复制图片、`Ctrl+S` 保存、`Ctrl+O` OCR、
`Ctrl+Z` 撤销、`Ctrl+Shift+Z` 重做、`F3` 切换置顶、`Esc` 关闭。
Wayland 下，置顶行为最终取决于窗口管理器。

`--auto-quit=true` 在成功保存后关闭窗口。复制后保留窗口，以继续提供 Wayland
剪贴板数据；粘贴完成后再关闭。`--enter=pin` 进入同一编辑窗口。
不支持的入口，例如 `--enter=long`，会返回参数错误。

可安装到用户目录，无需 root：

```sh
mise exec -- python 'Scripts/linux/build.py' --install '/home/cat/.local'
```

安装包含 ScreenCapture、ImageReader、模型、ONNX Runtime 及桌面菜单入口。
确保安装目录的 `bin` 在桌面会话 PATH 中。KDE 可在“系统设置 → 键盘 → 快捷键”中
添加命令 `/home/cat/.local/bin/ScreenCapture` 并绑定 Print；文字识别可绑定
`/home/cat/.local/bin/ScreenCapture --enter=ocr`。

## 验证与排错

构建脚本自动检查：原始像素尺寸、透明度、放大/缩小后的选区与裁剪、双击复制、
单行图片、标注撤销/重做、真实 OCR 插件的中英文识别、无效图片/模型的错误码、
主程序 PNG 导出，以及隔离 D-Bus 会话中的门户成功/取消/拒绝与异常响应。
这些检查使用离屏 Qt，不代替真实桌面门户测试。

另已在当前原生 Wayland 会话打开并检查编辑窗口，执行画布、OCR 集成与导出检查。

真实桌面还需检查门户授权、取消截图、多个屏幕和混合缩放、中文输入、跨应用粘贴、
置顶和托盘。截图依赖 `xdg-desktop-portal` 与桌面后端，KDE 对应
`xdg-desktop-portal-kde`，其授权或选择界面由桌面控制。

程序把警告和错误异步写入可执行文件旁的 `logs/errors.log`；目录不可写时输出到
stderr。OCR 临时选区图片放在用户缓存目录的私有临时目录中，在识别结束或关闭
识别窗口时清理。
