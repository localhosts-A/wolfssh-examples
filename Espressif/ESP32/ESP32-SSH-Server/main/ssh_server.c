/* ssh_server.c
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

/* wolfSSL */
/* Important: make sure settings.h appears before any other wolfSSL headers */
#include <wolfssl/wolfcrypt/settings.h>
#ifndef WOLFSSL_USER_SETTINGS
    #error "WOLFSSL_USER_SETTINGS should have been defined in project cmake"
#endif
#include <wolfssl/wolfcrypt/port/Espressif/esp32-crypt.h>
#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/logging.h>
#ifndef WOLFSSL_ESPIDF
    #error "Problem with wolfSSL user_settings."
    #error "Check [project]/components/wolfssl/include"
#endif

/* wolfSSL */
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/logging.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/wolfcrypt/sha256.h>

/* wolfSSH */
#include <wolfssh/ssh.h>
#include <wolfssh/test.h>

/* Espressif */
#include <esp_log.h>

/* Project */
#include "ssh_server_config.h"
#include "ssh_server.h"
#include "tx_rx_buffer.h"


/* note our actual buffer is used by RTOS threads, and eventually interrupts */
static volatile byte sshStreamTransmitBufferArray[EXT_TX_BUF_MAX_SZ];
static volatile byte sshStreamReceiveBufferArray[EXT_RX_BUF_MAX_SZ];

static const char* TAG = "ssh_server";

static const char samplePasswordBuffer[] =
    "jill:upthehill\n"
    "jack:fetchapail\n";


static const char samplePublicKeyEccBuffer[] =
    "ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAA"
    "BBBNkI5JTP6D0lF42tbxX19cE87hztUS6FSDoGvPfiU0CgeNSbI+aFdKIzTP5CQEJSvm25"
    "qUzgDtH7oyaQROUnNvk= hansel\n"
    "ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAA"
    "BBBKAtH8cqaDbtJFjtviLobHBmjCtG56DMkP6A4M2H9zX2/YCg1h9bYS7WHd9UQDwXO1Hh"
    "IZzRYecXh7SG9P4GhRY= gretel\n"
    "ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAA"
    "BBBIuaGC8Y+GsIwhuw3tGbqqp+KtuyCewgFfU/prGo29G1EonNdFkWGsB9383pOLlwJqTG"
    "UpQ/8B0KfWZKbv8bpcM= gojimmypi@DESKTOP\n";


static const char samplePublicKeyRsaBuffer[] =
    "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQC9P3ZFowOsONXHD5MwWiCciXytBRZGho"
    "MNiisWSgUs5HdHcACuHYPi2W6Z1PBFmBWT9odOrGRjoZXJfDDoPi+j8SSfDGsc/hsCmc3G"
    "p2yEhUZUEkDhtOXyqjns1ickC9Gh4u80aSVtwHRnJZh9xPhSq5tLOhId4eP61s+a5pwjTj"
    "nEhBaIPUJO2C/M0pFnnbZxKgJlX7t1Doy7h5eXxviymOIvaCZKU+x5OopfzM/wFkey0EPW"
    "NmzI5y/+pzU5afsdeEWdiQDIQc80H6Pz8fsoFPvYSG+s4/wz0duu7yeeV1Ypoho65Zr+pE"
    "nIf7dO0B8EblgWt+ud+JI8wrAhfE4x hansel\n"
    "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQCqDwRVTRVk/wjPhoo66+Mztrc31KsxDZ"
    "+kAV0139PHQ+wsueNpba6jNn5o6mUTEOrxrz0LMsDJOBM7CmG0983kF4gRIihECpQ0rcjO"
    "P6BSfbVTE9mfIK5IsUiZGd8SoE9kSV2pJ2FvZeBQENoAxEFk0zZL9tchPS+OCUGbK4SDjz"
    "uNZl/30Mczs73N3MBzi6J1oPo7sFlqzB6ecBjK2Kpjus4Y1rYFphJnUxtKvB0s+hoaadru"
    "biE57dK6BrH5iZwVLTQKux31uCJLPhiktI3iLbdlGZEctJkTasfVSsUizwVIyRjhVKmbdI"
    "RGwkU38D043AR1h0mUoGCPIKuqcFMf gretel\n";

/* #define SSH_SERVER_PROFILE */

#ifdef SSH_SERVER_PROFILE
    static int MaxSeenRxSize = 0;
    static int MaxSeenTxSize = 0;
#endif

/* Show HW lockdepth. Oddities here are often a symptom of stack overflow. */
#if !defined(NO_WOLFSSL_ESP32_CRYPT_HASH) && \
     defined(WOLFSSL_ESP32_HW_LOCK_DEBUG)
    #define SSH_SERVER_DEBUG_LOCKDEPTH
#endif
/* Map user names to passwords */
/* Use arrays for username and p. The password or public key can
 * be hashed and the hash stored here. Then I won't need the type. */
typedef struct PwMap {
    byte type;
    byte username[32];
    word32 usernameSz;
    byte p[WC_SHA256_DIGEST_SIZE];
    struct PwMap* next;
} PwMap;


typedef struct PwMapList {
    PwMap* head;
} PwMapList;


typedef struct {
    WOLFSSH* ssh;
    int fd;
    word32 id;
    char nonBlock;
} thread_ctx_t;


/* find a byte character [str] of length [bufSz] within [buf];
 * returns byte position if found, otherwise zero
 */
static byte find_char(const byte* str, const byte* buf, word32 bufSz)
{
    int ret = 0;
    const byte* cur;
    while (bufSz && (ret == 0) && (ret < 255)) {
        cur = str;
        while (*cur != '\0') {
            if (*cur == *buf) {
                ret = *cur;
            }
            cur++;
        }
        buf++;
        bufSz--;
    }
    if (ret == 255) {
        ESP_LOGE(TAG, "find_char not found in 254 chars");
    }

    return ret;
}


static int dump_stats(thread_ctx_t* ctx)
{
    ESP_LOGE(TAG,"dumpstats");
    char stats[1024];
    word32 statsSz;
    word32 txCount, rxCount, seq, peerSeq;

    wolfSSH_GetStats(ctx->ssh, &txCount, &rxCount, &seq, &peerSeq);

    WSNPRINTF(stats,
        sizeof(stats),
        "Statistics for Thread #%u:\r\n"
        "  txCount = %u\r\n  rxCount = %u\r\n"
        "  seq = %u\r\n  peerSeq = %u\r\n",
        ctx->id,
        txCount,
        rxCount,
        seq,
        peerSeq);
    statsSz = (word32)strlen(stats);

    fprintf(stderr, "%s", stats);
    return wolfSSH_stream_send(ctx->ssh, (byte*)stats, statsSz);
}

