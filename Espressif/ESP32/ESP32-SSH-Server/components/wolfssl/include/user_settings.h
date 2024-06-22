/* user_settings.h (this is a special file specifically for ESP SSH to UART)
 *
 *
 * Copyright (C) 2014-2024 wolfSSL Inc.
 *
 * This file is part of wolfSSH.
 *
 * wolfSSH is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfSSH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wolfSSH.  If not, see <http://www.gnu.org/licenses/>.
 */

/* NOTICE
 *
 * This is a wolfSSL user settings file for Espressif ESP32-[n] and ESP8266.
 *
 * This file should *NOT* be explicitly included in any source files.
 *
 * Please instead include this settings.h file which will reference this file:
 *
 *            #include <wolfssl/wolfcrypt/settings.h>
 *
 * Thank you for using wolfSSL.
 *
 */
#include "sdkconfig.h" /* essential to chip set detection */

#undef WOLFSSL_ESPIDF
#undef WOLFSSL_ESP32
#undef WOLFSSL_ESPWROOM32SE
#undef WOLFSSL_ESP32
#undef WOLFSSL_ESP8266

#define WOLFSSL_ESPIDF

/* The Espressif sdkconfig will have chipset info.
**
** Possible values:
**
**   CONFIG_IDF_TARGET_ESP32
**   CONFIG_IDF_TARGET_ESP32S2
**   CONFIG_IDF_TARGET_ESP32S3
**   CONFIG_IDF_TARGET_ESP32C3
**   CONFIG_IDF_TARGET_ESP32C6
*/
#define ESP_ENABLE_WOLFSSH
/* Optionally enable some wolfSSH settings */

#ifdef ESP_ENABLE_WOLFSSH
    /* The default SSH Windows size is massive for an embedded target. Limit it: */
    #define DEFAULT_WINDOW_SZ 2000

    /* These may be defined in cmake for other examples: */
    #undef  WOLFSSH_TERM
    #define WOLFSSH_TERM

    #undef  DEBUG_WOLFSSH
    /* Optionally turn on wolfSSH debugging messages */
    /* #define DEBUG_WOLFSSH */

    #undef  WOLFSSL_KEY_GEN
    #define WOLFSSL_KEY_GEN

    #undef  WOLFSSL_PTHREADS
    #define WOLFSSL_PTHREADS

    #define WOLFSSH_TEST_SERVER

    /* optional threading test code can be enabled: */
    /* #define WOLFSSH_TEST_THREADING */

    /* The "no file system" for wolfSSL */
    #undef  NO_FILESYSTEM
    #define NO_FILESYSTEM

    /* The "no file system" for wolfSSH */
    #undef  WOLFSSH_NO_FILESYSTEM
    #define WOLFSSH_NO_FILESYSTEM

    /* WOLFSSL_NONBLOCK is a value assigned to threadCtx->nonBlock
    * and should be a value 1 or 0
    */
    #define WOLFSSL_NONBLOCK 1

#endif /* ESP_ENABLE_WOLFSSH */

/* when you want to use SINGLE THREAD. Note Default ESP-IDF is FreeRTOS */
/* TODO: previously saw depth mismatch when disabling SINGLE_THREADED ?
 * (but putty cannot connect: server host key invalid when defined)
 *  fails for both `WOLFSSL_NONBLOCK 0` or `WOLFSSL_NONBLOCK 1` */
#define SINGLE_THREADED

/* Need to increase pthread stack size when using WOLFSSH_TEST_THREADING */
/* Minimum defined size should be 20096, but not in SINGLE_THREADED */
/* #define WOLFSSH_TEST_THREADING */

#if defined(WOLFSSH_TEST_THREADING) && defined(SINGLE_THREADED)
    #error "Cannot enable both WOLFSSH_TEST_THREADING and SINGLE_THREADED"
#endif

/*
 * choose ONE of these Espressif chips to define:
 *
 * WOLFSSL_ESP32
 * WOLFSSL_ESPWROOM32SE
 * WOLFSSL_ESP8266
 */

