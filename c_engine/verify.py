import requests
import time
import os

BASE_URL = "http://127.0.0.1:8000"

def test_health():
    print("Testing /health...")
    try:
        resp = requests.get(f"{BASE_URL}/health")
        if resp.status_code == 200 and resp.json()["status"] == "ok":
            print("PASS: Health check")
            return True
        else:
            print(f"FAIL: Health check returned {resp.status_code} {resp.text}")
            return False
    except Exception as e:
        print(f"FAIL: Health check exception: {e}")
        return False

def test_index_and_search():
    print("\nTesting /index and /search...")
    
    # Create a dummy file
    filename = "test_api_doc.txt"
    content = "The quick brown fox jumps over the lazy dog. Programming in C is fun."
    with open(filename, "w", encoding="utf-8") as f:
        f.write(content)
        
    try:
        # Index
        files = {'files': (filename, open(filename, 'rb'), 'text/plain')}
        resp = requests.post(f"{BASE_URL}/index", files=files)
        
        if resp.status_code == 200:
            count = resp.json().get("indexed_files", 0)
            print(f"Index response: {resp.json()}")
            if count == 1:
                print("PASS: Indexing success")
            else:
                print("FAIL: Indexed count is not 1")
        else:
            print(f"FAIL: Index error {resp.status_code} {resp.text}")
            return

        # Wait a moment
        time.sleep(0.5)

        # Search sentence
        query = "brown fox"
        resp = requests.get(f"{BASE_URL}/search", params={"query": query})
        print(f"Search '{query}' response: {resp.json()}")
        results = resp.json()
        if len(results) > 0 and results[0]['file_id'] > 0:
            print("PASS: Search returned results")
        else:
            print("FAIL: Search returned no results")

        # Autocomplete
        prefix = "pro"
        resp = requests.get(f"{BASE_URL}/autocomplete", params={"prefix": prefix})
        print(f"Autocomplete '{prefix}' response: {resp.json()}")
        suggestions = resp.json()
        if "programming" in [s.lower() for s in suggestions]:
            print("PASS: Autocomplete found 'programming'")
        else:
            print("FAIL: Autocomplete did not find 'programming'")

    except Exception as e:
        print(f"FAIL: Exception during test: {e}")
    finally:
        if os.path.exists(filename):
            os.remove(filename)

if __name__ == "__main__":
    # Wait for server to be ready
    for i in range(10):
        if test_health():
            break
        time.sleep(1)
    
    test_index_and_search()
