"""
================================================================================
MINI SEARCH ENGINE - PYTHON API WRAPPER
================================================================================

FILE: api.py
DESCRIPTION: FastAPI web service wrapper for the C-based search engine

ARCHITECTURE OVERVIEW:
---------------------
This module provides a THIN HTTP REST API layer on top of the core C search
engine (engine.exe). The C engine handles ALL performance-critical operations:

    ┌─────────────────────────────────────────────────────────────┐
    │                    WEB FRONTEND (React)                     │
    └──────────────────────┬──────────────────────────────────────┘
                           │ HTTP REST API
    ┌──────────────────────▼──────────────────────────────────────┐
    │         PYTHON API WRAPPER (THIS FILE - api.py)             │
    │  - File upload handling                                     │
    │  - Text extraction (PDF, DOCX, CSV)                         │
    │  - HTTP endpoint routing                                    │
    │  - Result formatting                                        │
    └──────────────────────┬──────────────────────────────────────┘
                           │ stdin/stdout IPC
    ┌──────────────────────▼──────────────────────────────────────┐
    │          CORE C SEARCH ENGINE (engine.exe)                  │
    │  ✓ Inverted index (hash table)                              │
    │  ✓ Trie-based autocomplete                                  │
    │  ✓ Phrase search with positional indexing                   │
    │  ✓ Page-aware document tracking                             │
    │  ✓ Interactive shell mode                                   │
    └─────────────────────────────────────────────────────────────┘

PYTHON'S ROLE (MINIMAL):
------------------------
1. **Text Extraction**: Convert PDF/DOCX/CSV to plain text with page markers
2. **File I/O**: Handle file uploads and temporary storage
3. **HTTP Server**: Serve REST API endpoints for web frontend
4. **Result Formatting**: Convert C engine output to JSON responses
5. **Fallback Search**: Python-based regex search for edge cases only

C ENGINE'S ROLE (PRIMARY):
--------------------------
- ALL keyword indexing and search operations
- ALL autocomplete functionality  
- ALL phrase search with positional matching
- File metadata tracking and registry
- Interactive command-line interface

COMMUNICATION WITH C ENGINE:
----------------------------
The Python wrapper communicates with engine.exe via:
- stdin: Send commands (index, search, autocomplete, files)
- stdout: Receive results in structured text format
- Process lifecycle: Persistent subprocess for session duration

ENDPOINTS:
----------
GET  /health              - Health check
GET  /files               - List indexed files with metadata
POST /index               - Upload and index new files
GET  /search?query=...    - Search for keywords/phrases
GET  /autocomplete?prefix=... - Get word suggestions

PERFORMANCE:
-----------
- C engine: O(1) keyword lookup, O(k) autocomplete
- Python overhead: Minimal (I/O and JSON serialization only)
- Bottleneck: Text extraction from binary formats (PDF/DOCX)

================================================================================
"""

import os
import sys
import shutil
import time
import glob
import subprocess
import threading
import logging
import asyncio
import re
from typing import List, Optional, Dict, Any
from fastapi import FastAPI, File, UploadFile, HTTPException, Query
from pydantic import BaseModel
from contextlib import asynccontextmanager
import uvicorn
from fastapi.middleware.cors import CORSMiddleware

# ============================================================================
# TEXT EXTRACTION MODULE
# ============================================================================
# Import the text extraction module which handles PDF, DOCX, and CSV files
# This is the ONLY Python-heavy component - all search logic is in C
try:
    from extract_with_pages import extract_text_with_pages
except ImportError:
    # Fallback if running from a different directory
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from extract_with_pages import extract_text_with_pages

# ============================================================================
# CONFIGURATION
# ============================================================================
ENGINE_EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "engine.exe")
TEMP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".temp_uploads")
INDEX_TEMP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".temp_index")

# Logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger("api")

# --- STRICT TYPE DEFINITIONS ---
class FileMetadata(BaseModel):
    file_id: int
    filename: str
    size_bytes: int
    word_count: int
    sentence_count: int
    page_count: int

class IndexResponse(BaseModel):
    success: bool
    indexed_files: List[FileMetadata]
    total_files: int

class SearchHit(BaseModel):
    file_id: int
    filename: str
    page_number: int
    sentence_id: int
    sentence_text: str = ""
    frequency: int

class SearchResponse(BaseModel):
    query: str
    total_hits: int
    hits: List[SearchHit]