static int NonBlockSSH_accept(WOLFSSH* ssh)
{
    int ret;
    int error;
    int sockfd;
    int select_ret = 0;
    int max_wait = 100;
    ESP_LOGI(TAG,"Start NonBlockSSH_accept");

    ret = wolfSSH_accept(ssh);
    error = wolfSSH_get_error(ssh);
    sockfd = (int)wolfSSH_get_fd(ssh);

    while (ret != WS_SUCCESS &&
            (error == WS_WANT_READ || error == WS_WANT_WRITE)) {

        max_wait--;
        if (max_wait < 0) {
            error = WS_FATAL_ERROR;
        }
#if (0)
        /* Some optional debugging verbosity: */
        if (error == WS_WANT_READ)
            ESP_LOGE(TAG,"... client would read block\n");
        else if (error == WS_WANT_WRITE)
            ESP_LOGE(TAG,"... client would write block\n");
#endif
        select_ret = tcp_select(sockfd, 1);
        if (select_ret == WS_SELECT_RECV_READY  ||
            select_ret == WS_SELECT_ERROR_READY ||
            error == WS_WANT_WRITE) {
            ret = wolfSSH_accept(ssh);
            error = wolfSSH_get_error(ssh);
        }
        else if (select_ret == WS_SELECT_TIMEOUT)
            error = WS_WANT_READ;
        else
            error = WS_FATAL_ERROR;

        /* RTOS yield */
        vTaskDelay(100 / portTICK_PERIOD_MS);
        #ifdef SSH_SERVER_WDT_RESET
        {
            esp_task_wdt_reset();
        }
        #endif
    }
    ESP_LOGI(TAG,"Exit NonBlockSSH_accept");

    return ret;
}


/*
 * server_worker is the main thread for a given SSH connection
 */
