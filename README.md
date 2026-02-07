# Mini Search Engine

A high-performance hybrid search engine with a C core, Python compatibility layer, and Next.js UI.

## Architecture

1.  **C Core** (`c_engine/src/Code`): In-memory inverted index + Trie. Handles persistent `interactive` session for indexing and searching.
2.  **Middleware** (`c_engine/api.py`): FastAPI backend. Wraps the C engine process, handles file uploads/extraction (PDF, DOCX, etc.), and exposes clean JSON endpoints.
3.  **Frontend** (`src/`): Next.js + Tailwind CSS UI. Provides file management, autocomplete, and sentence-level search results.

## Quick Start (Windows)

### Option 1: One-Click Start ⚡ (Recommended)
Simply double-click `start.bat` or run:
```powershell
.\start.bat
```
**What it does**:
- Creates Python virtual environment (if needed)
- Installs all dependencies automatically
- Compiles C engine (if needed)
- Starts both FastAPI backend and Next.js frontend

*Two terminal windows will open. Keep them running while using the app.*

### Option 2: Manual Setup

#### 1. Build C Core
Requires GCC (MinGW or similar).
```powershell
cd c_engine/src
gcc main.c trie.c index.c -o ../engine.exe
```

#### 2. Run API Server (Backend)
Requires Python 3.8+.
```powershell
cd c_engine
pip install fastapi uvicorn python-multipart pypdf python-docx
python api.py
```
*API runs at http://127.0.0.1:8000*

#### 3. Run UI (Frontend)
Requires Node.js 18+.
```powershell
# In the root 'mini-search-engine' folder
npm install
npm run dev
```
*UI runs at http://localhost:3000*

## Features
- **Fast**: C-based indexing and search.
- **Smart**: Sentence-level granularity.
- **Flexible**: Drag-and-drop support for .pdf, .docx, .csv, .txt.
- **Interactive**: Real-time autocomplete.

## Project Structure
- `c_engine/`
    - `src/`: C source code.
    - `api.py`: FastAPI application.
    - `extractor.py`: Text extraction logic.
- `src/`
    - `app/`: Next.js pages.
    - `components/`: React UI components.
    - `lib/api.ts`: API client.

## Future Transitions
The API is designed to be client-agnostic. It can be consumed by:
- Desktop Apps (Electron/Tauri)
- Browser Extensions
- Mobile Apps
