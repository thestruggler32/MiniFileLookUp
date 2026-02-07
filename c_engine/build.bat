@echo off
echo [BUILD] Compiling C Engine...
if exist engine.exe del engine.exe
gcc src/main.c src/index.c src/trie.c -I include -o engine.exe
if %errorlevel% neq 0 (
    echo [ERROR] Compilation Failed!
    exit /b %errorlevel%
)
echo [SUCCESS] New engine.exe created.
