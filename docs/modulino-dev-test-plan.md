# Modulino-dev Test Plan

## 1. Purpose

이 문서는 modulino-dev ESP32-S3 test firmware MVP의 시험 범위, 실행 조건,
기대 결과와 판정 기준을 정의한다. 실제 Printer와 실제 Parts Module protocol은
현재 범위가 아니며 mock 시험 결과와 구분한다.

대상 환경:

- Hub: ESP32-S3, ESP-IDF 5.5.2, 16MB flash, 4MB factory app partition
- Mock Parts Module: ESP32-C3, `mock_module_fw`, USB Serial/JTAG console
- Host: Windows + WSL2, Mosquitto 2.1.2, local development port 1883
- Parts UART: 115200 8N1, no parity, no flow control

실제 IP, serial port, USB BUSID는 환경마다 달라진다. 문서의 placeholder를 실제
환경 값으로 바꾸되 SSID/password, MAC address, 개인 credential은 시험 기록이나
문서에 남기지 않는다.

## 2. Status Definitions

| 상태 | 공통 의미 |
|---|---|
| PASS | 사전조건을 만족한 상태에서 실행했고 기대 결과를 관찰함 |
| FAIL | 실행했으나 초기화, 연결, timeout, payload 또는 응답이 기대 결과와 다름 |
| SKIP | 필수 설정이나 선행 연결이 없거나 runtime 시험을 아직 실행하지 않음 |
| NOT_SUPPORTED | 현재 test firmware 범위에서 의도적으로 구현하지 않음 |

Build 성공은 source compile/link 및 image 생성 결과다. 아래의 `실제 검증 상태`는
별도로 실제 보드나 Host subscriber에서 관찰한 결과다.

## 3. Test Matrix

| Test ID | Test item | 현재 실제 검증 상태 |
|---|---|---|
| MDT-BOOT-001 | Boot | PASS, 실제 ESP32-S3 |
| MDT-LOG-001 | Serial log | PASS, 실제 ESP32-S3 monitor |
| MDT-CLI-001 | Serial CLI | PASS, 실제 ESP32-S3 monitor |
| MDT-NVS-001 | NVS init | PASS, 실제 ESP32-S3 |
| MDT-WIFI-001 | Wi-Fi connect | PASS, IPv4 할당 확인 |
| MDT-MQTT-001 | MQTT connect | PASS, 실제 broker 연결 |
| MDT-MQTT-002 | MQTT LWT | PASS, actual publish 확인 |
| MDT-MQTT-003 | status | SKIP, Host 수신 증적 미확보 (firmware enqueue PASS) |
| MDT-MQTT-004 | birth | SKIP, Host 수신 증적 미확보 (firmware enqueue PASS) |
| MDT-MQTT-005 | heartbeat | PASS, 5초 주기 수신 |
| MDT-MQTT-006 | logs | SKIP, Host 수신 증적 미확보 (firmware enqueue PASS) |
| MDT-MQTT-007 | mock modules/discovery | SKIP, Host 수신 증적 미확보 (firmware enqueue PASS) |
| MDT-MQTT-008 | mock printer/status | PASS, 3초 주기 수신 |
| MDT-RPC-001 | RPC subscribe | PASS, 실제 RPC 경로 동작 |
| MDT-RPC-002 | M105 accepted/completed | PASS, Host subscriber |
| MDT-RPC-003 | invalid JSON rejection | PASS, `invalid_json`, `id=null` |
| MDT-RPC-004 | unsafe G-code rejection | PASS, G1 `unclassified_gcode` |
| MDT-RPC-005 | unsupported method rejection | PASS, `unsupported_method` |
| MDT-UART-001 | Parts UART mock handshake | PASS, 5회 연속 |
| MDT-MODULE-001 | actual Parts Module protocol | NOT_SUPPORTED |
| MDT-PRINTER-001 | actual Printer communication | NOT_SUPPORTED |