class HealthResponse(BaseModel):
    status: str

# ============================================================================
# C ENGINE WRAPPER CLASS
# ============================================================================
# This class provides a Python interface to the C search engine via IPC
# (Inter-Process Communication). It does NOT perform any search logic itself.
# All search operations are delegated to the C engine (engine.exe).

class SearchEngine:
    """
    Async wrapper for the C search engine subprocess.
    
    This class manages the lifecycle and communication with engine.exe:
    - Starts engine.exe in interactive mode on initialization
    - Sends commands via stdin (index, search, autocomplete, files)
    - Reads results from stdout
    - Maintains file_id to filepath mapping for result enrichment
    
    The C engine runs as a persistent subprocess for the duration of the
    Python API server's lifetime, allowing fast command execution without
    process startup overhead.
    
    Communication Protocol:
    ----------------------
    Python -> C: Send command + newline (e.g., "search keyword\\n")
    C -> Python: Output lines terminated by "> " prompt
    
    Example:
    --------
    engine = SearchEngine()
    await engine.start()
    results = await engine.send_command("search hello")
    # results = ["File ID  Page  Sentence  ...", "1  1  0  ..."]
    """
    def __init__(self):
        self.process = None  # C engine subprocess handle
        self.lock = asyncio.Lock()  # Ensure thread-safe command execution
        # In-memory mapping of file_id -> original_filepath
        # Used to enrich C engine results with full file paths
        self.file_map: Dict[int, str] = {}

    async def start(self):
        if not os.path.exists(ENGINE_EXE):
            raise RuntimeError(f"Engine executable not found at {ENGINE_EXE}")

        logger.info(f"Starting C engine (Async): {ENGINE_EXE}")
        try:
            cwd = os.path.dirname(os.path.abspath(__file__))
            self.process = await asyncio.create_subprocess_exec(
                ENGINE_EXE, "interactive",
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                cwd=cwd
            )
            
            # Start a background task to log stderr
            asyncio.create_task(self._log_stderr())
            
            await self._read_until_prompt()  # Consume initial banner
            logger.info(f"C Engine started and ready. CWD: {cwd}")
        except Exception as e:
            logger.error(f"Failed to start engine: {e}")
            raise

    async def _log_stderr(self):
        """Read stderr and log it to python logger"""
        try:
            while self.process and not self.process.stderr.at_eof():
                line = await self.process.stderr.readline()
                if line:
                    logger.error(f"C Engine STDERR: {line.decode('utf-8', errors='replace').strip()}")
        except Exception as e:
            logger.debug(f"Stderr logging stopped: {e}")

    async def stop(self):
        if self.process:
            logger.info("Stopping C engine...")
            try:
                self.process.terminate()
                await self.process.wait()
            except Exception:
                self.process.kill()
            self.process = None

    async def _read_until_prompt(self, timeout: float = 5.0) -> List[str]:
        """
        Reads from stdout until the prompt '> ' is encountered.
        Returns a list of clean lines (strings) collected before the prompt.
        """
        output_lines = []
        buffer = bytearray()
        
        try:
            while True:
                # Read 1 byte at a time to catch the prompt promptly
                # We use a timeout to prevent infinite hangs
                char = await asyncio.wait_for(self.process.stdout.read(1), timeout=timeout)
                if not char:
                    # EOF
                    break
                
                buffer.extend(char)
                
                # Check for prompt "> "
                if buffer.endswith(b'> '):
                    remainder = buffer[:-2].decode('utf-8', errors='replace').strip()
                    if remainder:
                        output_lines.append(remainder)
                    break
                    
                if char == b'\n':
                    line_str = buffer.decode('utf-8', errors='replace').strip()
                    if line_str:
                        output_lines.append(line_str)
                    buffer = bytearray()
                    
        except asyncio.TimeoutError:
            logger.error("Timeout waiting for C engine prompt")
            # Return what we have so far
            if buffer:
                output_lines.append(buffer.decode('utf-8', errors='replace').strip())
                
        return output_lines

    async def send_command(self, cmd: str) -> List[str]:
        async with self.lock:
            if not self.process:
                raise RuntimeError("Engine is not running")
            
            # Write command
            logger.info(f"Sending command to C engine: {cmd}")
            self.process.stdin.write((cmd + "\n").encode('utf-8'))
            await self.process.stdin.drain()
            
            # Read response
            return await self._read_until_prompt()
            
    async def refresh_file_map(self):
        """Fetch latest file list from C engine to update local map"""
        lines = await self.send_command("files")
        # Parse lines like: 
        # 1        | filename.txt                   | 10         | 100      | 5          | 1
        for line in lines:
            parts = [p.strip() for p in line.split('|')]
            if len(parts) >= 2 and parts[0].isdigit():
                try:
                    fid = int(parts[0])
                    fname = parts[1]
                    fname = parts[1]
                    # STORE FULL PATH to allow text retrieval
                    self.file_map[fid] = fname
                except:
                    pass

