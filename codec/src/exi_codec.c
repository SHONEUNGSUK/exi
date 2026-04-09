/**
 * @file  exi_codec.c
 * @brief W3C EXI 1.0 Codec — ISO 15118-20:2022
 *
 * Features:
 *  - Bit-packed, Byte-aligned, and zlib-Compressed alignment modes
 *  - 3-state document grammar (pre-SD / body / post-root-EE)
 *  - URI + LocalName + Value string-table partitions
 *  - ISO 15118-20 namespace pre-seeding
 *  - All large structs heap-allocated (no stack overflow)
 *  - Clean indented XML output with xmlns: prefix reconstruction
 *
 * W3C EXI 1.0 spec: https://www.w3.org/TR/exi/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>

#include "exi_codec.h"
#include "exi_bitstream.h"

/* ── EXI header constants ──────────────────────────────────── */
#define EXI_COOKIE_0  0x24u
#define EXI_COOKIE_1  0x45u
#define EXI_COOKIE_2  0x58u
#define EXI_COOKIE_3  0x49u

/* ── Schemaless grammar event codes (W3C §8.5 Table 8-1) ──── */
#define EC_W_BODY   3   /* SE=0 EE=1 AT=2 CH=3 NS=4 CM=5 PI=6 */

/* ── Parser / decoder limits ───────────────────────────────── */
#define MAX_DEPTH        128
#define MAX_ATTRS         32
#define ATTR_NM_MAX      128
#define ATTR_VAL_MAX     256
#define TAG_NM_MAX       256
#define MAX_PENDING_NS    16
#define MAX_NS_REG        64

/* ═══════════════════════════════════════════════════════════
 * String-table
 * ═══════════════════════════════════════════════════════════ */
static int st_find(const exi_string_table_t *t, const char *s)
{
    uint32_t i;
    for (i = 0; i < t->count; ++i)
        if (strcmp(t->entries[i].str, s) == 0) return (int)i;
    return -1;
}

static int st_add(exi_string_table_t *t, const char *s)
{
    size_t l;
    if (t->count >= EXI_ST_MAX_ENTRIES) return -1;
    l = strlen(s);
    if (l >= EXI_ST_MAX_STR_LEN) l = EXI_ST_MAX_STR_LEN - 1;
    memcpy(t->entries[t->count].str, s, l);
    t->entries[t->count].str[l] = '\0';
    t->entries[t->count].len    = l;
    return (int)(t->count++);
}

/* ISO 15118-20 well-known strings pre-population */
static const char *k_uris[] = {
    EXI_NS_ISO15118_APP, EXI_NS_ISO15118_CT,
    EXI_NS_ISO15118_CM,  EXI_NS_ISO15118_DC,
    EXI_NS_ISO15118_AC,
    "http://www.w3.org/2001/XMLSchema-instance", NULL
};
static const char *k_lnames[] = {
    "Header","SessionID","TimeStamp","Signature",
    "ResponseCode","EVSEProcessing","Exponent","Value",
    "MaximumChargePower","MinimumChargePower",
    "MaximumChargeCurrent","MinimumChargeCurrent",
    "MaximumVoltage","MinimumVoltage",
    "EVTargetVoltage","EVTargetCurrent",
    "EVSEPresentVoltage","EVSEPresentCurrent",
    "EVRESSSOC","EVErrorCode",
    "DisplayParameters","PresentSOC","TargetSOC",
    "BatteryEnergyCapacity","ChargingComplete",
    "EVTargetEnergyRequest","EVMaximumEnergyRequest","EVMinimumEnergyRequest",
    "AppProtocol","ProtocolNamespace","VersionNumberMajor","VersionNumberMinor",
    "SchemaID","Priority","EVSEID","EVCCID", NULL
};
static const char *k_vals[] = {
    "OK","FAILED","Finished","Ongoing","NO_ERROR",
    "true","false","0","1","2",
    "DC_core","AC_single_phase_core","AC_three_phase_core",
    "EIM","PnC","Start","Stop","Standby","Terminate","Pause", NULL
};

static void st_prepop_enc(exi_encoder_t *enc)
{
    int i;
    for (i=0; k_uris[i];   ++i) st_add(&enc->uri_table,        k_uris[i]);
    for (i=0; k_lnames[i]; ++i) st_add(&enc->local_name_table, k_lnames[i]);
    for (i=0; k_vals[i];   ++i) st_add(&enc->value_table,      k_vals[i]);
}

static void st_prepop_dec(exi_decoder_t *dec)
{
    int i;
    for (i=0; k_uris[i];   ++i) st_add(&dec->uri_table,        k_uris[i]);
    for (i=0; k_lnames[i]; ++i) st_add(&dec->local_name_table, k_lnames[i]);
    for (i=0; k_vals[i];   ++i) st_add(&dec->value_table,      k_vals[i]);
}

/* ═══════════════════════════════════════════════════════════
 * Byte-align helper (used in byte-aligned & compressed modes)
 * ═══════════════════════════════════════════════════════════ */
static exi_result_t align_enc(exi_encoder_t *enc)
{
    if (enc->options.alignment != EXI_ALIGN_BIT_PACKED)
        return bs_align(&enc->stream);
    return EXI_OK;
}
static exi_result_t align_dec(exi_decoder_t *dec)
{
    if (dec->options.alignment != EXI_ALIGN_BIT_PACKED)
        return bs_align(&dec->stream);
    return EXI_OK;
}

/* ═══════════════════════════════════════════════════════════
 * Document grammar state machine  (W3C §8.5.1)
 *
 *  doc_state 0  pre-SD   : 1-bit, 0 = SD
 *  doc_state 1  in-body  : 3-bit event codes
 *  doc_state 2  post-EE  : 1-bit, 0 = ED
 * ═══════════════════════════════════════════════════════════ */
static exi_result_t ec_write(exi_encoder_t *enc, exi_event_type_t type)
{
    exi_bitstream_t *bs = &enc->stream;
    exi_result_t rc;

    if (enc->doc_state == 0) {          /* pre-SD → SD */
        rc = bs_write_bits(bs, 0u, 1); if (rc) return rc;
        enc->doc_state = 1;
        return align_enc(enc);
    }
    if (enc->doc_state == 2) {          /* post-root-EE → ED */
        rc = bs_write_bits(bs, 0u, 1); if (rc) return rc;
        return align_enc(enc);
    }
    /* body */
    switch (type) {
    case EXI_EVENT_SE:
        rc = bs_write_bits(bs, 0u, EC_W_BODY); if (rc) return rc;
        return align_enc(enc);
    case EXI_EVENT_EE:
        rc = bs_write_bits(bs, 1u, EC_W_BODY); if (rc) return rc;
        if (enc->element_depth == 1) enc->doc_state = 2;
        return align_enc(enc);
    case EXI_EVENT_AT:
        rc = bs_write_bits(bs, 2u, EC_W_BODY); if (rc) return rc;
        return align_enc(enc);
    case EXI_EVENT_CH:
        rc = bs_write_bits(bs, 3u, EC_W_BODY); if (rc) return rc;
        return align_enc(enc);
    case EXI_EVENT_NS:
        rc = bs_write_bits(bs, 4u, EC_W_BODY); if (rc) return rc;
        return align_enc(enc);
    case EXI_EVENT_CM:
        rc = bs_write_bits(bs, 5u, EC_W_BODY); if (rc) return rc;
        return align_enc(enc);
    case EXI_EVENT_PI:
        rc = bs_write_bits(bs, 6u, EC_W_BODY); if (rc) return rc;
        return align_enc(enc);
    default:
        return EXI_ERROR_UNSUPPORTED;
    }
}