## 4. Detailed Tests

### MDT-BOOT-001: Boot

| 항목 | 내용 |
|---|---|
| 목적 | Hub image가 ESP32-S3에서 부팅되어 `app_main`에 진입하는지 확인 |
| 사전조건 | Hub build/flash 완료, Hub console port 연결 |
| 입력/실행 방법 | `idf.py -p <HUB_PORT> monitor` 후 EN/reset 또는 전원 재인가 |
| 기대 결과 | ESP-IDF boot 후 `modulino-dev test firmware boot` log 출력, firmware 계속 실행 |
| 판정 기준 | PASS: boot log와 CLI까지 도달; FAIL: reset loop, panic, app 미진입; SKIP: 보드/port 없음; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - 실제 ESP32-S3에서 boot 완료 |

### MDT-LOG-001: Serial Log

| 항목 | 내용 |
|---|---|
| 목적 | Hub UART0 console과 ESP-IDF monitor에서 boot/runtime log를 읽을 수 있는지 확인 |
| 사전조건 | GPIO43/44 기반 Hub UART0 USB 연결, 115200 monitor |
| 입력/실행 방법 | Hub monitor를 열고 reset 후 boot, NVS, Wi-Fi, MQTT 관련 log 관찰 |
| 기대 결과 | log가 손상 없이 출력되고 Parts UART GPIO17/18 데이터와 섞이지 않음 |
| 판정 기준 | PASS: console log 정상; FAIL: 깨진 문자, console 불능 또는 Parts UART 충돌; SKIP: console 미연결; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - 실제 monitor에서 확인 |

### MDT-CLI-001: Serial CLI

| 항목 | 내용 |
|---|---|
| 목적 | line buffering, echo, CR/LF 처리와 명령 dispatch 확인 |
| 사전조건 | Hub boot 완료, `modulino>` prompt 표시 |
| 입력/실행 방법 | `help`, 빈 줄, backspace 입력, `test all`, Printer 명령 실행 |
| 기대 결과 | 문자가 echo되고 Enter당 prompt 1회, 빈 줄은 무시, 지원 명령 정상 실행 |
| 판정 기준 | PASS: 입력/echo/prompt/명령 정상; FAIL: busy loop, prompt 반복, 입력 미표시 또는 오동작; SKIP: console 없음; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - 실제 ESP32-S3 CLI 확인 |

### MDT-NVS-001: NVS Init

| 항목 | 내용 |
|---|---|
| 목적 | 기존 NVS를 erase하지 않고 `nvs_flash_init()` 결과를 저장하는지 확인 |
| 사전조건 | Hub 정상 boot |
| 입력/실행 방법 | boot 후 `test all` 실행 |
| 기대 결과 | `[TEST] nvs init PASS - ESP_OK`; 실패해도 CLI는 계속 동작 |
| 판정 기준 | PASS: ESP_OK; FAIL: ESP-IDF error 반환; SKIP: boot 미수행; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - 실제 보드 NVS init 확인 |

### MDT-WIFI-001: Wi-Fi Connect

| 항목 | 내용 |
|---|---|
| 목적 | ESP32-S3가 STA mode로 AP에 연결하고 IPv4를 받는지 확인 |
| 사전조건 | menuconfig에 실제 SSID/password 설정, AP 사용 가능 |
| 입력/실행 방법 | Hub boot 후 `test all`, monitor와 IP detail 확인 |
| 기대 결과 | `[TEST] wifi connect PASS - IP=<assigned IP>`; password는 출력되지 않음 |
| 판정 기준 | PASS: 제한시간 내 IPv4; FAIL: init/auth/connect/timeout; SKIP: SSID 빈 문자열; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - Wi-Fi IP 할당 확인 |

### MDT-MQTT-001: MQTT Connect

