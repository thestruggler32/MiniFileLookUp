import subprocess
import time

def run_test():
    engine = subprocess.Popen(
        ['engine.exe', 'interactive'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,
        universal_newlines=True
    )

    def read_output():
        while True:
            line = engine.stdout.readline()
            if not line: break
            print(f"OUT: {line.strip()}")

    import threading
    t = threading.Thread(target=read_output, daemon=True)
    t.start()

    time.sleep(0.5)
    print("Sending index command...")
    engine.stdin.write("index test_manual.txt\n")
    engine.stdin.flush()
    
    time.sleep(0.5)
    print("Sending search command...")
    engine.stdin.write("search Hello\n")
    engine.stdin.flush()
    
    time.sleep(1)
    engine.stdin.write("exit\n")
    engine.stdin.flush()
    engine.wait()

if __name__ == "__main__":
    run_test()