static exi_result_t ec_read(exi_decoder_t *dec, exi_event_type_t *type)
{
    exi_bitstream_t *bs = &dec->stream;
    uint32_t c;
    exi_result_t rc;

    if (bs_eof(bs)) { *type = EXI_EVENT_EOF; return EXI_OK; }

    if (dec->doc_state == 0) {
        rc = bs_read_bits(bs, &c, 1); if (rc) return rc;
        *type = EXI_EVENT_SD;
        dec->doc_state = 1;
        return align_dec(dec);
    }
    if (dec->doc_state == 2) {
        rc = bs_read_bits(bs, &c, 1); if (rc) return rc;
        *type = EXI_EVENT_ED;
        return align_dec(dec);
    }
    /* body */
    rc = bs_read_bits(bs, &c, EC_W_BODY); if (rc) return rc;
    switch (c) {
    case 0: *type = EXI_EVENT_SE; break;
    case 1:
        *type = EXI_EVENT_EE;
        if (dec->element_depth == 1) dec->doc_state = 2;
        break;
    case 2: *type = EXI_EVENT_AT; break;
    case 3: *type = EXI_EVENT_CH; break;
    case 4: *type = EXI_EVENT_NS; break;
    case 5: *type = EXI_EVENT_CM; break;
    case 6: *type = EXI_EVENT_PI; break;
    default: return EXI_ERROR_INVALID_EXI;
    }
    return align_dec(dec);
}

/* ═══════════════════════════════════════════════════════════
 * QName codec  (URI + LocalName partitions)
 * ═══════════════════════════════════════════════════════════ */
static exi_result_t qname_encode(exi_encoder_t *enc,
                                  const char *uri, const char *ln)
{
    exi_bitstream_t *bs = &enc->stream;
    exi_result_t rc;
    int idx;

    /* URI partition */
    idx = st_find(&enc->uri_table, uri ? uri : "");
    if (idx >= 0) {
        rc = bs_write_bit(bs, 1); if (rc) return rc;
        rc = bs_write_uint(bs, (uint32_t)idx); if (rc) return rc;
    } else {
        const char *u = uri ? uri : "";
        rc = bs_write_bit(bs, 0); if (rc) return rc;
        rc = bs_write_string(bs, u, strlen(u)); if (rc) return rc;
        st_add(&enc->uri_table, u);
    }

    /* LocalName partition */
    idx = st_find(&enc->local_name_table, ln ? ln : "");
    if (idx >= 0) {
        rc = bs_write_bit(bs, 1); if (rc) return rc;
        rc = bs_write_uint(bs, (uint32_t)idx); if (rc) return rc;
    } else {
        const char *l = ln ? ln : "";
        rc = bs_write_bit(bs, 0); if (rc) return rc;
        rc = bs_write_string(bs, l, strlen(l)); if (rc) return rc;
        st_add(&enc->local_name_table, l);
    }
    return EXI_OK;
}

static exi_result_t qname_decode(exi_decoder_t *dec,
                                  char *uri_out, char *ln_out)
{
    exi_bitstream_t *bs = &dec->stream;
    exi_result_t rc;
    uint8_t flag;
    uint32_t idx;
    size_t slen;

    /* URI */
    rc = bs_read_bit(bs, &flag); if (rc) return rc;
    if (flag) {
        rc = bs_read_uint(bs, &idx); if (rc) return rc;
        if (idx >= dec->uri_table.count) return EXI_ERROR_INVALID_EXI;
        strncpy(uri_out, dec->uri_table.entries[idx].str, EXI_ST_MAX_STR_LEN-1);
        uri_out[EXI_ST_MAX_STR_LEN-1] = '\0';
    } else {
        rc = bs_read_string(bs, uri_out, EXI_ST_MAX_STR_LEN, &slen);
        if (rc) return rc;
        st_add(&dec->uri_table, uri_out);
    }

    /* LocalName */
    rc = bs_read_bit(bs, &flag); if (rc) return rc;
    if (flag) {
        rc = bs_read_uint(bs, &idx); if (rc) return rc;
        if (idx >= dec->local_name_table.count) return EXI_ERROR_INVALID_EXI;
        strncpy(ln_out, dec->local_name_table.entries[idx].str, EXI_ST_MAX_STR_LEN-1);
        ln_out[EXI_ST_MAX_STR_LEN-1] = '\0';
    } else {
        rc = bs_read_string(bs, ln_out, EXI_ST_MAX_STR_LEN, &slen);
        if (rc) return rc;
        st_add(&dec->local_name_table, ln_out);
    }
    return EXI_OK;
}

/* Value partition */
static exi_result_t val_encode(exi_encoder_t *enc, const char *v)
{
    exi_bitstream_t *bs = &enc->stream;
    exi_result_t rc;
    const char *s = v ? v : "";
    int idx = st_find(&enc->value_table, s);
    if (idx >= 0) {
        rc = bs_write_bit(bs, 1); if (rc) return rc;
        return bs_write_uint(bs, (uint32_t)idx);
    }
    rc = bs_write_bit(bs, 0); if (rc) return rc;
    rc = bs_write_string(bs, s, strlen(s)); if (rc) return rc;
    st_add(&enc->value_table, s);
    return EXI_OK;
}

static exi_result_t val_decode(exi_decoder_t *dec, char *out)
{
    exi_bitstream_t *bs = &dec->stream;
    exi_result_t rc;
    uint8_t flag;
    uint32_t idx;
    size_t slen;

    rc = bs_read_bit(bs, &flag); if (rc) return rc;
    if (flag) {
        rc = bs_read_uint(bs, &idx); if (rc) return rc;
        if (idx >= dec->value_table.count) return EXI_ERROR_INVALID_EXI;
        strncpy(out, dec->value_table.entries[idx].str, EXI_ST_MAX_STR_LEN-1);
        out[EXI_ST_MAX_STR_LEN-1] = '\0';
    } else {
        rc = bs_read_string(bs, out, EXI_ST_MAX_STR_LEN, &slen);
        if (rc) return rc;
        st_add(&dec->value_table, out);
    }
    return EXI_OK;
}

/* ═══════════════════════════════════════════════════════════
 * Schema Registry
 * ═══════════════════════════════════════════════════════════ */
