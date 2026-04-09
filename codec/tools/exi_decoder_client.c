/**
 * @file  exi_decoder_client.c
 * @brief ISO 15118-20 EXI → XML decoder client
 *
 * Uses libexi15118.so to convert an EXI binary message back to
 * human-readable ISO 15118-20 XML, with event-trace and statistics.
 *
 * Usage:
 *   exi_decoder_client [options] <input.exi> <output.xml>
 *
 * Options:
 *   -s <dir>   XSD directory (default: ../xsd/iso15118-2020)
 *   -a <mode>  Alignment: bit | byte  (must match encoder)
 *   -t         Trace: show each decoded EXI event
 *   -p         Pretty-print with indentation
 *   -h         Show this help
 *
 * Examples:
 *   ./exi_decoder_client out.exi decoded.xml
 *   ./exi_decoder_client -t -s ../../xsd/iso15118-2020 dc_precharge.exi decoded.xml
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "exi_codec.h"
#include "iso15118_types.h"

/* ─────────────────────────────────────────────────────────
 * ANSI colours
 * ───────────────────────────────────────────────────────── */
#define C_RESET  "\033[0m"
#define C_BOLD   "\033[1m"
#define C_GREEN  "\033[32m"
#define C_CYAN   "\033[36m"
#define C_YELLOW "\033[33m"
#define C_RED    "\033[31m"
#define C_BLUE   "\033[34m"
#define C_GREY   "\033[90m"
#define C_MAGENTA "\033[35m"

/* ─────────────────────────────────────────────────────────
 * CLI arguments
 * ───────────────────────────────────────────────────────── */
typedef struct {
    const char     *exi_path;
    const char     *xml_path;
    const char     *xsd_dir;
    exi_alignment_t alignment;
    int             trace;
    int             pretty;
} cli_args_t;

static void usage(const char *prog)
{
    printf(C_BOLD "Usage:" C_RESET " %s [options] <input.exi> <output.xml>\n\n"
           C_BOLD "Options:\n" C_RESET
           "  -s <dir>   XSD directory  (default: " DEFAULT_XSD_DIR ")\n"
           "  -a bit     Bit-packed alignment (default)\n"
           "  -a byte    Byte-aligned decoding\n"
           "  -t         Trace each decoded EXI event to stdout\n"
           "  -p         Pretty-print XML output\n"
           "  -h         Show this help\n\n"
           C_BOLD "Note:\n" C_RESET
           "  Alignment must match what was used during encoding.\n\n",
           prog);
}

