@echo off
echo [BUILD] Compiling C Engine with FM-Index...
if exist engine.exe del engine.exe
gcc src/main.c src/index.c src/trie.c src/fm_index.c -I include -o engine.exe -O2 -Wall -Wextra -Wno-unused-parameter
if %errorlevel% neq 0 (
    echo [ERROR] Compilation Failed!
    exit /b %errorlevel%
)
echo [SUCCESS] New engine.exe created (FM-Index backend).
