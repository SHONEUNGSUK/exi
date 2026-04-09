/**
 * @file  iso15118_types.h
 * @brief ISO 15118-20 message type IDs and schema metadata.
 *
 * Provides message-type constants, namespace mappings, and
 * helper macros used by the encoder/decoder and client programs.
 */

#ifndef ISO15118_TYPES_H
#define ISO15118_TYPES_H

#include "exi_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════
 * ISO 15118-20 Message Type IDs
 * ═══════════════════════════════════════════════════════════ */
typedef enum {
    /* AppProtocol */
    MSG_SUPPORTED_APP_PROTOCOL_REQ  = 0x0001,
    MSG_SUPPORTED_APP_PROTOCOL_RES  = 0x0002,

    /* CommonMessages */
    MSG_SESSION_SETUP_REQ           = 0x0101,
    MSG_SESSION_SETUP_RES           = 0x0102,
    MSG_AUTHORIZATION_SETUP_REQ     = 0x0103,
    MSG_AUTHORIZATION_SETUP_RES     = 0x0104,
    MSG_AUTHORIZATION_REQ           = 0x0105,
    MSG_AUTHORIZATION_RES           = 0x0106,
    MSG_SERVICE_DISCOVERY_REQ       = 0x0107,
    MSG_SERVICE_DISCOVERY_RES       = 0x0108,
    MSG_SERVICE_DETAIL_REQ          = 0x0109,
    MSG_SERVICE_DETAIL_RES          = 0x010A,
    MSG_SERVICE_SELECTION_REQ       = 0x010B,
    MSG_SERVICE_SELECTION_RES       = 0x010C,
    MSG_SCHEDULE_EXCHANGE_REQ       = 0x010D,
    MSG_SCHEDULE_EXCHANGE_RES       = 0x010E,
    MSG_POWER_DELIVERY_REQ          = 0x010F,
    MSG_POWER_DELIVERY_RES          = 0x0110,
    MSG_SESSION_STOP_REQ            = 0x0111,
    MSG_SESSION_STOP_RES            = 0x0112,

    /* DC Charging */
    MSG_DC_CHARGE_PARAM_DISC_REQ    = 0x0201,
    MSG_DC_CHARGE_PARAM_DISC_RES    = 0x0202,
    MSG_DC_CABLE_CHECK_REQ          = 0x0203,
    MSG_DC_CABLE_CHECK_RES          = 0x0204,
    MSG_DC_PRE_CHARGE_REQ           = 0x0205,
    MSG_DC_PRE_CHARGE_RES           = 0x0206,
    MSG_DC_CHARGE_LOOP_REQ          = 0x0207,
    MSG_DC_CHARGE_LOOP_RES          = 0x0208,
    MSG_DC_WELDING_DETECTION_REQ    = 0x0209,
    MSG_DC_WELDING_DETECTION_RES    = 0x020A,

    /* AC Charging */
    MSG_AC_CHARGE_PARAM_DISC_REQ    = 0x0301,
    MSG_AC_CHARGE_PARAM_DISC_RES    = 0x0302,
    MSG_AC_CHARGE_LOOP_REQ          = 0x0303,
    MSG_AC_CHARGE_LOOP_RES          = 0x0304,
} iso15118_msg_type_t;

/* ═══════════════════════════════════════════════════════════
 * XSD filenames within the iso15118-2020 directory
 * ═══════════════════════════════════════════════════════════ */
#define XSD_APP_PROTOCOL    "V2GCI_AppProtocol.xsd"
#define XSD_COMMON_TYPES    "V2GCI_CommonTypes.xsd"
#define XSD_COMMON_MESSAGES "V2GCI_CommonMessages.xsd"
#define XSD_DC              "V2GCI_DC.xsd"
#define XSD_AC              "V2GCI_AC.xsd"

/* Default XSD directory (relative to binary) */
#define DEFAULT_XSD_DIR     "../xsd/iso15118-2020"

/* ═══════════════════════════════════════════════════════════
 * ISO 15118-20 Schema Registry factory
 * ═══════════════════════════════════════════════════════════ */

/**
 * @brief  Build and load the ISO 15118-20 schema registry.
 *
 * Registers all five namespace → XSD mappings, then calls
 * exi_registry_load() to validate every XSD file exists.
 *
 * @param  reg      Output registry (caller-allocated).
 * @param  xsd_dir  Directory containing the ISO 15118-20 XSD files.
 * @return EXI_OK on success.
 */
