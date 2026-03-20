"""
Список использованных источников (~2 pages)
Adds references to the document.
"""


def add_chapter(doc, heading_style, subheading_style, body_style, code_style, caption_style, table_style):
    """Add References."""

    doc.add_paragraph('СПИСОК ИСПОЛЬЗОВАННЫХ ИСТОЧНИКОВ', heading_style)

    refs = [
        '[1] Diffie W., Hellman M. New Directions in Cryptography // IEEE Transactions '
        'on Information Theory. — 1976. — Vol. 22, No. 6. — P. 644–654.',

        '[2] Menezes A. J., van Oorschot P. C., Vanstone S. A. Handbook of Applied '
        'Cryptography. — CRC Press, 1996. — 816 p.',

        '[3] Hankerson D., Menezes A., Vanstone S. Guide to Elliptic Curve Cryptography. '
        '— Springer, 2004. — 332 p.',

        '[4] Barker E. Recommendation for Key Management: Part 1 — General // NIST '
        'Special Publication 800-57 Part 1. — Revision 5. — 2020.',

        '[5] Bernstein D. J., Duif N., Lange T., Schwabe P., Yang B.-Y. High-speed '
        'high-security signatures // Journal of Cryptographic Engineering. — 2012. — '
        'Vol. 2, No. 2. — P. 77–89.',

        '[6] Ylonen T., Lonvick C. The Secure Shell (SSH) Protocol Architecture // '
        'RFC 4251. — Internet Engineering Task Force. — January 2006.',

        '[7] Miller D. SSH Agent Protocol // Internet-Draft draft-miller-ssh-agent-04. '
        '— Internet Engineering Task Force. — 2020.',

        '[8] Moriarty K., Kaliski B., Rusch A. PKCS #5: Password-Based Cryptography '
        'Specification Version 2.1 // RFC 8018. — Internet Engineering Task Force. — '
        'January 2017.',

        '[9] Espressif Systems. ESP-IDF Programming Guide: mbed TLS // Espressif '
        'Documentation. — 2024. — URL: https://docs.espressif.com/projects/esp-idf/'
        'en/latest/esp32c3/api-reference/protocols/mbedtls.html.',

        '[10] National Institute of Standards and Technology. Advanced Encryption Standard '
        '(AES) // FIPS PUB 197. — November 2001.',

        '[11] Housley R. Cryptographic Message Syntax (CMS) // RFC 5652. — Internet '
        'Engineering Task Force. — September 2009.',

        '[12] Microchip Technology. ATECC608A CryptoAuthentication Device Summary '
        'Data Sheet. — 2020.',

        '[13] Espressif Systems. ESP32-C3 Series Datasheet. — Version 1.1. — 2023. — '
        'URL: https://www.espressif.com/sites/default/files/documentation/'
        'esp32-c3_datasheet_en.pdf.',

        '[14] Canonical Ltd. LXD Documentation // Linux Containers. — 2024. — '
        'URL: https://documentation.ubuntu.com/lxd/en/latest/.',

        '[15] Fruhwirth C. New Methods in Hard Disk Encryption // Institute for '
        'Computer Languages, Theory and Logic Group, Vienna University of Technology. '
        '— 2005.',

        '[16] Yubico AB. YubiKey 5 Series Technical Manual. — 2023. — '
        'URL: https://docs.yubico.com/hardware/yubikey/yk-5/tech-manual/.',

        '[17] Nitrokey GmbH. Nitrokey Documentation. — 2024. — '
        'URL: https://docs.nitrokey.com/.',

        '[18] Balfanz D., Czeskis A., Hodges J. et al. Web Authentication: An API for '
        'accessing Public Key Credentials Level 2 // W3C Recommendation. — April 2021.',

        '[19] Bernstein D. J. Curve25519: new Diffie-Hellman speed records // '
        'Public Key Cryptography — PKC 2006. Lecture Notes in Computer Science, '
        'vol 3958. — Springer. — P. 207–228.',

        '[20] Josefsson S., Liusvaara I. Edwards-Curve Digital Signature Algorithm '
        '(EdDSA) // RFC 8032. — Internet Engineering Task Force. — January 2017.',

        '[21] PlatformIO Labs. PlatformIO Documentation. — 2024. — '
        'URL: https://docs.platformio.org/.',

        '[22] The Go Authors. golang.org/x/crypto/ssh Package Documentation. — '
        '2024. — URL: https://pkg.go.dev/golang.org/x/crypto/ssh.',

        '[23] Zetetic LLC. SQLCipher — Full Database Encryption for SQLite. — '
        '2024. — URL: https://www.zetetic.net/sqlcipher/.',

        '[24] Ylonen T., Lonvick C. The Secure Shell (SSH) Authentication Protocol // '
        'RFC 4252. — Internet Engineering Task Force. — January 2006.',

        '[25] Dworkin M. Recommendation for Block Cipher Modes of Operation: Methods '
        'and Techniques // NIST Special Publication 800-38A. — 2001.',
    ]

    for ref in refs:
        doc.add_paragraph(ref, body_style)
