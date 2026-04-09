/**
 * @file  exi_encoder_client.c
 * @brief ISO 15118-20 XML → EXI encoder client
 *
 * Uses libexi15118.so to convert an ISO 15118-20 XML message to
 * EXI binary format, with optional hex dump and statistics output.
 *
 * Usage:
 *   exi_encoder_client [options] <input.xml> <output.exi>
 *
 * Options:
 *   -s <dir>      XSD directory (default: ../xsd/iso15118-2020)
 *   -a <mode>     Alignment: bit | byte  (default: bit)
 *   -v            Verbose: show EXI hex dump
 *   -x            Dump decoded XML back (round-trip verification)
 *   -h            Show this help
 *
 * Examples:
 *   ./exi_encoder_client session_setup_req.xml out.exi
 *   ./exi_encoder_client -s ../../xsd/iso15118-2020 -v dc_precharge_req.xml dc_precharge.exi
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "exi_codec.h"
#include "iso15118_types.h"

/* ─────────────────────────────────────────────────────────
 * ANSI colour macros
 * ───────────────────────────────────────────────────────── */
#define C_RESET  "\033[0m"
#define C_BOLD   "\033[1m"
#define C_GREEN  "\033[32m"
#define C_CYAN   "\033[36m"
#define C_YELLOW "\033[33m"
#define C_RED    "\033[31m"
#define C_BLUE   "\033[34m"
#define C_GREY   "\033[90m"

/* ─────────────────────────────────────────────────────────
 * CLI arguments
 * ───────────────────────────────────────────────────────── */
typedef struct {
    const char     *xml_path;
    const char     *exi_path;
    const char     *xsd_dir;
    exi_alignment_t alignment;
    int             verbose;
    int             roundtrip;
} cli_args_t;

static void usage(const char *prog)
{
    printf(C_BOLD "Usage:" C_RESET " %s [options] <input.xml> <output.exi>\n\n"
           C_BOLD "Options:\n" C_RESET
           "  -s <dir>   XSD directory  (default: " DEFAULT_XSD_DIR ")\n"
           "  -a bit     Bit-packed alignment (default)\n"
           "  -a byte    Byte-aligned encoding\n"
           "  -v         Verbose: print EXI hex dump\n"
           "  -x         Round-trip: decode EXI back and show XML\n"
           "  -h         Show this help\n\n"
           C_BOLD "ISO 15118-20 message examples:\n" C_RESET
           "  session_setup_req.xml, dc_charge_loop_req.xml,\n"
           "  ac_charge_param_disc_res.xml, ...\n\n",
           prog);
}

static int parse_args(int argc, char **argv, cli_args_t *a)
{
    int i;
    memset(a, 0, sizeof(*a));
    a->xsd_dir   = DEFAULT_XSD_DIR;
    a->alignment = EXI_ALIGN_BIT_PACKED;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h")) { usage(argv[0]); exit(0); }
        else if (!strcmp(argv[i], "-v")) { a->verbose   = 1; }
        else if (!strcmp(argv[i], "-x")) { a->roundtrip = 1; }
        else if (!strcmp(argv[i], "-s") && i+1 < argc) {
            a->xsd_dir = argv[++i];
        } else if (!strcmp(argv[i], "-a") && i+1 < argc) {
            ++i;
            if (!strcmp(argv[i],"byte")) a->alignment = EXI_ALIGN_BYTE_ALIGNED;
            else                         a->alignment = EXI_ALIGN_BIT_PACKED;
        } else if (!a->xml_path) {
            a->xml_path = argv[i];
        } else if (!a->exi_path) {
            a->exi_path = argv[i];
        }
    }
    return (a->xml_path && a->exi_path) ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────
 * File helpers
 * ───────────────────────────────────────────────────────── */
static char *file_read_all(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz && sz > 0) { free(buf); fclose(f); return NULL; }
    buf[sz] = '\0';
    if (out_len) *out_len = (size_t)sz;
    fclose(f);
    return buf;
}

