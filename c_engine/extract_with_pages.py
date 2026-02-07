import os
import csv
import logging

try:
    from pypdf import PdfReader
except ImportError:
    PdfReader = None

try:
    from docx import Document
except ImportError:
    Document = None

# Configure logging
logging.basicConfig(level=logging.ERROR, format='%(asctime)s - %(levelname)s - %(message)s')

def extract_text_with_pages(filepath):
    """
    Extracts text from a file and returns it as a UTF-8 string.
    Inserts [PAGE:N] markers for PDF and DOCX files.
    Supports .txt, .pdf, .docx, .csv.
    """
    if not os.path.exists(filepath):
        logging.error(f"File not found: {filepath}")
        return None

    ext = os.path.splitext(filepath)[1].lower()

    try:
        if ext == '.pdf':
            return _extract_pdf_with_pages(filepath)
        elif ext == '.docx':
            return _extract_docx_with_pages(filepath)
        elif ext == '.csv':
            # CSV usually doesn't have "pages", treat as Page 1
            return f"[PAGE:1] {_extract_csv(filepath)}"
        else:
            # Default to text, treat as Page 1
            content = _extract_txt(filepath)
            return f"[PAGE:1] {content}" if content else ""
    except Exception as e:
        logging.error(f"Failed to extract {filepath}: {e}")
        return None

def _extract_txt(filepath):
    # Try common encodings
    encodings = ['utf-8', 'utf-16', 'utf-16le', 'utf-16be', 'latin-1']
    
    for enc in encodings:
        try:
            with open(filepath, 'r', encoding=enc) as f:
                content = f.read()
                # Check for null bytes to avoid binary files
                if content.count('\x00') < (len(content) / 2):
                    # Normalize smart quotes  in extracted content to match api.py normalization
                    content = content.replace("'", "'").replace("'", "'").replace(""", '"').replace(""", '"')
                    return content
        except (UnicodeDecodeError, LookupError):
            continue
            
    # Fallback
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            return f.read().replace('\x00', '')
    except Exception as e:
        logging.error(f"Text extraction failed: {e}")
        return ""

def _extract_pdf_with_pages(filepath):
    if not PdfReader:
        logging.error("pypdf not installed.")
        return ""
    
    chunks = []
    try:
        reader = PdfReader(filepath)
        for i, page in enumerate(reader.pages):
            text = page.extract_text() or ""
            # Insert marker at start of each page
            # Page numbers are 1-based for users
            chunks.append(f"[PAGE:{i+1}] {text}")
    except Exception as e:
        logging.error(f"PDF error: {e}")
    
    return "\n".join(chunks)

def _extract_docx_with_pages(filepath):
    if not Document:
        logging.error("python-docx not installed.")
        return ""
    
    chunks = []
    try:
        doc = Document(filepath)
        current_page = 1
        char_count = 0
        CHARS_PER_PAGE = 3000 # Heuristic fallback
        
        for paragraph in doc.paragraphs:
            # Check for explicitly marked page breaks
            has_break = False
            if paragraph._element.xml:
                if 'lastRenderedPageBreak' in paragraph._element.xml or 'w:br w:type="page"' in paragraph._element.xml:
                    current_page += 1
                    char_count = 0
                    has_break = True
            
            text = paragraph.text.strip()
            if text:
                # Fallback: If no explicit break but we exceeded char limit
                if not has_break and char_count > CHARS_PER_PAGE:
                    current_page += 1
                    char_count = 0
                
                chunks.append(f"[PAGE:{current_page}] {text}")
                char_count += len(text)
                
    except Exception as e:
        logging.error(f"DOCX error: {e}")
        
    return "\n".join(chunks)

def _extract_csv(filepath):
    """
    Structured CSV: [ROW] Key: Value | format for precise search.
    """
    import csv
    text_rows = []
    
    try:
        with open(filepath, 'r', encoding='utf-8-sig', errors='replace') as f:
            reader = csv.DictReader(f)
            for row in reader:
                row_str = "[ROW] " + " | ".join([
                    f"{k}: {v}" for k, v in row.items() if v.strip()
                ]) + " [ENDROW]"
                text_rows.append(row_str)
    except Exception as e:
        logging.error(f"CSV error: {e}")
        try:
            # Fall back to plain text
            with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
                return f.read()
        except:
            return ""
    return "\n".join(text_rows)
