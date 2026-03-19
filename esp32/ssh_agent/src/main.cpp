#include <Arduino.h>
#include <Preferences.h>
#include "ed25519.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "esp_random.h"

#define SSH_AGENTC_REQUEST_IDENTITIES      11
#define SSH_AGENTC_SIGN_REQUEST            13
#define SSH_AGENTC_ADD_IDENTITY            17

#define SSH_AGENT_FAILURE                  5
#define SSH_AGENT_SUCCESS                  6
#define SSH_AGENT_IDENTITIES_ANSWER        12
#define SSH_AGENT_SIGN_RESPONSE            14

#define PROTOCOL_SIGN_CHALLENGE            0x01
#define PROTOCOL_GET_PUBLIC_KEY            0x02
#define PROTOCOL_UNLOCK                    0x03
#define PROTOCOL_LOCK                      0x04
#define PROTOCOL_GENERATE_ENCRYPTED        0x05

#define SSH_AGENT_LOCKED                   0x20

#define LED_PIN 8

#define PBKDF2_ITERATIONS 100000
#define SALT_LEN 16
#define IV_LEN 16
#define ENCRYPTED_SEED_LEN 48  // 32 bytes + PKCS7 padding to 48

Preferences prefs;

uint8_t privateKey[32];
uint8_t publicKey[32];
bool keyLoaded = false;
bool keyEncrypted = false;
bool keyUnlocked = false;

// Encrypted key storage (in memory)
uint8_t encryptedSeed[ENCRYPTED_SEED_LEN];
uint8_t salt[SALT_LEN];
uint8_t iv[IV_LEN];

void generateRandomBytes(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = esp_random();
        size_t remaining = len - i;
        size_t toCopy = remaining < 4 ? remaining : 4;
        memcpy(buf + i, &r, toCopy);
    }
}

bool deriveKey(const uint8_t* password, size_t passLen, const uint8_t* salt, uint8_t* aesKey) {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo) return false;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int ret = mbedtls_md_setup(&ctx, mdInfo, 1);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx, password, passLen,
                                     salt, SALT_LEN, PBKDF2_ITERATIONS,
                                     32, aesKey);
    mbedtls_md_free(&ctx);
    return ret == 0;
}

bool encryptSeed(const uint8_t* seed, const uint8_t* aesKey, const uint8_t* iv, uint8_t* output) {
    // PKCS7 pad: 32 bytes -> 48 bytes (pad value = 16)
    uint8_t padded[ENCRYPTED_SEED_LEN];
    memcpy(padded, seed, 32);
    memset(padded + 32, 16, 16);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int ret = mbedtls_aes_setkey_enc(&ctx, aesKey, 256);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    uint8_t ivCopy[IV_LEN];
    memcpy(ivCopy, iv, IV_LEN);

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, ENCRYPTED_SEED_LEN, ivCopy, padded, output);
    mbedtls_aes_free(&ctx);
    memset(padded, 0, sizeof(padded));
    return ret == 0;
}

bool decryptSeed(const uint8_t* encrypted, const uint8_t* aesKey, const uint8_t* iv, uint8_t* output) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int ret = mbedtls_aes_setkey_dec(&ctx, aesKey, 256);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    uint8_t ivCopy[IV_LEN];
    memcpy(ivCopy, iv, IV_LEN);

    uint8_t decrypted[ENCRYPTED_SEED_LEN];
    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, ENCRYPTED_SEED_LEN, ivCopy, encrypted, decrypted);
    mbedtls_aes_free(&ctx);

    if (ret != 0) {
        memset(decrypted, 0, sizeof(decrypted));
        return false;
    }

    // Validate PKCS7 padding: last 16 bytes should all be 0x10
    uint8_t padVal = decrypted[ENCRYPTED_SEED_LEN - 1];
    if (padVal == 0 || padVal > 16) {
        memset(decrypted, 0, sizeof(decrypted));
        return false;
    }
    for (int i = 0; i < padVal; i++) {
        if (decrypted[ENCRYPTED_SEED_LEN - 1 - i] != padVal) {
            memset(decrypted, 0, sizeof(decrypted));
            return false;
        }
    }

    memcpy(output, decrypted, 32);
    memset(decrypted, 0, sizeof(decrypted));
    return true;
}

bool generateEd25519Key() {
    ed25519_keypair(publicKey, privateKey);

    prefs.begin("ssh-agent", false);
    prefs.putBytes("pubkey", publicKey, 32);
    prefs.putBytes("privkey", privateKey, 32);
    prefs.putUChar("encrypted", 0);
    prefs.end();

    keyLoaded = true;
    keyEncrypted = false;
    keyUnlocked = true;
    Serial.println("Ed25519 key generated and stored (plaintext)");
    return true;
}

