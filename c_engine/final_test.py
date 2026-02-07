import subprocess
import time
import os

def run_final_test():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)
    
    if os.path.exists(".temp_index"):
        import shutil
        shutil.rmtree(".temp_index")

    process = subprocess.Popen(
        ['python', 'search_engine.py'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
        universal_newlines=True
    )

    def read_output():
        while True:
            line = process.stdout.readline()
            if not line: break
            print(f"WRAPPER: {line.strip()}")

    import threading
    t = threading.Thread(target=read_output, daemon=True)
    t.start()

    time.sleep(1)
    
    # Test Indexing
    print("--- Indexing multiple types ---")
    files = ["test_manual.txt", "test_utf16.txt", "test_docx.docx", "test_csv.csv"]
    process.stdin.write(f"index {' '.join(files)}\n")
    process.stdin.flush()
    time.sleep(3)
    
    # Test Search Word from TXT
    print("--- Searching 'working' (from TXT) ---")
    process.stdin.write("search working\n")
    process.stdin.flush()
    time.sleep(1)
    
    # Test Search Word from UTF-16
    print("--- Searching 'Banana' (from UTF-16) ---")
    process.stdin.write("search Banana\n")
    process.stdin.flush()
    time.sleep(1)
    
    # Test Search Word from DOCX
    print("--- Searching 'fox' (from DOCX) ---")
    process.stdin.write("search fox\n")
    process.stdin.flush()
    time.sleep(1)
    
    # Test Search Word from CSV
    print("--- Searching 'Carrot' (from CSV) ---")
    process.stdin.write("search Carrot\n")
    process.stdin.flush()
    time.sleep(1)

    # Test Autocomplete
    print("--- Autocomplete 'ba' ---")
    process.stdin.write("autocomplete ba\n")
    process.stdin.flush()
    time.sleep(1)

    process.stdin.write("exit\n")
    process.stdin.flush()
    process.wait()

if __name__ == "__main__":
    run_final_test()
