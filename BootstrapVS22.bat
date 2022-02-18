@ECHO off
REM Check that the submodules exist.
IF NOT EXIST "ext\VulkanBackend\.git" (
	ECHO -- Getting submodules --
	git submodule update --init --recursive
)
REM Run the premake procedure to generate a VS 2022 solution.
REM NOTE: Uses the premake5.lua file to obtain the generation details.
ECHO -- Configuring solution --
cd sln
CALL premake5.exe vs2022
PAUSE