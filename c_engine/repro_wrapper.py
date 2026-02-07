import subprocess
import time
import os

def run_test():
    # Make sure we are in the right dir
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)
    
    # Remove existing temp dir to be clean
    import shutil
    if os.path.exists(".temp_index"):
        shutil.rmtree(".temp_index")

    process = subprocess.Popen(
        ['python', 'search_engine.py'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
        universal_newlines=True
    )

    def read_output():
        while True:
            line = process.stdout.readline()
            if not line: break
            print(f"PYTHON_WRAPPER: {line.strip()}")

    import threading
    t = threading.Thread(target=read_output, daemon=True)
    t.start()

    time.sleep(2) # Wait for startup
    print("--- Sending index command ---")
    process.stdin.write("index test_utf16.txt\n")
    process.stdin.flush()
    
    time.sleep(2)
    print("--- Sending search command ---")
    process.stdin.write("search Apple\n")
    process.stdin.flush()
    
    time.sleep(2)
    process.stdin.write("exit\n")
    process.stdin.flush()
    process.wait()

if __name__ == "__main__":
    run_test()
