#pragma once
/* C-compatible opaque storage; callbacks use std::mutex on every platform. */
typedef struct {
    void *mutex;
} mbedtls_threading_mutex_t;
