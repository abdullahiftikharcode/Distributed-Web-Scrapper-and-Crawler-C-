@echo off
REM Build script for Visual Studio compiler

echo Checking for Visual Studio compiler...
where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
  echo Visual Studio compiler not found in PATH
  echo Please run this from a "Developer Command Prompt for VS"
  exit /b 1
)

echo Using Visual Studio compiler:
cl 2>&1 | findstr /R "Version"

REM Create output directories if they don't exist
if not exist bin mkdir bin
if not exist obj mkdir obj

REM Set compiler flags
set CXXFLAGS=/std:c++17 /EHsc /W4 /nologo /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS
set LDFLAGS=Ws2_32.lib

echo.
echo Compiling test_socket.cpp to verify socket functionality...
cl %CXXFLAGS% /Fo"obj\\" /c src\test_socket.cpp
if %ERRORLEVEL% NEQ 0 (
  echo Socket test compilation failed!
  exit /b 1
)

link /nologo obj\test_socket.obj %LDFLAGS% /OUT:bin\test_socket.exe
if %ERRORLEVEL% NEQ 0 (
  echo Socket test linking failed!
  exit /b 1
)

echo Socket test compiled successfully.
echo Running socket test...
bin\test_socket.exe
if %ERRORLEVEL% NEQ 0 (
  echo Socket test execution failed!
  exit /b 1
)

echo.
echo Socket test passed. Proceeding with main build.
echo.

REM Compile common files
echo Compiling common files...
cl %CXXFLAGS% /Fo"obj\\" /c src\HttpClient.cpp src\HtmlParser.cpp src\Crawler.cpp
if %ERRORLEVEL% NEQ 0 (
  echo Failed to compile common files!
  exit /b 1
)

REM Compile server
echo.
echo Compiling server...
cl %CXXFLAGS% /DSERVER_DEFINE_URL_HELPERS /Fo"obj\\" /c src\server.cpp 
if %ERRORLEVEL% NEQ 0 (
  echo Failed to compile server!
  exit /b 1
)

REM Link server executable
echo Linking server executable...
link /nologo obj\server.obj obj\HtmlParser.obj %LDFLAGS% /OUT:bin\server.exe /IGNORE:4006,4088
if %ERRORLEVEL% NEQ 0 (
  echo Failed to link server!

  echo.
  echo Attempting detailed link with verbose output...
  link /VERBOSE:LIB /nologo obj\server.obj obj\HtmlParser.obj %LDFLAGS% /OUT:bin\server.exe /IGNORE:4006,4088
  
  exit /b 1
)

REM Compile worker
echo.
echo Compiling worker...
cl %CXXFLAGS% /Fo"obj\\" /c src\worker.cpp
if %ERRORLEVEL% NEQ 0 (
  echo Failed to compile worker!
  exit /b 1
)

REM Link worker executable - make sure all required object files are included
echo Linking worker executable...
link /nologo obj\worker.obj obj\HttpClient.obj obj\HtmlParser.obj obj\Crawler.obj %LDFLAGS% /OUT:bin\worker.exe
if %ERRORLEVEL% NEQ 0 (
  echo Failed to link worker!

  echo.
  echo Attempting detailed link with verbose output...
  link /VERBOSE:LIB /nologo obj\worker.obj obj\HttpClient.obj obj\HtmlParser.obj obj\Crawler.obj %LDFLAGS% /OUT:bin\worker.exe
  
  exit /b 1
)

echo.
echo Build complete!
echo.
echo Server executable: bin\server.exe
echo Worker executable: bin\worker.exe
echo.
echo To run the distributed web scraper: run_distributed.bat 