newoption {
    trigger     = 'enable-big-endian',
    description = 'Enable big-endian byte order support (default is little-endian)'
}

solution "spillover"
    configurations { "Debug", "Release" }
    platforms { "x64", "x32" }

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
    links { "spillover" }
    files { "**.h", "test/**.c" }

    filter "configurations:Debug"
        defines { "DEBUG" }

        flags { 
            "Symbols",
        }

      filter "configurations:Release"
            defines { "NDEBUG" }
            optimize "On"
