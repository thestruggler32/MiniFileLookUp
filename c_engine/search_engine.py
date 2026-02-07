import sys
import os
import subprocess
import shutil
import time
import glob
import threading
from extractor import extract_text

# Configuration
ENGINE_EXE = "engine.exe"
TEMP_DIR = ".temp_index"

def cleanup():
    # Only clean on startup to allow debugging, or switch to full clean for prod
    if os.path.exists(TEMP_DIR):
        try:
            shutil.rmtree(TEMP_DIR)
        except Exception:
            pass

def output_reader(proc):
    """
    Reads stdout from the C process byte-by-byte or line-by-line unbuffered
    and prints it to Python's stdout immediately.
    """
    try:
        # Readline on binary pipe matches until \n
        for line in iter(proc.stdout.readline, b''):
            decoded = line.decode('utf-8', errors='replace')
            print(decoded, end='', flush=True)
    except ValueError:
        pass

def main():
    if not os.path.exists(ENGINE_EXE):
        print(f"Error: {ENGINE_EXE} not found. Please compile it first.")
        sys.exit(1)

    cleanup()
    os.makedirs(TEMP_DIR, exist_ok=True)

    print("Starting Mini Search Engine (Python Wrapped)...")
    
    # Start C Engine with Binary Pipes (unbuffered)
    try:
        process = subprocess.Popen(
            [ENGINE_EXE, "interactive"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, # Merge stderr into stdout
            bufsize=0 # Unbuffered
        )
    except Exception as e:
        print(f"Failed to launch {ENGINE_EXE}: {e}")
        return

    # Start reader thread
    t = threading.Thread(target=output_reader, args=(process,), daemon=True)
    t.start()
    
    # Give it a moment to print initial banner
    time.sleep(0.5)

    print("\n[Python] Wrapper ready. Use 'index', 'search', 'autocomplete'.")
    print("[Python] Note: index commands now support .pdf, .docx, .csv via Python extraction.\n")

    try:
        while True:
            try:
                # We don't print a prompt "> " here because the C engine prints it via the reader thread.
                # However, if the C prompt gets hidden, it might look like it's hanging.
                # Since we are essentially proxying, we rely on C output.
                user_input = input()
            except EOFError:
                break
            
            if not user_input.strip():
                continue
                
            parts = user_input.split()
            cmd = parts[0].lower()
            
            if cmd in ["exit", "quit"]:
                process.stdin.write(b"exit\n")
                process.stdin.flush()
                break
                
            elif cmd == "index":
                if len(parts) < 2:
                    print("Usage: index <file1> ...")
                    continue
                
                print("[Python] Extracting text...", flush=True)
                temp_paths = []
                for pattern in parts[1:]:
                    files = glob.glob(pattern)
                    if not files and os.path.exists(pattern):
                         files = [pattern]
                    
                    if not files:
                        print(f"[Python] Warning: {pattern} not found.")

                    for fpath in files:
                        content = extract_text(fpath)
                        if content:
                            # Use forward slashes for C compatibility
                            # Use underscores for spaces to ensure C strtok doesn't split path
                            safe_basename = os.path.basename(fpath).replace(" ", "_")
                            tname = f"{int(time.time()*1000)}_{safe_basename}.txt"
                            tpath = os.path.join(TEMP_DIR, tname).replace("\\", "/")
                            
                            try:
                                with open(tpath, "w", encoding="utf-8") as f:
                                    f.write(content)
                                temp_paths.append(tpath)
                                print(f"[Python] Processed: {fpath}", flush=True)
                            except Exception as e:
                                print(f"[Python] Error writing {tpath}: {e}")
                        else:
                            print(f"[Python] Failed/Empty: {fpath}", flush=True)
                
                if temp_paths:
                    # Construct C command
                    c_cmd = "index " + " ".join(temp_paths) + "\n"
                    process.stdin.write(c_cmd.encode('utf-8'))
                    process.stdin.flush()
                else:
                    print("[Python] No valid text extracted.", flush=True)

            else:
                # Pass through commands like 'search' or 'autocomplete'
                c_cmd = user_input + "\n"
                process.stdin.write(c_cmd.encode('utf-8'))
                process.stdin.flush()
            
            # Allow time for C output to appear
            time.sleep(0.1)

    except KeyboardInterrupt:
        pass
    finally:
        cleanup()
        process.terminate()

if __name__ == "__main__":
    main()
