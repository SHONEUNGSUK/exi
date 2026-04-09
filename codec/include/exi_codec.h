/**
 * @file    exi_codec.h
 * @brief   ISO 15118-20 aware EXI (Efficient XML Interchange) Codec
 *
 * W3C EXI 1.0 schemaless + schema-informed encoding/decoding
 * Targeted at ISO 15118-20:2022 V2G communication messages.
 *
 * Architecture
 * ────────────
 *  ┌─────────────────────────────────────┐
 *  │        Application / Client         │
 *  └──────────────┬──────────────────────┘
 *                 │  exi_encode_xml() / exi_decode_to_xml()
 *  ┌──────────────▼──────────────────────┐
 *  │   libexi15118.so  (this library)    │
 *  │  ┌────────────┐  ┌───────────────┐  │
 *  │  │ XSD Parser │  │  EXI Grammar  │  │
 *  │  └──────┬─────┘  └───────┬───────┘  │
 *  │  ┌──────▼─────────────────▼───────┐  │
 *  │  │     Bit-stream Engine          │  │
 *  │  │  (bit-packed / byte-aligned)   │  │
 *  │  └────────────────────────────────┘  │
 *  └─────────────────────────────────────┘
 *
 * Reference: https://www.w3.org/TR/exi/
 */

#ifndef EXI_CODEC_H
#define EXI_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════
 * Version
 * ═══════════════════════════════════════════════════════════ */
#define LIBEXI_VERSION_MAJOR  1
#define LIBEXI_VERSION_MINOR  0
#define LIBEXI_VERSION_PATCH  0
#define LIBEXI_VERSION_STR    "1.0.0"

/* ═══════════════════════════════════════════════════════════
 * Return codes
 * ═══════════════════════════════════════════════════════════ */
typedef enum exi_result {
    EXI_OK                   =  0,
    EXI_ERROR_NULL_PTR       = -1,
    EXI_ERROR_BUFFER_SMALL   = -2,
    EXI_ERROR_INVALID_XML    = -3,
    EXI_ERROR_INVALID_EXI    = -4,
    EXI_ERROR_MEMORY         = -5,
    EXI_ERROR_UNSUPPORTED    = -6,
    EXI_ERROR_OVERFLOW       = -7,
    EXI_ERROR_ENCODING       = -8,
    EXI_ERROR_DECODING       = -9,
    EXI_ERROR_SCHEMA         = -10,
    EXI_ERROR_IO             = -11,
    EXI_ERROR_SCHEMA_MISMATCH = -12
} exi_result_t;

/* ═══════════════════════════════════════════════════════════
 * EXI Options  (W3C §4)
 * ═══════════════════════════════════════════════════════════ */

/** Alignment options per W3C §4.1 */
typedef enum {
    EXI_ALIGN_BIT_PACKED    = 0,   /**< Default bit-packed                  */
    EXI_ALIGN_BYTE_ALIGNED  = 1,   /**< Byte-aligned (easier to debug)      */
    EXI_ALIGN_COMPRESSED    = 2    /**< zlib channel compression            */
} exi_alignment_t;

/** Preservation options per W3C §4.3 */
typedef struct {
    unsigned int comments       : 1;
    unsigned int pis            : 1;
    unsigned int dtd            : 1;
    unsigned int prefixes       : 1;
    unsigned int lexical_values : 1;
} exi_preserve_t;

/** Full options block */
typedef struct {
    exi_alignment_t  alignment;
    exi_preserve_t   preserve;
    int              strict;              /**< Strict schema-informed mode   */
    int              fragment;            /**< Fragment vs document          */
    uint32_t         block_size;          /**< Compression block size        */
    uint32_t         value_max_length;    /**< Max string value length       */
    uint32_t         value_partition_capacity;
    int              schema_informed;     /**< Use loaded schema grammar     */
} exi_options_t;

/** Default options initialiser macro */
#define EXI_OPTIONS_DEFAULT \
    { EXI_ALIGN_BIT_PACKED, {0,0,0,0,0}, 0, 0, 1000000, 0xFFFF, 0xFFFF, 0 }

/* ═══════════════════════════════════════════════════════════
 * XSD Schema registry  (ISO 15118-20 namespace → schema)
 * ═══════════════════════════════════════════════════════════ */

