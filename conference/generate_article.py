#!/usr/bin/env python3
"""
CTDA 2024 Conference Article Generator.

Generates a ~4-page article (with space for 2 diagrams) titled:
"Разработка защищённой системы удалённой виртуализации
с интеллектуальным анализом поведения пользователей"

Usage:
    python3 conference/generate_article.py

Output:
    article.docx in the project root directory.
"""

import os
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_PATH = os.path.join(PROJECT_ROOT, 'article.docx')

# Paths to diagram images (place PNG files here before generating)
ARCH_DIAGRAM = os.path.join(PROJECT_ROOT, 'conference', 'diagram_arch.png')
AI_DIAGRAM = os.path.join(PROJECT_ROOT, 'conference', 'diagram_ai.png')


def setup_styles(doc):
    """Create CTDA 2024 styles."""
    styles = doc.styles

    # Page setup
    for section in doc.sections:
        section.top_margin = Cm(2.5)
        section.bottom_margin = Cm(2.5)
        section.left_margin = Cm(2.5)
        section.right_margin = Cm(2.5)

    # ── Название статьи CTDA ──
    if 'Название статьи CTDA' not in [s.name for s in styles]:
        s = styles.add_style('Название статьи CTDA', 1)
    else:
        s = styles['Название статьи CTDA']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(14)
    s.font.bold = True
    s.font.all_caps = True
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    s.paragraph_format.space_before = Pt(0)
    s.paragraph_format.space_after = Pt(12)
    s.paragraph_format.line_spacing = 1.0

    # ── Автор ──
    if 'Автор' not in [s.name for s in styles]:
        s = styles.add_style('Автор', 1)
    else:
        s = styles['Автор']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(14)
    s.font.bold = True
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    s.paragraph_format.space_before = Pt(0)
    s.paragraph_format.space_after = Pt(0)
    s.paragraph_format.line_spacing = 1.0

    # ── Аффилиация ──
    if 'Аффилиация' not in [s.name for s in styles]:
        s = styles.add_style('Аффилиация', 1)
    else:
        s = styles['Аффилиация']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(12)
    s.font.italic = True
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    s.paragraph_format.space_before = Pt(0)
    s.paragraph_format.space_after = Pt(12)
    s.paragraph_format.line_spacing = 1.0

    # ── Аннотация ──
    if 'Аннотация' not in [s.name for s in styles]:
        s = styles.add_style('Аннотация', 1)
    else:
        s = styles['Аннотация']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(12)
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    s.paragraph_format.space_before = Pt(0)
    s.paragraph_format.space_after = Pt(6)
    s.paragraph_format.line_spacing = 1.0
    s.paragraph_format.first_line_indent = Cm(1.0)

    # ── Ключевые слова ──
    if 'Ключевые слова' not in [s.name for s in styles]:
        s = styles.add_style('Ключевые слова', 1)
    else:
        s = styles['Ключевые слова']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(12)
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    s.paragraph_format.space_before = Pt(0)
    s.paragraph_format.space_after = Pt(18)
    s.paragraph_format.line_spacing = 1.0
    s.paragraph_format.first_line_indent = Cm(1.0)

    # ── Подзаголовок ──
    if 'Подзаголовок' not in [s.name for s in styles]:
        s = styles.add_style('Подзаголовок', 1)
    else:
        s = styles['Подзаголовок']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(14)
    s.font.bold = True
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    s.paragraph_format.space_before = Pt(12)
    s.paragraph_format.space_after = Pt(6)
    s.paragraph_format.line_spacing = 1.0

    # ── Обычный (body text) ──
    normal = styles['Normal']
    normal.font.name = 'Times New Roman'
    normal.font.size = Pt(14)
    normal.font.color.rgb = RGBColor(0, 0, 0)
    normal.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    normal.paragraph_format.first_line_indent = Cm(1.0)
    normal.paragraph_format.space_before = Pt(0)
    normal.paragraph_format.space_after = Pt(0)
    normal.paragraph_format.line_spacing = 1.0

    # ── Подпись к рисунку ──
    if 'Подпись рисунка' not in [s.name for s in styles]:
        s = styles.add_style('Подпись рисунка', 1)
    else:
        s = styles['Подпись рисунка']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(12)
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    s.paragraph_format.space_before = Pt(6)
    s.paragraph_format.space_after = Pt(12)
    s.paragraph_format.line_spacing = 1.0
    s.paragraph_format.first_line_indent = Cm(0)

    # ── БИБЛИОГРАФИЧЕСКИЕ ССЫЛКИ ──
    if 'БИБЛИОГРАФИЧЕСКИЕ ССЫЛКИ' not in [s.name for s in styles]:
        s = styles.add_style('БИБЛИОГРАФИЧЕСКИЕ ССЫЛКИ', 1)
    else:
        s = styles['БИБЛИОГРАФИЧЕСКИЕ ССЫЛКИ']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(14)
    s.font.bold = True
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    s.paragraph_format.space_before = Pt(12)
    s.paragraph_format.space_after = Pt(6)
    s.paragraph_format.line_spacing = 1.0

    # ── Список литературы ──
    if 'Список литературы' not in [s.name for s in styles]:
        s = styles.add_style('Список литературы', 1)
    else:
        s = styles['Список литературы']
    s.font.name = 'Times New Roman'
    s.font.size = Pt(12)
    s.font.color.rgb = RGBColor(0, 0, 0)
    s.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    s.paragraph_format.space_before = Pt(0)
    s.paragraph_format.space_after = Pt(2)
    s.paragraph_format.line_spacing = 1.0
    s.paragraph_format.first_line_indent = Cm(0)
    s.paragraph_format.left_indent = Cm(0.5)