exi_result_t exi_registry_init(exi_schema_registry_t *reg, const char *xsd_dir)
{
    if (!reg) return EXI_ERROR_NULL_PTR;
    memset(reg, 0, sizeof(*reg));
    if (xsd_dir) {
        size_t l = strlen(xsd_dir);
        if (l >= EXI_MAX_PATH_LEN) l = EXI_MAX_PATH_LEN-1;
        memcpy(reg->xsd_dir, xsd_dir, l);
        reg->xsd_dir[l] = '\0';
    }
    return EXI_OK;
}

exi_result_t exi_registry_add(exi_schema_registry_t *reg,
                                const char *ns_uri,
                                const char *xsd_filename)
{
    size_t l;
    exi_schema_entry_t *e;
    if (!reg || !ns_uri || !xsd_filename) return EXI_ERROR_NULL_PTR;
    if (reg->count >= EXI_MAX_SCHEMAS) return EXI_ERROR_OVERFLOW;
    e = &reg->entries[reg->count++];
    l = strlen(ns_uri);    if (l>=EXI_MAX_NAMESPACE_LEN) l=EXI_MAX_NAMESPACE_LEN-1;
    memcpy(e->namespace_uri, ns_uri, l);    e->namespace_uri[l] = '\0';
    l = strlen(xsd_filename); if (l>=EXI_MAX_PATH_LEN) l=EXI_MAX_PATH_LEN-1;
    memcpy(e->xsd_path, xsd_filename, l);   e->xsd_path[l] = '\0';
    e->loaded = 0;
    return EXI_OK;
}

exi_result_t exi_registry_load(exi_schema_registry_t *reg)
{
    uint32_t i;
    char full_path[EXI_MAX_PATH_LEN * 2];
    char head[16];
    size_t fl, r;
    FILE *f;

    if (!reg) return EXI_ERROR_NULL_PTR;

    for (i = 0; i < reg->count; ++i) {
        exi_schema_entry_t *e = &reg->entries[i];

        if (reg->xsd_dir[0])
            snprintf(full_path, sizeof(full_path), "%s/%s",
                     reg->xsd_dir, e->xsd_path);
        else {
            fl = strlen(e->xsd_path);
            if (fl >= sizeof(full_path)) fl = sizeof(full_path)-1;
            memcpy(full_path, e->xsd_path, fl);
            full_path[fl] = '\0';
        }

        f = fopen(full_path, "r");
        if (!f) {
            fprintf(stderr, "[EXI] Schema not found: %s\n", full_path);
            return EXI_ERROR_SCHEMA;
        }
        memset(head, 0, sizeof(head));
        r = fread(head, 1, 15, f);
        fclose(f);
        (void)r;

        if (strncmp(head,"<?xml",5)!=0 &&
            strncmp(head,"<xs:", 4)!=0 &&
            strncmp(head,"<!--", 4)!=0) {
            fprintf(stderr, "[EXI] Schema invalid XML: %s\n", full_path);
            return EXI_ERROR_SCHEMA;
        }

        e->loaded = 1;
        fl = strlen(full_path);
        if (fl >= EXI_MAX_PATH_LEN) fl = EXI_MAX_PATH_LEN-1;
        memcpy(e->xsd_path, full_path, fl);
        e->xsd_path[fl] = '\0';
    }
    return EXI_OK;
}

void exi_registry_print(const exi_schema_registry_t *reg)
{
    uint32_t i;
    if (!reg) return;
    printf("  Schema Registry (%u entries, xsd_dir=%s)\n",
           reg->count, reg->xsd_dir[0] ? reg->xsd_dir : "(none)");
    for (i = 0; i < reg->count; ++i) {
        const exi_schema_entry_t *e = &reg->entries[i];
        printf("  [%u] %s  %-52s\n",
               i, e->loaded ? "OK" : "  ", e->namespace_uri);
        printf("        %s\n", e->xsd_path);
    }
}

/* ═══════════════════════════════════════════════════════════
 * Low-level Encoder
 * ═══════════════════════════════════════════════════════════ */
exi_result_t exi_encoder_init(exi_encoder_t *enc,
                               uint8_t *buf, size_t cap,
                               const exi_options_t *opts,
                               const exi_schema_registry_t *reg)
{
    if (!enc || !buf) return EXI_ERROR_NULL_PTR;
    memset(enc, 0, sizeof(*enc));
    bs_init_write(&enc->stream, buf, cap);
    if (opts) enc->options = *opts;
    else { exi_options_t d = EXI_OPTIONS_DEFAULT; enc->options = d; }
    enc->registry  = reg;
    enc->doc_state = 0;
    if (reg) st_prepop_enc(enc);
    return EXI_OK;
}

exi_result_t exi_encoder_header(exi_encoder_t *enc)
{
    exi_bitstream_t *bs = &enc->stream;
    exi_result_t rc;
    uint8_t options_byte = 0;

    /* 4-byte cookie: $EXI */
    rc = bs_write_bits(bs, EXI_COOKIE_0, 8); if (rc) return rc;
    rc = bs_write_bits(bs, EXI_COOKIE_1, 8); if (rc) return rc;
    rc = bs_write_bits(bs, EXI_COOKIE_2, 8); if (rc) return rc;
    rc = bs_write_bits(bs, EXI_COOKIE_3, 8); if (rc) return rc;

    /* Distinguishing bits: 1 0 (W3C §8.3.2) */
    rc = bs_write_bit(bs, 1); if (rc) return rc;
    rc = bs_write_bit(bs, 0); if (rc) return rc;

    /* Options presence bit:
     *   0 = no options (bit-packed, defaults)
     *   1 = options block follows
     */
    if (enc->options.alignment == EXI_ALIGN_BIT_PACKED) {
        rc = bs_write_bit(bs, 0); if (rc) return rc;  /* no options */
    } else {
        rc = bs_write_bit(bs, 1); if (rc) return rc;  /* options present */
        /* Simplified options byte:
         *   bit7-6: alignment  00=bit-packed 01=byte-aligned 10=compressed
         *   bit5-0: reserved (0)
         */
        if      (enc->options.alignment == EXI_ALIGN_BYTE_ALIGNED) options_byte = 0x40;
        else if (enc->options.alignment == EXI_ALIGN_COMPRESSED)    options_byte = 0x80;
        rc = bs_write_bits(bs, options_byte, 8); if (rc) return rc;
    }

    /* Version: 1 byte = 0x00 (version 1) */
    rc = bs_write_bits(bs, 0u, 8); if (rc) return rc;

    return EXI_OK;
}

exi_result_t exi_encoder_sd(exi_encoder_t *enc)
{
    enc->element_depth = 0;
    enc->doc_state     = 0;
    return ec_write(enc, EXI_EVENT_SD);
}

exi_result_t exi_encoder_ed(exi_encoder_t *enc)
{
    return ec_write(enc, EXI_EVENT_ED);
}

exi_result_t exi_encoder_se(exi_encoder_t *enc,
                              const char *uri, const char *ln)
{
    exi_result_t rc = ec_write(enc, EXI_EVENT_SE); if (rc) return rc;
    rc = qname_encode(enc, uri, ln);
    ++enc->element_depth;
    return rc;
}

