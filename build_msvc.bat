@echo off
setlocal

:: Try to find Visual Studio installation
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio not found. Please install Visual Studio 2022 with C++ development tools.
    exit /b 1
)

:: Get the VS2022 installation path
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo Visual Studio with C++ tools not found.
    echo Please install Visual Studio 2022 with C++ development tools.
    exit /b 1
)

:: Set up the Visual Studio environment
if exist "%VS_PATH%\Common7\Tools\VsDevCmd.bat" (
    call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
) else (
    echo Visual Studio environment setup script not found.
    echo Please run this script from a Visual Studio Developer Command Prompt.
    exit /b 1
)

echo Visual Studio environment set up successfully.

:: Create build directory if it doesn't exist
if not exist build mkdir build

:: First compile test_socket.cpp to verify socket functionality
echo Compiling test_socket.cpp to verify socket functionality...
cl /nologo /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /Fosrc\test_socket.obj /c src\test_socket.cpp
if errorlevel 1 (
    echo Failed to compile test_socket.cpp!
    exit /b 1
)
echo Socket test compiled successfully.

echo Running socket test...
cl /nologo /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /Febuild\test_socket.exe src\test_socket.obj ws2_32.lib
if errorlevel 1 (
    echo Failed to link test_socket.exe!
    exit /b 1
)

build\test_socket.exe
if errorlevel 1 (
    echo Socket test failed!
    exit /b 1
)

echo Socket test passed. Proceeding with worker build.

:: Compile common files
echo Compiling common files...
cl /nologo /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /I. /Fosrc\HttpClient.obj /c src\HttpClient.cpp
cl /nologo /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /I. /Fosrc\HtmlParser.obj /c src\HtmlParser.cpp
cl /nologo /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /I. /Fosrc\Crawler.obj /c src\Crawler.cpp

if errorlevel 1 (
    echo Failed to compile common files!
    exit /b 1
)

:: Compile and link worker
echo Compiling worker...
cl /nologo /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /I. /Fosrc\worker.obj /c src\worker.cpp
if errorlevel 1 (
    echo Failed to compile worker!
    exit /b 1
)

echo Linking worker...
cl /nologo /Febuild\worker.exe src\worker.obj src\HttpClient.obj src\HtmlParser.obj src\Crawler.obj ws2_32.lib
if errorlevel 1 (
    echo Failed to link worker!
    exit /b 1
)

echo Build completed successfully.
echo Worker executable is at: build\worker.exe 