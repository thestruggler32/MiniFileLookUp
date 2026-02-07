# FastAPI Wrapper for Mini Search Engine

This API wraps the C-based Search Engine using a persistent subprocess.

## Prerequisities
- Python 3.8+
- GCC (to compile `engine.exe`)
- `pip install fastapi uvicorn python-multipart pypdf python-docx`

## Setup
1. Compile the C engine:
   ```bash
   cd c_engine/src
   gcc main.c trie.c index.c -o ../engine.exe
   ```
2. Run the API:
   ```bash
   cd c_engine
   python api.py
   ```
   Server runs at `http://127.0.0.1:8000`.

## Endpoints

### `GET /health`
Returns `{"status": "ok"}` if the C engine is running.

### `POST /index`
Uploads files to be indexed.
- **Form Data**: `files` (List of files)
- **Supported types**: .txt, .pdf, .csv, .docx
- **Returns**: `{"indexed_files": <count>}`

### `GET /search`
Performs a sentence search.
- **Query Param**: `query` (string)
- **Returns**: JSON list of matches.
  ```json
  [
    { "file_id": 1, "sentence_id": 3, "frequency": 2 }
  ]
  ```

### `GET /autocomplete`
Provides autocomplete suggestions.
- **Query Param**: `prefix` (string)
- **Returns**: List of strings.
  ```json
  ["prefix", "prefixfoo", ...]
  ```

## Internal Details
- The API maintains a single persistent `subprocess.Popen` connection to `engine.exe interactive`.
- It saves uploaded files to `.temp_uploads`, extracts text to `.temp_index`, and sends commands to the C engine.
- A lock (`threading.Lock`) ensures thread safety for the C engine's stdin/stdout.
