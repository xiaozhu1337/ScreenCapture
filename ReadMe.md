简体中文 | [English](./Doc/ReadMe.en-US.md) | [Русский](./Doc/ReadMe.ru-RU.md) | [Bahasa Indonesia](./Doc/ReadMe.id-ID.md)

![banner](./Doc/banner.png)

**ScreenCapture** 一个小巧但功能强大的Windows截图工具。

## 特性

- 截图、绘图标注、滚动截图（截长图）、录屏（GIF/MP4）、文字识别（OCR）、二维码识别。
- 取景框（拾色器），支持快捷键复制 RGB 颜色（`Ctrl+R`）、 HEX 颜色（`Ctrl+H`）与 CMYK 颜色（`Ctrl+K`）。
- 绘制椭圆、正圆（按住`Shift`）、矩形、正方形（按住`Shift`）、箭头、标号等。
- 绘制曲线、直线（按住`Shift`）、马赛克、橡皮擦、文本。
- 可以随时修改、删除已绘制的元素（鼠标移到元素上）。
- 撤销（`Ctrl+Z`）、重做（`Ctrl+Y`）、保存为文件（`Ctrl+S`）、保存到剪贴板（`Ctrl+C`或双击）。
- 运行速度快、内存占用低。
- 体积小、仅一个可执行文件，无需安装，不依赖任何动态链接库（文字识别除外）。
- 支持多种命令行参数直接启动指定的功能。
- 支持用完即走（进程不驻留在系统中）。
- 多语言支持。

## 下载

