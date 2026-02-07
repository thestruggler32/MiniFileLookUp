@echo off
REM ========================================
REM   Mini Search Engine - One-Click Start
REM ========================================

REM Debug pause - remove this later if confirmed working, keeps window open to see syntax errors
echo Initializing...

echo [1/7] Checking prerequisites...

REM Check for Python
python --version >nul 2>&1
if errorlevel 1 goto NoPython

REM Check for Node.js
node --version >nul 2>&1
if errorlevel 1 goto NoNode

REM Check for GCC (just set a var, don't fail)
gcc --version >nul 2>&1
if errorlevel 1 (
    set GCC_AVAILABLE=0
) else (
    set GCC_AVAILABLE=1
)

echo [OK] Prerequisites check complete.
echo.

REM ============================================
REM Step 2: Python Virtual Environment Setup
REM ============================================

echo [2/7] Setting up Python virtual environment...
if exist "c_engine\venv\" goto VenvExists

echo Creating virtual environment...
python -m venv c_engine\venv
if errorlevel 1 goto VenvError
echo [OK] Virtual environment created.
goto InstallDeps

:VenvExists
echo [OK] Virtual environment already exists.

:InstallDeps
echo.
echo [3/7] Installing Python dependencies...

REM Check if packages installed
if exist "c_engine\venv\Lib\site-packages\fastapi" goto DepsInstalled

echo Installing: fastapi, uvicorn, python-multipart, pypdf, python-docx...
call c_engine\venv\Scripts\activate.bat
pip install --quiet fastapi uvicorn python-multipart pypdf python-docx
if errorlevel 1 goto PipError
echo [OK] Python dependencies installed.
goto CheckC

:DepsInstalled
echo [OK] Python dependencies already installed.

:CheckC
echo.
echo [4/7] Checking C engine...

if "%GCC_AVAILABLE%"=="0" goto CheckCNoGcc
if exist "c_engine\engine.exe" goto CEngineExists

echo Compiling C engine...
pushd c_engine\src
gcc main.c trie.c index.c -o ..\engine.exe
if errorlevel 1 goto CompileError
popd
echo [OK] C engine compiled successfully.
goto InstallNode

:CEngineExists
echo [OK] C engine already exists.
goto InstallNode

:CheckCNoGcc
if exist "c_engine\engine.exe" (
    echo [OK] C engine found (pre-compiled).
) else (
    echo [ERROR] C engine not found and GCC is not available.
    echo Please install GCC or use a pre-compiled engine.exe
    pause
    exit
)

:InstallNode
echo.
echo [5/7] Installing Node.js dependencies...

if exist "node_modules\" goto NodeDepsInstalled

echo Running npm install...
call npm install
if errorlevel 1 goto NpmError
echo [OK] Node.js dependencies installed.
goto StartServers

:NodeDepsInstalled
echo [OK] Node.js dependencies already installed.

:StartServers
echo.
echo [6/7] Starting servers...
echo.

echo Starting FastAPI backend (Python API)...
REM Using start with explicit title and directory change
start "FastAPI Backend" cmd /k "pushd "%~dp0c_engine" && call venv\Scripts\activate.bat && python api.py"

echo Starting Next.js frontend (React UI)...
start "Next.js Frontend" cmd /k "pushd "%~dp0" && npm run dev"

echo Waiting for services to initialize...
timeout /t 5 /nobreak >nul

echo [7/7] Launching browser...
start "" "http://localhost:3000"

echo.
echo ========================================
echo   Mini Search Engine IS LIVE!
echo ========================================
echo.
echo Dashboard: http://localhost:3000
echo API Docs:  http://127.0.0.1:8000/docs
echo.
echo Keep the other two terminal windows open.
echo Closing this window will NOT stop the servers.
echo.
pause
exit

REM ============================================
REM Error Support Labels
REM ============================================

:NoPython
echo [ERROR] Python is not installed or not in PATH.
echo Please install Python 3.8+ from https://www.python.org/downloads/
pause
exit

:NoNode
echo [ERROR] Node.js is not installed or not in PATH.
echo Please install Node.js 18+ from https://nodejs.org/
pause
exit

:VenvError
echo [ERROR] Failed to create virtual environment.
pause
exit

:PipError
echo [ERROR] Failed to install Python dependencies.
pause
exit

:CompileError
echo [ERROR] C engine compilation failed.
popd
pause
exit

:NpmError
echo [ERROR] Failed to install Node.js dependencies.
pause
exit
