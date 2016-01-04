newoption {
    trigger     = 'enable-big-endian',
    description = 'Enable big-endian byte order support (default is little-endian)'
}

newoption {
    trigger     = 'enable-ipv6',
    description = 'Enable IPv6 protocol support (disbled by default)'
}

solution "spillover"
    configurations { "Debug", "Release" }
    platforms { "x64", "x32" }

    if _OPTIONS['enable-big-endian'] then
        defines { "SPO_BIGENDIAN_PLATFORM" }
    end

    if _OPTIONS['enable-ipv6'] then
        defines { "SPO_IPV6_SUPPORT" }
    end

project "spillover"
    kind "StaticLib"
    language "C"
    targetdir "bin/%{cfg.platform}/%{cfg.buildcfg}"
    includedirs { "./include" }
    files { "**.h", "src/**.c" }

    filter "configurations:Debug"
        defines { "DEBUG" }

        flags { 
            "Symbols",
            "FatalWarnings",
            "FatalCompileWarnings"
        }

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"

project "spillover-tests"
    kind "ConsoleApp"
    language "C"
    targetdir "bin/%{cfg.platform}/%{cfg.buildcfg}"
    includedirs { "./include" }
    files { "**.h", "test/**.c" }
    links { "spillover" }

    configurations { "windows" }
        links { "Ws2_32.lib" }

    filter "configurations:Debug"
        defines { "DEBUG" }
        flags { "Symbols" }

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