#define WOLFSSL_ESP32

/* optionally turn off SHA512/224 SHA512/256 */
/* #define WOLFSSL_NOSHA512_224 */
/* #define WOLFSSL_NOSHA512_256 */

/* When you don't want to use the old SHA */
/* #define NO_SHA */
/* #define NO_OLD_TLS */

#define BENCH_EMBEDDED

#define NO_OLD_TLS

/* TLS 1.3 */
/* #define MY_USE_TLS13 */
#ifdef MY_USE_TLS13
    #define WOLFSSL_TLS13
    #define HAVE_TLS_EXTENSIONS
    #define WC_RSA_PSS
    #define HAVE_SUPPORTED_CURVES
    #define HAVE_FFDHE_2048

    /* or one of the other supported FFDHE sizes [2048, 3072, 4096, 6144, 8192] */
    /* #define HAVE_FFDHE_8192 */

#endif
#define HAVE_HKDF
#define HAVE_AEAD

/*  note "file system": "load keys and certificate from files" vs NO_FILESYSTEM
 *  and "access an actual file system via SFTP/SCP" vs WOLFSSH_NO_FILESYSTEM
 *  we'll typically have neither on an embedded device:
 */
#define NO_FILESYSTEM

#define HAVE_AESGCM

#define WOLFSSL_RIPEMD
/* when you want to use SHA224 */
/* #define WOLFSSL_SHA224      */

/* when you want to use SHA384, which is disabled by default */
/* #define WOLFSSL_SHA384 */

/* when you want to use SHA512. which is disabled by default */
/* #define WOLFSSL_SHA512 */

/* #define WOLFSSL_SHA3 */

#define MY_USE_ECC 1
#define MY_USE_RSA 0

/* Define DEMO_SERVER_384 to use ecdsa-sha2-nistp384
 * if DEMO_SERVER_384 is not defined then ecdsa-sha2-nistp256 is default. */
/* #define DEMO_SERVER_384 */

/* We can use either or both ECC and RSA, but must use at least one. */
/* RSA not yet implemented for SSH to UART demo */
#if MY_USE_ECC || MY_USE_RSA
    #if MY_USE_ECC
        /* ---- ECDSA / ECC ---- */
        #define HAVE_ECC
        #define HAVE_CURVE25519
        #define HAVE_ED25519
        /* ED25519 requires SHA512 */
        #undef  WOLFSSL_SHA512
        #define WOLFSSL_SHA512

        /*
        #define HAVE_ECC384
        #define CURVE25519_SMALL
        */

        #ifdef DEMO_SERVER_384
            /* we'll connect with ecdsa-sha2-nistp384  */
            /* confirmed working with both HW / SW ESP32 wolfcrypt */

            /* First, we need to force off the [ECDH/ECDSA]'NISTP256 items */
            #define WOLFSSH_NO_ECDH_SHA2_NISTP256
            #define WOLFSSH_NO_ECDSA_SHA2_NISTP256
            /* SHA256 still needed */

            #define WOLFSSL_SHA384
            #define HAVE_ECC384

            /* TODO confirm these are needed: */
            #define WOLFSSL_SHA512
            #define HAVE_ECC521

        #else
            /* default ecdsa-sha2-nistp256 needs no special settings. */
            #ifndef WOLFSSL_FULL_WOLFSSH_SUPPORT
                #include <version.h>
                #ifdef LIBWOLFSSL_VERSION_HEX
                    #if LIBWOLFSSL_VERSION_HEX < 0x05007000
                        /* wolfSSL 5.6.6 SHA256 HW not supported with wolfSSH
                         * prior to 5.7.0, so disable it: */
                       #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA256
                    #endif
                #endif
            #endif
        #endif
    #else
        /* Warning: only ECC implemented for SSH UART demo at this time */
        #define WOLFSSH_NO_ECC
        /* WOLFSSH_NO_ECDSA is typically defined automatically,
         * here for clarity: */
        #define WOLFSSH_NO_ECDSA
    #endif

    #if MY_USE_RSA
        /* Warning: RSA not implemented for SSH UART demo at this time */

        /* ---- RSA ----- */
        /* #define RSA_LOW_MEM */

        /* DH disabled by default, needed if ECDSA/ECC also turned off */
        #define HAVE_DH

        /* WC_RSA_PSS */
    #else
        #undef  WOLFSSH_NO_RSA
        #define WOLFSSH_NO_RSA

        #undef  NO_RSA
        #define NO_RSA
    #endif