static THREAD_RETURN WOLFSSH_THREAD server_worker(void* vArgs)
{
    int ret;

    /* we'll create a local instance of threadCtx since it will be
     * handed off to potentially multiple separate threads
     */
    thread_ctx_t* threadCtx = (thread_ctx_t*)vArgs;

#if defined(WOLFSSH_SCP) && defined(NO_FILESYSTEM)
    ScpBuffer scpBufferRecv, scpBufferSend;
    byte fileBuffer[49000];
    byte fileTmp[] = "wolfSSH SCP buffer file";

    WMEMSET(&scpBufferRecv, 0, sizeof(ScpBuffer));
    scpBufferRecv.buffer   = fileBuffer;
    scpBufferRecv.bufferSz = sizeof(fileBuffer);
    wolfSSH_SetScpRecvCtx(threadCtx->ssh, (void*)&scpBufferRecv);

    /* make buffer file to send if asked */
    WMEMSET(&scpBufferSend, 0, sizeof(ScpBuffer));
    WMEMCPY(scpBufferSend.name, "test.txt", sizeof("test.txt"));
    scpBufferSend.nameSz   = WSTRLEN("test.txt");
    scpBufferSend.buffer   = fileTmp;
    scpBufferSend.bufferSz = sizeof(fileBuffer);
    scpBufferSend.fileSz   = sizeof(fileTmp);
    scpBufferSend.mode     = 0x1A4;
    wolfSSH_SetScpSendCtx(threadCtx->ssh, (void*)&scpBufferSend);
#endif

    if (!threadCtx->nonBlock)
        ret = wolfSSH_accept(threadCtx->ssh);
    else
        ret = NonBlockSSH_accept(threadCtx->ssh);

    if (ret == WS_SUCCESS) {
        byte* this_rx_buf = NULL;

        int backlogSz = 0, rxSz, txSz, stop = 0, txSum;

        init_tx_rx_buffer(TXD_PIN, RXD_PIN);

        /*
         * we'll stay in this loop then entire time this worker thread has
         * a valid SSH connection open
         */
        do {
            /* int show_msg = 0;
             * TODO optionally disable echo of text to USB port */
            int has_err = 0;
            this_rx_buf = (byte*)&sshStreamReceiveBufferArray;
            vTaskDelay(10);

            if (!stop) {
                do {
                    int error = 0;
                    socklen_t len = sizeof(error);
                    int retval = getsockopt(threadCtx->fd,
                                           SOL_SOCKET, SO_ERROR, &error, &len);

                    if (retval != 0) {
                        /* if we can't even call getsockopt, give up */
                        stop = 1;
                        ESP_LOGE(TAG,"ERROR: getsockopt "
                                     "unable to query socket fd!");
                    }
                    if (error != 0) {
                        /* socket has a non zero error status */
                        ESP_LOGE(TAG,"ERROR: getsockopt "
                                     "returned error socket fd!");
                        stop = 1;
                    }

                    /* when polling, debugging can be verbose, turn it off */
                    #ifdef DEBUG_WOLFSSH
                        ESP_LOGV(TAG, "wolfSSH debugging off.");
                        wolfSSH_Debugging_OFF();
                    #endif

                    /* this is a blocking call, awaiting an SSH keypress
                     * unless nonBlock = 1 (normally we are NOT blocking) */
                    rxSz = wolfSSH_stream_read(threadCtx->ssh,
                                               this_rx_buf + backlogSz,
                                               EXAMPLE_BUFFER_SZ);

                    if (rxSz <= 0) {
                        rxSz = wolfSSH_get_error(threadCtx->ssh);
                        if (rxSz == WS_WANT_READ || rxSz == WS_WANT_WRITE)
                        {
                            /* WS_WANT_READ or WS_WANT_WRITE but no data yet */
                            rxSz = 0;
                        }
                        else
                        {
                            /*  any other negative value is an error */
                            has_err = 1;
                            ESP_LOGE(TAG, "wolfSSH_stream_read error!");
                        }
                    }
                    else {
#if defined(DISABLE_SSH_UART)
                        this_rx_buf[rxSz] = 0;
                        rxSz = 0;
                        /* printf is not ideal for embedded, but here for demo
                         * output only. setvbuf should have been set to flush
                         * output immediately:*/
                        printf("%s", this_rx_buf);
#else
                        ESP_LOGI(TAG, "Received %d bytes from client.", rxSz);
#endif
                    }


                    /* turn debugging back on */
                    #ifdef DEBUG_WOLFSSH
                        ESP_LOGV(TAG, "wolfSSH debugging on.");
                        wolfSSH_Debugging_ON();
                    #endif

                    taskYIELD();
                    #ifdef SSH_SERVER_WDT_RESET
                    {
                        esp_task_wdt_reset();
                    }
                    #endif

                } while ((WOLFSSL_NONBLOCK == 0) /* We'll wait only when
                                                  * not using non-blocking
                                                  * socket. */
                         &&
                         (rxSz == WS_WANT_READ || rxSz == WS_WANT_WRITE));

                /*
                 * if there's data in the external transmit buffer, typically
                 * from UART, we'll send that to the SSH client.
                 */
                if (ExternalTransmitBufferSz() > 0) {
                    ESP_LOGI(TAG,"Tx UART!");

                    /* our actual transit buffer array is not on the local
                     * stack to minimize RTOS requirements; we'll setup a
                     * pointer to it.
                     *
                     * Note this is a *different* buffer from the
                     * external (UART) which may change during RTOS threads,
                     * and future interrupt code.
                     * */
                    byte* sshStreamTransmitBuffer =
                          (byte*)(&sshStreamTransmitBufferArray);

                    /* We'll get a copy of the buffer and
                     * set _ExternalTransmitBufferSz to zero
                     *
                     * Note this is thread safe, getting both data and size.
                     */
                    int thisSize = Get_ExternalTransmitBuffer(
                                       &sshStreamTransmitBuffer
                                   );

                    if (sshStreamTransmitBuffer == NULL) {
                        /* TODO this is an error as the buffer should never
                         * be null if the size was larger than zero */
                        stop = 1;
                    }
                    else {
                        /* note thisSize will not have changed from any other
                         *  thread, since we have a copy and fixed size */
                        wolfSSH_stream_send(threadCtx->ssh,
                                            sshStreamTransmitBuffer,
                                            thisSize);
                    }
                } /* ExternalTransmitBufferSz() > 0 */

                /*
                 * If we received any data from the SSH client,
                 * we'll store it in the External Received Buffer
                 * for later sending to the UART.
                 *
                 * Reminder negative values for WS_WANT_READ || WS_WANT_WRITE
                 */
                if (rxSz > 0) {
                    /* Append external data, for something such as
                     * UART forwarding.
                     *
                     * Note any prior data saved in the buffer
                     * was _ExternalReceiveBufferSz.
                     *
                     * Here we perform the thread-safe equivalent of:
                     *
                        memcpy(
                            &_ExternalReceiveBuffer[_ExternalReceiveBufferSz],
                            buf,
                            rxSz);
                        _ExternalReceiveBufferSz = rxSz;
                     */
                    Set_ExternalReceiveBuffer(this_rx_buf, rxSz);

                    backlogSz += rxSz;
                    txSum = 0;
                    txSz = 0;

                    while (backlogSz != txSum && txSz >= 0 && !stop) {
                        /* we typically do NOT want to re-echo TTY data
                         * but it can be configured to do so by setting
                         * SSH_SERVER_ECHO to a value of 1
                         **/
                        if (SSH_SERVER_ECHO == 1) {
                            /* reminder we moved data from external buffer
                             * to our local buf, and this is ECHO
                             */
                            txSz = wolfSSH_stream_send(threadCtx->ssh,
                                                       this_rx_buf + txSum,
                                                       backlogSz - txSum);
                        }
                        else {
                            txSz = backlogSz - txSum;
                        }

                        if (txSz > 0) {
                            byte c;
                            const byte matches[] = { 0x03, 0x05, 0x06, 0x00 };

                            c = find_char(matches, this_rx_buf + txSum, txSz);

                            switch (c) {

                            case 0x03:
                                stop = 1;
                                break;

                            case 0x06:
                                if (wolfSSH_TriggerKeyExchange(threadCtx->ssh)
                                        != WS_SUCCESS) {
                                    stop = 1;
                                }
                                break;

                            case 0x05:
                                if (dump_stats(threadCtx) <= 0) {
                                    stop = 1;
                                }
                                break;
                            }

                            txSum += txSz;
                        }
                        else if (txSz != WS_REKEYING) {
                            stop = 1;
                        }

                        taskYIELD();
                        #ifdef SSH_SERVER_WDT_RESET
                        {
                            esp_task_wdt_reset();
                        }
                        #endif
                    } /* while */

                    if (txSum < backlogSz) {
                        memmove(this_rx_buf,
                                this_rx_buf + txSum,
                                backlogSz - txSum);
                    }
                    backlogSz -= txSum;
                }
                else {
                    /* we might have negative values for non-blocking Rx
                     * during WS_WANT_READ || WS_WANT_WRITE */
                    if (has_err == 1)
                    {
                        stop = 1;
                    }
                }
            }

        #ifdef DEBUG_WDT
            /* if we get panic faults, perhaps the watchdog needs attention? */
            taskYIELD();
            vTaskDelay(pdMS_TO_TICKS(10));
            esp_task_wdt_reset();
        #endif
        } while (!stop);
    } /* if (ret == WS_SUCCESS) */

    else if (ret == WS_SCP_COMPLETE) {
        ESP_LOGE(TAG,"scp file transfer completed\n");
#if defined(WOLFSSH_SCP) && defined(NO_FILESYSTEM)
        ESP_LOGE(TAG,"scp");
        if (scpBufferRecv.fileSz > 0) {
            word32 z;

            printf("file name : %s\n", scpBufferRecv.name);
            printf("     size : %d\n", scpBufferRecv.fileSz);
            printf("     mode : %o\n", scpBufferRecv.mode);
            printf("    mTime : %lu\n", scpBufferRecv.mTime);
            printf("\n");

            for (z = 0; z < scpBufferRecv.fileSz; z++)
                printf("%c", scpBufferRecv.buffer[z]);
            printf("\n");
        }
#endif
    } /* else if (ret == WS_SCP_COMPLETE) */
    else if (ret == WS_SFTP_COMPLETE) {
        ESP_LOGE(TAG,"Use example/echoserver/echoserver for SFTP\n");
    }

    wolfSSH_stream_exit(threadCtx->ssh, 0);

    /* check if open before closing */
    if (threadCtx->fd != SOCKET_INVALID) {
        ESP_LOGI(TAG,"Close sockfd socket");
        close(threadCtx->fd);
    }

    wolfSSH_free(threadCtx->ssh);
    free(threadCtx);

    return 0;
}

