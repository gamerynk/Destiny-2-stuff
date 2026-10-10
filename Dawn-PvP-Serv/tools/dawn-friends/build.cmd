@echo off
setlocal
for %%I in ("%~dp0..\..") do set "DAWN_ROOT=%%~fI"
if not defined VSCMD_ARG_TGT_ARCH (
  for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%I\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 exit /b 1
if not exist "%DAWN_ROOT%\build\tools\dawn-friends" mkdir "%DAWN_ROOT%\build\tools\dawn-friends"
pushd "%DAWN_ROOT%\build\tools\dawn-friends"
cl /nologo /EHsc /std:c++20 /O2 /MT /utf-8 /W4 /I "%DAWN_ROOT%\dawn\src\core\identity" "%~dp0dawn_friends.cpp" "%DAWN_ROOT%\dawn\src\core\identity\device_identity.cpp" "%DAWN_ROOT%\dawn\src\core\filesystem\path.cpp" /link /OUT:DawnFriends.exe bcrypt.lib crypt32.lib winhttp.lib advapi32.lib
set "DAWN_RESULT=%ERRORLEVEL%"
popd
exit /b %DAWN_RESULT%