#else
    #error "Either RSA or ECC must be enabled"
#endif

/* Optional OPENSSL compatibility */
/* #define OPENSSL_EXTRA */

/* when you want to use pkcs7 */
/* #define HAVE_PKCS7 */

#if defined(HAVE_PKCS7)
    #define HAVE_AES_KEYWRAP
    #define HAVE_X963_KDF
    #define WOLFSSL_AES_DIRECT
#endif

/* when you want to use aes counter mode */
/* #define WOLFSSL_AES_DIRECT */
/* #define WOLFSSL_AES_COUNTER */

/* debug options */
/* optionally turn on debugging to see wolfSSL connection steps, etc: */
/* #define DEBUG_WOLFSSL */

/* #define WOLFSSL_ESP32_CRYPT_DEBUG */
/* #define WOLFSSL_ATECC508A_DEBUG          */

/* date/time                               */
/* if it cannot adjust time in the device, */
/* enable macro below                      */
/* #define NO_ASN_TIME */
/* #define XTIME time */

/* adjust wait-timeout count if you see timeout in RSA HW acceleration */
#define ESP_RSA_TIMEOUT_CNT    0x349F00


/* USE_FAST_MATH is default */
#define USE_FAST_MATH

/*****      Use SP_MATH      *****/
/* #undef USE_FAST_MATH          */
/* #define SP_MATH               */
/* #define WOLFSSL_SP_MATH_ALL   */
/* #define WOLFSSL_SP_RISCV32    */

/***** Use Integer Heap Math *****/
/* #undef USE_FAST_MATH          */
/* #define USE_INTEGER_HEAP_MATH */

#define WOLFSSL_SMALL_STACK

/* The ESP32 has some detailed statup information available:*/
#define HAVE_VERSION_EXTENDED_INFO
/* #define HAVE_WC_INTROSPECTION */

/* optional SM4 Ciphers. See https://github.com/wolfSSL/wolfsm */
/*
#define WOLFSSL_SM2
#define WOLFSSL_SM3
#define WOLFSSL_SM4
*/

#if defined(WOLFSSL_SM2) || defined(WOLFSSL_SM3) || defined(WOLFSSL_SM4)
    /* SM settings, possible cipher suites:

        TLS13-AES128-GCM-SHA256
        TLS13-CHACHA20-POLY1305-SHA256
        TLS13-SM4-GCM-SM3
        TLS13-SM4-CCM-SM3

    #define WOLFSSL_ESP32_CIPHER_SUITE "TLS13-SM4-GCM-SM3"
    #define WOLFSSL_ESP32_CIPHER_SUITE "TLS13-SM4-CCM-SM3"
    #define WOLFSSL_ESP32_CIPHER_SUITE "ECDHE-ECDSA-SM4-CBC-SM3"
    #define WOLFSSL_ESP32_CIPHER_SUITE "ECDHE-ECDSA-SM4-GCM-SM3"
    #define WOLFSSL_ESP32_CIPHER_SUITE "ECDHE-ECDSA-SM4-CCM-SM3"
    #define WOLFSSL_ESP32_CIPHER_SUITE "TLS13-SM4-GCM-SM3:" \
                                       "TLS13-SM4-CCM-SM3:"
    */

    /* WOLFSSL_BASE16 required for WOLFSSL_SM2 */
    #undef  WOLFSSL_BASE16
    #define WOLFSSL_BASE16

    #undef  WOLFSSL_SM4_ECB
    #define WOLFSSL_SM4_ECB

    #undef  WOLFSSL_SM4_CBC
    #define WOLFSSL_SM4_CBC

    #undef  WOLFSSL_SM4_CTR
    #define WOLFSSL_SM4_CTR

    #undef  WOLFSSL_SM4_GCM
    #define WOLFSSL_SM4_GCM

    #undef  WOLFSSL_SM4_CCM
    #define WOLFSSL_SM4_CCM

    #define HAVE_POLY1305
    #define HAVE_CHACHA

    #undef  HAVE_AESGCM
    #define HAVE_AESGCM