#ifndef NO_FILESYSTEM
static int load_file(const char* fileName, byte* buf, word32 bufSz)
{
    FILE* file;
    word32 fileSz;
    word32 readSz;

    if (fileName == NULL) return 0;

    if (WFOPEN(&file, fileName, "rb") != 0)
        return 0;
    fseek(file, 0, SEEK_END);
    fileSz = (word32)ftell(file);
    rewind(file);

    if (fileSz > bufSz) {
        fclose(file);
        return 0;
    }

    readSz = (word32)fread(buf, 1, fileSz, file);
    if (readSz < fileSz) {
        fclose(file);
        return 0;
    }

    fclose(file);

    return fileSz;
}
#endif /* !NO_FILESYSTEM */



/* returns buffer size on success */
static int load_key(byte isEcc, byte* buf, word32 bufSz)
{
    word32 sz = 0;

#ifndef NO_FILESYSTEM
    const char* bufName;
    bufName = isEcc ? "./keys/server-key-ecc.der" :
                       "./keys/server-key-rsa.der";
    sz = load_file(bufName, buf, bufSz);
#else
    /* using buffers instead */
    if (isEcc) {
        if ((word32)sizeof_ecc_key_der_256 > bufSz) {
            return 0;
        }
    #ifdef DEMO_SERVER_384
        WMEMCPY(buf, ecc_key_der_384, sizeof_ecc_key_der_384);
        sz = sizeof_ecc_key_der_384;
    #else
        WMEMCPY(buf, ecc_key_der_256, sizeof_ecc_key_der_256);
        sz = sizeof_ecc_key_der_256;
    #endif

    }
    else {
        if ((word32)sizeof_rsa_key_der_2048 > bufSz) {
            return 0;
        }
        WMEMCPY(buf, rsa_key_der_2048, sizeof_rsa_key_der_2048);
        sz = sizeof_rsa_key_der_2048;
    }
#endif

    return sz;
}


/* our own little c word32 to array */
static WC_INLINE void c32toa(word32 u32, byte* c)
{
    c[0] = (u32 >> 24) & 0xff;
    c[1] = (u32 >> 16) & 0xff;
    c[2] = (u32 >>  8) & 0xff;
    c[3] =  u32 & 0xff;
}

static PwMap* PwMapNew(PwMapList* list,
                       byte type,
                       const byte* username,
                       word32 usernameSz,
                       const byte* p,
                       word32 pSz) {
    PwMap* map = NULL;

    map = (PwMap*)malloc(sizeof(PwMap));
    if (map != NULL) {
        wc_Sha256 sha = { };
        byte flatSz[4];
        int fsz = 0;

        map->type = type;
        if (usernameSz >= sizeof(map->username))
            usernameSz = sizeof(map->username) - 1;
        memcpy(map->username, username, usernameSz + 1);
        map->username[usernameSz] = 0;
        map->usernameSz = usernameSz;

        ESP_LOGI(TAG, "map->username = %s", map->username);

        wc_InitSha256(&sha);
        c32toa(pSz, flatSz);

        fsz = sizeof(flatSz);

    #ifdef DEBUG_WOLFSSH
        ESP_LOGI(TAG, "map->username = %s", map->username);
        ESP_LOGI(TAG, "SHA256 flatSz: 0x%02x%02x%02x%02x; size = %d",
                      flatSz[0], flatSz[1], flatSz[2], flatSz[3], fsz);
        ESP_LOGI(TAG, "SHA256 sample password: '%s': size = %d bytes", p, pSz);
    #endif
    #if defined(SSH_SERVER_DEBUG_LOCKDEPTH)
        ESP_LOGW(TAG, "PwMapNew sha256 final ctx->lockDepth = %d",
                       (&sha.ctx)->lockDepth);
        ESP_LOGW(TAG, "calling wc_Sha256Update(1) ctx->lockDepth = %d",
                       (&sha.ctx)->lockDepth);
    #endif
        wc_Sha256Update((wc_Sha256*)&sha, flatSz, fsz);
    #if defined(SSH_SERVER_DEBUG_LOCKDEPTH) && \
       !defined(NO_WOLFSSL_ESP32_CRYPT_HASH_SHA256)
        ESP_LOGW(TAG, "calling wc_Sha256Update(2) ctx->lockDepth = %d",
                      (&sha.ctx)->lockDepth);
    #endif
        wc_Sha256Update((wc_Sha256*)&sha, p, pSz);
    #if defined(SSH_SERVER_DEBUG_LOCKDEPTH) && \
       !defined(NO_WOLFSSL_ESP32_CRYPT_HASH_SHA256)

        ESP_LOGW(TAG, "calling wc_Sha256Final ctx->lockDepth = %d",
                      (&sha.ctx)->lockDepth);
    #endif
        wc_Sha256Final((wc_Sha256*)&sha, (byte*)map->p);

        map->next = list->head;
        list->head = map;
    }

    return map;
}

static void PwMapListDelete(PwMapList* list)
{
    if (list != NULL) {
        PwMap* head = list->head;

        while (head != NULL) {
            PwMap* cur = head;
            head = head->next;
            memset(cur, 0, sizeof(PwMap));
            free(cur);
        }
    }
}

static int LoadPasswordBuffer(byte* buf, word32 bufSz, PwMapList* list)
{
    char* str = (char*)buf;
    char* delimiter;
    char* username;
    char* password;

    /* Each line of passwd.txt is in the format
     *     username:password\n
     * This function modifies the passed-in buffer. */

    if (list == NULL)
        return -1;

    if (buf == NULL || bufSz == 0) {
        ESP_LOGW(TAG, "Warning: LoadPasswordBuffer size is zero!");
        return 0;
    }

    while (*str != 0) {
        delimiter = strchr(str, ':');
        if (delimiter == NULL) {
            return -1;
        }
        username = str;
        *delimiter = 0;
        password = delimiter + 1;
        str = strchr(password, '\n');
        if (str == NULL) {
            return -1;
        }
        *str = 0;
        str++;
        if (PwMapNew(list,
                     WOLFSSH_USERAUTH_PASSWORD,
                     (byte*)username,
                     (word32)strlen(username),
                     (byte*)password,
                     (word32)strlen(password)) == NULL) {

            return -1;
        }
    }

    return 0;
}