| 항목 | 내용 |
|---|---|
| 목적 | Wi-Fi 연결 후 plain MQTT broker에 제한시간 내 연결하는지 확인 |
| 사전조건 | Wi-Fi PASS, Mosquitto 실행, `mqtt://<BROKER_IP>:1883` 설정 |
| 입력/실행 방법 | Hub boot 후 `test all`, broker verbose log 관찰 |
| 기대 결과 | `[TEST] mqtt connect PASS - broker=<BROKER_IP>:1883` |
| 판정 기준 | PASS: `MQTT_EVENT_CONNECTED`; FAIL: URI/init/transport/refuse/timeout; SKIP: URI 없음 또는 Wi-Fi 미연결; NOT_SUPPORTED: TLS 연결 |
| 현재 실제 검증 상태 | **PASS** - Windows Mosquitto 실제 연결 확인 |

### MDT-MQTT-002: MQTT LWT

| 항목 | 내용 |
|---|---|
| 목적 | Hub 비정상 연결 종료 시 broker가 retained offline status를 발행하는지 확인 |
| 사전조건 | MQTT 연결 PASS, wildcard subscriber 실행, keepalive 10초 |
| 입력/실행 방법 | online 확인 후 Hub와 broker 사이 hotspot/network를 갑자기 차단하고 대기 |
| 기대 결과 | `{hub_id}/status`에 `status=offline`, `reason=mqtt_lwt`; QoS 1, retain true |
| 판정 기준 | PASS: Host subscriber에서 actual LWT 수신; FAIL: 감지 시간 이후 미수신/내용 불일치; SKIP: 정상 disconnect만 수행하거나 broker/subscriber 없음; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - LWT actual publish 확인 |

### MDT-MQTT-003: Status

| 항목 | 내용 |
|---|---|
| 목적 | 연결 직후 retained offline LWT를 online status가 대체하는지 확인 |
| 사전조건 | MQTT 연결 PASS, subscriber 준비 |
| 입력/실행 방법 | Hub boot/reconnect 직후 `{hub_id}/status` 확인 |
| 기대 결과 | schema `modulino.hub_status.v1`, `status=online`, `reason=connected`, QoS 1 retain true |
| 판정 기준 | PASS: online retained payload를 Host subscriber에서 직접 수신하고 필드/retain 정책 확인; FAIL: 관찰했으나 누락 또는 정책 불일치; SKIP: MQTT 미연결 또는 Host 수신 증적 미확보; NOT_SUPPORTED: 적용하지 않음. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 수신, retain 적용, subscriber 수신 PASS를 대신하지 않음 |
| 현재 실제 검증 상태 | **SKIP** - Host 수신 증적 미확보 (firmware enqueue PASS) |

### MDT-MQTT-004: Birth

| 항목 | 내용 |
|---|---|
| 목적 | boot metadata가 retained birth payload로 발행되는지 확인 |
| 사전조건 | MQTT 연결 PASS |
| 입력/실행 방법 | `{hub_id}/birth` subscribe 후 Hub boot |
| 기대 결과 | schema, hub_id, boot_id, seq, fw_version `0.1.0`, proto_version `1.0`, reset_reason 포함; QoS 1 retain true |
| 판정 기준 | PASS: Host subscriber에서 payload를 직접 수신하고 필수 필드와 retain 정책 확인; FAIL: 관찰했으나 payload/정책 불일치; SKIP: MQTT 미연결 또는 Host 수신 증적 미확보; NOT_SUPPORTED: 적용하지 않음. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 수신, retain 적용, subscriber 수신 PASS를 대신하지 않음 |
| 현재 실제 검증 상태 | **SKIP** - Host 수신 증적 미확보 (firmware enqueue PASS) |

### MDT-MQTT-005: Heartbeat