def add_keywords(doc, text):
    """Add keywords paragraph with 'Ключевые слова:' in italic."""
    p = doc.add_paragraph(style='Ключевые слова')
    run_label = p.add_run('Ключевые слова: ')
    run_label.italic = True
    run_label.font.name = 'Times New Roman'
    run_label.font.size = Pt(12)
    run_text = p.add_run(text)
    run_text.font.name = 'Times New Roman'
    run_text.font.size = Pt(12)


def add_reference(doc, number, text, authors_italic=None):
    """Add a bibliography entry."""
    p = doc.add_paragraph(style='Список литературы')
    prefix = f'{number}. '
    if authors_italic:
        run_num = p.add_run(prefix)
        run_num.font.name = 'Times New Roman'
        run_num.font.size = Pt(12)
        run_auth = p.add_run(authors_italic)
        run_auth.italic = True
        run_auth.font.name = 'Times New Roman'
        run_auth.font.size = Pt(12)
        rest = text[len(authors_italic):]
        run_rest = p.add_run(rest)
        run_rest.font.name = 'Times New Roman'
        run_rest.font.size = Pt(12)
    else:
        run = p.add_run(prefix + text)
        run.font.name = 'Times New Roman'
        run.font.size = Pt(12)


def body(doc, text):
    """Add a body text paragraph."""
    return doc.add_paragraph(text, style='Normal')


def add_figure(doc, image_path, caption, width_cm=14):
    """Add figure with image (if exists) or placeholder, plus caption."""
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(6)
    p.paragraph_format.space_after = Pt(0)
    p.paragraph_format.first_line_indent = Cm(0)
    if os.path.exists(image_path):
        run = p.add_run()
        run.add_picture(image_path, width=Cm(width_cm))
    else:
        # Placeholder text for missing image
        run = p.add_run(f'[ Вставьте изображение: {os.path.basename(image_path)} ]')
        run.font.name = 'Times New Roman'
        run.font.size = Pt(11)
        run.font.color.rgb = RGBColor(150, 150, 150)
        run.italic = True
    doc.add_paragraph(caption, style='Подпись рисунка')