/** ISO 15118-20 well-known namespace URIs */
#define EXI_NS_ISO15118_APP  "urn:iso:std:iso:15118:-20:AppProtocol"
#define EXI_NS_ISO15118_CT   "urn:iso:std:iso:15118:-20:CommonTypes"
#define EXI_NS_ISO15118_CM   "urn:iso:std:iso:15118:-20:CommonMessages"
#define EXI_NS_ISO15118_DC   "urn:iso:std:iso:15118:-20:DC"
#define EXI_NS_ISO15118_AC   "urn:iso:std:iso:15118:-20:AC"

#define EXI_MAX_SCHEMAS       16
#define EXI_MAX_NAMESPACE_LEN 128
#define EXI_MAX_PATH_LEN      512

/** Single schema registration */
typedef struct {
    char  namespace_uri[EXI_MAX_NAMESPACE_LEN];
    char  xsd_path[EXI_MAX_PATH_LEN];
    int   loaded;
} exi_schema_entry_t;

/** Schema registry (holds all registered XSD → namespace mappings) */
typedef struct {
    exi_schema_entry_t entries[EXI_MAX_SCHEMAS];
    uint32_t           count;
    char               xsd_dir[EXI_MAX_PATH_LEN]; /**< Base XSD directory  */
} exi_schema_registry_t;

/* ═══════════════════════════════════════════════════════════
 * Bit-stream
 * ═══════════════════════════════════════════════════════════ */
typedef struct {
    uint8_t  *data;
    size_t    capacity;
    size_t    byte_pos;
    uint8_t   bit_pos;     /**< 0..7, MSB first */
} exi_bitstream_t;

/* ═══════════════════════════════════════════════════════════
 * String-table partition
 * ═══════════════════════════════════════════════════════════ */
#define EXI_ST_MAX_ENTRIES   512
#define EXI_ST_MAX_STR_LEN   256

typedef struct {
    char     str[EXI_ST_MAX_STR_LEN];
    size_t   len;
} exi_st_entry_t;

typedef struct {
    exi_st_entry_t  entries[EXI_ST_MAX_ENTRIES];
    uint32_t        count;
} exi_string_table_t;

/* ═══════════════════════════════════════════════════════════
 * EXI Event types  (W3C §8.1)
 * ═══════════════════════════════════════════════════════════ */
typedef enum {
    EXI_EVENT_SD  = 0,
    EXI_EVENT_ED  = 1,
    EXI_EVENT_SE  = 2,
    EXI_EVENT_EE  = 3,
    EXI_EVENT_AT  = 4,
    EXI_EVENT_CH  = 5,
    EXI_EVENT_NS  = 6,
    EXI_EVENT_CM  = 7,
    EXI_EVENT_PI  = 8,
    EXI_EVENT_DT  = 9,
    EXI_EVENT_ER  = 10,
    EXI_EVENT_SC  = 11,
    EXI_EVENT_EOF = 12   /**< Synthetic: end of stream                     */
} exi_event_type_t;

/** Decoded event (output of decoder per step) */
typedef struct {
    exi_event_type_t  type;
    char  uri[EXI_ST_MAX_STR_LEN];
    char  local_name[EXI_ST_MAX_STR_LEN];
    char  prefix[64];
    char  value[EXI_ST_MAX_STR_LEN];
} exi_event_t;

/* ═══════════════════════════════════════════════════════════
 * Encoder / Decoder contexts
 * ═══════════════════════════════════════════════════════════ */
typedef struct {
    exi_bitstream_t     stream;
    exi_options_t       options;
    exi_string_table_t  uri_table;
    exi_string_table_t  local_name_table;
    exi_string_table_t  value_table;
    uint32_t            element_depth;
    uint32_t            doc_state;          /**< 0=pre-SD 1=body 2=post-EE */
    const exi_schema_registry_t *registry; /**< NULL = schemaless          */
} exi_encoder_t;

typedef struct {
    exi_bitstream_t     stream;
    exi_options_t       options;
    exi_string_table_t  uri_table;
    exi_string_table_t  local_name_table;
    exi_string_table_t  value_table;
    uint32_t            element_depth;
    uint32_t            doc_state;          /**< 0=pre-SD 1=body 2=post-EE */
    const exi_schema_registry_t *registry;
} exi_decoder_t;

/* ═══════════════════════════════════════════════════════════
 * Schema Registry API
 * ═══════════════════════════════════════════════════════════ */

/**
 * @brief Initialise an empty schema registry.
 * @param reg      Pointer to registry struct to initialise.
 * @param xsd_dir  Base directory that contains all XSD files.
 */
exi_result_t exi_registry_init(exi_schema_registry_t *reg,
                                const char            *xsd_dir);