/* Detect ISO 15118-20 root element from XML text */
static void detect_message(const char *xml, char *msg_name, size_t cap)
{
    const char *p = xml;
    msg_name[0] = '\0';
    /* Skip XML declaration and comments */
    while (*p) {
        while (*p && *p != '<') ++p;
        if (!*p) return;
        ++p;
        if (*p == '?') { while (*p && *p != '>') ++p; continue; }
        if (*p == '!') { while (*p && *p != '>') ++p; continue; }
        /* First real element */
        const char *start = p;
        while (*p && *p != '>' && *p != ' ' && *p != '\n' && *p != '\r') ++p;
        size_t len = (size_t)(p - start);
        /* Strip namespace prefix */
        const char *col = memchr(start, ':', len);
        if (col) { start = col+1; len = (size_t)(p - start); }
        if (len >= cap) len = cap-1;
        memcpy(msg_name, start, len);
        msg_name[len] = '\0';
        return;
    }
}

/* ─────────────────────────────────────────────────────────
 * Print banner
 * ───────────────────────────────────────────────────────── */
static void print_banner(void)
{
    printf(C_BOLD C_CYAN
        "\n╔══════════════════════════════════════════════════════════════╗\n"
          "║  ISO 15118-20 EXI Encoder Client                            ║\n"
          "║  " C_RESET C_CYAN "%s" C_BOLD "  ║\n"
          "╚══════════════════════════════════════════════════════════════╝\n"
          C_RESET "\n",
        exi_version());
}