| 항목 | 내용 |
|---|---|
| 목적 | Hub runtime 상태가 5초 주기로 발행되는지 확인 |
| 사전조건 | MQTT 연결 및 initial publish PASS |
| 입력/실행 방법 | `{hub_id}/heartbeat`를 최소 15초 관찰 |
| 기대 결과 | 약 5초 주기, uptime_ms 증가, free_heap_bytes와 wifi_rssi_dbm 포함; QoS 0 retain false |
| 판정 기준 | PASS: 연속 heartbeat와 5초 주기 확인; FAIL: 미수신/주기 또는 필드 이상; SKIP: MQTT 미연결; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - heartbeat 5초 수신 확인 |

### MDT-MQTT-006: Logs

| 항목 | 내용 |
|---|---|
| 목적 | MQTT 연결 직후 `mqtt_connected` info event가 1회 발행되는지 확인 |
| 사전조건 | subscriber를 Hub MQTT 연결 전에 시작 |
| 입력/실행 방법 | `{hub_id}/logs` subscribe 상태에서 Hub reset |
| 기대 결과 | schema `modulino.log.v1`, level `info`, event `mqtt_connected`; QoS 0 retain false |
| 판정 기준 | PASS: 연결 전에 시작한 Host subscriber에서 1회 직접 수신; FAIL: 관찰했으나 중복/필드 불일치; SKIP: non-retained event의 Host 수신 증적 미확보; NOT_SUPPORTED: 적용하지 않음. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 또는 subscriber 수신 PASS를 대신하지 않음 |
| 현재 실제 검증 상태 | **SKIP** - Host 수신 증적 미확보 (firmware enqueue PASS) |

### MDT-MQTT-007: Mock Modules/Discovery

| 항목 | 내용 |
|---|---|
| 목적 | 실제 module 발견으로 오인되지 않는 mock discovery를 확인 |
| 사전조건 | MQTT 연결 PASS |
| 입력/실행 방법 | `{hub_id}/modules/discovery` payload 확인 |
| 기대 결과 | schema `modulino.module_discovery.v1`, `source=mock`, `scan_id=scan_mock001`, `modules=[]`; QoS 1 retain true |
| 판정 기준 | PASS: Host subscriber에서 빈 mock 목록을 직접 수신하고 payload/retain 정책 확인; FAIL: 실제 module처럼 표현하거나 관찰한 payload/정책 불일치; SKIP: MQTT 미연결 또는 Host 수신 증적 미확보; NOT_SUPPORTED: actual discovery protocol. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 수신, retain 적용, subscriber 수신 PASS를 대신하지 않음 |
| 현재 실제 검증 상태 | **SKIP** - Host 수신 증적 미확보 (firmware enqueue PASS) |

### MDT-MQTT-008: Mock Printer/Status

| 항목 | 내용 |
|---|---|
| 목적 | 실제 Printer가 없음을 명시한 mock status를 3초 주기로 확인 |
| 사전조건 | MQTT 연결 및 initial publish PASS |
| 입력/실행 방법 | `{hub_id}/printer/status`를 최소 9초 관찰 |
| 기대 결과 | `printer_id=prt_mock001`, `connection=disconnected`, `source=mock`, 3초 주기; QoS 0 retain false |
| 판정 기준 | PASS: disconnected mock payload와 3초 주기; FAIL: connected/실측값으로 표현, 미수신 또는 주기 이상; SKIP: MQTT 미연결; NOT_SUPPORTED: actual Printer status |
| 현재 실제 검증 상태 | **PASS (mock only)** - printer/status 3초 수신 확인 |

### MDT-RPC-001: RPC Subscribe

| 항목 | 내용 |
|---|---|
| 목적 | Hub가 QoS 1로 `{hub_id}/rpc/request`를 subscribe하는지 확인 |
| 사전조건 | MQTT 연결 PASS |
| 입력/실행 방법 | boot 후 `test all`, broker subscribe/event log 또는 RPC request 처리 확인 |
| 기대 결과 | `[TEST] mqtt rpc subscribe PASS - topic=modulino/local/v1/{hub_id}/rpc/request` |
| 판정 기준 | PASS: SUBACK와 저장 결과 확인; FAIL: subscribe request/SUBACK/worker 실패; SKIP: MQTT 미연결; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - 실제 RPC request 처리로 경로 확인 |

