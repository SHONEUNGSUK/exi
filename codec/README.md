# ISO 15118-20 EXI Codec

**W3C EXI 1.0 기반 ISO 15118-20:2022 전기차 충전 통신 메시지 이진 압축 라이브러리**

---

## 프로젝트 구조

```
iso15118_exi/
├── Makefile                        ← 빌드 시스템
├── README.md
│
├── include/                        ← 공개 헤더
│   ├── exi_codec.h                 ← 메인 API
│   ├── exi_bitstream.h             ← 비트 스트림 프리미티브
│   └── iso15118_types.h            ← ISO 15118-20 타입 및 팩토리
│
├── src/
│   └── exi_codec.c                 ← 라이브러리 구현체
│
├── tools/
│   ├── exi_encoder_client.c        ← XML → EXI 클라이언트
│   └── exi_decoder_client.c        ← EXI → XML 클라이언트
│
├── xsd/
│   └── iso15118-2020/              ← ISO 15118-20 XSD 스키마 폴더
│       ├── V2GCI_AppProtocol.xsd
│       ├── V2GCI_CommonTypes.xsd
│       ├── V2GCI_CommonMessages.xsd
│       ├── V2GCI_DC.xsd
│       └── V2GCI_AC.xsd
│
├── examples/                       ← 샘플 ISO 15118-20 XML 메시지
│   ├── session_setup_req.xml
│   ├── service_discovery_req.xml
│   ├── dc_precharge_req.xml
│   ├── dc_charge_loop_req.xml
│   ├── ac_charge_param_disc_req.xml
│   └── power_delivery_req.xml
│
└── build/                          ← 빌드 결과물 (make 후 생성)
    ├── libexi15118.so
    ├── exi_encoder_client
    └── exi_decoder_client
```

---

## 빌드

```bash
# 전체 빌드 (라이브러리 + 클라이언트 2종)
make

# 공유 라이브러리만
make lib

# 디버그 빌드 (AddressSanitizer 포함)
make DEBUG=1
```

빌드 결과:
| 파일 | 설명 |
|------|------|
| `build/libexi15118.so` | EXI 코덱 공유 라이브러리 |
| `build/exi_encoder_client` | XML → EXI 변환 클라이언트 |
| `build/exi_decoder_client` | EXI → XML 변환 클라이언트 |

---

## 클라이언트 사용법

### XML → EXI 인코딩

```bash
cd build

# 기본 사용
./exi_encoder_client ../examples/session_setup_req.xml out.exi

# 커스텀 XSD 폴더 지정
./exi_encoder_client -s ../xsd/iso15118-2020 \
    ../examples/dc_charge_loop_req.xml dc_charge_loop.exi

# Hex 덤프 + 라운드트립 검증
./exi_encoder_client -v -x \
    ../examples/dc_precharge_req.xml dc_precharge.exi

# 바이트 정렬 모드
./exi_encoder_client -a byte \
    ../examples/session_setup_req.xml out_byte.exi
```

#### 옵션 (exi_encoder_client)
| 옵션 | 설명 |
|------|------|
| `-s <dir>` | XSD 디렉터리 경로 (기본: `../xsd/iso15118-2020`) |
| `-a bit` | 비트 패킹 정렬 (기본값, W3C EXI §4.1) |
| `-a byte` | 바이트 정렬 인코딩 |
| `-v` | Verbose: EXI 바이너리 Hex 덤프 출력 |
| `-x` | 라운드트립: 인코딩 후 즉시 디코딩하여 검증 |
| `-h` | 도움말 |

---

### EXI → XML 디코딩

```bash
cd build

# 기본 디코딩
./exi_decoder_client out.exi decoded.xml

# 이벤트 트레이스 출력
./exi_decoder_client -t out.exi decoded.xml

# 커스텀 XSD 폴더 + 트레이스
./exi_decoder_client -s ../xsd/iso15118-2020 -t \
    dc_charge_loop.exi dc_charge_loop_decoded.xml
```

#### 옵션 (exi_decoder_client)
| 옵션 | 설명 |
|------|------|
| `-s <dir>` | XSD 디렉터리 경로 |
| `-a bit` | 비트 패킹 (기본값, 인코딩과 동일해야 함) |
| `-a byte` | 바이트 정렬 |
| `-t` | EXI 이벤트 트레이스 출력 (SD/SE/AT/CH/EE/ED) |
| `-h` | 도움말 |

---

## 테스트

```bash
# 전체 round-trip 테스트 (6개 샘플 XML)
make test

# 단일 빠른 테스트
make test-quick
```

예시 출력:
```
══════════════════════════════════════════════════════════════
  ISO 15118-20 EXI Codec  —  Round-Trip Test Suite
══════════════════════════════════════════════════════════════
  session_setup_req.xml                    [ OK  ] 43B exi  (-76.4%)
  service_discovery_req.xml                [ OK  ] 38B exi  (-65.1%)
  dc_precharge_req.xml                     [ OK  ] 62B exi  (-68.3%)
  dc_charge_loop_req.xml                   [ OK  ] 89B exi  (-71.2%)
  ac_charge_param_disc_req.xml             [ OK  ] 55B exi  (-67.8%)
  power_delivery_req.xml                   [ OK  ] 47B exi  (-62.5%)

  Results: 6 passed, 0 failed
══════════════════════════════════════════════════════════════
```