/* ─────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    cli_args_t args;
    exi_result_t rc;

    print_banner();

    if (parse_args(argc, argv, &args) != 0) {
        usage(argv[0]);
        return 1;
    }

    /* ── 1. Load schema registry ── */
    printf(C_BOLD "[ 1/4 ]" C_RESET " Loading ISO 15118-20 schema registry...\n");
    printf("        XSD dir : %s\n\n", args.xsd_dir);

    exi_schema_registry_t reg;
    rc = iso15118_registry_create(&reg, args.xsd_dir);
    if (rc != EXI_OK) {
        fprintf(stderr, C_RED "  ERROR: Schema load failed: %s\n" C_RESET,
                exi_result_str(rc));
        fprintf(stderr, "  Hint : Check that '%s' contains the ISO 15118-20 XSD files.\n",
                args.xsd_dir);
        return 1;
    }
    printf(C_GREEN "  ✓  Schema registry loaded (%u namespaces)\n" C_RESET,
           reg.count);
    if (args.verbose) exi_registry_print(&reg);
    printf("\n");

    /* ── 2. Read input XML ── */
    printf(C_BOLD "[ 2/4 ]" C_RESET " Reading XML input...\n");
    printf("        File : %s\n", args.xml_path);

    size_t xml_len = 0;
    char *xml_buf = file_read_all(args.xml_path, &xml_len);
    if (!xml_buf) {
        fprintf(stderr, C_RED "  ERROR: Cannot read '%s': %s\n" C_RESET,
                args.xml_path, strerror(0));
        return 1;
    }
    printf("        Size : %zu bytes\n", xml_len);

    /* Detect message type */
    char msg_name[256] = "";
    detect_message(xml_buf, msg_name, sizeof(msg_name));
    if (msg_name[0]) {
        iso15118_msg_type_t mtype = iso15118_msg_type_from_root(msg_name);
        printf("        Root : " C_YELLOW "%s" C_RESET, msg_name);
        if (mtype)
            printf(" (type=0x%04X)", (unsigned)mtype);
        printf("\n");
    }
    printf("\n");

    /* ── 3. Encode to EXI ── */
    printf(C_BOLD "[ 3/4 ]" C_RESET " Encoding XML → EXI...\n");
    printf("        Alignment : %s\n",
           args.alignment == EXI_ALIGN_BIT_PACKED ? "bit-packed" : "byte-aligned");

    exi_options_t opts = EXI_OPTIONS_DEFAULT;
    opts.alignment = args.alignment;

    size_t exi_cap = xml_len * 3 + 512;
    uint8_t *exi_buf = (uint8_t*)malloc(exi_cap);
    if (!exi_buf) {
        free(xml_buf);
        fprintf(stderr, C_RED "  ERROR: Out of memory\n" C_RESET);
        return 1;
    }

    clock_t t0 = clock();
    size_t exi_len = exi_cap;
    rc = exi_encode_xml(xml_buf, exi_buf, &exi_len, &opts, &reg);
    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC * 1000.0;

    if (rc != EXI_OK) {
        fprintf(stderr, C_RED "  ERROR: Encoding failed: %s\n" C_RESET,
                exi_result_str(rc));
        free(xml_buf); free(exi_buf);
        return 1;
    }

    /* Write EXI output file */
    FILE *ef = fopen(args.exi_path, "wb");
    if (!ef) {
        fprintf(stderr, C_RED "  ERROR: Cannot write '%s'\n" C_RESET,
                args.exi_path);
        free(xml_buf); free(exi_buf);
        return 1;
    }
    fwrite(exi_buf, 1, exi_len, ef);
    fclose(ef);

    double ratio = 100.0 * (1.0 - (double)exi_len / (double)xml_len);

    printf(C_GREEN "  ✓  Encoding complete\n" C_RESET);
    printf("        Output   : %s\n", args.exi_path);
    printf("        XML size : %6zu bytes\n", xml_len);
    printf("        EXI size : %6zu bytes\n", exi_len);
    printf("        Reduction: " C_YELLOW "%5.1f%%" C_RESET "\n", ratio);
    printf("        Time     : %.3f ms\n", elapsed);
    printf("\n");

    /* Optional hex dump */
    if (args.verbose) {
        printf(C_GREY "  EXI binary (%zu bytes):\n", exi_len);
        size_t show = exi_len < 128 ? exi_len : 128;
        exi_hexdump(stdout, exi_buf, show);
        if (exi_len > 128) printf("  ... (%zu bytes total)\n", exi_len);
        printf(C_RESET "\n");
    }

    /* ── 4. Optional round-trip decode ── */
    if (args.roundtrip) {
        printf(C_BOLD "[ 4/4 ]" C_RESET
               " Round-trip: decoding EXI → XML for verification...\n");

        size_t xml_out_cap = exi_len * 15 + 4096;
        char  *xml_out_buf = (char*)malloc(xml_out_cap);
        if (!xml_out_buf) {
            fprintf(stderr, C_RED "  ERROR: Out of memory\n" C_RESET);
            free(xml_buf); free(exi_buf);
            return 1;
        }
        size_t xml_out_len = xml_out_cap;
        rc = exi_decode_to_xml(exi_buf, exi_len,
                                xml_out_buf, &xml_out_len,
                                &opts, &reg);
        if (rc != EXI_OK) {
            fprintf(stderr, C_RED "  ERROR: Round-trip decode failed: %s\n"
                    C_RESET, exi_result_str(rc));
        } else {
            printf(C_GREEN "  ✓  Round-trip decode OK (%zu bytes)\n\n"
                   C_RESET, xml_out_len);
            printf(C_GREY "  Decoded XML:\n" C_RESET
                   "  %.*s\n",
                   (int)(xml_out_len < 800 ? xml_out_len : 800),
                   xml_out_buf);
            if (xml_out_len > 800)
                printf(C_GREY "  ... (%zu bytes total)\n" C_RESET, xml_out_len);
        }
        free(xml_out_buf);
    } else {
        printf(C_BOLD "[ 4/4 ]" C_RESET " (use -x flag for round-trip verification)\n");
    }

    printf("\n" C_GREEN C_BOLD
           "  ══════════════════════════════════════════\n"
           "  Encoding complete: %s\n"
           "  ══════════════════════════════════════════\n"
           C_RESET "\n",
           args.exi_path);

    free(xml_buf);
    free(exi_buf);
    return 0;
}
