#!/usr/bin/env python3
"""
Isolated C Engine Communication Test
Purpose: Verify engine.exe can index files WITHOUT FastAPI complexity
"""

import os
import subprocess
import sys
import time
import select

# Configuration
ENGINE_EXE = os.path.join(os.path.dirname(__file__), "engine.exe")
TEST_FILE = os.path.join(os.path.dirname(__file__), "test.txt")

def create_test_file():
    """Create a multi-page test file"""
    with open(TEST_FILE, 'w', encoding='utf-8') as f:
        # Page 1
        f.write("[PAGE:1] This is the first page. It has some bits of data.\n")
        # Page 2
        f.write("[PAGE:2] This is the second page. It also has bits of data.\n")
    print(f"✓ Created test file: {TEST_FILE}")

def read_until_prompt(process, timeout=5):
    """Read stdout until we see the '> ' prompt"""
    output_lines = []
    buffer = bytearray()
    start_time = time.time()
    
    while True:
        if time.time() - start_time > timeout:
            print(f"⚠ Timeout waiting for prompt. Buffer so far: {buffer.decode('utf-8', errors='replace')}")
            break
            
        char = process.stdout.read(1)
        if not char:
            print("⚠ EOF reached (process died?)")
            break
            
        buffer.extend(char)
        
        # Check for prompt
        if buffer.endswith(b'> '):
            # Remove prompt from output
            remainder = buffer[:-2].decode('utf-8', errors='replace').strip()
            if remainder:
                output_lines.append(remainder)
            break
        
        # Collect complete lines
        if char == b'\n':
            line = buffer.decode('utf-8', errors='replace').strip()
            if line:
                output_lines.append(line)
            buffer = bytearray()
    
    return output_lines

def test_engine():
    """Main test sequence"""
    print("\n" + "="*60)
    print("STARTING C ENGINE ISOLATED TEST")
    print("="*60 + "\n")
    
    # Step 0: Verify engine exists
    if not os.path.exists(ENGINE_EXE):
        print(f"❌ FATAL: engine.exe not found at {ENGINE_EXE}")
        return False
    print(f"✓ Found engine: {ENGINE_EXE}")
    
    # Step 1: Create test file
    create_test_file()
    
    # Step 2: Convert to absolute path with forward slashes (CRITICAL for Windows)
    abs_path = os.path.abspath(TEST_FILE)
    safe_path = abs_path.replace(os.sep, '/')
    print(f"✓ Safe path for C engine: {safe_path}")
    
    # Step 3: Launch engine
    print(f"\n📡 Launching C engine in interactive mode...")
    try:
        process = subprocess.Popen(
            [ENGINE_EXE, "interactive"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,  # Separate stderr
            bufsize=0,
            cwd=os.path.dirname(__file__)  # Run from c_engine directory
        )
    except Exception as e:
        print(f"❌ FATAL: Failed to launch engine: {e}")
        return False
    
    print("✓ Engine process started")
    
    # Step 4: Consume initial banner
    print("\n📥 Reading initial banner...")
    banner = read_until_prompt(process)
    for line in banner:
        print(f"   {line}")
    
    # Step 5: Send index command
    print(f"\n📤 Sending command: index {safe_path}")
    cmd = f"index {safe_path}\n"
    process.stdin.write(cmd.encode('utf-8'))
    process.stdin.flush()
    
    # Step 6: Read response
    print("📥 Reading response...")
    response = read_until_prompt(process)
    for line in response:
        print(f"   {line}")

    # Check query acceptance
    success_marker = "Indexing File ID"
    if not any(success_marker in line for line in response):
         print(f"❌ STEP 1 FAILED: Expected '{success_marker}' in output")
         process.terminate()
         return False
    print("✅ STEP 1 PASSED: Engine accepted index command")

    # Step 7: Verify Multi-Page Search
    # We expect 'bits' to be found on Page 1 AND Page 2
    print(f"\n📤 Sending search command: sentence \"bits\"")
    process.stdin.write(b"sentence \"bits\"\n") 
    process.stdin.flush()
    
    search_output = read_until_prompt(process)
    print("\n--- Search Output ---")
    hit_count = 0
    pages_found = set()
    
    for line in search_output:
        print(f"   {line}")
        parts = line.split()
        # Format: FileID Page SentenceID Offset Length Freq
        # e.g. 1   1   1   10   20   1
        if len(parts) >= 6 and parts[0].isdigit():
            hit_count += 1
            pages_found.add(parts[1])
            
    print("--- End Search Output ---\n")
    
    success = False
    if hit_count >= 2 and "1" in pages_found and "2" in pages_found:
        print("✅ STEP 2 PASSED: 'bits' found on Page 1 AND Page 2")
        print("\n" + "="*60)
        print("🎉 SUCCESS: MULTI-PAGE SEARCH IS WORKING")
        print("="*60)
        success = True
    else:
        print(f"❌ STEP 2 FAILED: Found {hit_count} hits on pages {pages_found}. Expected hits on Page 1 & 2.")
        success = False
    
    # Cleanup
    print("\n🛑 Shutting down engine...")
    try:
        process.stdin.write(b"exit\n")
        process.stdin.flush()
        process.wait(timeout=2)
    except:
        process.terminate()
    
    return success

if __name__ == "__main__":
    try:
        success = test_engine()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n⚠ Interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ UNEXPECTED ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
