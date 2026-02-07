import os
import sys
import shutil
import time
import glob
import subprocess
import threading
import logging
import asyncio
from typing import List, Optional, Dict, Any
from fastapi import FastAPI, File, UploadFile, HTTPException, Query
from pydantic import BaseModel
from contextlib import asynccontextmanager
import uvicorn
from fastapi.middleware.cors import CORSMiddleware

# Import new extractor with page support
try:
    from extract_with_pages import extract_text_with_pages
except ImportError:
    # Fallback if running from a different directory
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from extract_with_pages import extract_text_with_pages

# Configuration
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

# Engine Wrapper (Async version)
class SearchEngine:
    def __init__(self):
        self.process = None
        self.lock = asyncio.Lock()
        # In-memory mapping of file_id -> original_filename
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
                    # Basename for display
                    self.file_map[fid] = os.path.basename(fname)
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
    logger.info(f"Files command output: {output}")
    
    files = []
    for line in output:
        parts = [p.strip() for p in line.split('|')]
        if len(parts) >= 6 and parts[0].isdigit():
            try:
                full_path = parts[1]
                basename = os.path.basename(full_path)
                
                fid = int(parts[0])
                engine.file_map[fid] = basename
                
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
            
            logger.info(f"Sending command: {cmd}")
            output = await engine.send_command(cmd)
            logger.info(f"Engine output: {output}")
            
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
        logger.error(f"Index error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/search", response_model=SearchResponse)
async def search(query: str):
    if not query.strip():
        return SearchResponse(query=query, total_hits=0, hits=[])
    
    # Use sentence command
    safe_query = query.replace('"', '').lower()
    cmd = f'sentence "{safe_query}"'
    
    output = await engine.send_command(cmd)
    
    hits = []
    # Parse output:
    # File ID    Page        Sentence    Frequency
    # ----------------------------------------------------
    # 1          2           3           2
    
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
        if len(parts) >= 4 and parts[0].isdigit():
            try:
                fid = int(parts[0])
                # Resolve filename
                fname = engine.file_map.get(fid, f"File {fid}")
                
                hits.append(SearchHit(
                    file_id=fid,
                    filename=fname,
                    page_number=int(parts[1]),
                    sentence_id=int(parts[2]),
                    sentence_text="", # Not available from C yet
                    frequency=int(parts[3])
                ))
            except ValueError:
                continue
                
    return SearchResponse(
        query=query,
        total_hits=len(hits),
        hits=hits
    )

@app.get("/autocomplete", response_model=List[str])
async def autocomplete(prefix: str):
    if not prefix.strip():
        return []
    
    safe_prefix = prefix.split()[0].lower() 
    
    cmd = f"autocomplete {safe_prefix}"
    output = await engine.send_command(cmd)
    
    suggestions = []
    # Output: "Suggestion: word"
    for line in output:
        if line.startswith("Suggestion: "):
            suggestions.append(line.split("Suggestion: ")[1].strip())
            
    return suggestions

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)