exi_result_t exi_encoder_ee(exi_encoder_t *enc)
{
    /* ec_write checks element_depth==1 before we decrement */
    exi_result_t rc = ec_write(enc, EXI_EVENT_EE);
    if (enc->element_depth) --enc->element_depth;
    return rc;
}

exi_result_t exi_encoder_at(exi_encoder_t *enc,
                              const char *uri, const char *ln,
                              const char *val)
{
    exi_result_t rc = ec_write(enc, EXI_EVENT_AT); if (rc) return rc;
    rc = qname_encode(enc, uri, ln);               if (rc) return rc;
    return val_encode(enc, val);
}

exi_result_t exi_encoder_ch(exi_encoder_t *enc, const char *val)
{
    exi_result_t rc = ec_write(enc, EXI_EVENT_CH); if (rc) return rc;
    return val_encode(enc, val);
}

exi_result_t exi_encoder_ns(exi_encoder_t *enc,
                              const char *uri, const char *prefix)
{
    exi_result_t rc = ec_write(enc, EXI_EVENT_NS); if (rc) return rc;
    rc = bs_write_string(&enc->stream,
                         uri ? uri : "", uri ? strlen(uri) : 0);
    if (rc) return rc;
    return bs_write_string(&enc->stream,
                           prefix ? prefix : "",
                           prefix ? strlen(prefix) : 0);
}

exi_result_t exi_encoder_finalize(exi_encoder_t *enc, size_t *bytes_written)
{
    if (bytes_written) *bytes_written = bs_bytes_used(&enc->stream);
    return EXI_OK;
}

/* ═══════════════════════════════════════════════════════════
 * Low-level Decoder
 * ═══════════════════════════════════════════════════════════ */
exi_result_t exi_decoder_init(exi_decoder_t *dec,
                               const uint8_t *buf, size_t size,
                               const exi_options_t *opts,
                               const exi_schema_registry_t *reg)
{
    if (!dec || !buf) return EXI_ERROR_NULL_PTR;
    memset(dec, 0, sizeof(*dec));
    bs_init_read(&dec->stream, buf, size);
    if (opts) dec->options = *opts;
    else { exi_options_t d = EXI_OPTIONS_DEFAULT; dec->options = d; }
    dec->registry  = reg;
    dec->doc_state = 0;
    if (reg) st_prepop_dec(dec);
    return EXI_OK;
}

exi_result_t exi_decoder_header(exi_decoder_t *dec)
{
    exi_bitstream_t *bs = &dec->stream;
    exi_result_t rc;
    uint32_t byte;
    uint8_t  bit;
    uint8_t expected[4] = {
        EXI_COOKIE_0,EXI_COOKIE_1,EXI_COOKIE_2,EXI_COOKIE_3
    };
    int i;

    /* Cookie */
    for (i = 0; i < 4; ++i) {
        rc = bs_read_bits(bs, &byte, 8); if (rc) return rc;
        if ((uint8_t)byte != expected[i]) return EXI_ERROR_INVALID_EXI;
    }

    /* Distinguishing bits: expect 1 0 */
    rc = bs_read_bit(bs, &bit); if (rc) return rc;
    if (!bit) return EXI_ERROR_INVALID_EXI;
    rc = bs_read_bit(bs, &bit); if (rc) return rc;
    if  (bit) return EXI_ERROR_INVALID_EXI;

    /* Options presence bit */
    rc = bs_read_bit(bs, &bit); if (rc) return rc;
    if (bit) {
        /* Read and apply options byte */
        uint32_t opt;
        rc = bs_read_bits(bs, &opt, 8); if (rc) return rc;
        if      ((opt & 0xC0) == 0x40) dec->options.alignment = EXI_ALIGN_BYTE_ALIGNED;
        else if ((opt & 0xC0) == 0x80) dec->options.alignment = EXI_ALIGN_COMPRESSED;
        else                           dec->options.alignment = EXI_ALIGN_BIT_PACKED;
    }

    /* Version byte */
    rc = bs_read_bits(bs, &byte, 8); if (rc) return rc;
    return EXI_OK;
}

exi_result_t exi_decoder_next_event(exi_decoder_t *dec, exi_event_t *ev)
{
    exi_result_t rc;
    if (!dec || !ev) return EXI_ERROR_NULL_PTR;
    memset(ev, 0, sizeof(*ev));

    rc = ec_read(dec, &ev->type); if (rc) return rc;

    switch (ev->type) {
    case EXI_EVENT_SD:
        dec->element_depth = 0;
        break;
    case EXI_EVENT_ED:
    case EXI_EVENT_EOF:
        break;
    case EXI_EVENT_SE:
        rc = qname_decode(dec, ev->uri, ev->local_name);
        if (rc) return rc;
        ++dec->element_depth;
        break;
    case EXI_EVENT_EE:
        if (dec->element_depth) --dec->element_depth;
        break;
    case EXI_EVENT_AT:
        rc = qname_decode(dec, ev->uri, ev->local_name); if (rc) return rc;
        rc = val_decode(dec, ev->value);                 if (rc) return rc;
        break;
    case EXI_EVENT_CH:
        rc = val_decode(dec, ev->value); if (rc) return rc;
        break;
    case EXI_EVENT_NS: {
        size_t sl;
        rc = bs_read_string(&dec->stream, ev->uri,    EXI_ST_MAX_STR_LEN, &sl);
        if (rc) return rc;
        rc = bs_read_string(&dec->stream, ev->prefix, sizeof(ev->prefix),  &sl);
        if (rc) return rc;
        break;
    }
    default:
        break;
    }
    return EXI_OK;
}

/* ═══════════════════════════════════════════════════════════
 * Minimal XML mini-parser  (no external deps)
 * ═══════════════════════════════════════════════════════════ */
typedef struct { const char *p; const char *end; } xc_t;

static void xc_ws(xc_t *c)
{ while (c->p < c->end && isspace((unsigned char)*c->p)) ++c->p; }

static void xml_unescape(char *s)
{
    char *r=s, *w=s;
    while (*r) {
        if (*r=='&') {
            if      (!strncmp(r,"&amp;", 5)){*w++='&'; r+=5;}
            else if (!strncmp(r,"&lt;",  4)){*w++='<'; r+=4;}
            else if (!strncmp(r,"&gt;",  4)){*w++='>'; r+=4;}
            else if (!strncmp(r,"&quot;",6)){*w++='"'; r+=6;}
            else if (!strncmp(r,"&apos;",6)){*w++='\'';r+=6;}
            else {*w++=*r++;}
        } else {*w++=*r++;}
    }
    *w='\0';
}

/* Minimal XML tag descriptor */
typedef struct {
    char name[TAG_NM_MAX];
    struct {
        char name[ATTR_NM_MAX];
        char value[ATTR_VAL_MAX];
    } attr[MAX_ATTRS];
    int  attr_count;
    int  self_close;
    int  is_close;
} xml_tag_t;