#else
    /* default settings */
#endif

/* esp32-wroom-32se specific definition */
#if defined(WOLFSSL_ESPWROOM32SE)
    #define WOLFSSL_ATECC508A
    #define HAVE_PK_CALLBACKS
    /* when you want to use a custom slot allocation for ATECC608A */
    /* unless your configuration is unusual, you can use default   */
    /* implementation.                                             */
    /* #define CUSTOM_SLOT_ALLOCATION                              */
#endif

/* Default is HW enabled unless turned off.
** Uncomment these lines to force SW instead of HW acceleration */
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(WOLFSSL_ESPWROOM32SE)
    /* wolfSSL HW Acceleration supported on ESP32. Uncomment to disable: */
    /*  #define NO_ESP32_CRYPT                 */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH    */

    /*****   Optionally turn off individual SHA:   *****/
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA        */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA224     */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA256     */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA384     */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA512     */

    /*  #define NO_WOLFSSL_ESP32_CRYPT_AES             */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI         */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MP_MUL  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MULMOD  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_EXPTMOD */

    /*  These are defined automatically in esp32-crypt.h, here for clarity:  */
    /* no SHA224 HW on ESP32  */
    #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA224

    /* Define USE_FAST_MATH and SMALL_STACK                        */
    #define ESP32_USE_RSA_PRIMITIVE

    /* threshold for performance adjustment for HW primitive use   */
    /* X bits of G^X mod P greater than                            */
    #define EPS_RSA_EXPT_XBTIS           32

    /* X and Y of X * Y mod P greater than                         */
    #undef  ESP_RSA_MULM_BITS
    #define ESP_RSA_MULM_BITS            16

    /***** END CONFIG_IDF_TARGET_ESP32 *****/

#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    /* wolfSSL HW Acceleration supported on ESP32-S2. Uncomment to disable: */
    /*  #define NO_ESP32_CRYPT                 */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH    */
    /* Note: There's no AES192 HW on the ESP32-S2; falls back to SW */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_AES     */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MP_MUL  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MULMOD  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_EXPTMOD */
    /***** END CONFIG_IDF_TARGET_ESP32S2 *****/

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    /* wolfSSL HW Acceleration supported on ESP32-S3. Uncomment to disable: */
    /*  #define NO_ESP32_CRYPT                         */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH            */
    /* Note: There's no AES192 HW on the ESP32-S3; falls back to SW */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_AES             */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI         */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MP_MUL  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MULMOD  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_EXPTMOD */
    /***** END CONFIG_IDF_TARGET_ESP32S3 *****/

#elif defined(CONFIG_IDF_TARGET_ESP32C2) || \
      defined(CONFIG_IDF_TARGET_ESP8684)
    /* ESP8684 is essentially ESP32-C2 chip + flash embedded together in a
     * single QFN 4x4 mm package. Out of released documentation, Technical
     * Reference Manual as well as ESP-IDF Programming Guide is applicable
     * to both ESP32-C2 and ESP8684.
     *
     * See: https://www.esp32.com/viewtopic.php?f=5&t=27926#:~:text=ESP8684%20is%20essentially%20ESP32%2DC2,both%20ESP32%2DC2%20and%20ESP8684. */

    /* wolfSSL HW Acceleration supported on ESP32-C2. Uncomment to disable: */
    /*  #define NO_ESP32_CRYPT                 */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH    */ /* to disable all SHA HW   */

    /* These are defined automatically in esp32-crypt.h, here for clarity    */
    #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA384    /* no SHA384 HW on C2  */
    #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA512    /* no SHA512 HW on C2  */

    /* There's no AES or RSA/Math accelerator on the ESP32-C2
     * Auto defined with NO_WOLFSSL_ESP32_CRYPT_RSA_PRI, for clarity: */
    #define NO_WOLFSSL_ESP32_CRYPT_AES
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MP_MUL
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MULMOD
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_EXPTMOD
    /***** END CONFIG_IDF_TARGET_ESP32C2 *****/

