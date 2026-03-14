#include <Arduino.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha512.h>
#include <Preferences.h>
#include <HardwareSerial.h>

#define SSH_AGENTC_REQUEST_IDENTITIES     11
#define SSH_AGENTC_SIGN_REQUEST            13
#define SSH_AGENTC_ADD_IDENTITY            17

#define SSH_AGENT_FAILURE                  5
#define SSH_AGENT_SUCCESS                  6
#define SSH_AGENT_IDENTITIES_ANSWER       12
#define SSH_AGENT_SIGN_RESPONSE            14

#define PROTOCOL_SIGN_CHALLENGE            0x01
#define PROTOCOL_GET_PUBLIC_KEY             0x02

#define LED_PIN 2

Preferences prefs;
HardwareSerial SerialAgent(1);

uint8_t privateKey[32];
uint8_t publicKey[32];
bool keyLoaded = false;

bool generateEd25519Key() {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_pk_context pkCtx;
    
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctrDrbg);
    mbedtls_pk_init(&pkCtx);

    int ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) {
        Serial.println("Failed to seed RNG");
        goto cleanup;
    }

    ret = mbedtls_pk_setup(&pkCtx, mbedtls_pk_info_from_type(MBEDTLS_PK_ED25519));
    if (ret != 0) {
        Serial.println("Failed to setup PK context");
        goto cleanup;
    }

    ret = mbedtls_pk_genkey(&pkCtx, MBEDTLS_PK_ED25519, mbedtls_ctr_drbg_random, &ctrDrbg);
    if (ret != 0) {
        Serial.println("Failed to generate Ed25519 key");
        goto cleanup;
    }

    uint8_t pubBuf[32];
    uint8_t privBuf[64];

    ret = mbedtls_pk_write_pubkey_der(&pkCtx, pubBuf, sizeof(pubBuf));
    if (ret <= 0) {
        Serial.println("Failed to export public key");
        goto cleanup;
    }
    memcpy(publicKey, pubBuf + ret - 32, 32);

    ret = mbedtls_pk_write_key_der(&pkCtx, privBuf, sizeof(privBuf));
    if (ret <= 0) {
        Serial.println("Failed to export private key");
        goto cleanup;
    }
    memcpy(privateKey, privBuf + ret - 64 + 32, 32);

    prefs.begin("ssh-agent", false);
    prefs.putBytes("pubkey", publicKey, 32);
    prefs.putBytes("privkey", privateKey, 32);
    prefs.end();

    keyLoaded = true;
    Serial.println("Ed25519 key generated and stored");
    ret = 0;

cleanup:
    mbedtls_pk_free(&pkCtx);
    mbedtls_ctr_drbg_free(&ctrDrbg);
    mbedtls_entropy_free(&entropy);
    
    return ret == 0;
}

bool loadKeyFromStorage() {
    prefs.begin("ssh-agent", true);
    if (prefs.getBytesLength("privkey") == 32 && prefs.getBytesLength("pubkey") == 32) {
        prefs.getBytes("pubkey", publicKey, 32);
        prefs.getBytes("privkey", privateKey, 32);
        keyLoaded = true;
        Serial.println("Key loaded from storage");
        prefs.end();
        return true;
    }
    prefs.end();
    return false;
}

bool signData(const uint8_t* message, size_t msgLen, uint8_t* signature) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctrDrbg);

    int ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) goto cleanup;

    ret = mbedtls_ed25519_sign(privateKey, message, msgLen, signature);
    if (ret != 0) goto cleanup;

    ret = 0;

cleanup:
    mbedtls_ctr_drbg_free(&ctrDrbg);
    mbedtls_entropy_free(&entropy);
    
    return ret == 0;
}

void sendResponse(uint8_t* data, size_t len) {
    SerialAgent.write((uint8_t)(len >> 24) & 0xFF);
    SerialAgent.write((uint8_t)(len >> 16) & 0xFF);
    SerialAgent.write((uint8_t)(len >> 8) & 0xFF);
    SerialAgent.write((uint8_t)len & 0xFF);
    SerialAgent.write(data, len);
    SerialAgent.flush();
}

bool readPacket(uint8_t* type, uint8_t* data, size_t* len, uint32_t timeout) {
    uint8_t header[4];
    size_t pos = 0;
    uint32_t start = millis();
    
    while (pos < 4) {
        if (SerialAgent.available()) {
            header[pos++] = SerialAgent.read();
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
        if (SerialAgent.available()) {
            data[pos++] = SerialAgent.read();
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
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    Serial.begin(115200);
    SerialAgent.begin(115200, SERIAL_8N1, 16, 17);

    delay(1000);
    Serial.println("ESP32 SSH Agent starting...");
    Serial.println("Version: 1.0.0");

    if (!loadKeyFromStorage()) {
        Serial.println("No key found, generating new Ed25519 key...");
        if (generateEd25519Key()) {
            Serial.println("Key generation successful");
        } else {
            Serial.println("Key generation failed!");
        }
    }

    Serial.println("SSH Agent ready");
    printPublicKey();
    Serial.println("Waiting for agent requests...");
    
    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    if (SerialAgent.available() > 0) {
        digitalWrite(LED_PIN, LOW);
        processAgentRequest();
        digitalWrite(LED_PIN, HIGH);
    }
    delay(10);
}
