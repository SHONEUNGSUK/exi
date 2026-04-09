/**
 * @file  exi_bitstream.h
 * @brief Low-level bit/byte stream read-write primitives.
 *
 * W3C EXI integer/string encoding per §7.1.
 * All functions are static-inline for zero-overhead inclusion.
 */

#ifndef EXI_BITSTREAM_H
#define EXI_BITSTREAM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "exi_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────────────────────────────────────────────────────
 * Init helpers
 * ───────────────────────────────────────────────────────── */
static inline void bs_init_write(exi_bitstream_t *bs, uint8_t *buf, size_t cap)
{
    bs->data = buf; bs->capacity = cap;
    bs->byte_pos = 0; bs->bit_pos = 0;
    memset(buf, 0, cap);
}

static inline void bs_init_read(exi_bitstream_t *bs,
                                 const uint8_t *buf, size_t sz)
{
    bs->data = (uint8_t*)buf; bs->capacity = sz;
    bs->byte_pos = 0; bs->bit_pos = 0;
}

static inline size_t bs_bytes_used(const exi_bitstream_t *bs)
{
    return bs->byte_pos + (bs->bit_pos > 0 ? 1u : 0u);
}

static inline int bs_eof(const exi_bitstream_t *bs)
{
    return bs->byte_pos >= bs->capacity;
}

/* ─────────────────────────────────────────────────────────
 * Single-bit write / read
 * ───────────────────────────────────────────────────────── */
static inline exi_result_t bs_write_bit(exi_bitstream_t *bs, uint8_t bit)
{
    if (bs->byte_pos >= bs->capacity) return EXI_ERROR_BUFFER_SMALL;
    if (bit) bs->data[bs->byte_pos] |= (uint8_t)(0x80u >> bs->bit_pos);
    if (++bs->bit_pos == 8) { bs->bit_pos = 0; ++bs->byte_pos; }
    return EXI_OK;
}

static inline exi_result_t bs_read_bit(exi_bitstream_t *bs, uint8_t *bit)
{
    if (bs->byte_pos >= bs->capacity) return EXI_ERROR_BUFFER_SMALL;
    *bit = (bs->data[bs->byte_pos] >> (7u - bs->bit_pos)) & 1u;
    if (++bs->bit_pos == 8) { bs->bit_pos = 0; ++bs->byte_pos; }
    return EXI_OK;
}

/* ─────────────────────────────────────────────────────────
 * N-bit unsigned integer  (n ≤ 32), MSB first
 * ───────────────────────────────────────────────────────── */
static inline exi_result_t bs_write_bits(exi_bitstream_t *bs,
                                          uint32_t val, uint8_t n)
{
    exi_result_t rc;
    int i;
    for (i = (int)n - 1; i >= 0; --i) {
        rc = bs_write_bit(bs, (uint8_t)((val >> i) & 1u));
        if (rc) return rc;
    }
    return EXI_OK;
}

static inline exi_result_t bs_read_bits(exi_bitstream_t *bs,
                                         uint32_t *val, uint8_t n)
{
    exi_result_t rc;
    uint8_t  bit;
    uint32_t r = 0;
    int i;
    for (i = (int)n - 1; i >= 0; --i) {
        rc = bs_read_bit(bs, &bit);
        if (rc) return rc;
        r |= ((uint32_t)bit << i);
    }
    *val = r;
    return EXI_OK;
}

/* ─────────────────────────────────────────────────────────
 * Unsigned Integer  (W3C §7.1.6)
 * 7-bit groups, LSB group first, bit7 = more-flag
 * ───────────────────────────────────────────────────────── */
static inline exi_result_t bs_write_uint(exi_bitstream_t *bs, uint32_t val)
{
    exi_result_t rc;
    do {
        uint32_t group = val & 0x7Fu;
        val >>= 7;
        rc = bs_write_bits(bs, val ? (group | 0x80u) : group, 8);
        if (rc) return rc;
    } while (val);
    return EXI_OK;
}

static inline exi_result_t bs_read_uint(exi_bitstream_t *bs, uint32_t *val)
{
    exi_result_t rc;
    uint32_t r = 0, shift = 0, byte;
    do {
        rc = bs_read_bits(bs, &byte, 8);
        if (rc) return rc;
        r |= (byte & 0x7Fu) << shift;
        shift += 7;
        if (shift > 35u) return EXI_ERROR_INVALID_EXI;
    } while (byte & 0x80u);
    *val = r;
    return EXI_OK;
}

/* ─────────────────────────────────────────────────────────
 * String value  (W3C §7.1.10)
 * UInt(len) + UTF-8 bytes
 * ───────────────────────────────────────────────────────── */
static inline exi_result_t bs_write_string(exi_bitstream_t *bs,
                                            const char *s, size_t len)
{
    exi_result_t rc;
    size_t i;
    rc = bs_write_uint(bs, (uint32_t)len);
    if (rc) return rc;
    for (i = 0; i < len; ++i) {
        rc = bs_write_bits(bs, (uint8_t)s[i], 8);
        if (rc) return rc;
    }
    return EXI_OK;
}

static inline exi_result_t bs_read_string(exi_bitstream_t *bs,
                                           char *buf, size_t cap,
                                           size_t *out_len)
{
    exi_result_t rc;
    uint32_t len, i, byte;
    rc = bs_read_uint(bs, &len);
    if (rc) return rc;
    if (len >= (uint32_t)cap) return EXI_ERROR_BUFFER_SMALL;
    for (i = 0; i < len; ++i) {
        rc = bs_read_bits(bs, &byte, 8);
        if (rc) return rc;
        buf[i] = (char)(uint8_t)byte;
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return EXI_OK;
}

/* ─────────────────────────────────────────────────────────
 * Byte-align (byte-aligned mode helper)
 * ───────────────────────────────────────────────────────── */
static inline exi_result_t bs_align(exi_bitstream_t *bs)
{
    if (bs->bit_pos) {
        bs->bit_pos = 0;
        if (++bs->byte_pos > bs->capacity) return EXI_ERROR_BUFFER_SMALL;
    }
    return EXI_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* EXI_BITSTREAM_H */
