# ══════════════════════════════════════════════════════════════════════════════
#  Makefile  —  ISO 15118-20 EXI Codec
#  libexi15118.so + exi_encoder_client + exi_decoder_client
# ══════════════════════════════════════════════════════════════════════════════

SRC_DIR      := src
INC_DIR      := include
TOOLS_DIR    := tools
EXAMPLES_DIR := examples
BUILD_DIR    := build
XSD_DIR      := xsd/iso15118-2020

CC           := gcc
CFLAGS_BASE  := -std=c99 -Wall -Wextra -Wpedantic \
                -Wno-unused-parameter \
                -Wno-stringop-truncation \
                -Wno-format-truncation \
                -fPIC -I$(INC_DIR)
CFLAGS_OPT   := -O2
CFLAGS       := $(CFLAGS_BASE) $(CFLAGS_OPT)
LDFLAGS      :=
LIBS         := -lz -lm

ifeq ($(DEBUG),1)
CFLAGS       := $(CFLAGS_BASE) -g3 -O0 -DDEBUG \
                -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS      += -fsanitize=address,undefined
endif

ifeq ($(V),1)
Q :=
else
Q := @
endif

LIB_BASE     := exi15118
LIB_SO       := $(BUILD_DIR)/lib$(LIB_BASE).so
LIB_A        := $(BUILD_DIR)/lib$(LIB_BASE).a
LIB_SONAME   := lib$(LIB_BASE).so.1
ENC_BIN      := $(BUILD_DIR)/exi_encoder_client
DEC_BIN      := $(BUILD_DIR)/exi_decoder_client

LIB_SRCS     := $(SRC_DIR)/exi_codec.c
LIB_OBJS     := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
ENC_SRC      := $(TOOLS_DIR)/exi_encoder_client.c
DEC_SRC      := $(TOOLS_DIR)/exi_decoder_client.c

PREFIX       := /usr/local
INST_LIB     := $(PREFIX)/lib
INST_INC     := $(PREFIX)/include
INST_BIN     := $(PREFIX)/bin

.PHONY: all lib static encoder decoder \
        test test-quick test-dc test-ac test-app \
        clean package install uninstall help dirs

all: dirs lib encoder decoder
	@echo ""
	@echo "  ┌──────────────────────────────────────────────────────────────┐"
	@echo "  │  ISO 15118-20 EXI Codec — Build Successful                   │"
	@echo "  │                                                               │"
	@echo "  │  Library  : $(LIB_SO)"
	@echo "  │  Encoder  : $(ENC_BIN)"
	@echo "  │  Decoder  : $(DEC_BIN)"
	@echo "  │                                                               │"
	@echo "  │  make test-quick   — interactive demo                        │"
	@echo "  │  make test         — full round-trip suite                   │"
	@echo "  └──────────────────────────────────────────────────────────────┘"

dirs:
	$(Q)mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | dirs
	@echo "  CC  $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

lib: $(LIB_SO)
$(LIB_SO): $(LIB_OBJS) | dirs
	@echo "  LD  $@ [shared]"
	$(Q)$(CC) -shared -Wl,-soname,$(LIB_SONAME) \
	    $(LDFLAGS) $(LIBS) -o $@ $(LIB_OBJS)
	$(Q)ln -sf lib$(LIB_BASE).so $(BUILD_DIR)/$(LIB_SONAME)

static: $(LIB_A)
$(LIB_A): $(LIB_OBJS) | dirs
	@echo "  AR  $@"
	$(Q)ar rcs $@ $(LIB_OBJS)

encoder: $(ENC_BIN)
$(ENC_BIN): $(ENC_SRC) $(LIB_SO) | dirs
	@echo "  CC  $< -> $@"
	$(Q)$(CC) $(CFLAGS) $< \
	    -L$(BUILD_DIR) -l$(LIB_BASE) \
	    -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) $(LIBS) -o $@

decoder: $(DEC_BIN)
$(DEC_BIN): $(DEC_SRC) $(LIB_SO) | dirs
	@echo "  CC  $< -> $@"
	$(Q)$(CC) $(CFLAGS) $< \
	    -L$(BUILD_DIR) -l$(LIB_BASE) \
	    -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) $(LIBS) -o $@

