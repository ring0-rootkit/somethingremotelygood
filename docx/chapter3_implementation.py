"""
Глава 3: Реализация (~12 pages)
Adds Chapter 3 content to the document.
"""

from docx.shared import Pt, Cm
from docx.enum.text import WD_ALIGN_PARAGRAPH


def add_chapter(doc, heading_style, subheading_style, body_style, code_style, caption_style, table_style):
    """Add Chapter 3: Implementation."""

    doc.add_paragraph('ГЛАВА 3. РЕАЛИЗАЦИЯ СИСТЕМЫ', heading_style)

    # ══════════════════════════════════════════════
    # 3.1 Прошивка ESP32
    # ══════════════════════════════════════════════
    doc.add_paragraph('3.1 Прошивка ESP32: генерация ключей и криптографические операции', subheading_style)

    doc.add_paragraph(
        'Прошивка ESP32 реализована на языке C++ с использованием фреймворка Arduino '
        'и библиотек ESP-IDF. Основной файл main.cpp содержит всю логику обработки '
        'протокола, криптографические функции и управление NVS-хранилищем. Реализация '
        'Ed25519 вынесена в отдельные файлы ed25519.cpp и ed25519.h и основана на '
        'библиотеке TweetNaCl, адаптированной для использования mbedTLS SHA-512 и '
        'аппаратного ГСЧ ESP32.',
        body_style
    )

    doc.add_paragraph('3.1.1 Генерация ключевой пары Ed25519', subheading_style)

    doc.add_paragraph(
        'Функция ed25519_keypair() генерирует ключевую пару, используя аппаратный ГСЧ '
        'ESP32. 32-байтовый seed создаётся восемью вызовами esp_random(), каждый из '
        'которых возвращает 4 байта истинно случайных данных. Затем вычисляется '
        'SHA-512 хеш seed, результат которого (первые 32 байта после «зажима») '
        'используется как скаляр для вычисления открытого ключа A = aB на кривой '
        'Edwards25519.',
        body_style
    )

    doc.add_paragraph(
        'Листинг 1 — Генерация ключевой пары Ed25519 (ed25519.cpp)',
        caption_style
    )
    code1 = doc.add_paragraph('', code_style)
    code1.text = (
        'void ed25519_keypair(uint8_t pk[32], uint8_t seed[32]) {\n'
        '    uint8_t d[64];\n'
        '    gf p[4];\n'
        '\n'
        '    for (int i = 0; i < 8; i++) {\n'
        '        uint32_t r = esp_random();\n'
        '        seed[i * 4]     = r & 0xff;\n'
        '        seed[i * 4 + 1] = (r >> 8) & 0xff;\n'
        '        seed[i * 4 + 2] = (r >> 16) & 0xff;\n'
        '        seed[i * 4 + 3] = (r >> 24) & 0xff;\n'
        '    }\n'
        '\n'
        '    mbedtls_sha512(seed, 32, d, 0);\n'
        '    d[0] &= 248;\n'
        '    d[31] &= 127;\n'
        '    d[31] |= 64;\n'
        '\n'
        '    scalarbase(p, d);\n'
        '    pack_point(pk, p);\n'
        '}'
    )

    doc.add_paragraph('')

    doc.add_paragraph('3.1.2 Шифрование и дешифрование seed', subheading_style)

    doc.add_paragraph(
        'Шифрование seed реализовано в двух функциях: encryptSeed() и decryptSeed(). '
        'Функция encryptSeed() принимает 32-байтовый seed, AES-ключ (полученный из '
        'пароля через PBKDF2) и вектор инициализации. Seed дополняется по схеме '
        'PKCS7 до 48 байт (один полный блок дополнения: 16 байт со значением 0x10), '
        'затем шифруется AES-256-CBC. IV копируется перед использованием, так как '
        'mbedtls_aes_crypt_cbc модифицирует его in-place.',
        body_style
    )

    doc.add_paragraph(
        'Листинг 2 — Шифрование seed с помощью AES-256-CBC (main.cpp)',
        caption_style
    )
    code2 = doc.add_paragraph('', code_style)
    code2.text = (
        'bool encryptSeed(const uint8_t* seed, const uint8_t* aesKey,\n'
        '                 const uint8_t* iv, uint8_t* output) {\n'
        '    // PKCS7 pad: 32 bytes -> 48 bytes (pad value = 16)\n'
        '    uint8_t padded[ENCRYPTED_SEED_LEN];\n'
        '    memcpy(padded, seed, 32);\n'
        '    memset(padded + 32, 16, 16);\n'
        '\n'
        '    mbedtls_aes_context ctx;\n'
        '    mbedtls_aes_init(&ctx);\n'
        '    int ret = mbedtls_aes_setkey_enc(&ctx, aesKey, 256);\n'
        '    if (ret != 0) {\n'
        '        mbedtls_aes_free(&ctx);\n'
        '        return false;\n'
        '    }\n'
        '\n'
        '    uint8_t ivCopy[IV_LEN];\n'
        '    memcpy(ivCopy, iv, IV_LEN);\n'
        '\n'
        '    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,\n'
        '        ENCRYPTED_SEED_LEN, ivCopy, padded, output);\n'
        '    mbedtls_aes_free(&ctx);\n'
        '    memset(padded, 0, sizeof(padded));\n'
        '    return ret == 0;\n'
        '}'
    )

    doc.add_paragraph('')

    doc.add_paragraph(
        'Функция decryptSeed() выполняет обратную операцию: расшифровывает 48 байт '
        'AES-256-CBC, затем проверяет корректность PKCS7-дополнения. Последний байт '
        'расшифрованных данных определяет значение дополнения padVal. Если padVal '
        'равен нулю, больше 16 или последние padVal байт не все равны padVal, '
        'расшифровка считается неуспешной (неверный пароль). При успешной проверке '
        'первые 32 байта копируются как seed, а буфер расшифрованных данных '
        'обнуляется (memset) для предотвращения утечки секретных данных.',
        body_style
    )

    doc.add_paragraph(
        'Листинг 3 — Валидация PKCS7-дополнения при расшифровке (main.cpp)',
        caption_style
    )
    code3 = doc.add_paragraph('', code_style)
    code3.text = (
        '// Validate PKCS7 padding\n'
        'uint8_t padVal = decrypted[ENCRYPTED_SEED_LEN - 1];\n'
        'if (padVal == 0 || padVal > 16) {\n'
        '    memset(decrypted, 0, sizeof(decrypted));\n'
        '    return false;\n'
        '}\n'
        'for (int i = 0; i < padVal; i++) {\n'
        '    if (decrypted[ENCRYPTED_SEED_LEN - 1 - i] != padVal) {\n'
        '        memset(decrypted, 0, sizeof(decrypted));\n'
        '        return false;\n'
        '    }\n'
        '}\n'
        'memcpy(output, decrypted, 32);\n'
        'memset(decrypted, 0, sizeof(decrypted));'
    )

    doc.add_paragraph('')

    doc.add_paragraph('3.1.3 Деривация ключа PBKDF2-SHA256', subheading_style)

    doc.add_paragraph(
        'Функция deriveKey() реализует PBKDF2-HMAC-SHA256 с использованием mbedTLS. '
        'Создаётся контекст HMAC (mbedtls_md_context_t) с алгоритмом SHA-256, '
        'затем вызывается mbedtls_pkcs5_pbkdf2_hmac() с 100000 итераций. Результат — '
        '32 байта AES-ключа. Контекст HMAC инициализируется с флагом is_hmac=1 '
        '(третий параметр mbedtls_md_setup), что необходимо для работы PBKDF2.',
        body_style
    )

    doc.add_paragraph(
        'Листинг 4 — Деривация AES-ключа из пароля (main.cpp)',
        caption_style
    )
    code4 = doc.add_paragraph('', code_style)
    code4.text = (
        'bool deriveKey(const uint8_t* password, size_t passLen,\n'
        '               const uint8_t* salt, uint8_t* aesKey) {\n'
        '    const mbedtls_md_info_t* mdInfo =\n'
        '        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);\n'
        '    if (!mdInfo) return false;\n'
        '\n'
        '    mbedtls_md_context_t ctx;\n'
        '    mbedtls_md_init(&ctx);\n'
        '    int ret = mbedtls_md_setup(&ctx, mdInfo, 1); // 1 = HMAC\n'
        '    if (ret != 0) {\n'
        '        mbedtls_md_free(&ctx);\n'
        '        return false;\n'
        '    }\n'
        '\n'
        '    ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx,\n'
        '        password, passLen,\n'
        '        salt, SALT_LEN,\n'
        '        PBKDF2_ITERATIONS,  // 100000\n'
        '        32, aesKey);        // 32 bytes output\n'
        '    mbedtls_md_free(&ctx);\n'
        '    return ret == 0;\n'
        '}'
    )

    doc.add_paragraph('')

    doc.add_paragraph('3.1.4 Обработка серийного протокола', subheading_style)

    doc.add_paragraph(
        'Функция processAgentRequest() является главным диспетчером команд. Она '
        'вызывает readPacket() для чтения входящего пакета (тип + данные) с таймаутом '
        '5 секунд, затем выполняет обработку в зависимости от типа сообщения. '
        'Поддерживаются семь типов: PROTOCOL_SIGN_CHALLENGE (0x01), '
        'PROTOCOL_GET_PUBLIC_KEY (0x02), PROTOCOL_UNLOCK (0x03), PROTOCOL_LOCK (0x04), '
        'PROTOCOL_GENERATE_ENCRYPTED (0x05), SSH_AGENTC_REQUEST_IDENTITIES (11), '
        'SSH_AGENTC_SIGN_REQUEST (13). Неизвестные типы возвращают SSH_AGENT_FAILURE.',
        body_style
    )

    doc.add_paragraph(
        'Функции обработки запросов на подпись (handleSignChallenge и '
        'handleAgentSignRequest) проверяют два условия перед выполнением: наличие '
        'загруженного ключа (keyLoaded) и разблокированное состояние (если ключ '
        'зашифрован). При заблокированном ключе возвращается SSH_AGENT_LOCKED (0x20), '
        'что позволяет клиенту определить необходимость разблокировки. Функция '
        'handleGetPublicKey() возвращает открытый ключ независимо от состояния '
        'блокировки, поскольку открытый ключ хранится незашифрованным — это позволяет '
        'SSH-клиенту получить список ключей даже при заблокированном агенте.',
        body_style
    )

    doc.add_paragraph(
        'Функция sendResponse() формирует ответный пакет: 4-байтовый заголовок длины '
        '(big-endian) и данные. Используется Serial.write() для побайтовой записи '
        'заголовка и Serial.write(data, len) для записи блока данных, после чего '
        'вызывается Serial.flush() для гарантии отправки.',
        body_style
    )

    doc.add_paragraph(
        'Листинг 5 — Формирование и отправка ответного пакета (main.cpp)',
        caption_style
    )
    code5 = doc.add_paragraph('', code_style)
    code5.text = (
        'void sendResponse(uint8_t* data, size_t len) {\n'
        '    Serial.write((uint8_t)(len >> 24) & 0xFF);\n'
        '    Serial.write((uint8_t)(len >> 16) & 0xFF);\n'
        '    Serial.write((uint8_t)(len >> 8) & 0xFF);\n'
        '    Serial.write((uint8_t)len & 0xFF);\n'
        '    Serial.write(data, len);\n'
        '    Serial.flush();\n'
        '}'
    )

    doc.add_paragraph('')

    doc.add_paragraph('3.1.5 Загрузка ключей из NVS', subheading_style)

    doc.add_paragraph(
        'Функция loadKeyFromStorage() вызывается при инициализации (setup()) и '
        'определяет тип хранимого ключа по флагу «encrypted» в NVS. Если '
        'encrypted=1, загружаются enc_seed, salt и iv, устанавливается '
        'keyEncrypted=true и keyUnlocked=false — агент стартует в заблокированном '
        'состоянии. Если encrypted=0, загружается plaintext privkey, '
        'keyEncrypted=false и keyUnlocked=true — агент сразу готов к подписанию. '
        'Открытый ключ (pubkey) загружается в обоих случаях.',
        body_style
    )

    doc.add_paragraph(
        'Версия 2.0.0 прошивки не генерирует ключи автоматически при первом запуске. '
        'Вместо этого выводится сообщение «No key found. Use keytool to generate a key.» '
        'и ожидаются входящие команды. Это изменение обусловлено необходимостью '
        'поддержки зашифрованных ключей: автоматическая генерация не может запросить '
        'пароль у пользователя.',
        body_style
    )

    # ══════════════════════════════════════════════
    # 3.2 Python-мост (SSH Agent Bridge)
    # ══════════════════════════════════════════════
    doc.add_paragraph('3.2 Python-мост: SSH Agent Bridge', subheading_style)

    doc.add_paragraph(
        'Файл esp32_agent_bridge.py реализует класс ESP32AgentBridge, который '
        'выступает в роли SSH-агента. При запуске мост подключается к ESP32 через '
        'серийный порт (pyserial), создаёт Unix-сокет и начинает принимать подключения '
        'от SSH-клиентов.',
        body_style
    )

    doc.add_paragraph('3.2.1 Трансляция форматов ключей и подписей', subheading_style)

    doc.add_paragraph(
        'Ключевой задачей моста является трансляция между «сырым» форматом ESP32 '
        '(32-байтовый ключ, 64-байтовая подпись) и SSH wire format. Для этого '
        'реализованы четыре вспомогательных метода:',
        body_style
    )

    doc.add_paragraph(
        '_build_ed25519_key_blob(raw_pubkey) — формирует SSH key blob из 32-байтового '
        'ключа: [4B len][\"ssh-ed25519\"][4B len][32B key]. Результат используется '
        'в ответе SSH_AGENT_IDENTITIES_ANSWER.',
        body_style
    )

    doc.add_paragraph(
        '_extract_raw_key(ssh_key_blob) — извлекает 32-байтовый ключ из SSH key blob, '
        'пропуская строку типа. Используется при обработке SSH_AGENTC_SIGN_REQUEST.',
        body_style
    )

    doc.add_paragraph(
        '_build_ed25519_sig_blob(raw_sig) — формирует SSH signature blob из '
        '64-байтовой подписи: [4B len][\"ssh-ed25519\"][4B len][64B sig].',
        body_style
    )

    doc.add_paragraph(
        '_ssh_string(data) — кодирует произвольные байты в формат SSH-строки: '
        '[4B length, big-endian][data].',
        body_style
    )

    doc.add_paragraph('3.2.2 Обработка запросов SSH-агента', subheading_style)

    doc.add_paragraph(
        'Метод handle_agent_request() является центральным диспетчером запросов. '
        'Он принимает байты сообщения (без заголовка длины), определяет тип по '
        'первому байту и вызывает соответствующий обработчик. Для '
        'SSH_AGENTC_SIGN_REQUEST выполняется разбор payload: извлечение key_blob '
        '(4B len + data), sign_data (4B len + data) и flags (4B). Для '
        'SSH_AGENTC_EXTENSION разбирается имя расширения (SSH-строка) и '
        'маршрутизируется на unlock() или lock().',
        body_style
    )

    doc.add_paragraph(
        'Листинг 6 — Обработка расширений SSH-агента (esp32_agent_bridge.py)',
        caption_style
    )
    code6 = doc.add_paragraph('', code_style)
    code6.text = (
        'elif msg_type == SSH_AGENTC_EXTENSION:\n'
        '    # Parse extension name\n'
        '    if len(payload) < 4:\n'
        '        return self._create_failure()\n'
        '    name_len = struct.unpack(">I", payload[0:4])[0]\n'
        '    ext_name = payload[4:4 + name_len]\n'
        '    ext_data = payload[4 + name_len:]\n'
        '\n'
        '    if ext_name == b"esp32-unlock":\n'
        '        pw_len = struct.unpack(">I", ext_data[0:4])[0]\n'
        '        password = ext_data[4:4 + pw_len]\n'
        '        return self.unlock(password)\n'
        '    elif ext_name == b"esp32-lock":\n'
        '        return self.lock()\n'
        '    else:\n'
        '        return self._create_failure()'
    )

    doc.add_paragraph('')

    doc.add_paragraph('3.2.3 Фильтрация отладочного текста', subheading_style)

    doc.add_paragraph(
        'Метод recv_from_esp32() реализует чтение бинарных пакетов от ESP32 с '
        'фильтрацией отладочного текста. ESP32 выводит текстовые сообщения через '
        'Serial.println() (например, «Agent unlocked», «Ed25519 key generated»), '
        'которые перемежаются с бинарными ответами. Алгоритм фильтрации: '
        'побайтовое чтение с Serial до обнаружения байта 0x00 (старший байт '
        '4-байтового заголовка длины, всегда равен нулю при length < 16 МБ). '
        'После нахождения 0x00 считываются ещё 3 байта заголовка, затем length '
        'байт данных. Используется deadline-based timeout: общее время ожидания '
        'не превышает timeout секунд, включая время пропуска отладочного текста.',
        body_style
    )

    doc.add_paragraph('3.2.4 Многопоточная обработка клиентов', subheading_style)

    doc.add_paragraph(
        'Метод start_unix_server() создаёт Unix-сокет с правами 0o600 (только '
        'владелец может подключиться) и слушает подключения с таймаутом 1 секунда. '
        'Для каждого подключившегося клиента создаётся daemon-поток, выполняющий '
        'handle_client(). Каждый клиент обрабатывается в цикле: чтение 4-байтового '
        'заголовка длины, чтение данных, вызов handle_agent_request(), отправка '
        'ответа с 4-байтовым заголовком длины. Доступ к Serial синхронизируется '
        'через self.serial_lock (threading.Lock).',
        body_style
    )

    # ══════════════════════════════════════════════
    # 3.3 Python keytool
    # ══════════════════════════════════════════════
    doc.add_paragraph('3.3 Утилита управления ключами (keytool)', subheading_style)

    doc.add_paragraph(
        'Файл esp32_keytool.py предоставляет CLI-интерфейс для управления ключами '
        'на ESP32. Утилита поддерживает пять операций: --generate (генерация ключа, '
        'опционально с --password), --get-key (получение открытого ключа в формате '
        'ssh-ed25519), --sign (подпись hex-данных), --unlock (разблокировка агента), '
        '--lock (блокировка агента).',
        body_style
    )

    doc.add_paragraph(
        'Функция send_packet() формирует пакет серийного протокола: [type: 1B]'
        '[length: 3B big-endian][data]. Длина кодируется в 3 байтах путём '
        'отрезания первого байта от 4-байтового struct.pack(\">I\", length).',
        body_style
    )

    doc.add_paragraph(
        'Листинг 7 — Отправка пакета серийного протокола (esp32_keytool.py)',
        caption_style
    )
    code7 = doc.add_paragraph('', code_style)
    code7.text = (
        'def send_packet(ser, msg_type, data):\n'
        '    length = len(data)\n'
        '    header = bytes([msg_type]) + \\\n'
        '             struct.pack(">I", length)[1:4]\n'
        '    ser.write(header + data)\n'
        '    ser.flush()'
    )

    doc.add_paragraph('')

    doc.add_paragraph(
        'Функция get_public_key() получает открытый ключ от ESP32 и форматирует '
        'его в стандартный формат ssh-ed25519. Из 32-байтового «сырого» ключа '
        'формируется SSH blob ([4B \"ssh-ed25519\"][4B 32-byte key]), который '
        'кодируется в base64 и выводится как «ssh-ed25519 AAAA...». Этот вывод '
        'можно напрямую использовать для добавления в authorized_keys на сервере '
        'или регистрации в менеджере.',
        body_style
    )

    # ══════════════════════════════════════════════
    # 3.4 Go-клиент
    # ══════════════════════════════════════════════
    doc.add_paragraph('3.4 Go-клиент: аутентификация и управление сессиями', subheading_style)

    doc.add_paragraph(
        'Go-клиент состоит из шести файлов: main.go (точка входа и разбор аргументов), '
        'agent.go (взаимодействие с SSH-агентом), crypto.go (подпись challenge для RSA), '
        'keys.go (загрузка и конвертация ключей), network.go (TCP-соединение с менеджером), '
        'ssh.go (запуск SSH-сессии).',
        body_style
    )

    doc.add_paragraph('3.4.1 Структура Agent и взаимодействие с SSH-агентом', subheading_style)

    doc.add_paragraph(
        'Файл agent.go определяет структуру Agent, оборачивающую Unix-сокет '
        'соединение (net.Conn). Метод sendRequest() реализует базовый протокол '
        'SSH-агента: формирует пакет [4B length][1B type][payload], отправляет '
        'его через сокет, затем читает ответ [4B length][data]. Если первый '
        'байт ответа равен sshAgentFailure (5), возвращается ошибка «agent request '
        'failed». Если равен sshAgentLocked (0x20), возвращается ошибка «agent locked». '
        'Это специальное значение используется Go-клиентом для инициирования '
        'процедуры разблокировки.',
        body_style
    )

    doc.add_paragraph(
        'Листинг 8 — Обработка статуса LOCKED в Go-клиенте (agent.go)',
        caption_style
    )
    code8 = doc.add_paragraph('', code_style)
    code8.text = (
        'if len(resp) == 0 || resp[0] == sshAgentFailure {\n'
        '    return nil, errors.New("agent request failed")\n'
        '}\n'
        '\n'
        'if resp[0] == sshAgentLocked {\n'
        '    return nil, errors.New("agent locked")\n'
        '}'
    )

    doc.add_paragraph('')

    doc.add_paragraph('3.4.2 Автоматическая разблокировка при подписании', subheading_style)

    doc.add_paragraph(
        'Структура ESP32Agent оборачивает Agent и добавляет логику автоматической '
        'разблокировки. Метод SignChallenge() сначала пытается подписать данные. '
        'Если агент заблокирован (ошибка «agent locked»), метод: '
        '(1) выводит приглашение «[*] ESP32 agent is locked. Enter password: »; '
        '(2) считывает пароль без эхо с помощью term.ReadPassword(int(syscall.Stdin)); '
        '(3) вызывает Unlock() для отправки расширения esp32-unlock с паролем; '
        '(4) при успешной разблокировке повторяет попытку подписания.',
        body_style
    )

    doc.add_paragraph(
        'Листинг 9 — Автоматическая разблокировка в Go-клиенте (agent.go)',
        caption_style
    )
    code9 = doc.add_paragraph('', code_style)
    code9.text = (
        'func (e *ESP32Agent) SignChallenge(challenge []byte) ([]byte, error) {\n'
        '    flags := uint8(0)\n'
        '    sig, err := e.agent.Sign(e.publicKey, challenge, flags)\n'
        '    if err != nil && err.Error() == "agent locked" {\n'
        '        fmt.Print("[*] ESP32 agent is locked. Enter password: ")\n'
        '        password, readErr := term.ReadPassword(\n'
        '            int(syscall.Stdin))\n'
        '        fmt.Println()\n'
        '        if readErr != nil {\n'
        '            return nil, fmt.Errorf(\n'
        '                "failed to read password: %w", readErr)\n'
        '        }\n'
        '        if unlockErr := e.agent.Unlock(password);\n'
        '           unlockErr != nil {\n'
        '            return nil, fmt.Errorf(\n'
        '                "unlock failed (wrong password?): %w",\n'
        '                unlockErr)\n'
        '        }\n'
        '        fmt.Println("[+] Agent unlocked")\n'
        '        sig, err = e.agent.Sign(\n'
        '            e.publicKey, challenge, flags)\n'
        '    }\n'
        '    return sig, err\n'
        '}'
    )

    doc.add_paragraph('')

    doc.add_paragraph('3.4.3 Метод Unlock: расширение SSH-агента', subheading_style)

    doc.add_paragraph(
        'Метод Unlock() формирует сообщение SSH_AGENTC_EXTENSION (тип 27) с '
        'именем расширения «esp32-unlock» и паролем в формате SSH-строки. Формат '
        'payload: [4B len(\"esp32-unlock\")][\"esp32-unlock\"][4B len(password)]'
        '[password]. Для формирования используется bytes.Buffer и '
        'binary.Write(binary.BigEndian) для записи 4-байтовых длин.',
        body_style
    )

    # ══════════════════════════════════════════════
    # 3.5 C-менеджер
    # ══════════════════════════════════════════════
    doc.add_paragraph('3.5 C-менеджер: управление контейнерами и шифрованием', subheading_style)

    doc.add_paragraph(
        'Серверный менеджер реализован в девяти файлах: main.c (точка входа), server.c '
        '(TCP-сервер и обработка клиентов), crypto.c (верификация подписей RSA и Ed25519), '
        'database.c (работа с SQLCipher), lxd.c (управление LXD-контейнерами), volume.c '
        '(LUKS-тома), cli.c (CLI-команды add-user и add-container), utils.c (вспомогательные '
        'функции), config.h (конфигурационные константы).',
        body_style
    )

    doc.add_paragraph('3.5.1 Обработка клиентских подключений', subheading_style)

    doc.add_paragraph(
        'Функция handle_client() в server.c реализует полный цикл обработки '
        'клиентского подключения. Сначала вызывается volume_cleanup_stopped_containers() '
        'для освобождения ресурсов остановленных контейнеров. Затем выполняется '
        'протокол аутентификации: чтение user_id, генерация 32-байтового challenge '
        '(RAND_bytes), верификация подписи. Для Ed25519 используется EVP_PKEY_new_raw_public_key '
        'и EVP_DigestVerify (OpenSSL 3.x API). Для RSA — PEM_read_bio_PUBKEY и '
        'EVP_DigestVerifyFinal с EVP_sha256().',
        body_style
    )

    doc.add_paragraph(
        'После успешной аутентификации менеджер: (1) получает ключ шифрования '
        'контейнера из SQLCipher (db_get_container_key); (2) проверяет/создаёт '
        'зашифрованный LUKS-том (volume_create_encrypted_home); (3) открывает и '
        'монтирует том (volume_open_encrypted_home); (4) настраивает SSH-ключи '
        'в томе (lxd_setup_ssh_in_volume); (5) запускает или создаёт контейнер '
        '(lxd_start_container / lxd_create_container); (6) создаёт пользователя '
        'и настраивает SSH (lxd_create_user_and_setup, lxd_ensure_sshd_running); '
        '(7) возвращает номер SSH-порта клиенту.',
        body_style
    )

    doc.add_paragraph('3.5.2 Управление зашифрованными томами', subheading_style)

    doc.add_paragraph(
        'Модуль volume.c реализует управление LUKS-томами. Структура VolumeInfo '
        'хранит container_id, mapper_name (формат «somethinigremotelygood_<id>»), '
        'mount_point (./homes/<id>_mnt) и флаг is_mounted. Массив mounted_volumes '
        'отслеживает все смонтированные тома.',
        body_style
    )

    doc.add_paragraph(
        'Функция volume_create_encrypted_home() создаёт новый зашифрованный том: '
        '(1) записывает 32-байтовый ключ во временный файл; (2) создаёт образ '
        '100 МБ нулями (dd if=/dev/zero); (3) форматирует образ как LUKS (cryptsetup '
        'luksFormat --batch-mode --key-file); (4) открывает, форматирует ext4, '
        'закрывает; (5) удаляет временный файл ключа. Функция '
        'volume_open_encrypted_home() открывает существующий том и монтирует его '
        'через mount(cmd, mount_point, \"ext4\", 0, NULL) — прямой системный вызов.',
        body_style
    )

    doc.add_paragraph(
        'Фоновый поток cleanup_worker (server.c) вызывает '
        'volume_cleanup_stopped_containers() каждые 10 секунд. Эта функция проверяет '
        'состояние всех смонтированных контейнеров через «lxc list --format csv -c s» '
        'и закрывает тома остановленных контейнеров (umount + cryptsetup close). '
        'Флаг provisioning_active предотвращает преждевременную очистку во время '
        'создания контейнера.',
        body_style
    )

    # ══════════════════════════════════════════════
    # 3.6 Система сборки
    # ══════════════════════════════════════════════
    doc.add_paragraph('3.6 Система сборки Makefile', subheading_style)

    doc.add_paragraph(
        'Проект использует единый Makefile для сборки всех компонентов и автоматизации '
        'рабочих процессов. Основные цели сборки:',
        body_style
    )

    doc.add_paragraph(
        'manager — компиляция C-менеджера из 8 исходных файлов с линковкой '
        'библиотек -lsqlcipher -lssl -lcrypto -lpthread. Используются флаги '
        '-Wall -Wextra -Werror для строгого контроля качества кода.',
        body_style
    )

    doc.add_paragraph(
        'client — сборка Go-клиента командой go build -o build/client ./src/client/.',
        body_style
    )

    doc.add_paragraph(
        'esp32-upload — прошивка ESP32: сначала esptool erase-flash, затем '
        'pio run --target upload из каталога esp32/ssh_agent.',
        body_style
    )

    doc.add_paragraph(
        'esp32-keygen — генерация ключа с поддержкой пароля: read -sp запрашивает '
        'пароль без эхо, при непустом пароле передаёт --password в keytool.',
        body_style
    )

    doc.add_paragraph(
        'setup — автоматическая установка всех зависимостей: определяет менеджер '
        'пакетов (pacman или apt), устанавливает LXD, cryptsetup, gcc, sqlcipher, '
        'OpenSSL, Go, Python; инициализирует LXD, настраивает iptables для сети '
        'контейнеров, создаёт каталоги build, keys, homes.',
        body_style
    )

    # Таблица 7 — цели Makefile
    doc.add_paragraph(
        'Таблица 7 — Основные цели Makefile',
        caption_style
    )
    table7 = doc.add_table(rows=13, cols=2, style=table_style)
    headers7 = ['Цель', 'Описание']
    for i, h in enumerate(headers7):
        table7.rows[0].cells[i].text = h
    make_targets = [
        ('make manager', 'Сборка C-менеджера'),
        ('make client', 'Сборка Go-клиента'),
        ('make esp32-upload', 'Прошивка ESP32'),
        ('make esp32-keygen', 'Генерация ключа (с опциональным паролем)'),
        ('make esp32-get-key', 'Получение открытого ключа'),
        ('make esp32-unlock', 'Разблокировка агента'),
        ('make esp32-lock', 'Блокировка агента'),
        ('make agent-bridge', 'Запуск SSH Agent Bridge'),
        ('make client-run-agent', 'Подключение через ESP32-агент'),
        ('make register-user', 'Регистрация пользователя в менеджере'),
        ('make setup', 'Установка всех зависимостей'),
        ('make clean', 'Очистка артефактов сборки'),
    ]
    for row_idx, (target, desc) in enumerate(make_targets, 1):
        table7.rows[row_idx].cells[0].text = target
        table7.rows[row_idx].cells[1].text = desc

    doc.add_paragraph('')

    doc.add_paragraph(
        'Выводы по главе: в данной главе были подробно рассмотрены реализации всех '
        'компонентов системы. Прошивка ESP32 обеспечивает генерацию ключей, шифрование '
        'seed паролем пользователя и обработку серийного протокола. Python-мост '
        'транслирует протокол SSH-агента в серийный протокол ESP32, обрабатывая '
        'расширения для блокировки/разблокировки. Go-клиент автоматически определяет '
        'заблокированный агент и запрашивает пароль у пользователя. C-менеджер '
        'координирует весь процесс: от аутентификации до запуска контейнера с '
        'зашифрованным домашним каталогом.',
        body_style
    )
