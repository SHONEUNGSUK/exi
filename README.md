# ISO 15118-20 EXI Codec Web

**ISO 15118-20:2022 V2G 충전 프로토콜 메시지를 웹에서 EXI 이진 포맷으로 인코딩/디코딩하는 무료 웹 앱입니다.**

🌐 **라이브 데모**: https://iso15118-exi-codec.onrender.com

---

## 기능

| 기능 | 설명 |
|------|------|
| **XML → EXI 인코딩** | ISO 15118-20 XML 메시지를 W3C EXI 1.0 이진 포맷으로 변환 |
| **EXI → XML 디코딩** | EXI 바이너리를 사람이 읽을 수 있는 XML로 복원 |
| **3가지 정렬 모드** | Bit-packed · Byte-aligned · zlib Compressed |
| **13개 예제 메시지** | AppProtocol, DC/AC 충전, 세션 관리 등 |
| **파일 업로드/다운로드** | `.xml` 업로드, `.exi` 다운로드 |
| **Hex / Binary / Base64 뷰** | EXI 바이너리를 다양한 형식으로 확인 |
| **REST API** | curl · Python · JavaScript 에서 직접 호출 가능 |

---

## 로컬 실행

### 요구 사항
- Node.js 18+
- GCC (build-essential)
- zlib 개발 헤더 (`zlib1g-dev` on Ubuntu)

### 빌드 & 실행

```bash
git clone https://github.com/yourname/iso15118-exi-web.git
cd iso15118-exi-web

# 코덱 빌드
bash scripts/build_codec.sh

# 서버 시작
node server/app.js
# → http://localhost:3000
```

### Docker

```bash
docker build -t exi-codec .
docker run -p 3000:3000 exi-codec
```

---

## REST API

### Encode: XML → EXI

```bash
curl -X POST https://iso15118-exi-codec.onrender.com/api/encode \
  -H "Content-Type: application/json" \
  -d '{
    "xml": "<?xml version=\"1.0\"?><cm:SessionSetupReq xmlns:cm=\"urn:iso:std:iso:15118:-20:CommonMessages\"><cm:Header><ct:SessionID xmlns:ct=\"urn:iso:std:iso:15118:-20:CommonTypes\">0000000000000000</ct:SessionID></cm:Header><cm:EVCCID>AABBCCDDEEFF</cm:EVCCID></cm:SessionSetupReq>",
    "alignment": "bit"
  }'
```

**응답:**
```json
{
  "success": true,
  "exi_base64": "JEXJgAECB6myubm0t7cpsr...",
  "exi_hex": "24 45 58 49 80 01 02 07 A9...",
  "exi_bytes": [36, 69, 88, 73, ...],
  "stats": {
    "xmlBytes": 639,
    "exiBytes": 164,
    "savedPercent": 74,
    "rootElement": "SessionSetupReq",
    "msgType": "0x0101"
  }
}
```

### Decode: EXI → XML

```bash
curl -X POST https://iso15118-exi-codec.onrender.com/api/decode \
  -H "Content-Type: application/json" \
  -d '{"exi_base64": "JEXJgAECB6myubm0t7cp...", "alignment": "bit"}'
```

### Python 예시

```python
import requests, base64

# Encode
r = requests.post("https://iso15118-exi-codec.onrender.com/api/encode",
    json={"xml": open("dc_charge_loop_req.xml").read(), "alignment": "bit"})
data = r.json()
print(f"Saved {data['stats']['savedPercent']}%")
exi = base64.b64decode(data["exi_base64"])

# Decode
r = requests.post("https://iso15118-exi-codec.onrender.com/api/decode",
    json={"exi_base64": data["exi_base64"], "alignment": "bit"})
print(r.json()["xml"])
```

---

## 무료 배포 방법

### Render.com (권장, 완전 무료)

1. https://render.com 회원가입
2. "New Web Service" → GitHub 저장소 연결
3. 자동으로 `render.yaml` 설정 적용
4. 배포 완료 → `https://[앱이름].onrender.com`

### Railway

```bash
npm install -g @railway/cli
railway login
railway init
railway up
```

### Fly.io

```bash
brew install flyctl
fly auth login
fly launch  # fly.toml 자동 감지
fly deploy
```

---

## 프로젝트 구조

```
iso15118-exi-web/
├── server/
│   └── app.js              ← Node.js HTTP 서버 (zero dependencies)
├── public/
│   ├── index.html          ← 웹 UI
│   ├── css/style.css       ← 스타일시트
│   └── js/app.js           ← 프론트엔드 JS
├── codec/                  ← ISO 15118-20 C 코덱 라이브러리
│   ├── src/exi_codec.c
│   ├── include/
│   ├── xsd/iso15118-2020/  ← XSD 스키마 파일
│   └── examples/           ← 13개 예제 XML
├── scripts/
│   └── build_codec.sh      ← 코덱 빌드 스크립트
├── Dockerfile
├── render.yaml
└── fly.toml
```

---

## 라이선스

MIT License