# ── one-shot test helper ──────────────────────────────────────────────────────
define run_test
	$(eval _XML  := $(1))
	$(eval _BASE := $(notdir $(basename $(_XML))))
	$(eval _XSD  := $(CURDIR)/$(XSD_DIR))
	@printf "  %-44s" "$(_BASE).xml"
	@cd $(BUILD_DIR) && \
	  ./exi_encoder_client -s "$(_XSD)" \
	      "$(CURDIR)/$(_XML)" "$(_BASE).exi" > /dev/null 2>&1 \
	  || { echo "[ FAIL encode ]"; exit 1; }
	@cd $(BUILD_DIR) && \
	  ./exi_decoder_client -s "$(_XSD)" \
	      "$(_BASE).exi" "$(_BASE).decoded.xml" > /dev/null 2>&1 \
	  || { echo "[ FAIL decode ]"; exit 1; }
	@ORIG=$$(wc -c < "$(CURDIR)/$(_XML)"); \
	 EXI=$$(wc -c  < "$(BUILD_DIR)/$(_BASE).exi"); \
	 SAV=$$((ORIG - EXI)); PCT=$$((SAV * 100 / ORIG)); \
	 printf "[ OK ] orig=%5dB  exi=%4dB  saved=%3d%%\n" $$ORIG $$EXI $$PCT
endef

# ── Full test suite ───────────────────────────────────────────────────────────
test: all
	@echo ""
	@echo "══════════════════════════════════════════════════════════════════════"
	@echo "  ISO 15118-20 EXI Codec — Round-Trip Test Suite (13 messages)"
	@echo "══════════════════════════════════════════════════════════════════════"
	@echo ""
	@echo "  ── AppProtocol ─────────────────────────────────────────────────────"
	$(call run_test,$(EXAMPLES_DIR)/app_protocol_req.xml)
	@echo ""
	@echo "  ── CommonMessages ──────────────────────────────────────────────────"
	$(call run_test,$(EXAMPLES_DIR)/session_setup_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/authorization_setup_res.xml)
	$(call run_test,$(EXAMPLES_DIR)/service_discovery_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/power_delivery_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/session_stop_req.xml)
	@echo ""
	@echo "  ── DC Charging ─────────────────────────────────────────────────────"
	$(call run_test,$(EXAMPLES_DIR)/dc_cable_check_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_charge_param_disc_res.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_precharge_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_charge_loop_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_welding_detection_req.xml)
	@echo ""
	@echo "  ── AC Charging ─────────────────────────────────────────────────────"
	$(call run_test,$(EXAMPLES_DIR)/ac_charge_param_disc_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/ac_charge_loop_res.xml)
	@echo ""
	@echo "══════════════════════════════════════════════════════════════════════"
	@echo "  All 13 tests passed"
	@echo "══════════════════════════════════════════════════════════════════════"

test-dc: all
	@echo "  ── DC Charging Tests ────────────────────────────────────────────"
	$(call run_test,$(EXAMPLES_DIR)/dc_cable_check_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_charge_param_disc_res.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_precharge_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_charge_loop_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/dc_welding_detection_req.xml)

test-ac: all
	@echo "  ── AC Charging Tests ────────────────────────────────────────────"
	$(call run_test,$(EXAMPLES_DIR)/ac_charge_param_disc_req.xml)
	$(call run_test,$(EXAMPLES_DIR)/ac_charge_loop_res.xml)

test-app: all
	@echo "  ── AppProtocol Tests ────────────────────────────────────────────"
	$(call run_test,$(EXAMPLES_DIR)/app_protocol_req.xml)

