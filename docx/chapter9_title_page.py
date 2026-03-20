"""
Титульный лист
Adds title page to the document.
"""

from docx.shared import Pt, Cm
from docx.enum.text import WD_ALIGN_PARAGRAPH


def add_chapter(doc, heading_style, subheading_style, body_style, code_style, caption_style, table_style):
    """Add title page."""

    # Ministry
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('МИНИСТЕРСТВО НАУКИ И ВЫСШЕГО ОБРАЗОВАНИЯ\nРОССИЙСКОЙ ФЕДЕРАЦИИ')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(14)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('Федеральное государственное бюджетное образовательное учреждение\n'
                     'высшего образования')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(14)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('«УНИВЕРСИТЕТ»')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(14)
    run.bold = True

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('Факультет информационных технологий\n'
                     'Кафедра информационной безопасности')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(14)

    # Spacing
    for _ in range(3):
        doc.add_paragraph()

    # Title
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('КУРСОВАЯ РАБОТА')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(16)
    run.bold = True

    doc.add_paragraph()

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('на тему:')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(14)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('«Разработка системы безопасного удалённого доступа\n'
                     'к контейнерным средам с аппаратным хранением\n'
                     'криптографических ключей на микроконтроллере ESP32»')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(16)
    run.bold = True

    # Spacing
    for _ in range(4):
        doc.add_paragraph()

    # Author / supervisor — right-aligned block
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    run = p.add_run('Выполнил: студент группы ИБ-21\n'
                     'Иванов И.И.\n\n'
                     'Научный руководитель:\n'
                     'к.т.н., доцент Петров П.П.')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(14)

    # Spacing
    for _ in range(4):
        doc.add_paragraph()

    # City and year
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run('Москва — 2026')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(14)

    # Page break after title
    doc.add_page_break()