static int LoadPublicKeyBuffer(byte* buf, word32 bufSz, PwMapList* list)
{
    char* str = (char*)buf;
    char* delimiter;
    byte* publicKey64;
    word32 publicKey64Sz;
    byte* username;
    word32 usernameSz;
    byte  publicKey[300];
    word32 publicKeySz;

    /* Each line of passwd.txt is in the format
     *     ssh-rsa AAAB3BASE64ENCODEDPUBLICKEYBLOB username\n
     * This function modifies the passed-in buffer. */
    if (list == NULL)
        return -1;

    if (buf == NULL || bufSz == 0) {
        ESP_LOGW(TAG, "Warning: LoadPublicKeyBuffer buffer size is zero!");
        return 0;
    }

    while (*str != 0) {
        /* Skip the public key type. This example will always be ssh-rsa. */
        delimiter = strchr(str, ' ');
        if (delimiter == NULL) {
            return -1;
        }
        str = delimiter + 1;
        delimiter = strchr(str, ' ');
        if (delimiter == NULL) {
            return -1;
        }
        publicKey64 = (byte*)str;
        *delimiter = 0;
        publicKey64Sz = (word32)(delimiter - str);
        str = delimiter + 1;
        delimiter = strchr(str, '\n');
        if (delimiter == NULL) {
            return -1;
        }
        username = (byte*)str;
        *delimiter = 0;
        usernameSz = (word32)(delimiter - str);
        str = delimiter + 1;
        publicKeySz = sizeof(publicKey);

        if (Base64_Decode(publicKey64,
            publicKey64Sz,
            publicKey,
            &publicKeySz) != 0) {

            return -1;
        }

        if (PwMapNew(list,
            WOLFSSH_USERAUTH_PUBLICKEY,
            username,
            usernameSz,
            publicKey,
            publicKeySz) == NULL) {

            return -1;
        }
    }

    return 0;
}

static int wsUserAuth(byte authType,
                      WS_UserAuthData* authData,
                      void* ctx)
{
    PwMapList* list;
    PwMap* map;
    byte authHash[WC_SHA256_DIGEST_SIZE];

    if (ctx == NULL) {
        ESP_LOGE(TAG,"wsUserAuth: ctx not set");
        return WOLFSSH_USERAUTH_FAILURE;
    }

    if (authType != WOLFSSH_USERAUTH_PASSWORD &&
        authType != WOLFSSH_USERAUTH_PUBLICKEY) {

        return WOLFSSH_USERAUTH_FAILURE;
    }

    /* Hash the password or public key with its length. */
    {
        wc_Sha256 sha;
        byte flatSz[4];
        wc_InitSha256(&sha);
        if (authType == WOLFSSH_USERAUTH_PASSWORD) {
            c32toa(authData->sf.password.passwordSz, flatSz);
            wc_Sha256Update(&sha, flatSz, sizeof(flatSz));
            wc_Sha256Update(&sha,
                authData->sf.password.password,
                authData->sf.password.passwordSz);
        }
        else if (authType == WOLFSSH_USERAUTH_PUBLICKEY) {
            c32toa(authData->sf.publicKey.publicKeySz, flatSz);
            wc_Sha256Update(&sha, flatSz, sizeof(flatSz));
            wc_Sha256Update(&sha,
                authData->sf.publicKey.publicKey,
                authData->sf.publicKey.publicKeySz);
        }
        wc_Sha256Final(&sha, authHash);
    }

    list = (PwMapList*)ctx;
    map = list->head;

    while (map != NULL) {
        if (authData->usernameSz == map->usernameSz &&
            memcmp(authData->username, map->username, map->usernameSz) == 0) {

            if (authData->type == map->type) {
                if (memcmp(map->p, authHash, WC_SHA256_DIGEST_SIZE) == 0) {
                    return WOLFSSH_USERAUTH_SUCCESS;
                }
                else {
                    return (authType == WOLFSSH_USERAUTH_PASSWORD ?
                            WOLFSSH_USERAUTH_INVALID_PASSWORD :
                            WOLFSSH_USERAUTH_INVALID_PUBLICKEY);
                }
            }
            else {
                return WOLFSSH_USERAUTH_INVALID_AUTHTYPE;
            }
        }
        map = map->next;
    }

    return WOLFSSH_USERAUTH_INVALID_USER;
}


#ifdef WOLFSSH_TEST_THREADING

typedef THREAD_RETURN WOLFSSH_THREAD THREAD_FUNC(void*);


static WC_INLINE void ThreadDetach(THREAD_TYPE thread) {
#ifdef SINGLE_THREADED
    (void)thread;
#elif defined(_POSIX_THREADS) && !defined(__MINGW32__)
    pthread_detach(thread);
#elif defined(WOLFSSL_TIRTOS)
#if 0
    while (1) {
        if (Task_getMode(thread) == Task_Mode_TERMINATED) {
            Task_sleep(5);
            break;
        }
        Task_yield();
    }
#endif
#else
    int res = CloseHandle((HANDLE)thread);
    assert(res);
    (void)res; /* Suppress un-used variable warning */
#endif
}

#endif /* WOLFSSH_TEST_THREADING */


/*
 * this my_IORecv callback is WIP and not currently used

TODO decide if we want to use callbacks or not

static int my_IORecv(WOLFSSH* ssh, void* buff, word32 sz, void* ctx) {
    int ret;

    ID  cepid;
    if (ctx != NULL)cepid = *(ID *)ctx;
    else return WS_CBIO_ERR_GENERAL;

    ret = tcp_rcv_dat(cepid, buff, sz, TMO_FEVR);
    byte* buf = NULL;
    int backlogSz = 0;

    ret = wolfSSH_stream_read(threadCtx->ssh,
        buf + backlogSz,
        EXAMPLE_BUFFER_SZ);
    ret = WOLFSSL_SUCCESS;
    return ret;
}

 * this my_IORecv callback is WIP and not currently used

static int my_IOSend(WOLFSSH* ssh, void* buff, word32 sz, void* ctx) {
    int ret;
    ID  cepid;

    if (ctx != NULL)cepid = *(ID *)ctx;
    else return WS_CBIO_ERR_GENERAL;

    ret = tcp_snd_dat(cepid, buff, sz, TMO_FEVR);
    ret = WOLFSSL_SUCCESS;

    return ret;
}
*/