def main():
    print('[*] Generating CTDA 2024 article...')
    doc = Document()
    setup_styles(doc)

    # Remove initial empty paragraph
    if doc.paragraphs and doc.paragraphs[0].text == '':
        p_el = doc.paragraphs[0]._element
        p_el.getparent().remove(p_el)

    # ════════════════════════════════════════════
    # TITLE + HEADER
    # ════════════════════════════════════════════
    doc.add_paragraph(
        'РАЗРАБОТКА ЗАЩИЩЁННОЙ СИСТЕМЫ УДАЛЁННОЙ ВИРТУАЛИЗАЦИИ '
        'С ИНТЕЛЛЕКТУАЛЬНЫМ АНАЛИЗОМ ПОВЕДЕНИЯ ПОЛЬЗОВАТЕЛЕЙ',
        style='Название статьи CTDA'
    )

    doc.add_paragraph('И. И. Иванов', style='Автор')

    p = doc.add_paragraph(style='Аффилиация')
    run = p.add_run(
        'Белорусский государственный университет, пр. Независимости, 4,\n'
        '220030, г. Минск, Беларусь, ivanov@bsu.by'
    )
    run.font.name = 'Times New Roman'
    run.font.size = Pt(12)
    run.italic = True

    p = doc.add_paragraph(style='Аффилиация')
    run = p.add_run('Научный руководитель — П. П. Петров, старший преподаватель')
    run.font.name = 'Times New Roman'
    run.font.size = Pt(12)
    run.italic = True

    # ════════════════════════════════════════════
    # ABSTRACT + KEYWORDS
    # ════════════════════════════════════════════
    doc.add_paragraph(
        'Представлена система безопасного удалённого доступа к контейнерным средам '
        'с аппаратным хранением ключей Ed25519 на микроконтроллере ESP32-C3 '
        'и двухуровневым анализом поведения пользователей. Первый уровень выполняет '
        'статистическое обнаружение аномалий в сессионных паттернах (z-score), '
        'второй — анализ истории команд локальной языковой моделью Qwen2.5 '
        'с пятиуровневой защитой от инъекций в промпт.',
        style='Аннотация'
    )

    add_keywords(
        doc,
        'аппаратное хранение ключей; ESP32; Ed25519; SSH-агент; '
        'контейнерная виртуализация; LXD; LUKS; '
        'обнаружение аномалий; LLM; Ollama.'
    )

    # ════════════════════════════════════════════
    # INTRODUCTION
    # ════════════════════════════════════════════
    body(doc,
        'Обеспечение безопасного удалённого доступа к серверным средам является '
        'одной из ключевых задач информационной безопасности. Традиционные подходы '
        'предполагают хранение криптографических ключей на файловой системе рабочей '
        'станции, что создаёт риск их компрометации [1]. Коммерческие аппаратные '
        'решения (YubiKey, Nitrokey) решают эту проблему, однако их стоимость '
        'и закрытость кода ограничивают применимость в образовательном контексте. '
        'Кроме того, после успешной аутентификации действия пользователя внутри '
        'изолированной среды остаются без мониторинга.')

    body(doc,
        'Целью работы является разработка системы, объединяющей аппаратное хранение '
        'ключей на ESP32-C3, управление контейнерными средами LXD через SSH [2] '
        'и двухуровневый анализ поведения пользователей. Приватный ключ никогда '
        'не покидает микроконтроллер, а подсистема мониторинга автоматически '
        'выявляет аномалии и позволяет проводить углублённый анализ командной '
        'истории с помощью локальной языковой модели.')

    # ════════════════════════════════════════════
    # ARCHITECTURE
    # ════════════════════════════════════════════
    doc.add_paragraph('Архитектура системы', style='Подзаголовок')

    body(doc,
        'Система состоит из пяти компонентов (рис. 1). Микроконтроллер '
        'ESP32-C3 (RISC-V, 160 МГц) генерирует ключевые пары Ed25519 '
        'с использованием аппаратного TRNG; приватный ключ хранится в NVS '
        'и может быть зашифрован паролем через PBKDF2-SHA256 + AES-256-CBC [3]. '
        'Python-мост подключается к ESP32 через USB-CDC (115200 бод) и создаёт '
        'Unix-сокет, совместимый с протоколом SSH-агента (RFC 4253) [4].')

    body(doc,
        'Go-клиент реализует аутентификацию «вызов-ответ»: менеджер отправляет '
        '32 случайных байта, клиент подписывает их через агент. C-менеджер '
        '(TCP-порт 5555) хранит учётные записи в БД SQLCipher [5], создаёт '
        'LUKS-тома (100 МБ) для каждого контейнера и управляет LXD-контейнерами '
        '(Alpine Linux 3.20) через REST API [6]. При каждом значимом событии '
        '(аутентификация, подключение, отключение, тайм-аут) менеджер записывает '
        'сессионное событие в таблицу sessions зашифрованной базы данных.')

    # ── Figure 1: Architecture ──
    add_figure(doc, ARCH_DIAGRAM,
               'Рис. 1. Архитектура системы безопасного удалённого доступа')

    # ════════════════════════════════════════════
    # AI BEHAVIOR ANALYSIS
    # ════════════════════════════════════════════
    doc.add_paragraph('Анализ поведения пользователей', style='Подзаголовок')

    body(doc,
        'Первый уровень (AI 1) реализует статистическое обнаружение аномалий '
        'без привлечения языковых моделей. Для каждого пользователя формируется '
        'базовый профиль за 30 дней по трём метрикам: распределение подключений '
        'по времени суток (24 часовых бакета, порог 2%), частота сессий в день '
        'и длительность сессий (z-score пар ssh_connected/ssh_disconnected). '
        'Серьёзность: z > 2,5 — low, > 3,5 — medium, > 4,5 — high; '
        'несколько типов аномалий повышают серьёзность (composite) [7].')

    body(doc,
        'Второй уровень (AI 2) запускается администратором (рис. 2). Модуль '
        'временно монтирует LUKS-том контейнера, считывает файлы истории '
        'оболочки и размонтирует том. Команды проходят пятиуровневую санитизацию: '
        'нумерованный формат с маркерами BEGIN/END; фильтрация паттернов инъекций '
        'регулярными выражениями; ограничение объёма (500 строк по 200 символов); '
        'явная маркировка блока как данных; валидация ответа по JSON-схеме '
        'с полями risk_level, summary, findings, recommendation [8]. '
        'В качестве модели используется Qwen2.5 (3B), запускаемая локально через '
        'Ollama [9], что исключает утечку данных. Результаты сохраняются в БД '
        'и JSON-файл для автоматической генерации PDF-отчёта.')

    # ── Figure 2: AI Flow ──
    add_figure(doc, AI_DIAGRAM,
               'Рис. 2. Поток анализа поведения пользователей (AI 1 + AI 2)')

    # ════════════════════════════════════════════
    # TESTING AND RESULTS
    # ════════════════════════════════════════════
    doc.add_paragraph('Результаты тестирования', style='Подзаголовок')

    body(doc,
        'Тестирование проводилось на стенде с ESP32-C3-DevKitM-1 и сервером '
        'Ubuntu 24.04 LTS. Корректность Ed25519 подтверждена перекрёстной '
        'верификацией (OpenSSL, ssh-keygen). Производительность: генерация '
        'ключевой пары — менее 100 мс, подпись — 200–250 мс, полный цикл '
        'аутентификации — около 500 мс, деривация PBKDF2 '
        '(100\u00A0000 итераций) — 3–4 с. Потребление памяти прошивкой — '
        '45 КБ из 400 КБ SRAM.')

    body(doc,
        'Подсистема анализа тестировалась на синтетических данных: '
        '60 дней нормальных сессий с инъекцией аномалий (подключения в '
        '2:00–5:00 UTC, 15 сессий/день, подозрительные команды). AI 1 '
        'обнаружил аномалии unusual_time и high_frequency (severity=high). '
        'AI 2 классифицировал историю с попытками чтения /etc/shadow '
        'и обратными оболочками как malicious. Данные контейнеров зашифрованы '
        '(LUKS); команды анализируются локально; санитизация защищает от '
        'инъекций. К ограничениям относится отсутствие Secure Element '
        'на ESP32-C3 [3].')

    # ════════════════════════════════════════════
    # CONCLUSION
    # ════════════════════════════════════════════
    body(doc,
        'Разработана система удалённого доступа к контейнерным средам, '
        'объединяющая аппаратное хранение ключей Ed25519 на ESP32-C3, управление '
        'LXD-контейнерами с LUKS-шифрованием и двухуровневый анализ поведения '
        '(статистический + LLM). Перспективы: интеграция защищённого элемента '
        'ATECC608A, беспроводное подключение (BLE/Wi-Fi), совместимость '
        'с FIDO2/WebAuthn [10], потоковый мониторинг в реальном времени '
        'и интеграция с системами оповещения (SIEM).')

    # ════════════════════════════════════════════
    # BIBLIOGRAPHY
    # ════════════════════════════════════════════
    doc.add_paragraph('Библиографические ссылки', style='БИБЛИОГРАФИЧЕСКИЕ ССЫЛКИ')

    refs = [
        ('Ylonen, T.',
         'Ylonen, T. The Secure Shell (SSH) Authentication Protocol / T. Ylonen, '
         'C. Lonvick // RFC 4252. 2006.'),

        ('Canonical Ltd.',
         'Canonical Ltd. LXD Documentation. URL: https://documentation.ubuntu.com/lxd/ '
         '(дата обращения: 15.03.2026).'),

        ('Espressif Systems.',
         'Espressif Systems. ESP32-C3 Series Datasheet Version 1.2. URL: '
         'https://www.espressif.com/sites/default/files/documentation/'
         'esp32-c3_datasheet_en.pdf (дата обращения: 15.03.2026).'),

        ('Ylonen, T.',
         'Ylonen, T. The Secure Shell (SSH) Transport Layer Protocol / T. Ylonen, '
         'C. Lonvick // RFC 4253. 2006.'),

        ('Zetetic LLC.',
         'Zetetic LLC. SQLCipher \u2014 Full Database Encryption for SQLite. URL: '
         'https://www.zetetic.net/sqlcipher/ (дата обращения: 15.03.2026).'),

        ('St\u00e9phane Graber.',
         'St\u00e9phane Graber. LXD \u2014 system containers and virtual machines. URL: '
         'https://github.com/canonical/lxd (дата обращения: 15.03.2026).'),

        ('Shewhart, W. A.',
         'Shewhart, W. A. Economic Control of Quality of Manufactured Product / '
         'W. A. Shewhart. \u2014 New York: D. Van Nostrand Company, 1931.'),

        ('Greshake, K.',
         'Greshake, K. Not what you\'ve signed up for: Compromising Real-World LLM-Integrated '
         'Applications with Indirect Prompt Injection / K. Greshake, S. Abdelnabi, '
         'S. Mishra et al. // Proceedings of AISec. 2023. P. 79\u201390.'),

        ('Ollama.',
         'Ollama. Get up and running with large language models locally. URL: '
         'https://ollama.com/ (дата обращения: 15.03.2026).'),

        ('FIDO Alliance.',
         'FIDO Alliance. FIDO2: WebAuthn & CTAP. URL: '
         'https://fidoalliance.org/fido2/ (дата обращения: 15.03.2026).'),
    ]

    for i, (author, full_text) in enumerate(refs, 1):
        add_reference(doc, i, full_text, authors_italic=author)

    # Save
    print(f'[*] Saving to {OUTPUT_PATH}...')
    doc.save(OUTPUT_PATH)
    print(f'[+] Done! Article saved: {OUTPUT_PATH}')
    print(f'    Total paragraphs: {len(doc.paragraphs)}')
    print(f'    NOTE: Place diagram_arch.png and diagram_ai.png in conference/ for images.')


if __name__ == '__main__':
    main()
