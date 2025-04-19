@echo off
REM Run script for Distributed Web Scraper on Windows

echo Starting Distributed Web Scraper...

REM Check if minimal test executables exist and run them if requested
if not exist bin\minimal_server.exe (
    echo Minimal test executables not found! 
    echo Would you like to build and test minimal examples first? (y/n)
    set /p choice=
    if /i "%choice%"=="y" (
        call build_minimal.bat
    ) else (
        echo Skipping minimal tests.
    )
)

REM Check if executables exist
if not exist bin\server.exe (
    echo Server executable not found! 
    echo Would you like to build the distributed web scraper now? (y/n)
    set /p choice=
    if /i "%choice%"=="y" (
        call build_distributed.bat
    ) else (
        echo Please build first using build_distributed.bat
        exit /b 1
    )
)

if not exist bin\worker.exe (
    echo Worker executable not found! Please build first using build_distributed.bat
    exit /b 1
)

REM Server settings
set SERVER_IP=127.0.0.1
set SERVER_PORT=9000

REM Website settings
set HOSTNAME=books.toscrape.com

REM Number of worker instances to start
set WORKER_COUNT=3

REM Start the server in a new window
echo Starting server on %SERVER_IP%:%SERVER_PORT%...
start "Web Scraper Server" cmd /c "bin\server.exe"

REM Wait a bit for the server to start
timeout /t 3 /nobreak > nul

REM Start multiple worker instances
echo Starting %WORKER_COUNT% worker instances...
for /l %%i in (1, 1, %WORKER_COUNT%) do (
    echo Starting worker instance %%i...
    start "Web Scraper Worker %%i" cmd /c "bin\worker.exe -s %SERVER_IP% -p %SERVER_PORT% -h %HOSTNAME%"
    timeout /t 1 /nobreak > nul
)

echo.
echo Distributed Web Scraper is running!
echo Server is running on %SERVER_IP%:%SERVER_PORT%
echo Started %WORKER_COUNT% worker instances that will run until server shutdown
echo.
echo Press Enter in the server window to shut down the server and all workers...
echo When server shutdown is complete, this window can be closed.
echo. 