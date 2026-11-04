@echo off
rem Windows launcher for prepare.sh: finds Git's bash and runs the script, so no Git Bash window is needed.
rem
rem usage:  prepare.bat [your config folder] [--no-bake] [--name NAME] [--build DIR] [variant ...]
rem
rem Without a config folder it looks for your OpenMW config in Documents\My Games\OpenMW, then in
rem OneDrive\Documents\My Games\OpenMW. The folder is only read. Everything else is the same as prepare.sh:
rem see the header of prepare.sh and README.md.
setlocal enableextensions

set "BASH="
for %%P in ("%ProgramFiles%\Git\bin\bash.exe" "%ProgramFiles%\Git\usr\bin\bash.exe" "%ProgramFiles(x86)%\Git\bin\bash.exe" "%LocalAppData%\Programs\Git\bin\bash.exe") do (
    if not defined BASH if exist %%P set "BASH=%%~P"
)
if not defined BASH (
    echo prepare.bat: Git for Windows was not found, so there is no bash to run prepare.sh.
    echo Install Git for Windows ^(https://git-scm.com/download/win^), or run prepare.sh from any bash.
    exit /b 1
)

set "ARGS=%*"
set "CONFIG="
if "%~1"=="" goto detect
if exist "%~1\" goto run
:detect
for %%D in ("%USERPROFILE%\Documents\My Games\OpenMW" "%USERPROFILE%\OneDrive\Documents\My Games\OpenMW") do (
    if not defined CONFIG if exist "%%~D\openmw.cfg" set "CONFIG=%%~D"
)
if not defined CONFIG (
    echo prepare.bat: no openmw.cfg found in the usual places. Give your config folder as the first argument:
    echo     prepare.bat "C:\Users\you\Documents\My Games\OpenMW"
    exit /b 1
)
echo prepare.bat: using the config folder %CONFIG% ^(read only^)
"%BASH%" "%~dp0prepare.sh" "%CONFIG%" %*
exit /b %ERRORLEVEL%

:run
"%BASH%" "%~dp0prepare.sh" %*
exit /b %ERRORLEVEL%
