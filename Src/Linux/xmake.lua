local function linux_application()
    add_rules("qt.widgetapp")
    add_frameworks("QtDBus")
    add_syslinks("yaml-cpp", "KF6GlobalAccel", "LayerShellQtInterface", "pipewire-0.3")
    add_includedirs(".", "..", "Capture", "Desktop", "Editing", "Settings", "Recording",
                    "/usr/include/KF6/KGlobalAccel", "/usr/include/pipewire-0.3", "/usr/include/spa-0.2")
    add_files("Capture/*.cpp", "Capture/*.h", "Desktop/*.cpp", "Desktop/*.h",
              "Editing/*.cpp", "Editing/*.h", "Settings/*.cpp", "Settings/*.h",
              "Recording/*.cpp", "Recording/*.h", "resources.qrc")
    add_files("../quirc/*.c", {defines = "QUIRC_MAX_REGIONS=65534"})
end

target("ScreenCapture")
    linux_application()
    add_files("main.cpp")
    add_installfiles("../../Scripts/linux/io.github.xland.ScreenCapture.desktop", {prefixdir = "share/applications"})
    after_install(function (target)
        local built = target:targetdir()
        assert(os.isfile(path.join(built, "ImageReader")), "Build the OCR bundle with Scripts/linux/build.py before installing")
        os.cp(path.join(built, "ImageReader"), path.join(target:installdir(), "bin/ImageReader"))
        os.mkdir(path.join(target:installdir(), "share/ImageReader/models"))
        os.cp(path.join(built, "models/*.onnx"), path.join(target:installdir(), "share/ImageReader/models") .. "/")
        os.mkdir(path.join(target:installdir(), "lib"))
        os.cp(path.join(built, "lib/libonnxruntime.so*"), path.join(target:installdir(), "lib") .. "/")
    end)
target_end()

for name, file in pairs({ScreenCaptureTests = "CanvasTests", ScreenCaptureDesktopTests = "DesktopTests",
                         ScreenCapturePortalTests = "PortalTests", ScreenCaptureSettingsTests = "SettingsDialogTests",
                         ScreenCaptureCanvasParityTests = "CanvasParityTests", ScreenCaptureRecordingTests = "RecordingTests",
                         ScreenCaptureMediaRegressionTests = "MediaRegressionTests", ScreenCaptureWindowGeometryTests = "WindowGeometryTests"}) do
    target(name)
        set_default(false)
        linux_application()
        add_frameworks("QtTest")
        if file == "WindowGeometryTests" then add_frameworks("QtQml") end
        local moc = file == "SettingsDialogTests" or file == "CanvasParityTests" or file == "RecordingTests" or file == "MediaRegressionTests"
        if file == "MediaRegressionTests" then
            add_ldflags("-Wl,--wrap=pw_stream_dequeue_buffer", "-Wl,--wrap=pw_stream_queue_buffer", {force = true})
        end
        add_files("../../Test/Linux/" .. file .. ".cpp", {rules = moc and "qt.moc" or nil})
        set_targetdir("../../build/tests/$(mode)")
    target_end()
end