static int parse_args(int argc, char **argv, cli_args_t *a)
{
    int i;
    memset(a, 0, sizeof(*a));
    a->xsd_dir   = DEFAULT_XSD_DIR;
    a->alignment = EXI_ALIGN_BIT_PACKED;
    a->pretty    = 1;   /* default on */

    for (i = 1; i < argc; ++i) {
        if      (!strcmp(argv[i],"-h")) { usage(argv[0]); exit(0); }
        else if (!strcmp(argv[i],"-t")) { a->trace  = 1; }
        else if (!strcmp(argv[i],"-p")) { a->pretty = 1; }
        else if (!strcmp(argv[i],"-s") && i+1<argc) { a->xsd_dir = argv[++i]; }
        else if (!strcmp(argv[i],"-a") && i+1<argc) {
            ++i;
            if (!strcmp(argv[i],"byte")) a->alignment = EXI_ALIGN_BYTE_ALIGNED;
            else                         a->alignment = EXI_ALIGN_BIT_PACKED;
        } else if (!a->exi_path) { a->exi_path = argv[i]; }
        else if   (!a->xml_path) { a->xml_path = argv[i]; }
    }
    return (a->exi_path && a->xml_path) ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────
 * Event trace printer
 * ───────────────────────────────────────────────────────── */
static const char *ev_name_str(exi_event_type_t t)
{
    switch (t) {
    case EXI_EVENT_SD:  return "SD ";
    case EXI_EVENT_ED:  return "ED ";
    case EXI_EVENT_SE:  return "SE ";
    case EXI_EVENT_EE:  return "EE ";
    case EXI_EVENT_AT:  return "AT ";
    case EXI_EVENT_CH:  return "CH ";
    case EXI_EVENT_NS:  return "NS ";
    case EXI_EVENT_CM:  return "CM ";
    case EXI_EVENT_PI:  return "PI ";
    case EXI_EVENT_DT:  return "DT ";
    case EXI_EVENT_EOF: return "EOF";
    default:            return "?  ";
    }
}

static void print_event(const exi_event_t *ev, uint32_t depth)
{
    const char *col;
    switch (ev->type) {
    case EXI_EVENT_SD:
    case EXI_EVENT_ED:  col = C_BLUE;    break;
    case EXI_EVENT_SE:  col = C_GREEN;   break;
    case EXI_EVENT_EE:  col = C_CYAN;    break;
    case EXI_EVENT_AT:  col = C_YELLOW;  break;
    case EXI_EVENT_CH:  col = C_MAGENTA; break;
    default:            col = C_GREY;
    }

    printf(C_GREY "  %3u" C_RESET " %s%s" C_RESET " ", depth, col, ev_name_str(ev->type));

    switch (ev->type) {
    case EXI_EVENT_SE:
    case EXI_EVENT_EE:
        if (ev->uri[0])
            printf("{%s}%s", ev->uri, ev->local_name);
        else
            printf("%s", ev->local_name);
        break;
    case EXI_EVENT_AT:
        if (ev->uri[0])
            printf("{%s}%s = \"%s\"", ev->uri, ev->local_name, ev->value);
        else
            printf("%s = \"%s\"", ev->local_name, ev->value);
        break;
    case EXI_EVENT_CH:
        if (strlen(ev->value) > 60)
            printf("\"%.57s...\"", ev->value);
        else
            printf("\"%s\"", ev->value);
        break;
    case EXI_EVENT_NS:
        printf("xmlns:%s = %s", ev->prefix, ev->uri);
        break;
    default:
        break;
    }
    printf("\n");
}

/* ─────────────────────────────────────────────────────────
 * Validate the EXI cookie
 * ───────────────────────────────────────────────────────── */
static int check_exi_magic(const uint8_t *buf, size_t len)
{
    if (len < 4) return 0;
    return buf[0]==0x24 && buf[1]==0x45 && buf[2]==0x58 && buf[3]==0x49;
}

/* ─────────────────────────────────────────────────────────
 * Print banner
 * ───────────────────────────────────────────────────────── */
static void print_banner(void)
{
    printf(C_BOLD C_CYAN
        "\n╔══════════════════════════════════════════════════════════════╗\n"
          "║  ISO 15118-20 EXI Decoder Client                            ║\n"
          "║  " C_RESET C_CYAN "%s" C_BOLD "  ║\n"
          "╚══════════════════════════════════════════════════════════════╝\n"
          C_RESET "\n",
        exi_version());
}

/* ─────────────────────────────────────────────────────────
 * Low-level trace decode (parallel pass for -t flag)
 * ───────────────────────────────────────────────────────── */
static void trace_decode(const uint8_t *exi_buf, size_t exi_len,
                          const exi_options_t *opts,
                          const exi_schema_registry_t *reg)
{
    exi_decoder_t dec;
    exi_event_t   ev;
    exi_result_t  rc;

    rc = exi_decoder_init(&dec, exi_buf, exi_len, opts, reg);
    if (rc) { printf(C_RED "  trace init failed\n" C_RESET); return; }

    rc = exi_decoder_header(&dec);
    if (rc) { printf(C_RED "  trace header failed\n" C_RESET); return; }

    printf(C_GREY "  Dep Event QName / Value\n"
                  "  ─── ───── ────────────────────────────────\n" C_RESET);

    uint32_t event_count = 0;
    int done = 0;
    while (!done) {
        rc = exi_decoder_next_event(&dec, &ev);
        if (rc) {
            printf(C_RED "  decode error at event %u: %s\n" C_RESET,
                   event_count, exi_result_str(rc));
            break;
        }
        print_event(&ev, dec.element_depth);
        ++event_count;
        if (ev.type == EXI_EVENT_ED || ev.type == EXI_EVENT_EOF) done = 1;
    }
    printf(C_GREY "  ─── total: %u events ───\n\n" C_RESET, event_count);
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
        fprintf(stderr,
                "  Hint : Check that '%s' contains the ISO 15118-20 XSD files.\n",
                args.xsd_dir);
        return 1;
    }
    printf(C_GREEN "  ✓  Schema registry loaded (%u namespaces)\n\n" C_RESET,
           reg.count);

    /* ── 2. Read EXI binary ── */
    printf(C_BOLD "[ 2/4 ]" C_RESET " Reading EXI binary...\n");
    printf("        File : %s\n", args.exi_path);

    FILE *ef = fopen(args.exi_path, "rb");
    if (!ef) {
        fprintf(stderr, C_RED "  ERROR: Cannot open '%s'\n" C_RESET,
                args.exi_path);
        return 1;
    }
    fseek(ef, 0, SEEK_END); long exi_sz = ftell(ef); rewind(ef);
    uint8_t *exi_buf = (uint8_t*)malloc((size_t)exi_sz);
    if (!exi_buf) { fclose(ef); fprintf(stderr, "OOM\n"); return 1; }
    if (fread(exi_buf, 1, (size_t)exi_sz, ef) != (size_t)exi_sz && exi_sz > 0) { free(exi_buf); fclose(ef); fprintf(stderr,"Read error\n"); return 1; }
    fclose(ef);

    printf("        Size : %ld bytes\n", exi_sz);

    /* Validate EXI cookie */
    if (!check_exi_magic(exi_buf, (size_t)exi_sz)) {
        fprintf(stderr,
            C_RED "  ERROR: File does not begin with EXI cookie ($EXI).\n"
                  "         Is this really an EXI-encoded file?\n" C_RESET);
        free(exi_buf);
        return 1;
    }
    printf(C_GREEN "  ✓  EXI cookie validated ($EXI)\n\n" C_RESET);

    /* Optional hex preview */
    if (args.trace) {
        size_t show = (size_t)exi_sz < 64 ? (size_t)exi_sz : 64u;
        printf(C_GREY "  First %zu bytes:\n  ", show);
        exi_hexdump(stdout, exi_buf, show);
        if ((size_t)exi_sz > 64)
            printf("  ... (%ld bytes total)\n", exi_sz);
        printf(C_RESET "\n");
    }

    /* ── 3. Event trace (optional) ── */
    exi_options_t opts = EXI_OPTIONS_DEFAULT;
    opts.alignment = args.alignment;

    if (args.trace) {
        printf(C_BOLD "[ 3/4 ]" C_RESET " EXI event trace:\n\n");
        trace_decode(exi_buf, (size_t)exi_sz, &opts, &reg);
    } else {
        printf(C_BOLD "[ 3/4 ]" C_RESET " (use -t for event trace)\n\n");
    }

    /* ── 4. Decode to XML ── */
    printf(C_BOLD "[ 4/4 ]" C_RESET " Decoding EXI → XML...\n");
    printf("        Alignment : %s\n",
           args.alignment == EXI_ALIGN_BIT_PACKED ? "bit-packed" : "byte-aligned");

    size_t xml_cap = (size_t)exi_sz * 15 + 4096;
    char  *xml_buf = (char*)malloc(xml_cap);
    if (!xml_buf) {
        free(exi_buf);
        fprintf(stderr, C_RED "  ERROR: Out of memory\n" C_RESET);
        return 1;
    }
    size_t xml_len = xml_cap;

    clock_t t0 = clock();
    rc = exi_decode_to_xml(exi_buf, (size_t)exi_sz,
                            xml_buf, &xml_len,
                            &opts, &reg);
    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC * 1000.0;

    if (rc != EXI_OK) {
        fprintf(stderr, C_RED "  ERROR: Decoding failed: %s\n" C_RESET,
                exi_result_str(rc));
        free(exi_buf); free(xml_buf);
        return 1;
    }

    /* Write XML output */
    FILE *xf = fopen(args.xml_path, "wb");
    if (!xf) {
        fprintf(stderr, C_RED "  ERROR: Cannot write '%s'\n" C_RESET,
                args.xml_path);
        free(exi_buf); free(xml_buf);
        return 1;
    }
    fwrite(xml_buf, 1, xml_len, xf);
    fclose(xf);

    double expansion = (exi_sz > 0)
        ? (double)xml_len / (double)exi_sz
        : 0.0;

    printf(C_GREEN "  ✓  Decoding complete\n" C_RESET);
    printf("        Output    : %s\n",   args.xml_path);
    printf("        EXI size  : %6ld bytes\n", exi_sz);
    printf("        XML size  : %6zu bytes\n", xml_len);
    printf("        Expansion : " C_YELLOW "%.1fx" C_RESET "\n", expansion);
    printf("        Time      : %.3f ms\n\n", elapsed);

    /* Preview decoded XML */
    printf(C_GREY "  Decoded XML (preview):\n"
           "  ─────────────────────────────────────────\n" C_RESET);
    size_t preview = xml_len < 600 ? xml_len : 600u;
    printf("%.*s\n", (int)preview, xml_buf);
    if (xml_len > 600)
        printf(C_GREY "  ... (%zu bytes total, see %s)\n" C_RESET,
               xml_len, args.xml_path);

    printf("\n" C_GREEN C_BOLD
           "  ══════════════════════════════════════════\n"
           "  Decoding complete: %s\n"
           "  ══════════════════════════════════════════\n"
           C_RESET "\n",
           args.xml_path);

    free(exi_buf);
    free(xml_buf);
    return 0;
}