static inline exi_result_t iso15118_registry_create(
        exi_schema_registry_t *reg,
        const char            *xsd_dir)
{
    exi_result_t rc;

    rc = exi_registry_init(reg, xsd_dir);
    if (rc != EXI_OK) return rc;

    rc = exi_registry_add(reg, EXI_NS_ISO15118_APP, XSD_APP_PROTOCOL);
    if (rc != EXI_OK) return rc;

    rc = exi_registry_add(reg, EXI_NS_ISO15118_CT, XSD_COMMON_TYPES);
    if (rc != EXI_OK) return rc;

    rc = exi_registry_add(reg, EXI_NS_ISO15118_CM, XSD_COMMON_MESSAGES);
    if (rc != EXI_OK) return rc;

    rc = exi_registry_add(reg, EXI_NS_ISO15118_DC, XSD_DC);
    if (rc != EXI_OK) return rc;

    rc = exi_registry_add(reg, EXI_NS_ISO15118_AC, XSD_AC);
    if (rc != EXI_OK) return rc;

    return exi_registry_load(reg);
}

/* ═══════════════════════════════════════════════════════════
 * Helper: map root element name → message type ID
 * ═══════════════════════════════════════════════════════════ */
static inline iso15118_msg_type_t iso15118_msg_type_from_root(
        const char *root_element)
{
    if (!root_element) return 0;
    struct { const char *name; iso15118_msg_type_t id; } map[] = {
        {"supportedAppProtocolReq",      MSG_SUPPORTED_APP_PROTOCOL_REQ},
        {"supportedAppProtocolRes",      MSG_SUPPORTED_APP_PROTOCOL_RES},
        {"SessionSetupReq",              MSG_SESSION_SETUP_REQ},
        {"SessionSetupRes",              MSG_SESSION_SETUP_RES},
        {"AuthorizationSetupReq",        MSG_AUTHORIZATION_SETUP_REQ},
        {"AuthorizationSetupRes",        MSG_AUTHORIZATION_SETUP_RES},
        {"AuthorizationReq",             MSG_AUTHORIZATION_REQ},
        {"AuthorizationRes",             MSG_AUTHORIZATION_RES},
        {"ServiceDiscoveryReq",          MSG_SERVICE_DISCOVERY_REQ},
        {"ServiceDiscoveryRes",          MSG_SERVICE_DISCOVERY_RES},
        {"ServiceDetailReq",             MSG_SERVICE_DETAIL_REQ},
        {"ServiceDetailRes",             MSG_SERVICE_DETAIL_RES},
        {"ServiceSelectionReq",          MSG_SERVICE_SELECTION_REQ},
        {"ServiceSelectionRes",          MSG_SERVICE_SELECTION_RES},
        {"ScheduleExchangeReq",          MSG_SCHEDULE_EXCHANGE_REQ},
        {"ScheduleExchangeRes",          MSG_SCHEDULE_EXCHANGE_RES},
        {"PowerDeliveryReq",             MSG_POWER_DELIVERY_REQ},
        {"PowerDeliveryRes",             MSG_POWER_DELIVERY_RES},
        {"SessionStopReq",               MSG_SESSION_STOP_REQ},
        {"SessionStopRes",               MSG_SESSION_STOP_RES},
        {"DC_ChargeParameterDiscoveryReq", MSG_DC_CHARGE_PARAM_DISC_REQ},
        {"DC_ChargeParameterDiscoveryRes", MSG_DC_CHARGE_PARAM_DISC_RES},
        {"DC_CableCheckReq",             MSG_DC_CABLE_CHECK_REQ},
        {"DC_CableCheckRes",             MSG_DC_CABLE_CHECK_RES},
        {"DC_PreChargeReq",              MSG_DC_PRE_CHARGE_REQ},
        {"DC_PreChargeRes",              MSG_DC_PRE_CHARGE_RES},
        {"DC_ChargeLoopReq",             MSG_DC_CHARGE_LOOP_REQ},
        {"DC_ChargeLoopRes",             MSG_DC_CHARGE_LOOP_RES},
        {"DC_WeldingDetectionReq",       MSG_DC_WELDING_DETECTION_REQ},
        {"DC_WeldingDetectionRes",       MSG_DC_WELDING_DETECTION_RES},
        {"AC_ChargeParameterDiscoveryReq", MSG_AC_CHARGE_PARAM_DISC_REQ},
        {"AC_ChargeParameterDiscoveryRes", MSG_AC_CHARGE_PARAM_DISC_RES},
        {"AC_ChargeLoopReq",             MSG_AC_CHARGE_LOOP_REQ},
        {"AC_ChargeLoopRes",             MSG_AC_CHARGE_LOOP_RES},
        {NULL, 0}
    };
    int i;
    for (i = 0; map[i].name; ++i)
        if (__builtin_strcmp(root_element, map[i].name) == 0)
            return map[i].id;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ISO15118_TYPES_H */