[Release](https://github.com/xland/ScreenCapture/releases/) （1MB）

## 常用功能与问题

- 按住 `Ctrl键` 框选截图区域后，直接进入图像标记窗口（钉图窗口）
- 在「设置 → 快捷键」中自定义钉图快捷键，默认 `F3`；框选完成后按下即可钉图，也就是进入图像编辑，点击「清除」可禁用。
- 在「设置 → 通用设置」中开启「编辑菜单始终显示在底部」。下方空间不足时，工具条显示在选区内部底部，主菜单和样式菜单一起保持可见。
- 在「设置 → 编辑菜单」中添加、移除和调整框选后一级菜单的顺序。例如添加「线条」并上移后，下次框选可以直接点击线条进入标注。关闭、保存和复制按钮始终保留，支持恢复默认菜单。
- 使用「线条」工具时，光标显示为圆点，圆点大小随线宽和图像缩放倍数变化。每次按下、拖动、松开完成一条独立笔画；从已有线条上起笔也会新建一条线，不会继续修改上一条线。按住 `Shift` 可绘制直线。
- 按住 `Ctrl键` 滚动鼠标滚轮可以放大、缩小图像标记窗口（钉图窗口）
- 长截图拼接不符合预期时，尝试调整截图区域往往能解决问题
- 如手动下载新版本，则必须退出老版本再启动新版本

## 支持的操作系统

- Windows 10 1803 or Later

## 编译

- main分支依赖 [Ling](https://github.com/xland/Ling) GUI 框架.
- 使用 Visual Studio 2026（With C++ Desktop Dev Kit）即可编译项目。
- [2.4.25（基于D2D）](https://github.com/xland/ScreenCapture/tree/2.4.25)或 [2.3.3（基于Qt）](https://github.com/xland/ScreenCapture/tree/2.3.3_qt)是以前的稳定分支。

### 使用 xmake 构建

需要 Windows x64、Git 和 Visual Studio 2026 的 C++ 桌面开发工具（包含 Windows SDK 和 ATL）。项目通过 `mise.toml` 固定 xmake、Python 和 Rust 工具版本，由 [mise](https://mise.jdx.dev/) 管理：

```powershell
mise trust
mise install
mise exec -- xmake f -p windows -a x64 -m release --vs=2026
mise exec -- xmake
```

Release 可执行文件输出到 `build/windows/x64/release/ScreenCapture.exe`。首次构建自动下载固定版本的 Ling（含 Yoga）和 gifski 1.34.0 到 `build/deps`，通过 Cargo 构建 gifski 静态库，再由 xmake 编译 Ling、Yoga 和应用；无需设置原 Visual Studio 项目中的绝对依赖路径。依赖缓存与构建产物不提交到 Git。

需要代理时，在运行以上命令的环境中配置 `HTTP_PROXY`、`HTTPS_PROXY` 和 `CARGO_HTTP_PROXY`。首次构建需要联网，依赖准备完成后可重复进行本地构建。

Debug 构建使用相同依赖，将配置切换为 `-m debug`：

```powershell
mise exec -- xmake f -p windows -a x64 -m debug --vs=2026
mise exec -- xmake
```

Debug 可执行文件输出到 `build/windows/x64/debug/ScreenCapture.exe`。两种构建均沿用原项目的 C++20、静态运行库、语言与字体资源、每显示器 DPI 感知和 Segment Heap 设置。应用配置仍使用原来的 `config.json`。

### 功能测试

```powershell
mise exec -- xmake build ScreenCaptureTests
& './build/tests/release/ScreenCaptureTests.exe'
& './build/tests/release/ScreenCaptureTests.exe' '--gui=true' '--auto-quit=true'
```

测试使用独立输出目录中的临时配置，不修改用户的配置。第一条测试检查快捷键解析、配置持久化和多显示器位置计算；第二条需要可交互的桌面，打开真实窗口，检查钉图、菜单实际鼠标点击、线条绘制与撤销重做，以及中英文设置页面的布局。界面截图保存到 `__tmp`。Debug 模式下把测试路径中的 `release` 换成 `debug`。

## 命令行

```
// 截图完成后即退出进程。
> ScreenCapture.exe --auto-quit=true

// 框选完成后不显示工具条，直接进入指定功能：
// pin 钉图/图像标记
> ScreenCapture.exe --enter=pin
// long 长截图
> ScreenCapture.exe --enter=long
// video 屏幕录制
> ScreenCapture.exe --enter=video
// ocr 文字识别
> ScreenCapture.exe --enter=ocr
// qr 二维码识别
> ScreenCapture.exe --enter=qr
// tray 仅注册托盘图标，不执行任何操作
> ScreenCapture.exe --enter=tray

// 两个参数可以联合使用，比如：不注册托盘图标，截完长图后进程直接退出
> ScreenCapture.exe --enter=long --auto-quit=true
```

## 文字识别插件

下载最新版本的文字识别工具 [ImageReader.exe](https://github.com/xland/ImageReader/releases) (约25MB) 并把此文件放置到 `%appdata%\ScreenCapture\plugin`目录下，然后重启应用即可使用（或 `ScreenCapture.exe` 同目录下亦可）

## 便携能力

默认情况下 ScreenCapture 会从 `%appdata%\ScreenCapture\Lang` 目录下读取配置信息、语言文件及插件。

但用户可以在 `ScreenCapture.exe` 同目录下创建一个 `config.json` 的空文件，重启应用，`ScreenCapture` 即会在此文件中设置配置信息。

`ScreenCapture.exe` 同目录下创建一个 `Lang` 子目录，然后把语言文件放置到此目录下，`ScreenCapture` 即会读取此目录下的语言文件。

文字识别插件 `ImageReader.exe` 也可以放置在 `ScreenCapture.exe` 同目录下

## 赞助

<table>
  <tr>
    <td align="center">
      <img alt="支付宝赞助" src="./Doc/alipay.jpg" width="160" height="160">
      <p>支付宝赞助</p>
    </td>
    <td align="center">
      <img alt="微信赞助" src="./Doc/wechat.png" width="160" height="160">
      <p>微信赞助</p>
    </td>
    <td align="center">
      <img alt="作者微信" src="./Doc/author.jpg" width="160" height="160">
      <p>作者微信</p>
    </td>
    <td align="center">
      <img alt="公众号二维码" src="./Doc/gongzhonghao.jpg" width="160" height="160">
      <p>公众号：桌面软件</p>
    </td>
  </tr>
</table>