/* Returns: 1=open  2=self-close  3=close  0=skip  -1=eof/error */
static int xml_parse_tag(xc_t *c, xml_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));
    xc_ws(c);
    if (c->p >= c->end || *c->p != '<') return -1;
    ++c->p;

    /* Closing tag */
    if (c->p < c->end && *c->p == '/') {
        ++c->p; tag->is_close = 1;
        xc_ws(c);
        size_t n = 0;
        while (c->p<c->end && *c->p!='>' &&
               !isspace((unsigned char)*c->p))
            if (n<TAG_NM_MAX-1) tag->name[n++]=*c->p++;
            else ++c->p;
        tag->name[n]='\0';
        while (c->p<c->end && *c->p!='>') ++c->p;
        if (c->p<c->end) ++c->p;
        return 3;
    }

    /* Comment / PI / DOCTYPE: skip */
    if (c->p<c->end && (*c->p=='!' || *c->p=='?')) {
        while (c->p<c->end && *c->p!='>') ++c->p;
        if (c->p<c->end) ++c->p;
        return 0;
    }

    /* Element name */
    xc_ws(c);
    size_t n = 0;
    while (c->p<c->end && !isspace((unsigned char)*c->p) &&
           *c->p!='>' && *c->p!='/')
        if (n<TAG_NM_MAX-1) tag->name[n++]=*c->p++;
        else ++c->p;
    tag->name[n]='\0';

    /* Attributes */
    while (c->p < c->end) {
        xc_ws(c);
        if (*c->p=='>' || *c->p=='/') break;

        char aname[ATTR_NM_MAX]={0}; size_t ai=0;
        while (c->p<c->end && *c->p!='=' &&
               !isspace((unsigned char)*c->p) &&
               *c->p!='>' && *c->p!='/')
            if (ai<ATTR_NM_MAX-1) aname[ai++]=*c->p++;
            else ++c->p;
        aname[ai]='\0';

        xc_ws(c);
        if (c->p>=c->end || *c->p!='=') continue;
        ++c->p; xc_ws(c);
        if (c->p>=c->end) break;

        char q = *c->p;
        if (q!='"' && q!='\'') continue;
        ++c->p;

        char aval[ATTR_VAL_MAX]={0}; size_t vi=0;
        while (c->p<c->end && *c->p!=q)
            if (vi<ATTR_VAL_MAX-1) aval[vi++]=*c->p++;
            else ++c->p;
        aval[vi]='\0';
        if (c->p<c->end) ++c->p;

        xml_unescape(aval);
        if (tag->attr_count < MAX_ATTRS) {
            size_t nl=strlen(aname); if(nl>=ATTR_NM_MAX) nl=ATTR_NM_MAX-1;
            size_t vl=strlen(aval);  if(vl>=ATTR_VAL_MAX) vl=ATTR_VAL_MAX-1;
            memcpy(tag->attr[tag->attr_count].name,  aname, nl);
            memcpy(tag->attr[tag->attr_count].value, aval,  vl);
            tag->attr[tag->attr_count].name[nl]='\0';
            tag->attr[tag->attr_count].value[vl]='\0';
            ++tag->attr_count;
        }
    }
    if (c->p<c->end && *c->p=='/') { tag->self_close=1; ++c->p; }
    if (c->p<c->end && *c->p=='>')  ++c->p;
    return tag->self_close ? 2 : 1;
}

/* Resolve "prefix:local" → URI + localname via registry */
static void split_qname(const char *name,
                         char *uri_out, char *ln_out,
                         const exi_schema_registry_t *reg)
{
    const char *colon = strchr(name, ':');
    if (colon) {
        size_t plen = (size_t)(colon - name);
        char prefix[64] = {0};
        if (plen < sizeof(prefix)) memcpy(prefix, name, plen);
        size_t ll = strlen(colon+1);
        if (ll >= EXI_ST_MAX_STR_LEN) ll = EXI_ST_MAX_STR_LEN-1;
        memcpy(ln_out, colon+1, ll); ln_out[ll]='\0';

        if (reg) {
            uint32_t i;
            for (i=0; i<reg->count; ++i) {
                const char *ns = reg->entries[i].namespace_uri;
                if ((strcmp(prefix,"v2gci_app")==0||strcmp(prefix,"app")==0)
                    && strstr(ns,"AppProtocol"))
                    { size_t nl=strlen(ns); if(nl>=EXI_MAX_NAMESPACE_LEN)nl=EXI_MAX_NAMESPACE_LEN-1; memcpy(uri_out,ns,nl); uri_out[nl]='\0'; return; }
                if ((strcmp(prefix,"v2gci_ct")==0||strcmp(prefix,"ct")==0)
                    && strstr(ns,"CommonTypes"))
                    { size_t nl=strlen(ns); if(nl>=EXI_MAX_NAMESPACE_LEN)nl=EXI_MAX_NAMESPACE_LEN-1; memcpy(uri_out,ns,nl); uri_out[nl]='\0'; return; }
                if ((strcmp(prefix,"v2gci_cm")==0||strcmp(prefix,"cm")==0)
                    && strstr(ns,"CommonMessages"))
                    { size_t nl=strlen(ns); if(nl>=EXI_MAX_NAMESPACE_LEN)nl=EXI_MAX_NAMESPACE_LEN-1; memcpy(uri_out,ns,nl); uri_out[nl]='\0'; return; }
                if ((strcmp(prefix,"v2gci_dc")==0||strcmp(prefix,"dc")==0)
                    && strstr(ns,":DC"))
                    { size_t nl=strlen(ns); if(nl>=EXI_MAX_NAMESPACE_LEN)nl=EXI_MAX_NAMESPACE_LEN-1; memcpy(uri_out,ns,nl); uri_out[nl]='\0'; return; }
                if ((strcmp(prefix,"v2gci_ac")==0||strcmp(prefix,"ac")==0)
                    && strstr(ns,":AC"))
                    { size_t nl=strlen(ns); if(nl>=EXI_MAX_NAMESPACE_LEN)nl=EXI_MAX_NAMESPACE_LEN-1; memcpy(uri_out,ns,nl); uri_out[nl]='\0'; return; }
            }
        }
        /* fallback: use prefix as URI fragment */
        size_t pl2 = plen < EXI_MAX_NAMESPACE_LEN-1 ? plen : EXI_MAX_NAMESPACE_LEN-2;
        memcpy(uri_out, name, pl2); uri_out[pl2]='\0';
    } else {
        uri_out[0]='\0';
        size_t ll=strlen(name); if(ll>=EXI_ST_MAX_STR_LEN)ll=EXI_ST_MAX_STR_LEN-1;
        memcpy(ln_out,name,ll); ln_out[ll]='\0';
    }
}

/* ═══════════════════════════════════════════════════════════
 * High-level: exi_encode_xml
 *
 * Supports:
 *  - Bit-packed (default)
 *  - Byte-aligned  (each event code + value byte-aligned)
 *  - Compressed    (bit-packed, then zlib-deflate)
 * ═══════════════════════════════════════════════════════════ */
