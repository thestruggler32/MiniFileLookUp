# Mini Search Engine - Hybrid Python/C Architecture

This project implements a high-performance search engine using a **Python Extraction Layer** for document processing and a **C Core** for indexing and searching.

## Architecture

### 1. Python Extraction Layer (`search_engine.py`, `extractor.py`)
- **Responsibility**: Gateway, File I/O, and Text Extraction.
- **Workflow**:
    1. Intercepts the `index` command.
    2. Detects file types (.pdf, .docx, .csv, .txt).
    3. Handles complex encodings (UTF-8, UTF-16, Latin-1, etc.).
    4. Extracts plain text and writes it to normalized UTF-8 temporary files in `.temp_index/`.
    5. Pipes the normalized file paths to the C Engine.
- **Why Python?**: Excellent library support for opaque binary formats (PDF/DOCX) and robust encoding detection which is tedious in pure C.

### 2. C Indexing & Search Core (`engine.exe`)
- **Responsibility**: Performance-critical data structures and algorithms.
- **Data Structures**:
    - **Trie**: Used for O(L) prefix-based autocomplete, where L is word length.
    - **Inverted Index**: Custom Hash Table with chaining to map words to `(file_id, sentence_id, frequency)`.
- **Workflow**:
    1. Runs as a persistent background process in `interactive` mode.
    2. Receives normalized text files from Python.
    3. Tokenizes text by sentences and words.
    4. Builds and maintains the index in memory for instant queries.
- **Why C?**: Maximum memory efficiency and lightning-fast search/autocomplete performance.

---

## Getting Started

### Prerequisites
- Python 3.x
- GCC (MinGW for Windows)
- Python Libraries: `pip install pypdf python-docx`

### Compilation
Compile the C Engine if you haven't already:
```bash
gcc -Iinclude -o engine.exe src/main.c src/trie.c src/index.c
```

### Running the Engine
Always start the engine via the Python wrapper for full file support:
```bash
python search_engine.py
```

### Commands
- `index <file1> <file2> ...` : Supports `.txt`, `.pdf`, `.docx`, `.csv`.
- `search <keyword>` : Search for a word across indexed documents.
- `autocomplete <prefix>` : Get suggestions for a word prefix.
- `exit` : Shuts down the engine and cleans up temporary files.

## Technical Improvements Added
- **Encoding Robustness**: The Python layer now automatically detects and converts UTF-16 (Little/Big Endian) and other encodings to UTF-8 before passing to C.
- **Real-time I/O**: The C Engine uses `fflush(stdout)` and Python uses unbuffered binary pipes to ensure text appears instantly in the terminal.
- **Safety**: Spaces in filenames are handled by the Python wrapper using path normalization, ensuring compatibility with C's `strtok` tokenizer.
