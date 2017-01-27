solution "cparse"
	language "C"
	configurations { "Debug", "Release" }
	platforms { "x32", "x64" }
	location "build"
	warnings "Extra"
	debugdir "."
	
	configuration "vs*"
		--buildoptions { "/wd4204" }

	configuration "Debug"
		flags "Symbols"

	configuration "Release"
		optimize "Speed"

	project "cparse_sample"
		kind "ConsoleApp"
		files { "*.h", "*.c" }
