#!lua
local output = "./build/" .. _ACTION

solution "handy_solution"
   configurations { "Debug", "Release" }

project "core"
   location (output)
   kind "StaticLib"
   language "C++"
   files { "../lynx/**.h", "../lynx/**.cpp" }
   buildoptions { "-Wall" }
   defines { "WANT_CRC32" }
   
   configuration "Debug"
      flags { "Symbols" }
      defines { "DEBUG" }

   configuration "Release"
      flags { "Optimize" }
      defines { "NDEBUG" }

project "handy"
   location (output)
   kind "ConsoleApp"
   language "C++"
   files { "./**.h", "./**.cpp",  "./**.c"}
   buildoptions { "-std=c++11 -Wall" }
   linkoptions { "-lgo2" }
   links {"core"}

   configuration "Debug"
      flags { "Symbols" }
      defines { "DEBUG" }

   configuration "Release"
      flags { "Optimize" }
      defines { "NDEBUG" }
