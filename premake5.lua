newoption {
    trigger     = 'use-big-endian',
    description = 'Use big-endian byte order (default is little-endian)'
}

newoption {
    trigger     = 'enable-ipv6',
    description = 'Enable IPv6 protocol support (disabled by default)'
}

solution "spillover"
    configurations { "Debug", "Release" }
    platforms { "x64", "x32" }

    if _OPTIONS['use-big-endian'] then
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
        defines { "_DEBUG" }

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
        defines { "_DEBUG" }
        flags { "Symbols" }

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