void server_test(void *arg)
{
    int DEFAULT_PORT = SSH_UART_PORT;
    int ret = WOLFSSL_SUCCESS; /* assume success until proven wrong */
    int sockfd = 0; /* the socket that will carry our secure connection */
    struct sockaddr_in servAddr;
    int                on;

    static int mConnd = SOCKET_INVALID;

    /* declare wolfSSL objects */
    WOLFSSH_CTX *ctx = NULL; /* the wolfSSL context object*/

    PwMapList pwMapList;

    word32 defaultHighwater = EXAMPLE_HIGHWATER_MARK;
    word32 threadCount = 0;
    char multipleConnections = 0;
    char useEcc = 0;

#ifdef HAVE_SIGNAL
    signal(SIGINT, sig_handler);
#endif

#ifdef DEBUG_WOLFSSL
    wolfSSL_Debugging_ON();
    ESP_LOGI(TAG,"Debug ON v0.2c");
    /* TODO ShowCiphers(); */
#endif /* DEBUG_WOLFSSL */

#ifdef DEBUG_WOLFSSH
    wolfSSH_Debugging_ON();
    /* TODO ShowCiphers(); */
#endif /* DEBUG_WOLFSSL */

    /* Initialize the server address struct with zeros */
    memset(&servAddr, 0, sizeof(servAddr));

    /* Fill in the server address */
    servAddr.sin_family      = AF_INET; /* using IPv4      */
    servAddr.sin_port        = htons(DEFAULT_PORT); /* on DEFAULT_PORT */
    servAddr.sin_addr.s_addr = INADDR_ANY; /* from anywhere   */

    /*
    ***************************************************************************
    * Create a socket that uses an internet IPv4 address,
    * Sets the socket to be stream based (TCP),
    * 0 means choose the default protocol.
    *
    *  #include <sys/socket.h>
    *
    *  int socket(int domain, int type, int protocol);
    *
    *  The socket() function shall create an unbound socket in a communications
    *  domain, and return a file descriptor that can be used in later function
    *  calls that operate on sockets.
    *
    *  The socket() function takes the following arguments:
    *    domain     Specifies the communications domain in which a
    *                 socket is to be created.
    *    type       Specifies the type of socket to be created.
    *    protocol   Specifies a particular protocol to be used with the socket.
    *               Specifying a protocol of 0 causes socket() to use an
    *               unspecified default protocol appropriate for the
    *               requested socket type.
    *
    *    The domain argument specifies the address family used in the
    *    communications domain. The address families supported by the system
    *    are implementation-defined.
    *
    *    Symbolic constants that can be used for the domain argument are
    *    defined in the <sys/socket.h> header.
    *
    *  The type argument specifies the socket type, which determines the
    *  semantics of communication over the socket. The following socket types
    *  are defined; implementations may specify additional socket types:
    *
    *    SOCK_STREAM    Provides sequenced, reliable, bidirectional,
    *                   connection-mode byte streams, and may provide a
    *                   transmission mechanism for out-of-band data.
    *    SOCK_DGRAM     Provides datagrams, which are connectionless-mode,
    *                   unreliable messages of fixed maximum length.
    *    SOCK_SEQPACKET Provides sequenced, reliable, bidirectional,
    *                   connection-mode transmission paths for records.
    *                   A record can be sent using one or more output
    *                   operations and received using one or more input
    *                   operations, but a single operation never transfers
    *                   part of more than one record. Record boundaries
    *                   are visible to the receiver via the MSG_EOR flag.
    *
    *                   If the protocol argument is non-zero, it shall
    *                   specify a protocol that is supported by the address
    *                   family. If the protocol argument is zero, the default
    *                   protocol for this address family and type shall be
    *                   used. The protocols supported by the system are
    *                   implementation-defined.
    *
    *    The process may need to have appropriate privileges to use the
    *    socket() function or to create some sockets.
    *
    *  Return Value
    *    Upon successful completion, socket() shall return a non-negative
    *    integer, the socket file descriptor. Otherwise, a value of -1 shall
    *    be returned and errno set to indicate the error.
    *
    *  Errors; The socket() function shall fail if:
    *
    *    EAFNOSUPPORT    The implementation does not support the specified
    *                    address family.
    *    EMFILE          No more file descriptors are available for
    *                    this process.
    *    ENFILE          No more file descriptors are available for
    *                    the system.
    *    EPROTONOSUPPORT The protocol is not supported by the address family,
    *                    or the protocol is not supported by the implementation.
    *    EPROTOTYPE      The socket type is not supported by the protocol.
    *
    *  The socket() function may fail if:
    *
    *    EACCES  The process does not have appropriate privileges.
    *    ENOBUFS Insufficient resources were available in the system to
    *            perform the operation.
    *    ENOMEM  Insufficient memory was available to fulfill the request.
    *
    *  see: https://linux.die.net/man/3/socket
    ***************************************************************************
    */
    if (ret == WOLFSSL_SUCCESS) {
        /* Upon successful completion, socket() shall return
         * a non-negative integer, the socket file descriptor.
        */
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd > 0) {
            ESP_LOGI(TAG,"socket creation successful");
        }
        else {
            /* TODO show errno */
            ret = WOLFSSL_FAILURE;
           ESP_LOGE(TAG,"\r\nERROR: failed to create a socket.\n");
        }
    }
    else {
        ESP_LOGE(TAG,"Skipping socket create.\n");
    }


    /*
    ***************************************************************************
    * set SO_REUSEADDR on socket
    *
    *  #include <sys/types.h>
    *  # include <sys / socket.h>
    *  int getsockopt(int sockfd,
    *    int level,
    *    int optname,
    *    void *optval,
    *    socklen_t *optlen); int setsockopt(int sockfd,
    *    int level,
    *    int optname,
    *    const void *optval,
    *    socklen_t optlen);
    *
    *  setsockopt() manipulates options for the socket referred to by the file
    *  descriptor sockfd. Options may exist at multiple protocol levels; they
    *  are always present at the uppermost socket level.
    *
    *  When manipulating socket options, the level at which the option resides
    *  and the name of the option must be specified. To manipulate options at
    *  the sockets API level, level is specified as SOL_SOCKET. To manipulate
    *  options at any other level the protocol number of the appropriate
    *  protocol controlling the option is supplied. For example, to indicate
    *  that an option is to be interpreted by the TCP protocol, level should
    *  be set to the protocol number of TCP
    *
    *  Return Value
    *    On success, zero is returned. On error, -1 is returned, and errno is
    *    set appropriately.
    *
    *  Errors
    *    EBADF       The argument sockfd is not a valid descriptor.
    *    EFAULT      The address pointed to by optval is not in a valid part
    *                of the process address space. For getsockopt(), this error
    *                may also be returned if optlen is not in a valid part of
    *                the process address space.
    *    EINVAL      optlen invalid in setsockopt(). In some cases this error
    *                can also occur for an invalid value in optval
    *                (e.g. for IP_ADD_MEMBERSHIP option described in ip(7)).
    *    ENOPROTOOPT The option is unknown at the level indicated.
    *    ENOTSOCK    The argument sockfd is a file, not a socket.
    *
    *  see: https://linux.die.net/man/2/setsockopt
    ***************************************************************************
    */
    if (ret == WOLFSSL_SUCCESS) {
        /* make sure server is setup for reuse addr/port */
        int soc_ret;

        on = 1;
        soc_ret = setsockopt(sockfd,
                             SOL_SOCKET,
                             SO_REUSEADDR,
                             (char*)&on,
                             (socklen_t)sizeof(on)
                            );

        if (soc_ret == 0) {
            ESP_LOGI(TAG,"setsockopt re-use addr successful");
        }
        else {
            /* TODO show errno */
            ret = WOLFSSL_FAILURE;
            ESP_LOGE(TAG,"\r\nERROR: failed to setsockopt addr on socket.\n");
        }
    }
    else {
        ESP_LOGE(TAG,"Skipping setsockopt addr\n");
    }

