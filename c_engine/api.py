import os
import sys
import shutil
import time
import glob
import subprocess
import threading
import logging
from typing import List, Optional, Dict, Any
from fastapi import FastAPI, File, UploadFile, HTTPException, Query
from pydantic import BaseModel
from contextlib import asynccontextmanager
import uvicorn

# Import existing extractor
try:
    from extractor import extract_text
except ImportError:
    # Fallback if running from a different directory
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from extractor import extract_text

# Configuration
ENGINE_EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "engine.exe")
TEMP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".temp_uploads")
INDEX_TEMP_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".temp_index")

# Logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger("api")

# Models
class SearchResult(BaseModel):
    file_id: int
    sentence_id: int
    frequency: int

class IndexResponse(BaseModel):
    indexed_files: int

class HealthResponse(BaseModel):
    status: str

# Engine Wrapper
class SearchEngine:
    def __init__(self):
        self.process = None
        self.lock = threading.Lock()

    def start(self):
        if not os.path.exists(ENGINE_EXE):
            raise RuntimeError(f"Engine executable not found at {ENGINE_EXE}")

        logger.info(f"Starting C engine: {ENGINE_EXE}")
        try:
            self.process = subprocess.Popen(
                [ENGINE_EXE, "interactive"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # Merge stderr to handle errors in same stream
                bufsize=0,  # Unbuffered
            )
            self._read_until_prompt()  # Consume initial banner
            logger.info("C Engine started and ready.")
        except Exception as e:
            logger.error(f"Failed to start engine: {e}")
            raise

    def stop(self):
        if self.process:
            logger.info("Stopping C engine...")
            try:
                self.process.terminate()
                self.process.wait(timeout=2)
            except Exception:
                self.process.kill()
            self.process = None

    def _read_until_prompt(self) -> List[str]:
        """
        Reads from stdout byte-by-byte until the prompt '> ' is encountered.
        Returns a list of clean lines (strings) collected before the prompt.
        """
        output_lines = []
        current_line = bytearray()
        
        while True:
            char = self.process.stdout.read(1)
            if not char:
                # EOF/Process died
                break
            
            # Check for prompt "> " (last 2 chars)
            # We append first to check the sequence
            current_line.extend(char)
            
            # Optimization: Check directly if we have a newline
            if char == b'\n':
                line_str = current_line.decode('utf-8', errors='replace').strip()
                if line_str:
                    output_lines.append(line_str)
                current_line = bytearray()
            elif current_line.endswith(b'> '):
                # Prompt detected.
                # The prompt itself is NOT part of the output lines we want to return.
                # However, there might be text before the prompt on the same line (rare but possible).
                remainder = current_line[:-2].decode('utf-8', errors='replace').strip()
                if remainder:
                    output_lines.append(remainder)
                break
                
        return output_lines

    def send_command(self, cmd: str) -> List[str]:
        with self.lock:
            if not self.process:
                raise RuntimeError("Engine is not running")
            
            # Write command
            logger.info(f"Sending command: {cmd}")
            self.process.stdin.write((cmd + "\n").encode('utf-8'))
            self.process.stdin.flush()
            
            # Read response
            return self._read_until_prompt()

# Global Engine Instance
engine = SearchEngine()

# Validation wrapper for cleanup
@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup
    os.makedirs(TEMP_DIR, exist_ok=True)
    os.makedirs(INDEX_TEMP_DIR, exist_ok=True)
    engine.start()
    yield
    # Shutdown
    engine.stop()
    shutil.rmtree(TEMP_DIR, ignore_errors=True)
    shutil.rmtree(INDEX_TEMP_DIR, ignore_errors=True)

app = FastAPI(lifespan=lifespan)

from fastapi.middleware.cors import CORSMiddleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

@app.get("/health", response_model=HealthResponse)
async def health():
    if engine.process and engine.process.poll() is None:
        return {"status": "ok"}
    raise HTTPException(status_code=503, detail="Engine not running")

@app.get("/")
async def root():
    return {"message": "Mini Search Engine API is running. Visit /docs for API documentation."}

@app.post("/index", response_model=IndexResponse)
async def index_files(files: List[UploadFile] = File(...)):
    indexed_count = 0
    temp_txt_paths = []

    try:
        # 1. Process and Extract
        for file in files:
            # Save upload to temp disk
            safe_name = "".join([c for c in file.filename if c.isalnum() or c in "._-"])
            upload_path = os.path.join(TEMP_DIR, f"{int(time.time())}_{safe_name}")
            
            with open(upload_path, "wb") as f:
                shutil.copyfileobj(file.file, f)
            
            # Extract text
            content = extract_text(upload_path)
            
            if content and content.strip():
                # Write to clean .txt file for C engine
                # Encode spaces in filename to avoiding command parsing issues in C (strtok)
                # The C engine uses space as delimiter.
                # We'll use underscores for the filename passed to C.
                txt_filename = f"{safe_name}.txt".replace(" ", "_")
                txt_path = os.path.join(INDEX_TEMP_DIR, txt_filename)
                
                # Ensure unique path if multiple users (though redundant with time prefix above)
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
            # Command: index file1 file2 ...
            # IMPORTANT: The C engine uses strtok with space delimiter, so paths with spaces break!
            # Use RELATIVE paths from the engine's working directory (c_engine folder).
            # The .temp_index folder is relative to the engine, so we use ".temp_index/filename"
            base_dir = os.path.dirname(os.path.abspath(__file__))
            cmd_paths = []
            for p in temp_txt_paths:
                # Convert to relative path from base_dir
                rel_path = os.path.relpath(p, base_dir).replace(os.sep, "/")
                cmd_paths.append(rel_path)
            cmd = "index " + " ".join(cmd_paths)
            
            output = engine.send_command(cmd)
            
            # Count the files that were successfully prepared and sent
            indexed_count = len(temp_txt_paths)
        
        return {"indexed_files": indexed_count}

    except Exception as e:
        logger.error(f"Index error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/search", response_model=List[SearchResult])
async def search(query: str):
    if not query.strip():
        return []
    
    # Use sentence command
    # Sanitize query: remove quotes to avoid breaking the protocol
    safe_query = query.replace('"', '')
    cmd = f'sentence "{safe_query}"'
    
    output = engine.send_command(cmd)
    
    results = []
    # Parse output:
    # File ID    Sentence ID  Frequency
    # ----------------------------------------
    # 1          3            2
    
    start_parsing = False
    for line in output:
        if "File ID" in line and "Sentence ID" in line:
            start_parsing = True
            continue
        if "----" in line:
            continue
        if not start_parsing:
            continue
            
        parts = line.split()
        if len(parts) >= 3 and parts[0].isdigit():
            try:
                results.append(SearchResult(
                    file_id=int(parts[0]),
                    sentence_id=int(parts[1]),
                    frequency=int(parts[2])
                ))
            except ValueError:
                continue
                
    return results

@app.get("/autocomplete", response_model=List[str])
async def autocomplete(prefix: str):
    if not prefix.strip():
        return []
    
    # Sanitize prefix (simple single word expected usually, but space-safe)
    safe_prefix = prefix.split()[0] # Autocomplete usually works on the last word, but here implementation takes a prefix.
    # main.c calls autocomplete(trie, arg) where arg is strtok'd. So it only takes one word.
    
    cmd = f"autocomplete {safe_prefix}"
    output = engine.send_command(cmd)
    
    suggestions = []
    # Output: "Suggestion: word"
    for line in output:
        if line.startswith("Suggestion: "):
            suggestions.append(line.split("Suggestion: ")[1].strip())
            
    return suggestions

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)
