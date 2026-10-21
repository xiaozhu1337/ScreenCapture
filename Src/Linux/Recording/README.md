# Linux long capture and recording integration

These are independent top-level widgets. They do not include or depend on Editor.

```cpp
#include "Recording/LongCapture.h"
#include "Recording/VideoCapture.h"

auto* capture = new LongCapture(selectedImage, globalLogicalRegion, settings, oneShot);
connect(capture, &LongCapture::pinRequested, this,
    [this](QImage image, QRect originalRegion) { openPin(image, originalRegion); });
connect(capture, &LongCapture::finished, this, [this] { /* clear tool state */ });
capture->show();

// Alternative entry for GIF / MP4:
auto* recording = new VideoCapture(globalLogicalRegion, settings, oneShot);
connect(recording, &VideoCapture::finished, this, [this] { /* clear tool state */ });
recording->show();
```

`selectedImage` is the selected screenshot crop; `globalLogicalRegion` uses Qt global logical desktop coordinates. Hide the selection overlay and editor before showing either tool, otherwise they will be recorded. Both widgets set `WA_DeleteOnClose`; use `QPointer` if retaining references and do not delete them in a `finished` callback. Settings must outlive the widgets.

The toolbar opens with a real Start button. Starting asks the desktop to share the monitor containing the selection. A different monitor is rejected when the portal supplies geometry. Cross-monitor selections are rejected. Long capture requests ScreenCast and pointer control in one RemoteDesktop session, positions the pointer over the region and sends discrete vertical scroll events. It keeps the Windows strip matching, settling, nine failed-scroll stop and 36000-pixel height limit, with a confidence check against unrelated frames. The preview, Stop, Copy, Save and Pin actions work before or after scrolling. `pinRequested(QImage, QRect)` transfers the stitched image and original logical region to the caller, which must open the actual pin window.

MP4 uses 30 fps, HEVC when the actual encoder/size probe succeeds and H.264 otherwise. GIF captures at 16 fps, includes the cursor and converts through an H.264 intermediate using bounded per-frame palettes. MP4 defaults to system sound enabled and microphone disabled; enabled sources are resolved with `pactl` and mixed by FFmpeg. The limits are six minutes for GIF and 120 minutes for MP4. Save/Copy during capture stops it, finishes encoding, and performs that action. Stop alone keeps the result available. Escape/Close cancels and removes unexported temporary files. Save uses asynchronous chunked copying into `QSaveFile`.

`finished()` is emitted when a tool closes. With `oneShot=true`, cancelling or successfully saving quits the application. Long-image copying and file copying keep the process available as the Wayland clipboard owner until ownership changes. The copied recording itself is intentionally retained in the application's `CacheLocation/recordings` directory because clipboard managers retain file URLs after taking ownership; cache cleanup may remove those exported copies later. Pin handoff leaves lifetime management to the caller. Do not force application exit from `finished()` after a clipboard or pin action.

## Build wiring for the main checkout

Update `Src/Linux/xmake.lua` (owned by the integrating agent):

- Add `Recording/*.cpp` and `Recording/*.h` to the application sources so Qt's moc sees the QObject headers.
- Include pkg-config cflags and libs for `libpipewire-0.3` (the installed headers require both `/usr/include/pipewire-0.3` and `/usr/include/spa-0.2`); link `pipewire-0.3`.
- Keep the existing Qt Widgets, Qt DBus, Settings/yaml-cpp, Desktop/Ui and LayerShellQt links/resources. No Qt Multimedia or new package install is needed.
- Register a separate Qt Widgets/Qt DBus/Qt Test executable for `Test/Linux/RecordingTests.cpp`, all these Recording sources/headers, Settings sources and Desktop/Ui sources, with the same PipeWire and LayerShellQt links. Include `Src` and `Src/Linux/Recording`. Run the tests under `QT_QPA_PLATFORM=offscreen dbus-run-session -- <test-binary>`.
- The test executable runs real `ffmpeg`/`ffprobe`; the three audio cases also need the existing local PulseAudio-compatible service and `pactl`. It never invokes the actual desktop picker or modifies a live recording.

The isolated baseline also has an existing compile error at `Desktop/Ui.cpp:143`: the two anchor enums are OR'ed into an integer. The integrating agent should use `LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) | LayerShellQt::Window::AnchorLeft`. Independent verification compiled only that existing Ui object with `-fpermissive`; all newly added sources compiled with standard C++20 and `-Wall -Wextra` without that workaround.

Runtime requirements are the desktop ScreenCast portal, RemoteDesktop pointer support for automatic scrolling, PipeWire, `ffmpeg` with rawvideo/PulseAudio input and libx264 (libx265 preferred), and `pactl` when recording audio. Capture uses the portal's PipeWire remote directly because the available Qt backend fixes the cursor to hidden and does not reliably verify the selected monitor. When the portal supplies a PipeWire serial, the stream targets that serial to avoid node ID reuse.

All failures use `qCritical`, leaving asynchronous file logging to the application's existing ErrorLog handler. The floating tools use `Ui::positionWindow`; they are placed outside the selected region when screen space permits. Full-screen regions can still contain the controls.

Automated checks cover stitching and fixed headers, bottom/settling/height stops, unrelated-frame rejection, crop scaling and negative monitor origins, invalid PipeWire buffers, portal rejection/cancellation cleanup, encoder parameters, real MP4/GIF and audio track creation, initial widget controls, pin signals, cancellation and copied-file retention. Real compositor permission dialogs, hardware microphone/speaker content, fractional scaling and a full interactive long-scroll session require manual desktop verification.