/**
 * @brief Register one namespace / XSD-file mapping.
 * @param reg           Registry.
 * @param namespace_uri Namespace URI (e.g. EXI_NS_ISO15118_DC).
 * @param xsd_filename  Filename relative to xsd_dir.
 */
exi_result_t exi_registry_add(exi_schema_registry_t *reg,
                               const char            *namespace_uri,
                               const char            *xsd_filename);

/**
 * @brief Load all registered XSD files (parses element/type names).
 *        Validates that every file exists and is well-formed XML.
 */
exi_result_t exi_registry_load(exi_schema_registry_t *reg);

/**
 * @brief Print registry status to stdout.
 */
void exi_registry_print(const exi_schema_registry_t *reg);

/* ═══════════════════════════════════════════════════════════
 * High-level Codec API
 * ═══════════════════════════════════════════════════════════ */

/**
 * @brief  Encode a null-terminated XML string to EXI binary.
 *
 * @param  xml_in   Null-terminated XML source.
 * @param  exi_out  Caller-allocated output buffer.
 * @param  out_size [in] capacity of exi_out, [out] bytes written.
 * @param  opts     EXI options (NULL → defaults).
 * @param  reg      Schema registry (NULL → schemaless).
 * @return EXI_OK on success.
 */
exi_result_t exi_encode_xml(const char                  *xml_in,
                             uint8_t                     *exi_out,
                             size_t                      *out_size,
                             const exi_options_t         *opts,
                             const exi_schema_registry_t *reg);

/**
 * @brief  Decode EXI binary back to XML text.
 *
 * @param  exi_in    EXI binary buffer.
 * @param  exi_size  Byte count of exi_in.
 * @param  xml_out   Caller-allocated output buffer.
 * @param  out_size  [in] capacity, [out] bytes written (excl. NUL).
 * @param  opts      EXI options (NULL → defaults).
 * @param  reg       Schema registry (NULL → schemaless).
 * @return EXI_OK on success.
 */
exi_result_t exi_decode_to_xml(const uint8_t               *exi_in,
                                size_t                       exi_size,
                                char                        *xml_out,
                                size_t                      *out_size,
                                const exi_options_t         *opts,
                                const exi_schema_registry_t *reg);

/**
 * @brief  Encode an XML file to an EXI file.
 */
exi_result_t exi_encode_file(const char                  *xml_path,
                              const char                  *exi_path,
                              const exi_options_t         *opts,
                              const exi_schema_registry_t *reg);

/**
 * @brief  Decode an EXI file to an XML file.
 */
exi_result_t exi_decode_file(const char                  *exi_path,
                              const char                  *xml_path,
                              const exi_options_t         *opts,
                              const exi_schema_registry_t *reg);

/* ═══════════════════════════════════════════════════════════
 * Low-level Encoder API
 * ═══════════════════════════════════════════════════════════ */
exi_result_t exi_encoder_init    (exi_encoder_t *enc,
                                   uint8_t *buf, size_t cap,
                                   const exi_options_t *opts,
                                   const exi_schema_registry_t *reg);
exi_result_t exi_encoder_header  (exi_encoder_t *enc);
exi_result_t exi_encoder_sd      (exi_encoder_t *enc);
exi_result_t exi_encoder_ed      (exi_encoder_t *enc);
exi_result_t exi_encoder_se      (exi_encoder_t *enc,
                                   const char *uri, const char *local_name);
exi_result_t exi_encoder_ee      (exi_encoder_t *enc);
exi_result_t exi_encoder_at      (exi_encoder_t *enc,
                                   const char *uri, const char *local_name,
                                   const char *value);
exi_result_t exi_encoder_ch      (exi_encoder_t *enc, const char *value);
exi_result_t exi_encoder_ns      (exi_encoder_t *enc,
                                   const char *uri, const char *prefix);
exi_result_t exi_encoder_finalize(exi_encoder_t *enc, size_t *bytes_written);

/* ═══════════════════════════════════════════════════════════
 * Low-level Decoder API
 * ═══════════════════════════════════════════════════════════ */
exi_result_t exi_decoder_init      (exi_decoder_t *dec,
                                     const uint8_t *buf, size_t size,
                                     const exi_options_t *opts,
                                     const exi_schema_registry_t *reg);
exi_result_t exi_decoder_header    (exi_decoder_t *dec);
exi_result_t exi_decoder_next_event(exi_decoder_t *dec, exi_event_t *ev);

/* ═══════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════ */
const char *exi_version     (void);
const char *exi_result_str  (exi_result_t rc);
void        exi_hexdump     (FILE *out, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EXI_CODEC_H */
