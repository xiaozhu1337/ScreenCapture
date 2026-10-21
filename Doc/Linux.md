# Linux 版本

Linux 版使用 Qt 6 原生窗口，通过 xdg-desktop-portal 截图，适用于当前的
CachyOS / KDE Wayland x86_64，也可在提供截图门户的 X11 桌面使用。
Windows 版仍使用原有 Ling / Direct2D 实现和构建流程。

## 已支持

- 通过桌面门户非交互获取完整桌面图像，在编辑器内框选和裁剪。
- 打开图片、拖动框选、裁剪，保存与复制整图或选区。
- 画笔、矩形、椭圆、箭头、文字、颜色和线宽调整、标注撤销与重做。
- PNG 保存、图片剪贴板、置顶钉图、系统托盘和二维码识别。
- 默认托盘常驻；菜单含截图、截图 OCR、打开图片、设置、关于和退出，重复启动转发给已有实例。
- 通用、快捷键、编辑菜单和关于四页设置，YAML 配置持久化，XDG 登录自启及 KDE 全局快捷键。
- 中英文 OCR：使用专门 fork 并移植的 [ImageReader](https://github.com/xiaozhu1337/ImageReader/tree/linux-support)，
  保留原 TinyOCR 引擎和 PP-OCRv6 模型，本地推理，无需上传图片。

滚动长截图、GIF/MP4 录屏、自动窗口吸附、马赛克、橡皮擦、标注对象编辑、
取景放大/拾色、自由缩放和多语言设置尚未移植。
Linux 界面目前使用中文。这里列出的功能范围与 Windows 版不同。

## 构建

需要 mise、Git、C++ 编译器、pkg-config，以及本机的 Qt 6 Base、Qt 6 Wayland、
OpenCV 开发文件、yaml-cpp、KDE Frameworks 6 的 KGlobalAccel 开发文件和 xmake。当前机器已经安装这些系统组件；mise 选用本机
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
'./build/linux/x86_64/release/ScreenCapture' --enter=settings
'./build/linux/x86_64/release/ScreenCapture' --image '/absolute/path/image.png'
'./build/linux/x86_64/release/ScreenCapture' --output '/absolute/path/screenshot.png'
```

正常启动只创建托盘，不发起截图请求，也不会触发截图授权窗口。
点击托盘“截图”、按全局快捷键或显式使用 `--enter=cap` 后，才会请求桌面门户截图。
首次授权后直接获取完整桌面图像并进入编辑窗口，随后可在编辑器内框选区域；
不再每次打开 KDE 的范围、延迟和指针选择窗口。
取消截图或关闭编辑窗口后托盘仍保留，从托盘菜单选择“退出”才结束常驻进程。
托盘左键截图，右键打开菜单；KDE 可能把图标收进托盘的隐藏项。
`--enter=tray` 只进入托盘，`--enter=settings` 直接打开设置。
“设置 → 通用”的“启动后立即截图”默认关闭；启用后普通启动仍会立即请求截图。
旧版本保存的 `capture_on_start: true` 可在此关闭，或改为 YAML 中的 `false`。
`--enter=ocr` 在截图后直接识别文字；也可在编辑窗口框选后点击“文字识别 OCR”，只识别当前选区。
右键清除选区。没有选区时，保存、复制、OCR 和二维码识别使用整张图片。
底图像素坐标与窗口缩放分离，导出保持原图像素尺寸。

常用按键：`Ctrl+N` 新截图、`Ctrl+C` 复制图片、`Ctrl+S` 保存、`Ctrl+O` OCR、
`Ctrl+Z` 撤销、`Ctrl+Shift+Z` 重做、`F3` 切换置顶、`Esc` 关闭。
Wayland 下，置顶行为最终取决于窗口管理器。

`--auto-quit=true` 在成功保存后关闭窗口。它和 `--output` 使用独立的一次性进程，
不会创建托盘或改写常驻进程的偏好。复制后保留窗口，以继续提供 Wayland
剪贴板数据；粘贴完成后再关闭。`--enter=pin` 进入同一编辑窗口。
不支持的入口，例如 `--enter=long`，会返回参数错误。

可安装到用户目录，无需 root：

```sh
mise exec -- python 'Scripts/linux/build.py' --install '/home/cat/.local'
```

安装包含 ScreenCapture、ImageReader、模型、ONNX Runtime 及桌面菜单入口。
确保安装目录的 `bin` 在桌面会话 PATH 中。截图的默认全局快捷键是 `Ctrl+Alt+A`，
可在应用的“设置 → 快捷键”中修改、清空或另设全局 OCR 快捷键。全局快捷键当前使用
KDE 的 KGlobalAccel 服务，其他桌面可通过系统快捷键启动命令。

设置保存于 `$XDG_CONFIG_HOME/xland/ScreenCapture/config.yaml`，默认位置为
`~/.config/xland/ScreenCapture/config.yaml`，每个字段均有注释。颜色、线宽与最近保存目录
会自动记住；工具栏位置、工具的启用状态与顺序会立即应用到已打开的编辑窗口。
配置损坏时保留原文件并使用默认值，只有在设置窗口明确保存才替换。
登录自启默认关闭，启用后写入
`~/.config/autostart/io.github.xland.ScreenCapture.desktop`，以托盘待命模式启动。
若启用后移动了可执行文件，请在新位置重新关闭再开启自启。

## 截图授权

程序使用 `interactive=false` 请求截图。当前 KDE 门户可按应用身份保存首次授权，
允许后后续截图以及重新启动程序均无需再次确认。原来的交互截图窗口每次都会出现，
其中的允许操作不等于保存非交互截图授权。

先安装上面的桌面入口，确保存在
`~/.local/share/applications/io.github.xland.ScreenCapture.desktop`。
本机 Qt 6.12 会用程序已设置的桌面 ID 自动向门户注册，因此从终端启动也按
`io.github.xland.ScreenCapture` 保存权限。只有仓库中的 `.desktop` 文件还不够；
未安装桌面入口时，门户可能把程序识别为启动它的 Konsole。

通常首次在系统授权窗口允许即可。已有 `ask` 记录会继续每次询问，`no` 会直接拒绝。
若希望明确始终允许当前用户的 ScreenCapture，可执行一次：

```sh
busctl --user call \
  'org.freedesktop.impl.portal.PermissionStore' \
  '/org/freedesktop/impl/portal/PermissionStore' \
  'org.freedesktop.impl.portal.PermissionStore' \
  SetPermission sbssas screenshot true screenshot 'io.github.xland.ScreenCapture' 1 yes
```

这项系统授权跨进程重启保存，允许该应用无需再次确认读取屏幕。
程序自身不会写入或扩大系统授权；用户撤销授权后，截图仍会被拒绝。
上述流程已在本机 `xdg-desktop-portal 1.22.1`、KDE 门户 `6.8.0` 验证，
其他门户后端和旧版 Qt 的身份注册与授权行为需要分别确认。

## 验证与排错

构建脚本自动检查：原始像素尺寸、透明度、放大/缩小后的选区与裁剪、双击复制、
单行图片、标注撤销/重做、真实 OCR 插件的中英文识别、无效图片/模型的错误码、
主程序 PNG 导出，配置保存/恢复/损坏保护、设置窗口、工具栏联动、XDG 自启，
以及隔离 D-Bus 会话中的门户成功/取消/拒绝与异常响应、一次性输出和单实例转发。
这些检查使用离屏 Qt，不代替真实桌面门户测试。

另已在当前原生 Wayland 会话打开并检查编辑窗口，执行画布、OCR 集成与导出检查。
为 ScreenCapture 保存允许权限后，已使用构建目录和安装目录中的程序各进行一次真实
非交互截图，两个独立进程均直接导出 2560×1600 PNG，没有再次显示截图选择或授权窗口。

真实桌面还需检查首次授权的拒绝/取消、多个屏幕和混合缩放、中文输入、跨应用粘贴、
置顶和托盘。截图依赖 `xdg-desktop-portal` 与桌面后端，KDE 对应
`xdg-desktop-portal-kde`，最终授权行为由桌面控制。

程序把警告和错误异步写入可执行文件旁的 `logs/errors.log`；目录不可写时输出到
stderr。OCR 临时选区图片放在用户缓存目录的私有临时目录中，在识别结束或关闭
识别窗口时清理。