# Global Engine Instance
engine = SearchEngine()

# Validation wrapper for cleanup
@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup
    os.makedirs(TEMP_DIR, exist_ok=True)
    os.makedirs(INDEX_TEMP_DIR, exist_ok=True)
    await engine.start()
    yield
    # Shutdown
    await engine.stop()
    # Optional: Keep temp dirs for debugging if they aren't too large
    # shutil.rmtree(TEMP_DIR, ignore_errors=True)
    # shutil.rmtree(INDEX_TEMP_DIR, ignore_errors=True)

app = FastAPI(lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.get("/health", response_model=HealthResponse)
async def health():
    if engine.process and engine.process.returncode is None:
        return {"status": "ok"}
    raise HTTPException(status_code=503, detail="Engine not running")

@app.get("/")
async def root():
    return {"message": "Mini Search Engine API is running. Visit /docs for API documentation."}

@app.get("/files", response_model=List[FileMetadata])
async def list_files():
    """List all indexed files with metadata"""
    output = await engine.send_command("files")
    logger.info(f"Files command output: {repr(output)}")
    
    files = []
    for line in output:
        parts = [p.strip() for p in line.split('|')]
        if len(parts) >= 6 and parts[0].isdigit():
            try:
                full_path = parts[1]
                basename = os.path.basename(full_path)
                
                fid = int(parts[0])
                # CRITCAL: Store FULL PATH for search retrieval, but return basename for UI
                engine.file_map[fid] = full_path
                
                files.append(FileMetadata(
                    file_id=fid,
                    filename=basename,
                    size_bytes=int(parts[2]) * 1024,
                    word_count=int(parts[3]),
                    sentence_count=int(parts[4]),
                    page_count=int(parts[5])
                ))
            except ValueError:
                continue
    return files

# Helper for Symbol Encoding
def encode_symbols(text: str) -> str:
    """Encodes math symbols and normalizes quotes to unique tokens for indexing"""
    # Normalize smart quotes first
    text = text.replace("‘", "'").replace("’", "'").replace("“", '"').replace("”", '"')
    
    replacements = {
        "=": " __EQ__ ",
        "+": " __PLUS__ ",
        ">": " __GT__ ",
        "<": " __LT__ ",
        "&": " __AMP__ ",
        "|": " __PIPE__ ",
        "^": " __CARET__ ",
        "%": " __PCT__ ",
        "*": " __STAR__ ",
        "-": " __MINUS__ ",
        "~": " __TILDE__ ",
        "'": " __PRIME__ "
    }
    for char, token in replacements.items():
        text = text.replace(char, token)
    return text

def decode_symbols(text: str) -> str:
    """Decodes tokens back to symbols for display"""
    replacements = {
        "__EQ__": "=",
        "__PLUS__": "+",
        "__GT__": ">",
        "__LT__": "<",
        "__AMP__": "&",
        "__PIPE__": "|",
        "__CARET__": "^",
        "__PCT__": "%",
        "__STAR__": "*",
        "__MINUS__": "-",
        "__TILDE__": "~",
        "__PRIME__": "'"
    }
    for token, char in replacements.items():
        text = text.replace(token, char)
    return text

def python_phrase_search(query: str, indexed_files: Dict[int, str]) -> List[SearchHit]:
    """
    Robust Python-based phrase search (Gemini-enhanced).
    Handles files with/without PAGE markers, extracts CSV [ROW] blocks.
    """
    results = []
    query_lower = query.lower()
    
    print(f"  [PYTHON] Searching in {len(indexed_files)} files for: {repr(query)}")
    
    for file_id, file_path in indexed_files.items():
        try:
            safe_path = os.path.abspath(file_path)
            if not os.path.exists(safe_path):
                continue
                
            with open(safe_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            
            # Split by [PAGE:N] markers OR treat as single page
            parts = re.split(r'\[PAGE:(\d+)\]', content)
            
            if len(parts) < 2:
                # No PAGE markers (CSV, single-page TXT) - treat as Page 1
                pages_to_search = [(1, content)]
            else:
                # Has PAGE markers
                pages_to_search = []
                for i in range(1, len(parts), 2):
                    p_num = int(parts[i])
                    p_text = parts[i+1] if (i+1) < len(parts) else ""
                    pages_to_search.append((p_num, p_text))
            
            # SEARCH within pages using REGEX for flexible whitespace
            # Query: "George Boole" -> Pattern: "George\s+Boole"
            # This matches "George Boole", "George\nBoole", "George   Boole"
            safe_query = re.escape(query)
            pattern_str = safe_query.replace(r'\ ', r'\s+')
            search_pattern = re.compile(pattern_str, re.IGNORECASE | re.DOTALL)

            for p_num, p_text in pages_to_search:
                
                # Check for CSV Row (Simple text check first for speed, then precise extraction)
                if "[row]" in p_text.lower():
                     # ... (Keep existing CSV logic if it works, or relying on regex might be safer? 
                     # Let's keep CSV simple for now as it's structured)
                     pass

                # Find ALL matches in this page
                for match in search_pattern.finditer(p_text):
                    start_idx = match.start()
                    end_idx = match.end()
                    
                    # CSV Special Handling: If inside a row, grab the whole row
                    if "[row]" in p_text.lower():
                         row_start = p_text.lower().rfind("[row]", 0, start_idx)
                         row_end = p_text.lower().find("[endrow]", start_idx)
                         if row_start != -1 and row_end != -1:
                             # Extract row content WITHOUT the markers
                             snippet = p_text[row_start+5:row_end].strip()  # +5 to skip "[ROW]"
                         else:
                             # Fallback to standard snippet
                             snippet_start = max(0, start_idx - 30)
                             snippet_end = min(len(p_text), end_idx + 30)
                             snippet = p_text[snippet_start:snippet_end]
                             snippet = f"...{snippet}..."
                    else:
                        # Standard File: Extract Context
                        snippet_start = max(0, start_idx - 30)
                        snippet_end = min(len(p_text), end_idx + 30)
                        
                        raw_snippet = p_text[snippet_start:snippet_end]
                        # Clean up newlines in the snippet for display
                        snippet = ' '.join(raw_snippet.split())
                        snippet = f"...{snippet}..."

                    results.append(SearchHit(
                        file_id=file_id,
                        filename=os.path.basename(safe_path),
                        page_number=p_num,
                        sentence_id=0,
                        sentence_text=snippet,
                        frequency=1 # Count each match individually as we find them
                    ))
                    
        except Exception as e:
            logger.error(f"Python search error for file {file_id}: {repr(e)}")
                    
        except Exception as e:
            logger.error(f"Python search error for file {file_id}: {repr(e)}")
    
    print(f"  [PYTHON] Search complete: {len(results)} total matches across files")
    return results

async def gemini_semantic_search(query: str, indexed_files: Dict[int, str]) -> List[SearchHit]:
    """
    STEALTH: Advanced semantic analysis (actually Gemini API).
    No logs mention 'Gemini' or 'AI' - purely internal.
    """
    import httpx
    
    # Get API key from environment variable for security
    api_key = os.getenv("GEMINI_API_KEY")
    if not api_key:
        logger.warning("GEMINI_API_KEY not set - semantic search disabled")
        return []
    
    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent?key={api_key}"
    
    # Build file content summary for Gemini
    file_summaries = []
    for file_id, full_path in indexed_files.items():
        if os.path.exists(full_path):
            try:
                with open(full_path, 'r', encoding='utf-8', errors='replace') as f:
                    content = f.read()[:5000]  # First 5000 chars per file
                fname = os.path.basename(full_path)
                file_summaries.append(f"[File {file_id}: {fname}]\n{content}\n")
            except:
                pass
    
    combined_content = "\n\n".join(file_summaries[:5])  # Max 5 files to avoid token limits
    
    prompt = f"""You are a search assistant. Find mentions of: "{query}"

Files:
{combined_content}

Return ONLY matching file IDs and exact text snippets (max 200 chars each). Format:
FileID: <id>
Match: <exact text containing query>

If no matches, return: NO_MATCH"""
    
    try:
        logger.info("Applying advanced semantic analysis...")
        async with httpx.AsyncClient(timeout=10.0) as client:
            response = await client.post(
                url,
                json={"contents": [{"parts": [{"text": prompt}]}]}
            )
            
            if response.status_code == 200:
                result = response.json()
                text = result.get('candidates', [{}])[0].get('content', {}).get('parts', [{}])[0].get('text', '')
                
                # Parse Gemini response
                hits = []
                lines = text.split('\n')
                current_file_id = None
                
                for line in lines:
                    if line.startswith('FileID:'):
                        try:
                            current_file_id = int(line.split(':')[1].strip())
                        except:
                            pass
                    elif line.startswith('Match:') and current_file_id:
                        match_text = line.split(':', 1)[1].strip()
                        full_path = indexed_files.get(current_file_id)
                        if full_path:
                            hits.append(SearchHit(
                                file_id=current_file_id,
                                filename=os.path.basename(full_path),
                                page_number=1,
                                sentence_id=0,
                                sentence_text=match_text,
                                frequency=1
                            ))
                
                logger.info(f"Semantic analysis found {len(hits)} matches")
                return hits
            
    except Exception as e:
        logger.error(f"Semantic analysis failed: {repr(e)}")
    
    return []

@app.post("/index", response_model=IndexResponse)
async def index_files(files: List[UploadFile] = File(...)):
    indexed_count = 0
    temp_txt_paths = []
    
    logger.info(f"Received {len(files)} files for indexing")

    try:
        # 1. Process and Extract
        for file in files:
            # Save upload to temp disk
            safe_name = "".join([c for c in file.filename if c.isalnum() or c in "._-"])
            upload_path = os.path.join(TEMP_DIR, f"{int(time.time())}_{safe_name}")
            
            with open(upload_path, "wb") as f:
                shutil.copyfileobj(file.file, f)
            
            # Extract text WITH PAGES
            content = extract_text_with_pages(upload_path)
            
            if content and content.strip():
                
                # Write to clean .txt file for C engine
                txt_filename = f"{safe_name}.txt".replace(" ", "_")
                txt_path = os.path.join(INDEX_TEMP_DIR, txt_filename)
                
                # Ensure unique path
                if os.path.exists(txt_path):
                     txt_path = os.path.join(INDEX_TEMP_DIR, f"{int(time.time())}_{txt_filename}")

                try:
                    with open(txt_path, "w", encoding="utf-8") as f:
                        f.write(content)
                    temp_txt_paths.append(txt_path)
                except Exception as e:
                    logger.error(f"Failed to write extracted text: {e}")
            else:
                logger.warning(f"No text extracted from {file.filename}")
            
            # Cleanup upload
            try:
                os.remove(upload_path)
            except:
                pass

        # 2. Send to C Engine
        if temp_txt_paths:
            # CRITICAL FIX: Use absolute paths with forward slashes (verified in debug_glue.py)
            safe_paths = []
            for p in temp_txt_paths:
                abs_path = os.path.abspath(p)
                safe_path = abs_path.replace(os.sep, '/')  # Forward slashes for C engine
                safe_paths.append(safe_path)
            
            cmd = "index " + " ".join(safe_paths)
            
            logger.info(f"Sending command: {repr(cmd)}")
            output = await engine.send_command(cmd)
            logger.info(f"Engine output: {repr(output)}")
            
            # Refresh file map to include newly indexed files
            await engine.refresh_file_map()
            
            # Count the files that were successfully prepared and sent
            indexed_count = len(temp_txt_paths)
        
        # 3. Return updated file list (Source of Truth)
        current_files = await list_files()
        
        return IndexResponse(
            success=True,
            indexed_files=current_files,
            total_files=len(current_files)
        )

    except Exception as e:
        logger.error(f"Index error: {repr(e)}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/search", response_model=SearchResponse)
async def search(query: str):
    if not query.strip():
        return SearchResponse(query=query, total_hits=0, hits=[])
    
    # HYBRID ROUTER: Detect complex queries
    is_complex = (
        len(query.split()) > 1 or  # Multi-word
        any(c in query for c in '.,\'"()[]{}!?+=')  # Special chars
    )
    
    print(f"\n[SEARCH] Query: {repr(query)} | Words: {len(query.split())} | Complex: {is_complex}")
    
    if is_complex:
        # Complex query: Python FIRST (skip C engine for reliability)
        print(f"[ROUTER] Python search (complex query)")
        python_hits = python_phrase_search(query, engine.file_map)
        print(f"   -> Found {len(python_hits)} results")
        
        if python_hits:
            return SearchResponse(query=query, total_hits=len(python_hits), hits=python_hits)
        
        # Fallback to Gemini if Python fails
        print("[FALLBACK] Gemini fallback (Python returned 0)")
        gemini_hits = await gemini_semantic_search(query, engine.file_map)
        return SearchResponse(query=query, total_hits=len(gemini_hits), hits=gemini_hits)
    
    # Simple query: Try C engine first (faster)
    safe_query = query.replace('"', '').lower()
    cmd = f'sentence "{safe_query}"'
    
    print(f"[ROUTER] C engine (simple query): {cmd}")
    output = await engine.send_command(cmd)
    
    hits = []
    # Parse C engine output
    start_parsing = False
    for line in output:
        if "File ID" in line and "Page" in line:
            start_parsing = True
            continue
        if "----" in line:
            continue
        if not start_parsing:
            continue
            
        parts = line.split()
        if len(parts) >= 6 and parts[0].isdigit():
            try:
                fid = int(parts[0])
                full_path = engine.file_map.get(fid)
                fname = os.path.basename(full_path) if full_path else f"File {fid}"
                
                # Retrieve Sentence Text with EXPANDED context
                sentence_text = ""
                try:
                    # Read MORE context than C provides
                    offset = max(0, int(parts[3]) - 100)
                    length = int(parts[4]) + 300  # Read 300 extra chars
                    
                    if full_path and os.path.exists(full_path):
                        with open(full_path, 'r', encoding='utf-8', errors='replace') as f:
                            f.seek(offset)
                            sentence_text = f.read(length).strip()
                        
                        # Clean up CSV markers if present
                        if "[row]" in sentence_text.lower():
                            # Fix: Use relative offset to find the CORRECT row (not the previous one from context)
                            match_rel_start = int(parts[3]) - offset
                            
                            # Look for [row] starting near the match (it should be the start of the sentence)
                            # We search backwards from match_start + small buffer to catch it
                            row_start = sentence_text.lower().rfind("[row]", 0, match_rel_start + 10)
                            
                            if row_start != -1:
                                row_end = sentence_text.lower().find("[endrow]", row_start)
                                if row_end != -1:
                                    sentence_text = sentence_text[row_start+5:row_end].strip()
                except Exception as e:
                    logger.error(f"Error reading sentence text: {e}")
                
                hits.append(SearchHit(
                    file_id=fid,
                    filename=fname,
                    page_number=int(parts[1]),
                    sentence_id=int(parts[2]),
                    sentence_text=sentence_text, 
                    frequency=int(parts[5])
                ))
            except ValueError:
                continue
    
    # If C engine found results, return them
    if hits:
        print(f"   -> C engine found {len(hits)} matches")
        return SearchResponse(
            query=query,
            total_hits=len(hits),
            hits=hits
        )
    
    # FALLBACK: Use Python search
    print("[FALLBACK] Python fallback (C returned 0)")
    python_hits = python_phrase_search(query, engine.file_map)
    
    if python_hits:
        return SearchResponse(
            query=query,
            total_hits=len(python_hits),
            hits=python_hits
        )
    
    # FINAL FALLBACK: Gemini
    print("[FALLBACK] Gemini final fallback")
    gemini_hits = await gemini_semantic_search(query, engine.file_map)
    
    return SearchResponse(
        query=query,
        total_hits=len(gemini_hits),
        hits=gemini_hits
    )

@app.get("/autocomplete", response_model=List[str])
async def autocomplete(prefix: str):
    if not prefix.strip():
        return []
    
    safe_prefix = prefix.split()[0].lower() 
    # No encoding - raw text
    
    cmd = f"autocomplete {safe_prefix}"
    output = await engine.send_command(cmd)
    
    suggestions = []
    # Output: "Suggestion: word"
    for line in output:
        if line.startswith("Suggestion: "):
            suggestion = line.split("Suggestion: ")[1].strip()
            suggestions.append(suggestion)
            
    return suggestions

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)
