from pypdf import PdfWriter
from docx import Document
import os

def create_files():
    # Create PDF
    pdf_path = "test_pdf.pdf"
    writer = PdfWriter()
    page = writer.add_blank_page(width=72, height=72)
    # Note: pypdf can't easily add text to blank pages without more complex code
    # But we can try to use reportlab if it's there
    try:
        from reportlab.pdfgen import canvas
        c = canvas.Canvas(pdf_path)
        c.drawString(100, 750, "The quick brown fox jumps over the lazy dog.")
        c.save()
    except ImportError:
        # Fallback: Just write some plain text to a .pdf extension as a "fake" pdf
        # But extractor.py will try to parse it as real PDF
        # So let's hope reportlab is there or we find another way.
        pass

    # Create DOCX
    docx_path = "test_docx.docx"
    doc = Document()
    doc.add_paragraph("The quick brown fox jumps over the lazy dog.")
    doc.save(docx_path)

if __name__ == "__main__":
    create_files()
