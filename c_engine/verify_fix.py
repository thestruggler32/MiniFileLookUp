import re
import os
import sys

# Mock SearchHit
class SearchHit:
    def __init__(self, file_id, filename, page_number, sentence_id, sentence_text, frequency):
        self.file_id = file_id
        self.filename = filename
        self.page_number = page_number
        self.sentence_id = sentence_id
        self.sentence_text = sentence_text
        self.frequency = frequency

# Mock Logger that prints
class Logger:
    def error(self, msg): print(f"ERROR: {msg}")
    def info(self, msg): print(f"INFO: {msg}")
logger = Logger()

# THE FUNCTION UNDER TEST (Copied from api.py)
def python_phrase_search(query: str, indexed_files):
    results = []
    query_lower = query.lower()
    
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
                             snippet = p_text[row_start:row_end+8] # Include [ENDROW]
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
            
    return results

# TEST RUNNER
def run_test():
    # 1. Create dummy file simulating PDF extraction
    # The newline is between "known" and "as" in "known as"
    test_content = """[PAGE:1]
    Boolean Algebra
    George Boole in 1854 invented a new kind of algebra known 
    as Boolean algebra. It is sometimes called switching algebra.
    """
    
    test_file = "test_pdf_structure.txt"
    with open(test_file, "w", encoding="utf-8") as f:
        f.write(test_content)
        
    print(f"Created {test_file} with split phrase 'known \\n as'")
    
    # 2. Run Search (Exact query from user)
    query = "George Boole in 1854 invented a new kind of algebra known as Boolean algebra"
    files = {1: test_file}
    
    print(f"Searching for full sentence: '{query}'")
    hits = python_phrase_search(query, files)
    
    # 3. Verify
    if len(hits) > 0:
        print("\n✅ SUCCESS: Found full sentence match despite newline!")
        print(f"Snippet: {hits[0].sentence_text}")
    else:
        print("\n❌ FAILURE: No match found.")

    # Cleanup
    if os.path.exists(test_file):
        os.remove(test_file)

if __name__ == "__main__":
    run_test()
