workspace "HawkEye"
	architecture "x64"
	configurations { "Debug", "Release" }
	startproject "Test"
	location ""
	
	flags
	{
		"MultiProcessorCompile",
		"FatalWarnings"
	}
	
include "../dependencies.lua"
include "../Test/dependencies.lua"
	
include "../proj/HawkEye"
include "../proj/Test"