#ifdef SO_REUSEPORT
    /* see above for details on getsockopt  */
    if (ret == WOLFSSL_SUCCESS) {
        int soc_ret = setsockopt(sockfd,
            SOL_SOCKET,
            SO_REUSEPORT,
            (char*)&on,
            (socklen_t)sizeof(on));

        if (soc_ret == 0) {
            ESP_LOGI(TAG,"setsockopt re-use port successful\n");
        }
        else {
            ESP_LOGE(TAG,"\r\nERROR: failed to setsockopt port on socket." \
                         "  >> IGNORED << \n");
        }
    }
    else {
        ESP_LOGE(TAG,"Skipping setsockopt port\n");
    }
#else
    ESP_LOGI(TAG,"SO_REUSEPORT not configured for setsockopt to re-use port\n");
#endif

    /*
    ***************************************************************************
    *  #include <sys/types.h>
    *  #include <sys/socket.h>
    *
    *  int bind(int sockfd,
    *      const struct sockaddr *addr,
    *      socklen_t addrlen);
    *
    *  Description
    *
    *  When a socket is created with socket(2), it exists in a name
    *  space(address family) but has no address assigned to it.
    *
    *  bind() assigns the address specified by addr to the socket referred to
    *  by the file descriptor sockfd.addrlen specifies the size, in bytes, of
    *  the address structure pointed to by addr.Traditionally, this operation
    *  is called "assigning a name to a socket".
    *
    *   It is normally necessary to assign a local address using bind() before
    *   a SOCK_STREAM socket may receive connections.
    *
    *  Return Value
    *    On success, zero is returned.
    *    On error, -1 is returned, and errno is set appropriately.
    *
    *  Errors
    *    EACCES     The address is protected, and the user is not the superuser.
    *    EADDRINUSE The given address is already in use.
    *    EBADF      sockfd is not a valid descriptor.
    *    EINVAL     The socket is already bound to an address.
    *    ENOTSOCK   sockfd is a descriptor for a file, not a socket.
    *
    *   see: https://linux.die.net/man/2/bind
    *
    *       https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/lwip.html
    ***************************************************************************
    */
    if (ret == WOLFSSL_SUCCESS) {
        /* Bind the server socket to our port */
        int soc_ret = bind(sockfd,
                           (struct sockaddr*)&servAddr,
                                       sizeof(servAddr)
                          );

        if (soc_ret > -1) {
            ESP_LOGI(TAG,"socket bind successful.");
        }
        else {
            ret = WOLFSSL_FAILURE;
            ESP_LOGE(TAG,"\r\nERROR: failed to bind to socket.\n");
        }
    }

    /*
    ***************************************************************************
    *  Listen for a new connection, allow 5 pending connections
    *
    *  #include <sys/types.h>
    *  #include <sys/socket.h>
    *  int listen(int sockfd, int backlog);
    *
    *  Description
    *
    *  listen() marks the socket referred to by sockfd as a passive socket,
    *  that is, as a socket that will be used to accept incoming connection
    *  requests using accept.
    *
    *  The sockfd argument is a file descriptor that refers to a socket of
    *  type SOCK_STREAM or SOCK_SEQPACKET.
    *
    *  The backlog argument defines the maximum length to which the queue of
    *  pending connections for sockfd may grow.If a connection request arrives
    *  when the queue is full, the client may receive an error with an
    *  indication of ECONNREFUSED or, if the underlying protocol supports
    *  retransmission, the request may be ignored so that a later reattempt
    *  at connection succeeds.
    *
    *   Return Value
    *     On success, zero is returned.
    *     On Error, -1 is returned, and errno is set appropriately.
    *   Errors
    *     EADDRINUSE   Another socket is already listening on the same port.
    *     EBADF        The argument sockfd is not a valid descriptor.
    *     ENOTSOCK     The argument sockfd is not a socket.
    *     EOPNOTSUPP   The socket is not of a type that supports
    *                  the listen() operation.
    *
    *  see: https://linux.die.net/man/2/listen
    */

    if (ret == WOLFSSL_SUCCESS) {
        int backlog = 5;
        int soc_ret = listen(sockfd, backlog);
        if (soc_ret > -1) {
            ESP_LOGI(TAG,"socket listen successful\n");
        }
        else {
           ret = WOLFSSL_FAILURE;
           ESP_LOGE(TAG,"\r\nERROR: failed to listen to socket.\n");
        }
    }

#ifdef NO_RSA
    /* If wolfCrypt isn't built with RSA, force ECC on. */
    useEcc = 1;
    ESP_LOGI(TAG,"Found NO_RSA, setting useEcc = 1");