#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    /* wolfSSL HW Acceleration supported on ESP32-C3. Uncomment to disable: */

    /*  #define NO_ESP32_CRYPT                 */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH    */ /* to disable all SHA HW   */

    /* These are defined automatically in esp32-crypt.h, here for clarity:  */
    #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA384    /* no SHA384 HW on C6  */
    #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA512    /* no SHA512 HW on C6  */

    /*  #define NO_WOLFSSL_ESP32_CRYPT_AES             */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI         */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MP_MUL  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MULMOD  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_EXPTMOD */
    /***** END CONFIG_IDF_TARGET_ESP32C3 *****/

#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    /* wolfSSL HW Acceleration supported on ESP32-C6. Uncomment to disable: */

    /*  #define NO_ESP32_CRYPT                 */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_HASH    */
    /*  These are defined automatically in esp32-crypt.h, here for clarity:  */
    #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA384    /* no SHA384 HW on C6  */
    #define NO_WOLFSSL_ESP32_CRYPT_HASH_SHA512    /* no SHA512 HW on C6  */

    /*  #define NO_WOLFSSL_ESP32_CRYPT_AES             */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI         */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MP_MUL  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MULMOD  */
    /*  #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_EXPTMOD */
    /***** END CONFIG_IDF_TARGET_ESP32C6 *****/

#elif defined(CONFIG_IDF_TARGET_ESP32H2)
    /*  wolfSSL Hardware Acceleration not yet implemented */
    #define NO_ESP32_CRYPT
    #define NO_WOLFSSL_ESP32_CRYPT_HASH
    #define NO_WOLFSSL_ESP32_CRYPT_AES
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI
    /***** END CONFIG_IDF_TARGET_ESP32H2 *****/

#elif defined(CONFIG_IDF_TARGET_ESP8266)
    /*  TODO: Revisit ESP8266 */
    #define NO_ESP32_CRYPT
    #define NO_WOLFSSL_ESP32_CRYPT_HASH
    #define NO_WOLFSSL_ESP32_CRYPT_AES
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI
    /***** END CONFIG_IDF_TARGET_ESP266 *****/

#elif defined(CONFIG_IDF_TARGET_ESP8684)
    /*  There's no Hardware Acceleration available on ESP8684 */
    #define NO_ESP32_CRYPT
    #define NO_WOLFSSL_ESP32_CRYPT_HASH
    #define NO_WOLFSSL_ESP32_CRYPT_AES
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI
    /***** END CONFIG_IDF_TARGET_ESP8684 *****/

#else
    /* Anything else encountered, disable HW accleration */
    #define NO_ESP32_CRYPT
    #define NO_WOLFSSL_ESP32_CRYPT_HASH
    #define NO_WOLFSSL_ESP32_CRYPT_AES
    #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI
#endif /* CONFIG_IDF_TARGET Check */

/* Debug options:

#define ESP_VERIFY_MEMBLOCK
#define DEBUG_WOLFSSL
#define DEBUG_WOLFSSL_VERBOSE
#define DEBUG_WOLFSSL_SHA_MUTEX
#define WOLFSSL_ESP32_CRYPT_DEBUG
#define WOLFSSL_ESP32_CRYPT_HASH_SHA224_DEBUG
#define NO_RECOVER_SOFTWARE_CALC
#define WOLFSSL_TEST_STRAY 1
#define USE_ESP_DPORT_ACCESS_READ_BUFFER
#define WOLFSSL_ESP32_HW_LOCK_DEBUG
#define WOLFSSL_DEBUG_ESP_RSA_MULM_BITS
#define ESP_DISABLE_HW_TASK_LOCK
*/

