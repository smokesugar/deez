workspace "deez"
    configurations { "Debug", "Release" }
    architecture "x86_64"
    startproject "deez"

project "deez"
    kind "WindowedApp"
    language "C"
    cdialect "C99"

    targetdir "target/bin/%{prj.name}/%{cfg.buildcfg}"
    objdir "target/obj/%{prj.name}/%{cfg.buildcfg}"
    debugdir "data"

    warnings "Extra"
    flags { "FatalWarnings" }

    disablewarnings { "4505" }

    files {
        "src/**.h",
        "src/**.c",
        "src/**.cpp",
    }

    includedirs {
        "src",
    }

    links {
        "dxgi.lib",
        "d3d12.lib",
        "d3dcompiler.lib"
    }

    filter "configurations:Debug"
        defines { "_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"