#endif

    if (wolfSSH_Init() != WS_SUCCESS) {
        ESP_LOGE(TAG,"Couldn't initialize wolfSSH.\n");
        exit(EXIT_FAILURE);
    }

    ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
    if (ctx == NULL) {
        ESP_LOGE(TAG,"Couldn't allocate SSH CTX data.\n");
        exit(EXIT_FAILURE);
    }


    memset(&pwMapList, 0, sizeof(pwMapList));

    /* authorization is a callback, so assign it here: wsUserAuth */
    wolfSSH_SetUserAuth(ctx, wsUserAuth);

    /* set the login banner message as defined in ssh_server_config.h */
    wolfSSH_CTX_SetBanner(ctx, SSH_SERVER_BANNER);

    {
        const char* bufName;
        byte buf[SCRATCH_BUFFER_SZ];
        word32 bufSz;
        int ret = 0;

        bufSz = load_key(useEcc, buf, SCRATCH_BUFFER_SZ);
        if (bufSz == 0) {
            ESP_LOGE(TAG, "Couldn't load key.\n");
            exit(EXIT_FAILURE);
        }
        if (wolfSSH_CTX_UsePrivateKey_buffer(ctx,
                                             buf,
                                             bufSz,
                                             WOLFSSH_FORMAT_ASN1) < 0) {
            ESP_LOGE(TAG,"Couldn't use key buffer.\n");
            exit(EXIT_FAILURE);
        }

        bufSz = (word32)strlen(samplePasswordBuffer);
        memcpy(buf, samplePasswordBuffer, bufSz);
        buf[bufSz] = 0;
        ret = LoadPasswordBuffer(buf, bufSz, &pwMapList);
        if (ret != 0) {
            ESP_LOGE(TAG, "Error: failed LoadPasswordBuffer %d", ret);
            exit(EXIT_FAILURE);
        }

        bufName = useEcc ? samplePublicKeyEccBuffer :
                           samplePublicKeyRsaBuffer;
        bufSz = (word32)strlen(bufName);
        memcpy(buf, bufName, bufSz);
        buf[bufSz] = 0;
        LoadPublicKeyBuffer(buf, bufSz, &pwMapList);
        if (ret != 0) {
            ESP_LOGE(TAG, "Error: failed LoadPasswordBuffer %d", ret);
            exit(EXIT_FAILURE);
        }
    }

    listen(sockfd, 5);

    do {
        int      clientFd = 0;
        struct sockaddr_in clientAddr;
        socklen_t     clientAddrSz = sizeof(clientAddr);
#ifndef SINGLE_THREADED
        THREAD_TYPE   thread;
        ESP_LOGI(TAG,"Did not find SINGLE_THREADED defined");
#endif
        WOLFSSH*      ssh;

        /* We'll create a new instance of threadCtx since it will be
         * handed off to potentially multiple separate threads.
         */
        thread_ctx_t* threadCtx;

        threadCtx = (thread_ctx_t*)malloc(sizeof(thread_ctx_t));
        if (threadCtx == NULL) {
            ESP_LOGE(TAG,"Couldn't allocate thread context data.\n");
            exit(EXIT_FAILURE);
        }

        /*
         * optionally register some callbacks (these are not working)
        wolfSSH_SetIORecv(ctx, my_IORecv);
        wolfSSH_SetIOSend(ctx, my_IOSend);
         */

        ssh = wolfSSH_new(ctx);
        if (ssh == NULL) {
            ESP_LOGE(TAG,"Failed to create ssh object during wolfSSH_new.\n");
            exit(EXIT_FAILURE);
        }
        wolfSSH_SetUserAuthCtx(ssh, &pwMapList);
        /* Use the session object for its own highwater callback ctx */
        if (defaultHighwater > 0) {
            wolfSSH_SetHighwaterCtx(ssh, (void*)ssh);
            wolfSSH_SetHighwater(ssh, defaultHighwater);
        }

        clientFd = accept(sockfd,
                          (struct sockaddr*)&clientAddr,
                          &clientAddrSz
                         );

        if (clientFd == -1) {
            ESP_LOGI(TAG,"ERROR: failed accept");
            exit(EXIT_FAILURE);
        }

        if (WOLFSSL_NONBLOCK)
            tcp_set_nonblocking(&clientFd);

        wolfSSH_set_fd(ssh, (int)clientFd);

        threadCtx->ssh = ssh;
        threadCtx->fd = clientFd;
        threadCtx->id = threadCount++;
        threadCtx->nonBlock = WOLFSSL_NONBLOCK;

        ESP_LOGI(TAG,"server_worker started.");
#ifndef SINGLE_THREADED
    #ifdef WOLFSSH_TEST_THREADING
        ThreadStart(server_worker, threadCtx, &thread);

        if (multipleConnections)
            ThreadDetach(thread);
        else
            ThreadJoin(thread);
    #else
        /* see "wolfssh/test.h" check user_settings.h */
        #error "WOLFSSH_TEST_THREADING must be enabled unless SINGLE_THREADED"
    #endif
#else
        server_worker(threadCtx);
#endif /* SINGLE_THREADED */
        ESP_LOGI(TAG,"server_worker completed.");
        vTaskDelay(10);
    } while (multipleConnections);
    ESP_LOGI(TAG,"all servers exited.");

    PwMapListDelete(&pwMapList);
    wolfSSH_CTX_free(ctx);
    if (wolfSSH_Cleanup() != WS_SUCCESS) {
        ESP_LOGE(TAG,"Couldn't clean up wolfSSH.\n");
        exit(EXIT_FAILURE);
    }
#if defined(HAVE_ECC) && defined(FP_ECC) && defined(HAVE_THREAD_LS)
    wc_ecc_fp_free(); /* free per thread cache */
#endif

    ESP_LOGI(TAG,"server test done!");

    /* Cleanup and return */

    if (mConnd != SOCKET_INVALID) {
        ESP_LOGI(TAG,"Close mConnd socket");
        close(mConnd); /* Close the connection to the client   */
        mConnd = SOCKET_INVALID;
    }
    if (sockfd != SOCKET_INVALID) {
        ESP_LOGI(TAG,"Close sockfd socket");
        close(sockfd); /* Close the socket listening for clients   */
        sockfd = SOCKET_INVALID;
    }

    return;
}
