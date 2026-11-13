// Configuración mínima de mbedTLS para verificar_gcm_mbedtls.cpp: solo AES y
// GCM, en C portable (sin el hardware del ESP32).
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_CIPHER_MODE_CFB
#define MBEDTLS_CIPHER_MODE_OFB
#define MBEDTLS_CIPHER_MODE_XTS
#define MBEDTLS_CIPHER_PADDING_PKCS7
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_AES_ROM_TABLES
