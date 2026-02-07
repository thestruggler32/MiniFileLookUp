import re

# Simulate the CSV content
csv_content = "[PAGE:1] [ROW] ID: 1 | Name: Apple | Description: A red fruit that is sweet. [ENDROW]\n[ROW] ID: 2 | Name: Banana | Description: A yellow fruit that is long A yellow fruit. [ENDROW]\n[ROW] ID: 3 | Name: Carrot | Description: An orange vegetable. [ENDROW]"

# Simulate search for "carrot"
query = "carrot"
safe_query = re.escape(query)
pattern_str = safe_query.replace(r'\ ', r'\s+')
search_pattern = re.compile(pattern_str, re.IGNORECASE | re.DOTALL)

print(f"Searching for: '{query}'")
print(f"Pattern: {pattern_str}\n")

# Split by PAGE
parts = re.split(r'\[PAGE:(\d+)\]', csv_content)
if len(parts) < 2:
    pages_to_search = [(1, csv_content)]
else:
    pages_to_search = []
    for i in range(1, len(parts), 2):
        p_num = int(parts[i])
        p_text = parts[i+1] if (i+1) < len(parts) else ""
        pages_to_search.append((p_num, p_text))

for p_num, p_text in pages_to_search:
    print(f"Page {p_num} content:")
    print(repr(p_text[:200]))
    print()
    
    for match in search_pattern.finditer(p_text):
        start_idx = match.start()
        end_idx = match.end()
        
        print(f"Match found at index {start_idx}-{end_idx}")
        print(f"Matched text: '{p_text[start_idx:end_idx]}'")
        
        # CSV Special Handling
        if "[row]" in p_text.lower():
            row_start = p_text.lower().rfind("[row]", 0, start_idx)
            row_end = p_text.lower().find("[endrow]", start_idx)
            
            print(f"Row markers: row_start={row_start}, row_end={row_end}")
            
            if row_start != -1 and row_end != -1:
                # Extract row content WITHOUT the markers
                snippet = p_text[row_start+5:row_end].strip()  # +5 to skip "[ROW]"
                print(f"Extracted snippet (without markers): '{snippet}'")
            else:
                print("Row markers not found properly")
