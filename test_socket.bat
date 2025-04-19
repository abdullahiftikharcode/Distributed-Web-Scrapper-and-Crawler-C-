@echo off
echo Building Socket Test...

if not exist bin mkdir bin

echo Compiling test_socket.cpp...
g++ -std=c++11 -Wall -Wextra -D_WIN32_WINNT=0x0601 -I./include src/test_socket.cpp -o bin/test_socket -lwsock32 -lws2_32
if %errorlevel% neq 0 goto error

echo Build successful!
echo Running socket test...
bin\test_socket
goto end

:error
echo Build failed!

:end 