exi_result_t exi_encode_xml(const char *xml_in,
                              uint8_t *exi_out, size_t *out_size,
                              const exi_options_t *opts,
                              const exi_schema_registry_t *reg)
{
    if (!xml_in || !exi_out || !out_size) return EXI_ERROR_NULL_PTR;

    exi_alignment_t align = EXI_ALIGN_BIT_PACKED;
    if (opts) align = opts->alignment;

    /* For compressed mode: encode to temp buffer first, then deflate */
    uint8_t *work_buf    = exi_out;
    size_t   work_cap    = *out_size;
    uint8_t *tmp_buf     = NULL;

    if (align == EXI_ALIGN_COMPRESSED) {
        size_t xml_len = strlen(xml_in);
        tmp_buf = (uint8_t*)malloc(xml_len * 3 + 512);
        if (!tmp_buf) return EXI_ERROR_MEMORY;
        work_buf = tmp_buf;
        work_cap = xml_len * 3 + 512;
    }

    /* ── Core encode (bit-packed or byte-aligned) ── */
    exi_encoder_t *enc = (exi_encoder_t*)calloc(1, sizeof(exi_encoder_t));
    if (!enc) { free(tmp_buf); return EXI_ERROR_MEMORY; }

    xml_tag_t *tag = (xml_tag_t*)malloc(sizeof(xml_tag_t));
    if (!tag) { free(enc); free(tmp_buf); return EXI_ERROR_MEMORY; }

    exi_result_t rc = exi_encoder_init(enc, work_buf, work_cap, opts, reg);
    if (rc) goto enc_done;

    rc = exi_encoder_header(enc); if (rc) goto enc_done;
    rc = exi_encoder_sd(enc);     if (rc) goto enc_done;

    xc_t cur = { xml_in, xml_in + strlen(xml_in) };
    char txt[EXI_ST_MAX_STR_LEN];
    int r;

    while (cur.p < cur.end) {
        xc_ws(&cur);
        if (cur.p >= cur.end) break;

        if (*cur.p == '<') {
            r = xml_parse_tag(&cur, tag);
            if (r == 0) continue;

            if (r == 1 || r == 2) {
                char uri[EXI_MAX_NAMESPACE_LEN] = {0};
                char ln[EXI_ST_MAX_STR_LEN]     = {0};
                split_qname(tag->name, uri, ln, reg);

                rc = exi_encoder_se(enc, uri, ln); if (rc) goto enc_done;

                /* xmlns attributes → NS events */
                int a;
                for (a=0; a<tag->attr_count; ++a) {
                    if (strncmp(tag->attr[a].name,"xmlns",5)==0) {
                        const char *pfx = strchr(tag->attr[a].name,':');
                        rc = exi_encoder_ns(enc, tag->attr[a].value,
                                            pfx ? pfx+1 : "");
                        if (rc) goto enc_done;
                    }
                }
                /* Normal attributes */
                for (a=0; a<tag->attr_count; ++a) {
                    if (strncmp(tag->attr[a].name,"xmlns",5)==0) continue;
                    char auri[EXI_MAX_NAMESPACE_LEN]={0};
                    char aln[EXI_ST_MAX_STR_LEN]    ={0};
                    split_qname(tag->attr[a].name, auri, aln, reg);
                    rc = exi_encoder_at(enc, auri, aln, tag->attr[a].value);
                    if (rc) goto enc_done;
                }
                if (r==2) { rc = exi_encoder_ee(enc); if(rc) goto enc_done; }

            } else if (r==3) {
                rc = exi_encoder_ee(enc); if (rc) goto enc_done;
            }

        } else {
            /* Text content */
            size_t ti=0;
            while (cur.p<cur.end && *cur.p!='<')
                if (ti<sizeof(txt)-1) txt[ti++]=*cur.p++;
                else ++cur.p;
            txt[ti]='\0';

            int hw=0; size_t k;
            for (k=0;k<ti;++k)
                if (!isspace((unsigned char)txt[k])) { hw=1; break; }
            if (hw) {
                xml_unescape(txt);
                rc = exi_encoder_ch(enc, txt); if (rc) goto enc_done;
            }
        }
    }

    rc = exi_encoder_ed(enc); if (rc) goto enc_done;
    {
        size_t raw_len = 0;
        rc = exi_encoder_finalize(enc, &raw_len);
        if (rc) goto enc_done;

        if (align == EXI_ALIGN_COMPRESSED) {
            /* zlib deflate the encoded payload */
            uLongf dst_len = (uLongf)*out_size;
            int zrc = compress2(exi_out, &dst_len,
                                work_buf, (uLong)raw_len,
                                Z_BEST_COMPRESSION);
            if (zrc != Z_OK) { rc = EXI_ERROR_ENCODING; goto enc_done; }
            *out_size = (size_t)dst_len;
        } else {
            *out_size = raw_len;
        }
    }

enc_done:
    free(tag);
    free(enc);
    free(tmp_buf);
    return rc;
}

/* ═══════════════════════════════════════════════════════════
 * High-level: exi_decode_to_xml
 *
 * Supports bit-packed, byte-aligned, and zlib-compressed input.
 * ═══════════════════════════════════════════════════════════ */

/* XML-escape src → out buffer */
static size_t xml_esc(const char *src, char *out, size_t cap)
{
    size_t n=0;
    while (*src && n<cap-1) {
        switch (*src) {
        case '&': if(n+5<cap){memcpy(out+n,"&amp;",5);n+=5;} break;
        case '<': if(n+4<cap){memcpy(out+n,"&lt;",4);n+=4;}  break;
        case '>': if(n+4<cap){memcpy(out+n,"&gt;",4);n+=4;}  break;
        case '"': if(n+6<cap){memcpy(out+n,"&quot;",6);n+=6;}break;
        default:  out[n++]=*src;
        }
        ++src;
    }
    out[n]='\0';
    return n;
}

/* Element stack frame */
typedef struct {
    char full_name[EXI_ST_MAX_STR_LEN];
    char uri[EXI_MAX_NAMESPACE_LEN];
    int  has_content;
} elem_frame_t;

/* Pending NS events (xmlns attributes buffered until '>' is written) */
typedef struct {
    char uri[EXI_MAX_NAMESPACE_LEN];
    char pfx[64];
} ns_entry_t;