### MDT-RPC-002: M105 Accepted/Completed

| 항목 | 내용 |
|---|---|
| 목적 | SAFE_READ M105가 progress 후 Printer mock 응답으로 완료되는지 확인 |
| 사전조건 | RPC subscribe PASS, progress/response subscriber 실행 |
| 입력/실행 방법 | `printer.gcode.run`, `printer_id=prt_mock001`, `script=M105`, string id를 QoS 1 publish |
| 기대 결과 | 같은 id로 `progress.status=accepted`, 이어서 `result.status=completed`, `source=mock`, mock raw_response |
| 판정 기준 | PASS: accepted -> completed 순서와 공통 boot_id/전역 seq 확인; FAIL: rejection/누락/순서/ID 불일치; SKIP: MQTT/RPC 미연결; NOT_SUPPORTED: actual Printer 실행 |
| 현재 실제 검증 상태 | **PASS (mock end-to-end)** - M105 MQTT RPC accepted -> completed를 Host에서 확인. M114/M115는 구현 및 Serial CLI mock 확인, MQTT RPC end-to-end는 미검증 |

### MDT-RPC-003: Invalid JSON Rejection

| 항목 | 내용 |
|---|---|
| 목적 | 파싱 불가능한 request가 실행 없이 거부되는지 확인 |
| 사전조건 | RPC subscribe PASS, response subscriber 실행 |
| 입력/실행 방법 | 잘린 JSON 등 invalid ASCII payload를 QoS 1 publish |
| 기대 결과 | progress 없음, response `error.code=invalid_json`, `error.data.status=rejected`, `id=null` |
| 판정 기준 | PASS: invalid_json/id null rejection; FAIL: accepted/실행/다른 reason/응답 없음; SKIP: MQTT/RPC 미연결; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - `invalid_json`, `id=null` 확인 |

### MDT-RPC-004: Unsafe G-code Rejection

| 항목 | 내용 |
|---|---|
| 목적 | default-deny safety가 G1 등 비분류 G-code의 mock/실제 실행을 차단하는지 확인 |
| 사전조건 | RPC subscribe PASS |
| 입력/실행 방법 | 유효한 request의 script를 `G1 X10`으로 publish |
| 기대 결과 | progress 없음, `error.code=unclassified_gcode`, status rejected |
| 판정 기준 | PASS: 즉시 unclassified_gcode rejection; FAIL: accepted/completed 또는 UART 전송; SKIP: MQTT/RPC 미연결; NOT_SUPPORTED: 적용하지 않음 |
| 현재 실제 검증 상태 | **PASS** - G1 rejection 확인 |

### MDT-RPC-005: Unsupported Method Rejection

| 항목 | 내용 |
|---|---|
| 목적 | 공통 JSON-RPC field 검증 후 미지원 method를 method 전용 params보다 먼저 거부하는지 확인 |
| 사전조건 | RPC subscribe PASS |
| 입력/실행 방법 | `method=hub.reboot`, `params={}`, string id인 request publish |
| 기대 결과 | progress 없음, 같은 id의 `error.code=unsupported_method`, status rejected |
| 판정 기준 | PASS: unsupported_method; FAIL: invalid_params, accepted 또는 무응답; SKIP: MQTT/RPC 미연결; NOT_SUPPORTED: 해당 method 실행 자체는 의도적으로 미지원 |
| 현재 실제 검증 상태 | **PASS** - validation 순서 수정 후 실제 확인 |

### MDT-UART-001: Parts UART Mock Handshake