bool generateEncryptedKey(const uint8_t* password, size_t passLen) {
    ed25519_keypair(publicKey, privateKey);

    // Generate random salt and IV
    generateRandomBytes(salt, SALT_LEN);
    generateRandomBytes(iv, IV_LEN);

    // Derive AES key from password
    uint8_t aesKey[32];
    if (!deriveKey(password, passLen, salt, aesKey)) {
        Serial.println("PBKDF2 key derivation failed");
        memset(aesKey, 0, sizeof(aesKey));
        return false;
    }

    // Encrypt the seed
    if (!encryptSeed(privateKey, aesKey, iv, encryptedSeed)) {
        Serial.println("Encryption failed");
        memset(aesKey, 0, sizeof(aesKey));
        return false;
    }
    memset(aesKey, 0, sizeof(aesKey));

    // Store to NVS
    prefs.begin("ssh-agent", false);
    prefs.putBytes("pubkey", publicKey, 32);
    prefs.putBytes("enc_seed", encryptedSeed, ENCRYPTED_SEED_LEN);
    prefs.putBytes("salt", salt, SALT_LEN);
    prefs.putBytes("iv", iv, IV_LEN);
    prefs.putUChar("encrypted", 1);
    // Remove plaintext key if it existed
    prefs.remove("privkey");
    prefs.end();

    // Clear plaintext private key from memory - key starts locked
    memset(privateKey, 0, 32);
    keyLoaded = true;
    keyEncrypted = true;
    keyUnlocked = false;
    Serial.println("Ed25519 key generated and stored (encrypted)");
    return true;
}

bool loadKeyFromStorage() {
    prefs.begin("ssh-agent", true);

    // Check if public key exists
    if (prefs.getBytesLength("pubkey") != 32) {
        prefs.end();
        return false;
    }
    prefs.getBytes("pubkey", publicKey, 32);

    uint8_t enc = prefs.getUChar("encrypted", 0);

    if (enc == 1) {
        // Load encrypted key data
        if (prefs.getBytesLength("enc_seed") == ENCRYPTED_SEED_LEN &&
            prefs.getBytesLength("salt") == SALT_LEN &&
            prefs.getBytesLength("iv") == IV_LEN) {
            prefs.getBytes("enc_seed", encryptedSeed, ENCRYPTED_SEED_LEN);
            prefs.getBytes("salt", salt, SALT_LEN);
            prefs.getBytes("iv", iv, IV_LEN);
            keyLoaded = true;
            keyEncrypted = true;
            keyUnlocked = false;
            Serial.println("Encrypted key loaded from storage (locked)");
            prefs.end();
            return true;
        }
        prefs.end();
        return false;
    } else {
        // Load plaintext key
        if (prefs.getBytesLength("privkey") == 32) {
            prefs.getBytes("privkey", privateKey, 32);
            keyLoaded = true;
            keyEncrypted = false;
            keyUnlocked = true;
            Serial.println("Key loaded from storage");
            prefs.end();
            return true;
        }
        prefs.end();
        return false;
    }
}

bool signData(const uint8_t* message, size_t msgLen, uint8_t* signature) {
    ed25519_sign(signature, message, msgLen, privateKey, publicKey);
    return true;
}

void sendResponse(uint8_t* data, size_t len) {
    Serial.write((uint8_t)(len >> 24) & 0xFF);
    Serial.write((uint8_t)(len >> 16) & 0xFF);
    Serial.write((uint8_t)(len >> 8) & 0xFF);
    Serial.write((uint8_t)len & 0xFF);
    Serial.write(data, len);
    Serial.flush();
}

bool readPacket(uint8_t* type, uint8_t* data, size_t* len, uint32_t timeout) {
    uint8_t header[4];
    size_t pos = 0;
    uint32_t start = millis();

    while (pos < 4) {
        if (Serial.available()) {
            header[pos++] = Serial.read();
        } else {
            if (millis() - start > timeout) return false;
            delay(1);
        }
    }

    *len = ((size_t)header[1] << 16) | ((size_t)header[2] << 8) | (size_t)header[3];
    *type = header[0];

    if (*len > 4096 || *len > 4096) return false;

    pos = 0;
    while (pos < *len) {
        if (Serial.available()) {
            data[pos++] = Serial.read();
        } else {
            if (millis() - start > timeout) return false;
            delay(1);
        }
    }

    return true;
}

void handleSignChallenge(uint8_t* data, size_t len) {
    if (!keyLoaded) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    if (keyEncrypted && !keyUnlocked) {
        uint8_t locked = SSH_AGENT_LOCKED;
        sendResponse(&locked, 1);
        return;
    }

    uint8_t signature[64];
    if (!signData(data, len, signature)) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    uint8_t response[1 + 64];
    response[0] = SSH_AGENT_SIGN_RESPONSE;
    memcpy(response + 1, signature, 64);
    sendResponse(response, 65);
}

void handleGetPublicKey() {
    if (!keyLoaded) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    uint8_t response[1 + 32];
    response[0] = PROTOCOL_GET_PUBLIC_KEY;
    memcpy(response + 1, publicKey, 32);
    sendResponse(response, 33);
}

void handleRequestIdentities() {
    if (!keyLoaded) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    // Identity listing works even when locked (public key is unencrypted)
    uint8_t response[1 + 4 + 4 + 32 + 4];
    size_t pos = 0;

    response[pos++] = SSH_AGENT_IDENTITIES_ANSWER;
    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 1;

    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 32;
    memcpy(response + pos, publicKey, 32);
    pos += 32;

    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 0;

    sendResponse(response, pos);
}

