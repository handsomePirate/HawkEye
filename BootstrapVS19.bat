@ECHO off
REM Run the premake procedure to generate a VS 2019 solution.
REM NOTE: Uses the premake5.lua file to obtain the generation details.
ECHO -- Configuring solution --
cd sln
CALL premake5.exe vs2019
PAUSE