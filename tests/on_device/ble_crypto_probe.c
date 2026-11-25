/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Optional diagnostic for ESP-IDF 5.5.4's NimBLE crypto backend on real P4.
 * These internal symbols are deliberately confined to this opt-in test.
 * The constants are public Bluetooth LE Secure Connections P-256 sample data.
 * No generated private key, session secret or peer key is logged.
 */
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

int __real_ble_sm_alg_gen_key_pair(uint8_t *pub, uint8_t *priv);
int __real_ble_sm_alg_gen_dhkey(const uint8_t *x, const uint8_t *y,
                               const uint8_t *priv, uint8_t *out);
int __real_ble_hs_hci_util_rand(void *dst, int len);

static const char *TAG = "ble_crypto_probe";
static const uint8_t priv_a[32] = {
    0x3f,0x49,0xf6,0xd4,0xa3,0xc5,0x5f,0x38,0x74,0xc9,0xb3,0xe3,0xd2,0x10,0x3f,0x50,
    0x4a,0xff,0x60,0x7b,0xeb,0x40,0xb7,0x99,0x58,0x99,0xb8,0xa6,0xcd,0x3c,0x1a,0xbd};
static const uint8_t pub_b[64] = {
    0x1e,0xa1,0xf0,0xf0,0x1f,0xaf,0x1d,0x96,0x09,0x59,0x22,0x84,0xf1,0x9e,0x4c,0x00,
    0x47,0xb5,0x8a,0xfd,0x86,0x15,0xa6,0x9f,0x55,0x90,0x77,0xb2,0x2f,0xaa,0xa1,0x90,
    0x4c,0x55,0xf3,0x3e,0x42,0x9d,0xad,0x37,0x73,0x56,0x70,0x3a,0x9a,0xb8,0x51,0x60,
    0x47,0x2d,0x11,0x30,0xe2,0x8e,0x36,0x76,0x5f,0x89,0xaf,0xf9,0x15,0xb1,0x21,0x4a};
static const uint8_t expected[32] = {
    0xec,0x02,0x34,0xa3,0x57,0xc8,0xad,0x05,0x34,0x10,0x10,0xa6,0x0a,0x39,0x7d,0x9b,
    0x99,0x79,0x6b,0x13,0xb4,0xf8,0x66,0xf1,0x86,0x8d,0x34,0xf3,0x73,0xbf,0xa6,0x98};
static void reverse(uint8_t *dst, const uint8_t *src) {
    for (int i = 0; i < 32; ++i) dst[i] = src[31 - i];
}
int __wrap_ble_hs_hci_util_rand(void *dst, int len) {
    int rc = __real_ble_hs_hci_util_rand(dst, len);
    if (rc) ESP_LOGW(TAG, "controller RNG: rc=%d len=%d", rc, len);
    return rc;
}
int __wrap_ble_sm_alg_gen_key_pair(uint8_t *pub, uint8_t *priv) {
    int rc = __real_ble_sm_alg_gen_key_pair(pub, priv);
    ESP_LOGI(TAG, "NimBLE key generation: rc=%d", rc);
    return rc;
}
int __wrap_ble_sm_alg_gen_dhkey(const uint8_t *x, const uint8_t *y,
                               const uint8_t *priv, uint8_t *out) {
    int rc = __real_ble_sm_alg_gen_dhkey(x, y, priv, out);
    ESP_LOGI(TAG, "NimBLE DHKey: rc=%d", rc);
    return rc;
}
void ble_crypto_probe(void) {
    uint8_t x[32], y[32], k[32], out[32] = {0}, want[32];
    reverse(x, pub_b); reverse(y, pub_b + 32);
    reverse(k, priv_a); reverse(want, expected);
    int rc = __wrap_ble_sm_alg_gen_dhkey(x, y, k, out);
    ESP_LOGI(TAG, "SC known-vector: rc=%d match=%d", rc, !rc && !memcmp(out, want, 32));
    uint8_t generated_pub[64], generated_priv[32];
    rc = __wrap_ble_sm_alg_gen_key_pair(generated_pub, generated_priv);
    ESP_LOGI(TAG, "SC random key generation: rc=%d", rc);
    volatile uint8_t *secret = generated_priv;
    for (unsigned i = 0; i < sizeof generated_priv; ++i) secret[i] = 0;
}