void handleAgentSignRequest(uint8_t* data, size_t len) {
    if (!keyLoaded || len < 32 + 4 + 1) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    if (keyEncrypted && !keyUnlocked) {
        uint8_t locked = SSH_AGENT_LOCKED;
        sendResponse(&locked, 1);
        return;
    }

    size_t keyLen = ((size_t)data[0] << 24) | ((size_t)data[1] << 16) |
                    ((size_t)data[2] << 8) | (size_t)data[3];

    if (keyLen != 32 || memcmp(data + 4, publicKey, 32) != 0) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    size_t dataOffset = 4 + keyLen;
    size_t dataLen = ((size_t)data[dataOffset] << 24) | ((size_t)data[dataOffset + 1] << 16) |
                     ((size_t)data[dataOffset + 2] << 8) | (size_t)data[dataOffset + 3];

    uint8_t* signDataBuf = data + dataOffset + 4;

    uint8_t signature[64];
    if (!signData(signDataBuf, dataLen, signature)) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    uint8_t response[1 + 4 + 64];
    size_t pos = 0;
    response[pos++] = SSH_AGENT_SIGN_RESPONSE;
    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 0;
    response[pos++] = 64;
    memcpy(response + pos, signature, 64);

    sendResponse(response, 5 + 64);
}

void handleUnlock(uint8_t* data, size_t len) {
    if (!keyLoaded || !keyEncrypted) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    if (keyUnlocked) {
        // Already unlocked
        uint8_t success = SSH_AGENT_SUCCESS;
        sendResponse(&success, 1);
        return;
    }

    // data = password bytes
    uint8_t aesKey[32];
    if (!deriveKey(data, len, salt, aesKey)) {
        memset(aesKey, 0, sizeof(aesKey));
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    if (!decryptSeed(encryptedSeed, aesKey, iv, privateKey)) {
        memset(aesKey, 0, sizeof(aesKey));
        memset(privateKey, 0, 32);
        Serial.println("Unlock failed: wrong password");
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    memset(aesKey, 0, sizeof(aesKey));
    keyUnlocked = true;
    Serial.println("Agent unlocked");
    uint8_t success = SSH_AGENT_SUCCESS;
    sendResponse(&success, 1);
}

void handleLock(uint8_t* data, size_t len) {
    (void)data;
    (void)len;

    if (!keyLoaded || !keyEncrypted) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    memset(privateKey, 0, 32);
    keyUnlocked = false;
    Serial.println("Agent locked");
    uint8_t success = SSH_AGENT_SUCCESS;
    sendResponse(&success, 1);
}

void handleGenerateEncrypted(uint8_t* data, size_t len) {
    if (len < 1) {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
        return;
    }

    if (generateEncryptedKey(data, len)) {
        uint8_t success = SSH_AGENT_SUCCESS;
        sendResponse(&success, 1);
    } else {
        uint8_t fail = SSH_AGENT_FAILURE;
        sendResponse(&fail, 1);
    }
}

void processAgentRequest() {
    uint8_t type;
    uint8_t data[4096];
    size_t len;

    if (!readPacket(&type, data, &len, 5000)) {
        return;
    }

    switch (type) {
        case PROTOCOL_SIGN_CHALLENGE:
            handleSignChallenge(data, len);
            break;

        case PROTOCOL_GET_PUBLIC_KEY:
            handleGetPublicKey();
            break;

        case PROTOCOL_UNLOCK:
            handleUnlock(data, len);
            break;

        case PROTOCOL_LOCK:
            handleLock(data, len);
            break;

        case PROTOCOL_GENERATE_ENCRYPTED:
            handleGenerateEncrypted(data, len);
            break;

        case SSH_AGENTC_REQUEST_IDENTITIES:
            handleRequestIdentities();
            break;

        case SSH_AGENTC_SIGN_REQUEST:
            handleAgentSignRequest(data, len);
            break;

        default:
            uint8_t fail = SSH_AGENT_FAILURE;
            sendResponse(&fail, 1);
            break;
    }
}

void printPublicKey() {
    if (!keyLoaded) {
        Serial.println("No key loaded");
        return;
    }

    Serial.println("ssh-ed25519 AAAA...");
    Serial.print("Public key (hex): ");
    for (int i = 0; i < 32; i++) {
        if (publicKey[i] < 16) Serial.print("0");
        Serial.print(publicKey[i], HEX);
    }
    Serial.println();
    if (keyEncrypted) {
        Serial.println("Key is encrypted (use UNLOCK to sign)");
    }
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);

    delay(1000);
    Serial.println("ESP32 SSH Agent starting...");
    Serial.println("Version: 2.0.0");

    if (!loadKeyFromStorage()) {
        Serial.println("No key found. Use keytool to generate a key.");
    }

    Serial.println("SSH Agent ready");
    printPublicKey();
    Serial.println("Waiting for agent requests...");

    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    if (Serial.available() > 0) {
        digitalWrite(LED_PIN, LOW);
        processAgentRequest();
        digitalWrite(LED_PIN, HIGH);
    }
    delay(10);
}