exi_result_t exi_decode_to_xml(const uint8_t *exi_in,  size_t exi_size,
                                 char          *xml_out, size_t *out_size,
                                 const exi_options_t         *opts,
                                 const exi_schema_registry_t *reg)
{
    if (!exi_in || !xml_out || !out_size) return EXI_ERROR_NULL_PTR;

    /* ── Detect and handle compressed input (zlib magic 0x78 0x9C etc.) ── */
    const uint8_t *dec_in   = exi_in;
    size_t         dec_size = exi_size;
    uint8_t       *inflate_buf = NULL;

    /* Check if data starts with zlib magic (0x78 ...) after EXI cookie */
    /* Compressed EXI: the encoded body (after header) is deflate-compressed.
     * Our simplified approach: if it does NOT start with $EXI cookie,
     * treat the entire buffer as zlib-compressed EXI. */
    if (exi_size >= 4 &&
        !(exi_in[0]==0x24 && exi_in[1]==0x45 &&
          exi_in[2]==0x58 && exi_in[3]==0x49)) {
        /* Try zlib inflate */
        uLongf inf_len = (uLongf)(exi_size * 10 + 4096);
        inflate_buf = (uint8_t*)malloc(inf_len);
        if (!inflate_buf) return EXI_ERROR_MEMORY;
        int zrc = uncompress(inflate_buf, &inf_len, exi_in, (uLong)exi_size);
        if (zrc != Z_OK) { free(inflate_buf); return EXI_ERROR_INVALID_EXI; }
        dec_in   = inflate_buf;
        dec_size = (size_t)inf_len;
    }

    /* ── Allocate decode context + buffers on heap ── */
    exi_decoder_t *dec   = (exi_decoder_t*)calloc(1, sizeof(exi_decoder_t));
    exi_event_t   *ev    = (exi_event_t*)calloc(1, sizeof(exi_event_t));
    elem_frame_t  *stack = (elem_frame_t*)calloc(MAX_DEPTH, sizeof(elem_frame_t));
    ns_entry_t    *ns_reg = (ns_entry_t*)calloc(MAX_NS_REG, sizeof(ns_entry_t));
    ns_entry_t    *pend   = (ns_entry_t*)calloc(MAX_PENDING_NS, sizeof(ns_entry_t));

    if (!dec || !ev || !stack || !ns_reg || !pend) {
        free(dec); free(ev); free(stack); free(ns_reg); free(pend);
        free(inflate_buf);
        return EXI_ERROR_MEMORY;
    }

    exi_result_t rc;
    size_t  cap   = *out_size;
    size_t  pos   = 0;
    int     top   = 0;
    int     tag_open  = 0;   /* 1 = opening tag not yet closed with '>' */
    int     ns_count  = 0;
    int     pend_count = 0;

/* Output write macro */
#define W(...) do { \
    int _n = snprintf(xml_out+pos, cap-pos, __VA_ARGS__); \
    if (_n<0 || (size_t)_n >= cap-pos) { rc=EXI_ERROR_BUFFER_SMALL; goto dec_done; } \
    pos += (size_t)_n; \
} while(0)

    rc = exi_decoder_init(dec, dec_in, dec_size, opts, reg);
    if (rc) goto dec_done;
    rc = exi_decoder_header(dec);
    if (rc) goto dec_done;

    int done = 0;
    while (!done) {
        rc = exi_decoder_next_event(dec, ev);
        if (rc) goto dec_done;

        char esc[EXI_ST_MAX_STR_LEN * 2];

        switch (ev->type) {

        /* ── SD ──────────────────────────────────── */
        case EXI_EVENT_SD:
            W("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
            break;

        /* ── ED / EOF ─────────────────────────────── */
        case EXI_EVENT_ED:
        case EXI_EVENT_EOF:
            done = 1;
            break;

        /* ── NS ──────────────────────────────────── */
        case EXI_EVENT_NS:
            /* Cache into global NS registry */
            if (ns_count < MAX_NS_REG) {
                size_t ul = strlen(ev->uri);
                size_t pl = strlen(ev->prefix);
                if (ul>=EXI_MAX_NAMESPACE_LEN) ul=EXI_MAX_NAMESPACE_LEN-1;
                if (pl>=64) pl=63;
                memcpy(ns_reg[ns_count].uri, ev->uri,    ul);
                ns_reg[ns_count].uri[ul]='\0';
                memcpy(ns_reg[ns_count].pfx, ev->prefix, pl);
                ns_reg[ns_count].pfx[pl]='\0';
                ++ns_count;
            }
            /* Also buffer as pending xmlns attribute */
            if (pend_count < MAX_PENDING_NS) {
                size_t ul = strlen(ev->uri);
                size_t pl = strlen(ev->prefix);
                if (ul>=EXI_MAX_NAMESPACE_LEN) ul=EXI_MAX_NAMESPACE_LEN-1;
                if (pl>=64) pl=63;
                memcpy(pend[pend_count].uri, ev->uri,    ul);
                pend[pend_count].uri[ul]='\0';
                memcpy(pend[pend_count].pfx, ev->prefix, pl);
                pend[pend_count].pfx[pl]='\0';
                ++pend_count;
            }
            break;

        /* ── SE ──────────────────────────────────── */
        case EXI_EVENT_SE: {
            /* Close any open tag first */
            if (tag_open) {
                /* Flush pending xmlns before closing */
                int pi;
                for (pi=0; pi<pend_count; ++pi) {
                    if (pend[pi].pfx[0])
                        W(" xmlns:%s=\"%s\"", pend[pi].pfx, pend[pi].uri);
                    else
                        W(" xmlns=\"%s\"", pend[pi].uri);
                }
                pend_count = 0;
                W(">\n");
                tag_open = 0;
            }

            /* Resolve URI → prefix */
            char pfx_str[64] = {0};
            if (ev->uri[0]) {
                int pi;
                for (pi=0; pi<ns_count; ++pi) {
                    if (strcmp(ns_reg[pi].uri, ev->uri)==0) {
                        size_t pl=strlen(ns_reg[pi].pfx);
                        if (pl>=sizeof(pfx_str)) pl=sizeof(pfx_str)-1;
                        memcpy(pfx_str, ns_reg[pi].pfx, pl);
                        pfx_str[pl]='\0';
                        break;
                    }
                }
            }

            /* Build full qualified name */
            char full[EXI_ST_MAX_STR_LEN] = {0};
            if (pfx_str[0])
                snprintf(full, sizeof(full), "%s:%s", pfx_str, ev->local_name);
            else {
                size_t ll=strlen(ev->local_name);
                if(ll>=sizeof(full)) ll=sizeof(full)-1;
                memcpy(full, ev->local_name, ll); full[ll]='\0';
            }

            /* Indentation */
            int d; for (d=0; d<top; ++d) W("  ");
            W("<%s", full);

            /* If no NS events seen yet for root element, emit xmlns here */
            if (ev->uri[0] && top==0 && ns_count==0 && pend_count==0)
                W(" xmlns=\"%s\"", ev->uri);

            tag_open = 1;

            /* Push frame */
            if (top < MAX_DEPTH) {
                size_t fl=strlen(full);
                if(fl>=EXI_ST_MAX_STR_LEN) fl=EXI_ST_MAX_STR_LEN-1;
                memcpy(stack[top].full_name, full, fl);
                stack[top].full_name[fl]='\0';
                size_t ul=strlen(ev->uri);
                if(ul>=EXI_MAX_NAMESPACE_LEN) ul=EXI_MAX_NAMESPACE_LEN-1;
                memcpy(stack[top].uri, ev->uri, ul);
                stack[top].uri[ul]='\0';
                stack[top].has_content = 0;
                ++top;
            }
            break;
        }

        /* ── AT ──────────────────────────────────── */
        case EXI_EVENT_AT:
            /* Attributes come while tag_open==1 */
            xml_esc(ev->value, esc, sizeof(esc));
            if (ev->uri[0])
                W(" %s:%s=\"%s\"", ev->uri, ev->local_name, esc);
            else
                W(" %s=\"%s\"", ev->local_name, esc);
            break;

        /* ── CH ──────────────────────────────────── */
        case EXI_EVENT_CH:
            if (tag_open) {
                /* Flush pending xmlns */
                int pi;
                for (pi=0; pi<pend_count; ++pi) {
                    if (pend[pi].pfx[0])
                        W(" xmlns:%s=\"%s\"", pend[pi].pfx, pend[pi].uri);
                    else
                        W(" xmlns=\"%s\"", pend[pi].uri);
                }
                pend_count = 0;
                W(">");
                tag_open = 0;
            }
            xml_esc(ev->value, esc, sizeof(esc));
            W("%s", esc);
            if (top>0) stack[top-1].has_content = 1;
            break;

        /* ── EE ──────────────────────────────────── */
        case EXI_EVENT_EE:
            if (top > 0) {
                --top;
                if (tag_open) {
                    /* Self-closing: flush pending xmlns then /> */
                    int pi;
                    for (pi=0; pi<pend_count; ++pi) {
                        if (pend[pi].pfx[0])
                            W(" xmlns:%s=\"%s\"", pend[pi].pfx, pend[pi].uri);
                        else
                            W(" xmlns=\"%s\"", pend[pi].uri);
                    }
                    pend_count = 0;
                    W("/>\n");
                    tag_open = 0;
                } else if (stack[top].has_content) {
                    /* Inline content: close tag on same line */
                    W("</%s>\n", stack[top].full_name);
                } else {
                    /* Child elements: indented close tag */
                    int d; for (d=0; d<top; ++d) W("  ");
                    W("</%s>\n", stack[top].full_name);
                }
            }
            break;

        default:
            break;
        }
    }

    xml_out[pos] = '\0';
    *out_size = pos;

dec_done:
#undef W
    free(pend);
    free(ns_reg);
    free(stack);
    free(ev);
    free(dec);
    free(inflate_buf);
    return rc;
}