| 항목 | 내용 |
|---|---|
| 목적 | ESP32-S3 UART1과 ESP32-C3 UART1 사이의 물리 송수신 경로 확인 |
| 사전조건 | C3 mock ready, TX/RX cross-connect, GND 공통, 두 보드 별도 USB 전원, 전원 rail 미연결 |
| 입력/실행 방법 | Hub CLI에서 `test module uart` 실행; 안정성 확인 시 5회 반복 |
| 기대 결과 | Hub가 `MODULINO_PING\n` 전송, C3가 `MODULINO_PONG\n` 응답, Hub PASS |
| 판정 기준 | PASS: `response=MODULINO_PONG`; FAIL: 1000ms timeout/init error/mismatch; SKIP: 아직 실행하지 않아 `test not run`; NOT_SUPPORTED: actual Parts Module protocol |
| 현재 실제 검증 상태 | **PASS (test-only mock)** - 5회 연속 PASS. C3 reset 직후 ready 이전 첫 시도는 1회 timeout 가능 |

### MDT-MODULE-001: Actual Parts Module Protocol

| 항목 | 내용 |
|---|---|
| 목적 | 실제 Parts Module discovery/version/production protocol 지원 여부 명시 |
| 사전조건 | 실제 protocol specification과 실제 module 필요 |
| 입력/실행 방법 | 현재 실행 경로 없음. PING/PONG mock 시험과 분리 |
| 기대 결과 | 현재 firmware에서 실제 module을 발견하거나 version을 보고하지 않음 |
| 판정 기준 | PASS: 적용하지 않음; FAIL: mock을 actual로 오표현하거나 의도치 않은 실제 명령 전송; SKIP: 적용하지 않음; NOT_SUPPORTED: 현재 정상 상태 |
| 현재 실제 검증 상태 | **NOT_SUPPORTED** |

### MDT-PRINTER-001: Actual Printer Communication

| 항목 | 내용 |
|---|---|
| 목적 | 실제 Printer UART 송수신 지원 여부와 mock 경계를 명시 |
| 사전조건 | 실제 Printer protocol/배선은 현재 범위에 없음 |
| 입력/실행 방법 | `test all`의 `printer_uart real` 결과 확인 |
| 기대 결과 | `NOT_SUPPORTED - ESP_ERR_NOT_SUPPORTED`; 실제 UART로 G-code를 보내지 않음 |
| 판정 기준 | PASS: 적용하지 않음; FAIL: 실제 UART 송신 또는 mock을 실제 측정으로 표현; SKIP: 적용하지 않음; NOT_SUPPORTED: 현재 정상 상태 |
| 현재 실제 검증 상태 | **NOT_SUPPORTED** - M105/M114/M115 결과는 mock only |

## 5. Overall Verified Result

초기 MQTT 네 메시지인 status, birth, modules/discovery, logs는 firmware에서
`esp_mqtt_client_enqueue()` 성공까지 확인했다. 이는 broker 수신, retain 적용 또는
Host subscriber 수신 PASS가 아니다. 현재 문서 작성 기준으로 네 topic의 개별 Host
수신 증적은 확보되지 않았다.

heartbeat 5초 주기와 mock printer/status 3초 주기는 Host에서 실제 수신했다. MQTT
LWT offline, 공통 boot_id/전역 seq, RPC M105와 rejection 경로, Parts UART
PING/PONG도 실제 end-to-end로 확인했다. M114/M115 MQTT RPC, 실제 Printer 및 실제
Parts Module protocol은 검증되지 않았다.

## 6. Spec Compliance Gaps

다음 항목은 product firmware 전환 전에 해결해야 하는 v0.1 사양 차이다.

- Duplicate RPC command ID deduplication이 구현되지 않아 같은 ID가 다시 실행된다.
- MQTT auto reconnect와 제품 수준 장애 복구가 구현되지 않았다.
- 현재 `boot_id`는 `boot_` + 32자리 lowercase hexadecimal이며 v0.1의 lowercase
  base32/base36 식별자 정책과 다르다.
- 실제 Printer 통신과 실제 Parts Module discovery/version protocol이 구현되지 않았다.