test-quick: all
	@echo ""
	@echo "════════════════════════════════════════════════════════════════════"
	@echo "  Quick Demo: SessionSetupReq"
	@echo "════════════════════════════════════════════════════════════════════"
	@cd $(BUILD_DIR) && XSD="$(CURDIR)/$(XSD_DIR)" && \
	  ./exi_encoder_client -s "$$XSD" -v -x \
	      ../$(EXAMPLES_DIR)/session_setup_req.xml session_setup_req.exi
	@echo ""
	@cd $(BUILD_DIR) && XSD="$(CURDIR)/$(XSD_DIR)" && \
	  ./exi_decoder_client -s "$$XSD" -t \
	      session_setup_req.exi session_setup_req.decoded.xml
	@echo ""
	@echo "════════════════════════════════════════════════════════════════════"
	@echo "  DC_ChargeLoopReq (most complex message)"
	@echo "════════════════════════════════════════════════════════════════════"
	@cd $(BUILD_DIR) && XSD="$(CURDIR)/$(XSD_DIR)" && \
	  ./exi_encoder_client -s "$$XSD" -x \
	      ../$(EXAMPLES_DIR)/dc_charge_loop_req.xml dc_charge_loop_req.exi
	@echo ""
	@cd $(BUILD_DIR) && XSD="$(CURDIR)/$(XSD_DIR)" && \
	  ./exi_decoder_client -s "$$XSD" -t \
	      dc_charge_loop_req.exi dc_charge_loop_req.decoded.xml

clean:
	@echo "  RM  $(BUILD_DIR)/"
	$(Q)rm -rf $(BUILD_DIR)

package: clean all
	@echo "  PKG iso15118_exi.tar.gz"
	$(Q)tar czf iso15118_exi.tar.gz \
	    --exclude='build/*.o' \
	    --exclude='build/*.exi' \
	    --exclude='build/*.xml' \
	    --transform 's|^\./||' \
	    --transform 's|^|iso15118_exi/|' \
	    ./Makefile ./README.md \
	    ./include ./src ./tools ./xsd ./examples ./build
	@ls -lh iso15118_exi.tar.gz

install: all
	install -d $(INST_LIB) $(INST_INC) $(INST_BIN)
	install -m 755 $(LIB_SO)   $(INST_LIB)/$(LIB_SONAME)
	ln -sf $(LIB_SONAME)       $(INST_LIB)/lib$(LIB_BASE).so
	install -m 644 $(INC_DIR)/exi_codec.h      $(INST_INC)/
	install -m 644 $(INC_DIR)/exi_bitstream.h  $(INST_INC)/
	install -m 644 $(INC_DIR)/iso15118_types.h $(INST_INC)/
	install -m 755 $(ENC_BIN)  $(INST_BIN)/
	install -m 755 $(DEC_BIN)  $(INST_BIN)/
	-ldconfig
	@echo "  Installed → $(PREFIX)"

uninstall:
	rm -f $(INST_LIB)/$(LIB_SONAME) $(INST_LIB)/lib$(LIB_BASE).so
	rm -f $(INST_INC)/exi_codec.h $(INST_INC)/exi_bitstream.h \
	      $(INST_INC)/iso15118_types.h
	rm -f $(INST_BIN)/exi_encoder_client $(INST_BIN)/exi_decoder_client
	-ldconfig

help:
	@echo ""
	@echo "  ISO 15118-20 EXI Codec — Makefile"
	@echo ""
	@echo "  Build:   make [all | lib | static | encoder | decoder]"
	@echo "  Test:    make [test | test-quick | test-dc | test-ac | test-app]"
	@echo "  Utility: make [clean | package | install | uninstall]"
	@echo ""
	@echo "  Variables: DEBUG=1  V=1  PREFIX=/path"
	@echo ""
	@echo "  Encoder: $(ENC_BIN)"
	@echo "    -s <xsd_dir>   XSD directory (default: ../xsd/iso15118-2020)"
	@echo "    -a bit|byte    Alignment mode"
	@echo "    -v             Hex dump of EXI output"
	@echo "    -x             Round-trip decode verify"
	@echo ""
	@echo "  Decoder: $(DEC_BIN)"
	@echo "    -s <xsd_dir>   XSD directory"
	@echo "    -a bit|byte    Alignment mode"
	@echo "    -t             EXI event trace"
	@echo ""
