#!/usr/bin/env python3
"""
Report Generator — assembles all chapter modules into a single .docx file.

Usage:
    python3 docx/generate_report.py

Output:
    report.docx in the project root directory.
"""

import os
import sys
import importlib.util

from docx import Document
from docx.shared import Pt, Cm, Inches, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.section import WD_ORIENT
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement


# ─────────────────────────────────────────────
# Paths
# ─────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_PATH = os.path.join(PROJECT_ROOT, 'report.docx')


# ─────────────────────────────────────────────
# Helper: load a chapter module by filename
# ─────────────────────────────────────────────
def load_chapter(filename):
    """Dynamically import a chapter module from the docx/ directory."""
    path = os.path.join(SCRIPT_DIR, filename)
    spec = importlib.util.spec_from_file_location(filename.replace('.py', ''), path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ─────────────────────────────────────────────
# Style setup
# ─────────────────────────────────────────────
def setup_styles(doc):
    """Create and configure all document styles. Returns style name dict."""
    styles = doc.styles

    # ── Section/page setup ──
    for section in doc.sections:
        section.top_margin = Cm(2)
        section.bottom_margin = Cm(2)
        section.left_margin = Cm(3)
        section.right_margin = Cm(1.5)

    # ── Page numbers (center footer) ──
    for section in doc.sections:
        footer = section.footer
        footer.is_linked_to_previous = False
        fp = footer.paragraphs[0] if footer.paragraphs else footer.add_paragraph()
        fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
        # Add PAGE field
        run = fp.add_run()
        fld_char_begin = OxmlElement('w:fldChar')
        fld_char_begin.set(qn('w:fldCharType'), 'begin')
        run._r.append(fld_char_begin)

        run2 = fp.add_run()
        instr = OxmlElement('w:instrText')
        instr.set(qn('xml:space'), 'preserve')
        instr.text = ' PAGE '
        run2._r.append(instr)

        run3 = fp.add_run()
        fld_char_end = OxmlElement('w:fldChar')
        fld_char_end.set(qn('w:fldCharType'), 'end')
        run3._r.append(fld_char_end)

        run.font.name = 'Times New Roman'
        run.font.size = Pt(12)

    # ── Default paragraph font ──
    style_normal = styles['Normal']
    style_normal.font.name = 'Times New Roman'
    style_normal.font.size = Pt(12)
    style_normal.paragraph_format.line_spacing = 1.5
    style_normal.paragraph_format.space_after = Pt(0)
    style_normal.paragraph_format.space_before = Pt(0)

    # ── Heading style (chapter titles) ──
    if 'ReportHeading' not in [s.name for s in styles]:
        heading = styles.add_style('ReportHeading', 1)  # 1 = paragraph
    else:
        heading = styles['ReportHeading']
    heading.font.name = 'Times New Roman'
    heading.font.size = Pt(16)
    heading.font.bold = True
    heading.font.color.rgb = RGBColor(0, 0, 0)
    heading.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    heading.paragraph_format.space_before = Pt(12)
    heading.paragraph_format.space_after = Pt(12)
    heading.paragraph_format.line_spacing = 1.5
    heading.paragraph_format.page_break_before = True

    # ── Subheading style (section titles) ──
    if 'ReportSubheading' not in [s.name for s in styles]:
        subheading = styles.add_style('ReportSubheading', 1)
    else:
        subheading = styles['ReportSubheading']
    subheading.font.name = 'Times New Roman'
    subheading.font.size = Pt(14)
    subheading.font.bold = True
    subheading.font.color.rgb = RGBColor(0, 0, 0)
    subheading.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT
    subheading.paragraph_format.space_before = Pt(12)
    subheading.paragraph_format.space_after = Pt(6)
    subheading.paragraph_format.line_spacing = 1.5
    subheading.paragraph_format.first_line_indent = Cm(1.25)

    # ── Body text style ──
    if 'ReportBody' not in [s.name for s in styles]:
        body = styles.add_style('ReportBody', 1)
    else:
        body = styles['ReportBody']
    body.font.name = 'Times New Roman'
    body.font.size = Pt(14)
    body.font.color.rgb = RGBColor(0, 0, 0)
    body.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    body.paragraph_format.first_line_indent = Cm(1.25)
    body.paragraph_format.space_before = Pt(0)
    body.paragraph_format.space_after = Pt(6)
    body.paragraph_format.line_spacing = 1.5

    # ── Code style (monospace) ──
    if 'ReportCode' not in [s.name for s in styles]:
        code = styles.add_style('ReportCode', 1)
    else:
        code = styles['ReportCode']
    code.font.name = 'Courier New'
    code.font.size = Pt(9)
    code.font.color.rgb = RGBColor(0, 0, 0)
    code.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT
    code.paragraph_format.first_line_indent = Cm(0)
    code.paragraph_format.space_before = Pt(3)
    code.paragraph_format.space_after = Pt(3)
    code.paragraph_format.line_spacing = 1.0

    # ── Caption style (figures, tables, listings) ──
    if 'ReportCaption' not in [s.name for s in styles]:
        caption = styles.add_style('ReportCaption', 1)
    else:
        caption = styles['ReportCaption']
    caption.font.name = 'Times New Roman'
    caption.font.size = Pt(12)
    caption.font.italic = True
    caption.font.color.rgb = RGBColor(0, 0, 0)
    caption.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    caption.paragraph_format.space_before = Pt(6)
    caption.paragraph_format.space_after = Pt(6)
    caption.paragraph_format.line_spacing = 1.5

    # ── Table style name (built-in) ──
    table_style_name = 'Table Grid'

    return {
        'heading': 'ReportHeading',
        'subheading': 'ReportSubheading',
        'body': 'ReportBody',
        'code': 'ReportCode',
        'caption': 'ReportCaption',
        'table': table_style_name,
    }


def style_all_tables(doc):
    """Post-process: apply consistent font to all table cells."""
    for table in doc.tables:
        table.alignment = WD_TABLE_ALIGNMENT.CENTER
        for row in table.rows:
            for cell in row.cells:
                for paragraph in cell.paragraphs:
                    paragraph.paragraph_format.space_before = Pt(2)
                    paragraph.paragraph_format.space_after = Pt(2)
                    paragraph.paragraph_format.line_spacing = 1.0
                    for run in paragraph.runs:
                        run.font.name = 'Times New Roman'
                        run.font.size = Pt(10)
                    if not paragraph.runs:
                        # Style text added via cell.text
                        run = paragraph.add_run(paragraph.text)
                        paragraph.clear()
                        paragraph.add_run(run.text)
                        for r in paragraph.runs:
                            r.font.name = 'Times New Roman'
                            r.font.size = Pt(10)
        # Bold first row (header)
        if table.rows:
            for cell in table.rows[0].cells:
                for paragraph in cell.paragraphs:
                    for run in paragraph.runs:
                        run.bold = True


def fix_code_font(doc):
    """Post-process: ensure all ReportCode paragraphs have Courier New applied to runs."""
    for paragraph in doc.paragraphs:
        if paragraph.style and paragraph.style.name == 'ReportCode':
            if paragraph.text and not paragraph.runs:
                text = paragraph.text
                paragraph.clear()
                run = paragraph.add_run(text)
                run.font.name = 'Courier New'
                run.font.size = Pt(9)
            else:
                for run in paragraph.runs:
                    run.font.name = 'Courier New'
                    run.font.size = Pt(9)


# ─────────────────────────────────────────────
# Main assembly
# ─────────────────────────────────────────────
def main():
    print('[*] Creating document...')
    doc = Document()
    snames = setup_styles(doc)

    # Ordered list of chapter files in document order
    chapter_files = [
        'chapter9_title_page.py',     # 1. Title page
        'chapter10_toc.py',           # 2. Table of contents
        'chapter11_introduction.py',  # 3. Introduction
        'chapter1_theory.py',         # 4. Chapter 1
        'chapter2_architecture.py',   # 5. Chapter 2
        'chapter3_implementation.py', # 6. Chapter 3
        'chapter4_testing.py',        # 7. Chapter 4
        'chapter5_improvements.py',   # 8. Chapter 5
        'chapter6_applications.py',   # 9. Chapter 6
        'chapter7_conclusion.py',     # 10. Conclusion
        'chapter8_references.py',     # 11. References
        'chapter12_appendices.py',    # 12. Appendices
    ]

    for i, filename in enumerate(chapter_files, 1):
        print(f'  [{i:2d}/12] Adding {filename}...')
        mod = load_chapter(filename)
        mod.add_chapter(
            doc,
            heading_style=snames['heading'],
            subheading_style=snames['subheading'],
            body_style=snames['body'],
            code_style=snames['code'],
            caption_style=snames['caption'],
            table_style=snames['table'],
        )

    print('[*] Post-processing tables...')
    style_all_tables(doc)

    print('[*] Post-processing code blocks...')
    fix_code_font(doc)

    # Remove the very first empty paragraph that Document() creates
    if doc.paragraphs and doc.paragraphs[0].text == '' and \
       (not doc.paragraphs[0].style or doc.paragraphs[0].style.name == 'Normal'):
        p_element = doc.paragraphs[0]._element
        p_element.getparent().remove(p_element)

    # Disable page break before for the title page heading
    for p in doc.paragraphs:
        if p.style and p.style.name == 'ReportHeading':
            # Only the very first heading should NOT have page break
            p.paragraph_format.page_break_before = False
            break

    print(f'[*] Saving to {OUTPUT_PATH}...')
    doc.save(OUTPUT_PATH)
    print(f'[+] Done! Report saved: {OUTPUT_PATH}')
    print(f'    Total paragraphs: {len(doc.paragraphs)}')
    print(f'    Total tables: {len(doc.tables)}')


if __name__ == '__main__':
    main()
