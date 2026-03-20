"""
Приложения — full source code listings
Adds appendices to the document.
"""

import os


def _read_file(path):
    """Read file content, return empty string if not found."""
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            return f.read()
    except Exception:
        return f'[Файл не найден: {path}]'


def add_chapter(doc, heading_style, subheading_style, body_style, code_style, caption_style, table_style):
    """Add Appendices with full source code."""

    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    doc.add_paragraph('ПРИЛОЖЕНИЯ', heading_style)

    # ── Приложение А: ESP32 ──
    doc.add_paragraph('ПРИЛОЖЕНИЕ А', heading_style)
    doc.add_paragraph('Исходный код прошивки ESP32', subheading_style)

    doc.add_paragraph('А.1 main.cpp — основной файл прошивки', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'esp32', 'ssh_agent', 'src', 'main.cpp'))

    doc.add_paragraph('')
    doc.add_paragraph('А.2 ed25519.cpp — реализация алгоритма Ed25519', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'esp32', 'ssh_agent', 'src', 'ed25519.cpp'))

    doc.add_paragraph('')
    doc.add_paragraph('А.3 ed25519.h — заголовочный файл Ed25519', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'esp32', 'ssh_agent', 'src', 'ed25519.h'))

    doc.add_paragraph('')
    doc.add_paragraph('А.4 platformio.ini — конфигурация PlatformIO', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'esp32', 'ssh_agent', 'platformio.ini'))

    doc.add_page_break()

    # ── Приложение Б: Python ──
    doc.add_paragraph('ПРИЛОЖЕНИЕ Б', heading_style)
    doc.add_paragraph('Исходный код Python-компонентов', subheading_style)

    doc.add_paragraph('Б.1 esp32_agent_bridge.py — SSH Agent Bridge', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'wrapper', 'esp32_agent_bridge.py'))

    doc.add_paragraph('')
    doc.add_paragraph('Б.2 esp32_keytool.py — утилита управления ключами', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'wrapper', 'esp32_keytool.py'))

    doc.add_page_break()

    # ── Приложение В: Go ──
    doc.add_paragraph('ПРИЛОЖЕНИЕ В', heading_style)
    doc.add_paragraph('Исходный код Go-клиента', subheading_style)

    go_files = [
        ('В.1 main.go — точка входа', 'main.go'),
        ('В.2 agent.go — взаимодействие с SSH-агентом', 'agent.go'),
        ('В.3 crypto.go — криптографические операции', 'crypto.go'),
        ('В.4 keys.go — загрузка и конвертация ключей', 'keys.go'),
        ('В.5 network.go — сетевое взаимодействие', 'network.go'),
        ('В.6 ssh.go — SSH-сессии', 'ssh.go'),
    ]

    for caption_text, filename in go_files:
        doc.add_paragraph(caption_text, caption_style)
        p = doc.add_paragraph('', code_style)
        p.text = _read_file(os.path.join(base, 'src', 'client', filename))
        doc.add_paragraph('')

    doc.add_page_break()

    # ── Приложение Г: C-менеджер ──
    doc.add_paragraph('ПРИЛОЖЕНИЕ Г', heading_style)
    doc.add_paragraph('Исходный код C-менеджера', subheading_style)

    c_files = [
        ('Г.1 main.c — точка входа', 'main.c'),
        ('Г.2 server.c — TCP-сервер и обработка клиентов', 'server.c'),
        ('Г.3 crypto.c — верификация подписей', 'crypto.c'),
        ('Г.4 database.c — работа с SQLCipher', 'database.c'),
        ('Г.5 lxd.c — управление LXD-контейнерами', 'lxd.c'),
        ('Г.6 volume.c — управление LUKS-томами', 'volume.c'),
        ('Г.7 cli.c — CLI-команды', 'cli.c'),
        ('Г.8 utils.c — вспомогательные функции', 'utils.c'),
        ('Г.9 config.h — конфигурационные константы', 'config.h'),
    ]

    for caption_text, filename in c_files:
        doc.add_paragraph(caption_text, caption_style)
        p = doc.add_paragraph('', code_style)
        p.text = _read_file(os.path.join(base, 'src', 'manager', filename))
        doc.add_paragraph('')

    doc.add_page_break()

    # ── Приложение Д: Makefile ──
    doc.add_paragraph('ПРИЛОЖЕНИЕ Д', heading_style)
    doc.add_paragraph('Система сборки', subheading_style)

    doc.add_paragraph('Д.1 Makefile — основной файл сборки', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'Makefile'))

    doc.add_paragraph('')
    doc.add_paragraph('Д.2 go.mod — зависимости Go-клиента', caption_style)
    p = doc.add_paragraph('', code_style)
    p.text = _read_file(os.path.join(base, 'go.mod'))
