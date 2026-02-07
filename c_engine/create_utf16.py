text = "This is a UTF-16 encoded file. It has some keywords like Apple and Banana."
with open("test_utf16.txt", "w", encoding="utf-16") as f:
    f.write(text)
print("Created test_utf16.txt")
