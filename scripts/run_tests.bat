@echo off
setlocal

:: Configures, builds and runs tests\CMakeLists.txt.
:: An argument of "debug" builds and runs the debug configuration; the default is release.
:: Any further arguments go to the test executable, e.g. a Catch2 test spec such as "[benchmark]".

set "CONFIG=Release"
if /i "%~1"=="debug" set "CONFIG=Debug"

set "BUILD_DIR=%~dp0..\build\%CONFIG%"

:: CMAKE_BUILD_TYPE serves single-config generators, --config multi-config ones; each ignores the other
cmake -S "%~dp0..\tests" -B "%BUILD_DIR%" -Wno-unused-cli -DCMAKE_BUILD_TYPE=%CONFIG% -DOUTPUT_DIR="%BUILD_DIR%\bin" || exit /b 1
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel || exit /b 1

"%BUILD_DIR%\bin\tests.exe" %2 %3 %4 %5