/* Pause in a loop rather than exit. */
#define WOLFSSL_ESPIDF_ERROR_PAUSE

/* #define WOLFSSL_HW_METRICS */

/* for test.c */
/* #define HASH_SIZE_LIMIT */

/* Optionally turn off HW math checks */
/* #define NO_HW_MATH_TEST */

/* Optionally include alternate HW test library: alt_hw_test.h */
/* When enabling, the ./components/wolfssl/CMakeLists.txt file
 * will need the name of the library in the idf_component_register
 * for the PRIV_REQUIRES list. */
/* #define INCLUDE_ALT_HW_TEST */

/* optionally turn off individual math HW acceleration features */

/* Turn off Large Number ESP32 HW Multiplication:
** [Z = X * Y] in esp_mp_mul()                                  */
/* #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MP_MUL                */

/* Turn off Large Number ESP32 HW Modular Exponentiation:
** [Z = X^Y mod M] in esp_mp_exptmod()                          */
/* #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_EXPTMOD               */

/* Turn off Large Number ESP32 HW Modular Multiplication
** [Z = X * Y mod M] in esp_mp_mulmod()                         */
/* #define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI_MULMOD                */

#define WOLFSSL_PUBLIC_MP /* used by benchmark */

/* when turning on ECC508 / ECC608 support
#define WOLFSSL_ESPWROOM32SE
#define HAVE_PK_CALLBACKS
#define WOLFSSL_ATECC508A
#define ATCA_WOLFSSL
*/

/* optional SM4 Ciphers. See https://github.com/wolfSSL/wolfsm
#define WOLFSSL_SM2
#define WOLFSSL_SM3
#define WOLFSSL_SM4
*/

#if defined(WOLFSSL_SM2) || defined(WOLFSSL_SM3) || defined(WOLFSSL_SM4)
    #include <wolfssl/certs_test_sm.h>
    #define CTX_CA_CERT          root_sm2
    #define CTX_CA_CERT_SIZE     sizeof_root_sm2
    #define CTX_CA_CERT_TYPE     WOLFSSL_FILETYPE_PEM
    #define CTX_SERVER_CERT      server_sm2
    #define CTX_SERVER_CERT_SIZE sizeof_server_sm2
    #define CTX_SERVER_CERT_TYPE WOLFSSL_FILETYPE_PEM
    #define CTX_SERVER_KEY       server_sm2_priv
    #define CTX_SERVER_KEY_SIZE  sizeof_server_sm2_priv
    #define CTX_SERVER_KEY_TYPE  WOLFSSL_FILETYPE_PEM

    #undef  WOLFSSL_BASE16
    #define WOLFSSL_BASE16
#else
    #define USE_CERT_BUFFERS_2048
    #define USE_CERT_BUFFERS_256
    #define CTX_CA_CERT          ca_cert_der_2048
    #define CTX_CA_CERT_SIZE     sizeof_ca_cert_der_2048
    #define CTX_CA_CERT_TYPE     WOLFSSL_FILETYPE_ASN1
    #define CTX_SERVER_CERT      server_cert_der_2048
    #define CTX_SERVER_CERT_SIZE sizeof_server_cert_der_2048
    #define CTX_SERVER_CERT_TYPE WOLFSSL_FILETYPE_ASN1
    #define CTX_SERVER_KEY       server_key_der_2048
    #define CTX_SERVER_KEY_SIZE  sizeof_server_key_der_2048
    #define CTX_SERVER_KEY_TYPE  WOLFSSL_FILETYPE_ASN1
#endif

/* See settings.h for some of the possible hardening options:
 *
 *  #define NO_ESPIDF_DEFAULT
 *  #define WC_NO_CACHE_RESISTANT
 *  #define WC_AES_BITSLICED
 *  #define HAVE_AES_ECB
 *  #define HAVE_AES_DIRECT
 */

/* Custom stack check (experimental) */
#define WOLFSSL_STACK_CHECK