---

## C API 사용법

### 고수준 API (파일 기반)

```c
#include "exi_codec.h"
#include "iso15118_types.h"

// 1. 스키마 레지스트리 생성 (ISO 15118-20 전용 팩토리)
exi_schema_registry_t reg;
iso15118_registry_create(&reg, "xsd/iso15118-2020");

// 2. XML → EXI 파일 변환
exi_encode_file("dc_charge_loop_req.xml", "output.exi", NULL, &reg);

// 3. EXI → XML 파일 변환
exi_decode_file("output.exi", "decoded.xml", NULL, &reg);
```

### 고수준 API (메모리 기반)

```c
// XML → EXI (메모리)
uint8_t exi_buf[4096];
size_t  exi_len = sizeof(exi_buf);
exi_encode_xml(xml_string, exi_buf, &exi_len, NULL, &reg);

// EXI → XML (메모리)
char   xml_out[16384];
size_t xml_len = sizeof(xml_out);
exi_decode_to_xml(exi_buf, exi_len, xml_out, &xml_len, NULL, &reg);
```

### 저수준 이벤트 API

```c
// 인코더
exi_encoder_t enc;
uint8_t buf[4096];
exi_encoder_init(&enc, buf, sizeof(buf), NULL, &reg);
exi_encoder_header(&enc);
exi_encoder_sd(&enc);
  exi_encoder_se(&enc, "urn:iso:std:iso:15118:-20:DC", "DC_PreChargeReq");
    exi_encoder_se(&enc, "urn:iso:std:iso:15118:-20:CommonTypes", "Header");
      exi_encoder_se(&enc, "", "SessionID");
        exi_encoder_ch(&enc, "A1B2C3D4E5F60708");
      exi_encoder_ee(&enc);
    exi_encoder_ee(&enc);
  exi_encoder_ee(&enc);
exi_encoder_ed(&enc);
size_t written;
exi_encoder_finalize(&enc, &written);

// 디코더
exi_decoder_t dec;
exi_event_t   ev;
exi_decoder_init(&dec, buf, written, NULL, &reg);
exi_decoder_header(&dec);
while (1) {
    exi_decoder_next_event(&dec, &ev);
    if (ev.type == EXI_EVENT_ED || ev.type == EXI_EVENT_EOF) break;
    // ev.type, ev.local_name, ev.value 처리
}
```

---

## XSD 폴더 관리

`xsd/iso15118-2020/` 폴더에 ISO 15118-20 표준 XSD 파일들이 있습니다:

| 파일 | 네임스페이스 | 내용 |
|------|------------|------|
| `V2GCI_AppProtocol.xsd` | `urn:iso:std:iso:15118:-20:AppProtocol` | 프로토콜 협상 |
| `V2GCI_CommonTypes.xsd` | `urn:iso:std:iso:15118:-20:CommonTypes` | 공통 타입 정의 |
| `V2GCI_CommonMessages.xsd` | `urn:iso:std:iso:15118:-20:CommonMessages` | 세션/인증/서비스 메시지 |
| `V2GCI_DC.xsd` | `urn:iso:std:iso:15118:-20:DC` | DC 충전 메시지 |
| `V2GCI_AC.xsd` | `urn:iso:std:iso:15118:-20:AC` | AC 충전 메시지 |

다른 XSD 폴더를 사용하려면 `-s` 옵션으로 경로를 지정합니다:
```bash
./exi_encoder_client -s /path/to/your/xsd input.xml output.exi
```

---

## 지원하는 ISO 15118-20 메시지 타입

| 카테고리 | 메시지 |
|----------|--------|
| **AppProtocol** | SupportedAppProtocolReq/Res |
| **CommonMessages** | SessionSetup, AuthorizationSetup, Authorization, ServiceDiscovery, ServiceDetail, ServiceSelection, ScheduleExchange, PowerDelivery, SessionStop |
| **DC Charging** | DC_ChargeParameterDiscovery, DC_CableCheck, DC_PreCharge, DC_ChargeLoop, DC_WeldingDetection |
| **AC Charging** | AC_ChargeParameterDiscovery, AC_ChargeLoop |

---

## W3C EXI 표준 구현 내용

| 기능 | 구현 상태 |
|------|----------|
| EXI Cookie (`$EXI`) | ✅ |
| Bit-packed stream | ✅ |
| Byte-aligned stream | ✅ |
| Unsigned Integer (§7.1.6) | ✅ |
| String value (§7.1.10) | ✅ |
| URI / LocalName / Value string-tables | ✅ |
| Schemaless grammar | ✅ |
| Schema-informed string-table pre-population | ✅ |
| Namespace declarations (NS event) | ✅ |
| EXI Options in header | 기본값 사용 |
| Pre-compression (zlib) | 빌드 포함 |

---

## 의존성

| 라이브러리 | 용도 | 비고 |
|-----------|------|------|
| `libz` (zlib) | 압축 채널 지원 | Ubuntu: 기본 설치 |
| `libc` | 표준 C 런타임 | - |

외부 XML 파서(libxml2 등) 불필요 — 내장 미니 XML 파서 사용.