/* ═══════════════════════════════════════════════════════════
 * File-based convenience API
 * ═══════════════════════════════════════════════════════════ */
static char *file_read_text(const char *path, size_t *out_len)
{
    FILE *f = fopen(path,"rb");
    if (!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    char *buf = (char*)malloc((size_t)sz+1);
    if (!buf) { fclose(f); return NULL; }
    if (sz>0 && fread(buf,1,(size_t)sz,f)!=(size_t)sz)
        { free(buf); fclose(f); return NULL; }
    buf[sz]='\0';
    if (out_len) *out_len=(size_t)sz;
    fclose(f); return buf;
}

static uint8_t *file_read_bin(const char *path, size_t *out_len)
{
    FILE *f = fopen(path,"rb");
    if (!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    uint8_t *buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (sz>0 && fread(buf,1,(size_t)sz,f)!=(size_t)sz)
        { free(buf); fclose(f); return NULL; }
    if (out_len) *out_len=(size_t)sz;
    fclose(f); return buf;
}

exi_result_t exi_encode_file(const char *xml_path, const char *exi_path,
                               const exi_options_t *opts,
                               const exi_schema_registry_t *reg)
{
    if (!xml_path || !exi_path) return EXI_ERROR_NULL_PTR;
    size_t xml_len = 0;
    char *xml_buf = file_read_text(xml_path, &xml_len);
    if (!xml_buf) return EXI_ERROR_IO;

    size_t cap = xml_len * 4 + 1024;
    uint8_t *exi_buf = (uint8_t*)malloc(cap);
    if (!exi_buf) { free(xml_buf); return EXI_ERROR_MEMORY; }

    size_t exi_len = cap;
    exi_result_t rc = exi_encode_xml(xml_buf, exi_buf, &exi_len, opts, reg);
    free(xml_buf);

    if (rc == EXI_OK) {
        FILE *f = fopen(exi_path, "wb");
        if (!f) { free(exi_buf); return EXI_ERROR_IO; }
        fwrite(exi_buf, 1, exi_len, f);
        fclose(f);
    }
    free(exi_buf);
    return rc;
}

exi_result_t exi_decode_file(const char *exi_path, const char *xml_path,
                               const exi_options_t *opts,
                               const exi_schema_registry_t *reg)
{
    if (!exi_path || !xml_path) return EXI_ERROR_NULL_PTR;
    size_t exi_len = 0;
    uint8_t *exi_buf = file_read_bin(exi_path, &exi_len);
    if (!exi_buf) return EXI_ERROR_IO;

    size_t cap = exi_len * 20 + 4096;
    char *xml_buf = (char*)malloc(cap);
    if (!xml_buf) { free(exi_buf); return EXI_ERROR_MEMORY; }

    size_t xml_len = cap;
    exi_result_t rc = exi_decode_to_xml(exi_buf, exi_len,
                                         xml_buf, &xml_len, opts, reg);
    free(exi_buf);

    if (rc == EXI_OK) {
        FILE *f = fopen(xml_path, "wb");
        if (!f) { free(xml_buf); return EXI_ERROR_IO; }
        fwrite(xml_buf, 1, xml_len, f);
        fclose(f);
    }
    free(xml_buf);
    return rc;
}

/* ═══════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════ */
const char *exi_version(void)
{
    return "libexi15118 " LIBEXI_VERSION_STR
           " (W3C EXI 1.0 | ISO 15118-20:2022"
           " | bit-packed, byte-aligned, compressed)";
}

const char *exi_result_str(exi_result_t rc)
{
    switch (rc) {
    case EXI_OK:                   return "OK";
    case EXI_ERROR_NULL_PTR:       return "Null pointer";
    case EXI_ERROR_BUFFER_SMALL:   return "Buffer too small";
    case EXI_ERROR_INVALID_XML:    return "Invalid XML";
    case EXI_ERROR_INVALID_EXI:    return "Invalid EXI stream";
    case EXI_ERROR_MEMORY:         return "Out of memory";
    case EXI_ERROR_UNSUPPORTED:    return "Unsupported feature";
    case EXI_ERROR_OVERFLOW:       return "Overflow";
    case EXI_ERROR_ENCODING:       return "Encoding error";
    case EXI_ERROR_DECODING:       return "Decoding error";
    case EXI_ERROR_SCHEMA:         return "Schema error";
    case EXI_ERROR_IO:             return "I/O error";
    case EXI_ERROR_SCHEMA_MISMATCH:return "Schema mismatch";
    default:                       return "Unknown error";
    }
}

void exi_hexdump(FILE *out, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        if (i && i%16==0) fprintf(out, "\n");
        fprintf(out, "%02X ", buf[i]);
    }
    fprintf(out, "\n");
}
