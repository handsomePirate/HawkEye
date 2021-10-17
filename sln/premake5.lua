workspace "HawkEye"
	architecture "x64"
	configurations { "Debug", "Release" }
	location ""
	
	flags
	{
		"MultiProcessorCompile"
	}
	
include "../dependencies.lua"
include "../Test/dependencies.lua"
	
include "../proj/HawkEye"
include "../proj/Test"