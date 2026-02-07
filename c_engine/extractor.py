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

def extract_text(filepath):
    """
    Extracts text from a file and returns it as a UTF-8 string.
    Supports .txt, .pdf, .docx, .csv.
    """
    if not os.path.exists(filepath):
        logging.error(f"File not found: {filepath}")
        return None

    ext = os.path.splitext(filepath)[1].lower()

    try:
        if ext == '.pdf':
            return _extract_pdf(filepath)
        elif ext == '.docx':
            return _extract_docx(filepath)
        elif ext == '.csv':
            return _extract_csv(filepath)
        else:
            # Default to text, attempting UTF-8 then fallback
            return _extract_txt(filepath)
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
                # If we successfully read it, check if it looks like garbage (lots of nulls)
                # UTF-16 read as latin-1 often has interleaved nulls
                if content.count('\x00') < (len(content) / 2):
                    return content
        except (UnicodeDecodeError, LookupError):
            continue
            
    # Fallback to ignore errors if all failed
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            # Still remove null bytes to avoid C engine rejection
            return content.replace('\x00', '')
    except Exception as e:
        logging.error(f"Text extraction failed: {e}")
        return ""

def _extract_pdf(filepath):
    if not PdfReader:
        logging.error("pypdf not installed.")
        return ""
    
    text = []
    try:
        reader = PdfReader(filepath)
        for page in reader.pages:
            text.append(page.extract_text() or "")
    except Exception as e:
        logging.error(f"PDF error: {e}")
    return "\n".join(text)

def _extract_docx(filepath):
    if not Document:
        logging.error("python-docx not installed.")
        return ""
    
    text = []
    try:
        doc = Document(filepath)
        for para in doc.paragraphs:
            text.append(para.text)
    except Exception as e:
        logging.error(f"DOCX error: {e}")
    return "\n".join(text)

def _extract_csv(filepath):
    text = []
    try:
        # Attempt to read as text first to avoid encoding issues
        with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
            reader = csv.reader(f)
            for row in reader:
                text.append(" ".join(row))
    except Exception as e:
        logging.error(f"CSV error: {e}")
    return "\n".join(text)
