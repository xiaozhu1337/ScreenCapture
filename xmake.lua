set_xmakever("3.1.1")
set_project("ScreenCapture")
set_version("2.5.15")
set_allowedplats("windows")
set_allowedarchs("x64")
set_defaultmode("release")
set_languages("c11", "cxx20")
set_runtimes("MT")
set_toolchains("msvc")
add_rules("mode.debug", "mode.release")
add_defines("UNICODE", "_UNICODE", "NOMINMAX", "WIN32_LEAN_AND_MEAN")
add_cxflags("/utf-8", {tools = "cl"})
add_cxxflags("/permissive-", {tools = "cl"})

local lingdir = "build/deps/Ling"
local gifskidir = "build/deps/gifski"

rule("screencapture.dependencies")
    on_load(function (target)
        os.vrunv("mise", {"exec", "--", "python", path.join(os.projectdir(), "Scripts/prepare_deps.py")})
    end)
rule_end()

target("Yoga")
    set_kind("static")
    set_default(false)
    add_rules("screencapture.dependencies")
    add_includedirs(lingdir, {public = true})
    on_load(function (target)
        target:add("files", path.join(os.projectdir(), "build/deps/Ling/yoga/**.cpp"))
    end)
target_end()

target("Ling")
    set_kind("static")
    set_default(false)
    add_rules("screencapture.dependencies")
    add_deps("Yoga")
    add_includedirs(lingdir, lingdir .. "/src", {public = true})
    set_pcxxheader(lingdir .. "/src/pch.h")
    on_load(function (target)
        for _, source in ipairs(os.files(path.join(os.projectdir(), "build/deps/Ling/src/*.cpp"))) do
            if path.filename(source) ~= "pch.cpp" then
                target:add("files", source)
            end
        end
    end)
target_end()

target("gifski")
    set_kind("phony")
    set_default(false)
    add_rules("screencapture.dependencies")
    on_build(function (target)
        os.vrunv("mise", {"exec", "--", "python", path.join(os.projectdir(), "Scripts/prepare_deps.py"), "--gifski"},
                 {envs = os.joinenvs(target:toolchain("msvc"):runenvs())})
    end)
target_end()

local function application_sources()
    add_rules("win.sdk.application", "screencapture.dependencies")
    add_deps("Ling", "gifski")
    add_includedirs("Src", "Src/Res", lingdir, gifskidir)
    set_pcxxheader("Src/pch.h")
    add_files("Src/**.cpp|pch.cpp|main.cpp")
    add_files("Src/Win/cgif/*.c")
    add_files("Src/quirc/*.c", {defines = "QUIRC_MAX_REGIONS=65534"})
    add_files("Src/Res/Resource.rc")
    add_mrcflags("/c65001", {force = true})
    add_linkdirs(gifskidir .. "/target/x86_64-pc-windows-msvc/release")
    add_links("gifski")
    add_syslinks("dwmapi", "windowsapp", "comctl32", "imm32", "version",
                 "mf", "mfreadwrite", "mfplat", "mfuuid", "ntdll", "Userenv",
                 "d2d1", "dwrite", "d3d11", "dxgi", "shcore", "windowscodecs", "ws2_32", "bcrypt")
    add_ldflags("/MANIFEST:EMBED", "/MANIFESTINPUT:Scripts/ScreenCapture.manifest", {force = true})
    on_load(function (target)
        import("core.base.bytes")
        local resources = ""
        for _, file in ipairs({"Lang/简体中文.zh-CN.json", "Lang/English.en-US.json",
                               "Src/Res/iconfont.ttf", "Src/Res/logo.ico"}) do
            resources = resources .. io.readfile(path.join(os.projectdir(), file), {encoding = "binary"})
        end
        target:add("mrcflags", "/dSCREEN_CAPTURE_RESOURCE_REVISION=0x" .. hash.sha256(bytes(resources)):sub(1, 16), {force = true})
    end)
end

target("ScreenCapture")
    set_kind("binary")
    application_sources()
    add_files("Src/main.cpp")
target_end()

target("ScreenCaptureTests")
    set_kind("binary")
    set_default(false)
    application_sources()
    set_values("windows.subsystem", "console")
    set_targetdir("build/tests/$(mode)")
    add_files("Test/*Tests.cpp")
target_end()
