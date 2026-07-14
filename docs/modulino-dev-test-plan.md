# Modulino Hub Test Firmware Test Plan

## 1. Purpose

이 문서는 Modulino Hub Test Firmware의 전체 시험 범위, 실행 조건, 기대 결과와
판정 기준을 정의한다. 실제 실행 결과와 hardware verification 이력은
[Test Results](modulino-dev-test-results.md)에서 별도로 관리한다.

대상 환경:

- Hub: ESP32-S3, ESP-IDF 5.5.2, 16MB flash, 4MB factory app partition
- Mock Parts Module: ESP32-C3, `mock_module_fw`, USB Serial/JTAG console
- Host: Windows + WSL2, Mosquitto 2.1.2, local development port 1883
- Parts UART: 115200 8N1, no parity, no flow control
- Printer UART: UART2, Hub TX GPIO7, Hub RX GPIO8, 8N1, LF, 3000 ms timeout
- Printer baud: menuconfig choice, 기본 115200
- 실제 Printer 대상: Marlin 기반 Ender-3 V3 SE

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

검증 단계 표기는 다음과 같다.

| 단계 | 의미 |
|---|---|
| BUILD_VERIFIED | source configure/compile/link 및 image 생성 성공 |
| READY_FOR_HW_TEST | 실기기 시험 코드와 절차 준비 완료, 실제 결과 없음 |
| HW_VERIFIED | 담당자가 실제 대상 장비에서 증적을 확보해 판정 완료 |

각 단계의 실제 판정과 증적은 Test Results에 날짜와 commit별로 기록한다.

## 3. Test Matrix

| Test ID | Test item |
|---|---|
| MDT-BUILD-001 | ESP32-S3 firmware build |
| MDT-BOOT-001 | Boot |
| MDT-LOG-001 | Serial log |
| MDT-CLI-001 | Serial CLI |
| MDT-NVS-001 | NVS init |
| MDT-WIFI-001 | Wi-Fi connect |
| MDT-MQTT-001 | MQTT connect |
| MDT-MQTT-002 | MQTT LWT |
| MDT-MQTT-003 | status |
| MDT-MQTT-004 | birth |
| MDT-MQTT-005 | heartbeat |
| MDT-MQTT-006 | logs |
| MDT-MQTT-007 | mock modules/discovery |
| MDT-RPC-001 | RPC subscribe |
| MDT-RPC-003 | invalid JSON rejection |
| MDT-RPC-004 | unsafe G-code rejection |
| MDT-RPC-005 | unsupported method rejection |
| MDT-UART-001 | Parts UART mock handshake |
| MDT-MODULE-001 | actual Parts Module protocol |
| MDT-PRINTER-UART-001 | UART initialization and idle state |
| MDT-PRINTER-UART-002 | M105 actual UART |
| MDT-PRINTER-UART-003 | M114 actual UART |
| MDT-PRINTER-UART-004 | M115 multiline UART |
| MDT-PRINTER-UART-005 | CR/LF/CRLF response handling |
| MDT-PRINTER-SAFETY-001 | unsafe G-code default deny |
| MDT-PRINTER-CONCURRENCY-001 | CLI/MQTT mutex serialization |
| MDT-PRINTER-RPC-001 | RPC accepted → completed |
| MDT-PRINTER-RPC-002 | RPC accepted → failed |
| MDT-PRINTER-RPC-003 | validation rejection |
| MDT-PRINTER-STATUS-001 | recent transaction printer/status |

## 4. Detailed Tests

### MDT-BUILD-001: ESP32-S3 Firmware Build

| 항목 | 내용 |
|---|---|
| 목적 | 현재 source와 Kconfig가 ESP32-S3 image로 configure/compile/link되는지 확인 |
| 사전조건 | ESP-IDF 5.5.2 export, target `esp32s3` |
| 입력/실행 방법 | repository root에서 `idf.py build` |
| 기대 결과 | `build/modulino_test_fw.bin` 생성, 4MB app partition 이내 |
| 판정 기준 | BUILD_VERIFIED: build 성공; FAIL: configure/compile/link/partition 오류. 실제 Printer 통신 PASS로 판정하지 않음 |

### MDT-BOOT-001: Boot

| 항목 | 내용 |
|---|---|
| 목적 | Hub image가 ESP32-S3에서 부팅되어 `app_main`에 진입하는지 확인 |
| 사전조건 | Hub build/flash 완료, Hub console port 연결 |
| 입력/실행 방법 | `idf.py -p <HUB_PORT> monitor` 후 EN/reset 또는 전원 재인가 |
| 기대 결과 | ESP-IDF boot 후 `modulino-dev test firmware boot` log 출력, firmware 계속 실행 |
| 판정 기준 | PASS: boot log와 CLI까지 도달; FAIL: reset loop, panic, app 미진입; SKIP: 보드/port 없음; NOT_SUPPORTED: 적용하지 않음 |

### MDT-LOG-001: Serial Log

| 항목 | 내용 |
|---|---|
| 목적 | Hub UART0 console과 ESP-IDF monitor에서 boot/runtime log를 읽을 수 있는지 확인 |
| 사전조건 | GPIO43/44 기반 Hub UART0 USB 연결, 115200 monitor |
| 입력/실행 방법 | Hub monitor를 열고 reset 후 boot, NVS, Wi-Fi, MQTT 관련 log 관찰 |
| 기대 결과 | log가 손상 없이 출력되고 Parts UART GPIO17/18 데이터와 섞이지 않음 |
| 판정 기준 | PASS: console log 정상; FAIL: 깨진 문자, console 불능 또는 Parts UART 충돌; SKIP: console 미연결; NOT_SUPPORTED: 적용하지 않음 |

### MDT-CLI-001: Serial CLI

| 항목 | 내용 |
|---|---|
| 목적 | line buffering, echo, CR/LF 처리와 명령 dispatch 확인 |
| 사전조건 | Hub boot 완료, `modulino>` prompt 표시 |
| 입력/실행 방법 | `help`, 빈 줄, backspace 입력, `test all`, Printer 명령 실행 |
| 기대 결과 | 문자가 echo되고 Enter당 prompt 1회, 빈 줄은 무시, 지원 명령 정상 실행 |
| 판정 기준 | PASS: 입력/echo/prompt/명령 정상; FAIL: busy loop, prompt 반복, 입력 미표시 또는 오동작; SKIP: console 없음; NOT_SUPPORTED: 적용하지 않음 |

### MDT-NVS-001: NVS Init

| 항목 | 내용 |
|---|---|
| 목적 | 기존 NVS를 erase하지 않고 `nvs_flash_init()` 결과를 저장하는지 확인 |
| 사전조건 | Hub 정상 boot |
| 입력/실행 방법 | boot 후 `test all` 실행 |
| 기대 결과 | `[TEST] nvs init PASS - ESP_OK`; 실패해도 CLI는 계속 동작 |
| 판정 기준 | PASS: ESP_OK; FAIL: ESP-IDF error 반환; SKIP: boot 미수행; NOT_SUPPORTED: 적용하지 않음 |

### MDT-WIFI-001: Wi-Fi Connect

| 항목 | 내용 |
|---|---|
| 목적 | ESP32-S3가 STA mode로 AP에 연결하고 IPv4를 받는지 확인 |
| 사전조건 | menuconfig에 실제 SSID/password 설정, AP 사용 가능 |
| 입력/실행 방법 | Hub boot 후 `test all`, monitor와 IP detail 확인 |
| 기대 결과 | `[TEST] wifi connect PASS - IP=<assigned IP>`; password는 출력되지 않음 |
| 판정 기준 | PASS: 제한시간 내 IPv4; FAIL: init/auth/connect/timeout; SKIP: SSID 빈 문자열; NOT_SUPPORTED: 적용하지 않음 |

### MDT-MQTT-001: MQTT Connect

| 항목 | 내용 |
|---|---|
| 목적 | Wi-Fi 연결 후 plain MQTT broker에 제한시간 내 연결하는지 확인 |
| 사전조건 | Wi-Fi PASS, Mosquitto 실행, `mqtt://<BROKER_IP>:1883` 설정 |
| 입력/실행 방법 | Hub boot 후 `test all`, broker verbose log 관찰 |
| 기대 결과 | `[TEST] mqtt connect PASS - broker=<BROKER_IP>:1883` |
| 판정 기준 | PASS: `MQTT_EVENT_CONNECTED`; FAIL: URI/init/transport/refuse/timeout; SKIP: URI 없음 또는 Wi-Fi 미연결; NOT_SUPPORTED: TLS 연결 |

### MDT-MQTT-002: MQTT LWT

| 항목 | 내용 |
|---|---|
| 목적 | Hub 비정상 연결 종료 시 broker가 retained offline status를 발행하는지 확인 |
| 사전조건 | MQTT 연결 PASS, wildcard subscriber 실행, keepalive 10초 |
| 입력/실행 방법 | online 확인 후 Hub와 broker 사이 hotspot/network를 갑자기 차단하고 대기 |
| 기대 결과 | `{hub_id}/status`에 `status=offline`, `reason=mqtt_lwt`; QoS 1, retain true |
| 판정 기준 | PASS: Host subscriber에서 actual LWT 수신; FAIL: 감지 시간 이후 미수신/내용 불일치; SKIP: 정상 disconnect만 수행하거나 broker/subscriber 없음; NOT_SUPPORTED: 적용하지 않음 |

### MDT-MQTT-003: Status

| 항목 | 내용 |
|---|---|
| 목적 | 연결 직후 retained offline LWT를 online status가 대체하는지 확인 |
| 사전조건 | MQTT 연결 PASS, subscriber 준비 |
| 입력/실행 방법 | Hub boot/reconnect 직후 `{hub_id}/status` 확인 |
| 기대 결과 | schema `modulino.hub_status.v1`, `status=online`, `reason=connected`, QoS 1 retain true |
| 판정 기준 | PASS: online retained payload를 Host subscriber에서 직접 수신하고 필드/retain 정책 확인; FAIL: 관찰했으나 누락 또는 정책 불일치; SKIP: MQTT 미연결 또는 Host 수신 증적 미확보; NOT_SUPPORTED: 적용하지 않음. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 수신, retain 적용, subscriber 수신 PASS를 대신하지 않음 |

### MDT-MQTT-004: Birth

| 항목 | 내용 |
|---|---|
| 목적 | boot metadata가 retained birth payload로 발행되는지 확인 |
| 사전조건 | MQTT 연결 PASS |
| 입력/실행 방법 | `{hub_id}/birth` subscribe 후 Hub boot |
| 기대 결과 | schema, hub_id, boot_id, seq, fw_version `0.1.0`, proto_version `1.0`, reset_reason 포함; QoS 1 retain true |
| 판정 기준 | PASS: Host subscriber에서 payload를 직접 수신하고 필수 필드와 retain 정책 확인; FAIL: 관찰했으나 payload/정책 불일치; SKIP: MQTT 미연결 또는 Host 수신 증적 미확보; NOT_SUPPORTED: 적용하지 않음. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 수신, retain 적용, subscriber 수신 PASS를 대신하지 않음 |

### MDT-MQTT-005: Heartbeat

| 항목 | 내용 |
|---|---|
| 목적 | Hub runtime 상태가 5초 주기로 발행되는지 확인 |
| 사전조건 | MQTT 연결 및 initial publish PASS |
| 입력/실행 방법 | `{hub_id}/heartbeat`를 최소 15초 관찰 |
| 기대 결과 | 약 5초 주기, uptime_ms 증가, free_heap_bytes와 wifi_rssi_dbm 포함; QoS 0 retain false |
| 판정 기준 | PASS: 연속 heartbeat와 5초 주기 확인; FAIL: 미수신/주기 또는 필드 이상; SKIP: MQTT 미연결; NOT_SUPPORTED: 적용하지 않음 |

### MDT-MQTT-006: Logs

| 항목 | 내용 |
|---|---|
| 목적 | MQTT 연결 직후 `mqtt_connected` info event가 1회 발행되는지 확인 |
| 사전조건 | subscriber를 Hub MQTT 연결 전에 시작 |
| 입력/실행 방법 | `{hub_id}/logs` subscribe 상태에서 Hub reset |
| 기대 결과 | schema `modulino.log.v1`, level `info`, event `mqtt_connected`; QoS 0 retain false |
| 판정 기준 | PASS: 연결 전에 시작한 Host subscriber에서 1회 직접 수신; FAIL: 관찰했으나 중복/필드 불일치; SKIP: non-retained event의 Host 수신 증적 미확보; NOT_SUPPORTED: 적용하지 않음. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 또는 subscriber 수신 PASS를 대신하지 않음 |

### MDT-MQTT-007: Mock Modules/Discovery

| 항목 | 내용 |
|---|---|
| 목적 | 실제 module 발견으로 오인되지 않는 mock discovery를 확인 |
| 사전조건 | MQTT 연결 PASS |
| 입력/실행 방법 | `{hub_id}/modules/discovery` payload 확인 |
| 기대 결과 | schema `modulino.module_discovery.v1`, `source=mock`, `scan_id=scan_mock001`, `modules=[]`; QoS 1 retain true |
| 판정 기준 | PASS: Host subscriber에서 빈 mock 목록을 직접 수신하고 payload/retain 정책 확인; FAIL: 실제 module처럼 표현하거나 관찰한 payload/정책 불일치; SKIP: MQTT 미연결 또는 Host 수신 증적 미확보; NOT_SUPPORTED: actual discovery protocol. `[TEST] mqtt initial publish PASS`는 enqueue 성공 판정이며 broker 수신, retain 적용, subscriber 수신 PASS를 대신하지 않음 |

### MDT-RPC-001: RPC Subscribe

| 항목 | 내용 |
|---|---|
| 목적 | Hub가 QoS 1로 `{hub_id}/rpc/request`를 subscribe하는지 확인 |
| 사전조건 | MQTT 연결 PASS |
| 입력/실행 방법 | boot 후 `test all`, broker subscribe/event log 또는 RPC request 처리 확인 |
| 기대 결과 | `[TEST] mqtt rpc subscribe PASS - topic=modulino/local/v1/{hub_id}/rpc/request` |
| 판정 기준 | PASS: SUBACK와 저장 결과 확인; FAIL: subscribe request/SUBACK/worker 실패; SKIP: MQTT 미연결; NOT_SUPPORTED: 적용하지 않음 |

### MDT-RPC-003: Invalid JSON Rejection

| 항목 | 내용 |
|---|---|
| 목적 | 파싱 불가능한 request가 실행 없이 거부되는지 확인 |
| 사전조건 | RPC subscribe PASS, response subscriber 실행 |
| 입력/실행 방법 | 잘린 JSON 등 invalid ASCII payload를 QoS 1 publish |
| 기대 결과 | progress 없음, response `error.code=invalid_json`, `error.data.status=rejected`, `id=null` |
| 판정 기준 | PASS: invalid_json/id null rejection; FAIL: accepted/실행/다른 reason/응답 없음; SKIP: MQTT/RPC 미연결; NOT_SUPPORTED: 적용하지 않음 |

### MDT-RPC-004: Unsafe G-code Rejection

| 항목 | 내용 |
|---|---|
| 목적 | default-deny safety가 G1 등 비분류 G-code의 mock/실제 실행을 차단하는지 확인 |
| 사전조건 | RPC subscribe PASS |
| 입력/실행 방법 | 유효한 request의 script를 `G1 X10`으로 publish |
| 기대 결과 | progress 없음, `error.code=unclassified_gcode`, status rejected |
| 판정 기준 | PASS: 즉시 unclassified_gcode rejection; FAIL: accepted/completed 또는 UART 전송; SKIP: MQTT/RPC 미연결; NOT_SUPPORTED: 적용하지 않음 |

### MDT-RPC-005: Unsupported Method Rejection

| 항목 | 내용 |
|---|---|
| 목적 | 공통 JSON-RPC field 검증 후 미지원 method를 method 전용 params보다 먼저 거부하는지 확인 |
| 사전조건 | RPC subscribe PASS |
| 입력/실행 방법 | `method=hub.reboot`, `params={}`, string id인 request publish |
| 기대 결과 | progress 없음, 같은 id의 `error.code=unsupported_method`, status rejected |
| 판정 기준 | PASS: unsupported_method; FAIL: invalid_params, accepted 또는 무응답; SKIP: MQTT/RPC 미연결; NOT_SUPPORTED: 해당 method 실행 자체는 의도적으로 미지원 |

### MDT-UART-001: Parts UART Mock Handshake

| 항목 | 내용 |
|---|---|
| 목적 | ESP32-S3 UART1과 ESP32-C3 UART1 사이의 물리 송수신 경로 확인 |
| 사전조건 | C3 mock ready, TX/RX cross-connect, GND 공통, 두 보드 별도 USB 전원, 전원 rail 미연결 |
| 입력/실행 방법 | Hub CLI에서 `test module uart` 실행; 안정성 확인 시 5회 반복 |
| 기대 결과 | Hub가 `MODULINO_PING\n` 전송, C3가 `MODULINO_PONG\n` 응답, Hub PASS |
| 판정 기준 | PASS: `response=MODULINO_PONG`; FAIL: 1000ms timeout/init error/mismatch; SKIP: 아직 실행하지 않아 `test not run`; NOT_SUPPORTED: actual Parts Module protocol |

### MDT-MODULE-001: Actual Parts Module Protocol

| 항목 | 내용 |
|---|---|
| 목적 | 실제 Parts Module discovery/version/production protocol 지원 여부 명시 |
| 사전조건 | 실제 protocol specification과 실제 module 필요 |
| 입력/실행 방법 | 현재 실행 경로 없음. PING/PONG mock 시험과 분리 |
| 기대 결과 | 현재 firmware에서 실제 module을 발견하거나 version을 보고하지 않음 |
| 판정 기준 | PASS: 적용하지 않음; FAIL: mock을 actual로 오표현하거나 의도치 않은 실제 명령 전송; SKIP: 적용하지 않음; NOT_SUPPORTED: 현재 정상 상태 |
### Printer UART Fixed Interface

| 항목 | 값 |
|---|---|
| UART controller | UART2 |
| Hub pins | TX GPIO7, RX GPIO8 |
| frame | 8N1, parity none, flow control none |
| TX line ending | LF (`\n`) |
| terminal response timeout | 3000 ms |
| baud | menuconfig choice; 기본 115200 |
| allowed G-code | M105, M114, M115 only |
| printer ID/source | `prt_test001` / `uart` |

다음 항목은 코드 build와 실기기 실행을 분리해 판정한다.

### MDT-PRINTER-UART-001: UART Initialization and Idle State

| 항목 | 내용 |
|---|---|
| 목적 | UART2 driver/mutex 초기화 결과와 transaction 전 상태가 정확히 구분되는지 확인 |
| 사전조건 | firmware boot, MQTT printer/status subscriber는 선택 사항 |
| 입력/실행 방법 | boot log, `test all`, `{hub_id}/printer/status` 확인 |
| 기대 결과 | 초기화 성공 후 실제 transaction 전에는 `connection=unknown`, `reason=not_tested`; 초기화 실패 시 `connection=disconnected`, `reason=uart_initialization_error` |
| 판정 기준 | FAIL: mutex 생성 또는 uart_param_config/uart_set_pin/uart_driver_install 실패. 초기화 성공만으로 actual Printer PASS를 기록하지 않음 |

### MDT-PRINTER-UART-002: M105 Actual UART

| 항목 | 내용 |
|---|---|
| 목적 | 실제 Printer의 temperature/status read 응답을 terminal line까지 수집 |
| 사전조건 | UART 교차 배선과 공통 GND, Printer와 같은 baud, UART 초기화 성공 |
| 입력/실행 방법 | CLI `printer m105` 또는 MQTT `script=M105` |
| 기대 결과 | 3000 ms 안에 line이 정확히 `ok`이거나 `ok` 다음 문자가 space/tab인 terminal response, raw response 보존 |
| 판정 기준 | PASS: 허용된 terminal `ok` 경계 수신; FAIL: init/runtime UART 오류, timeout, `Error:`, overflow. 실제 값 자체는 고정하지 않음 |

### MDT-PRINTER-UART-003: M114 Actual UART

| 항목 | 내용 |
|---|---|
| 목적 | 실제 Printer position read 응답을 terminal line까지 수집 |
| 사전조건 | MDT-PRINTER-UART-002와 동일 |
| 입력/실행 방법 | CLI `printer m114` 또는 MQTT `script=M114` |
| 기대 결과 | 의미 있는 raw response와 유효한 terminal `ok` line |
| 판정 기준 | MDT-PRINTER-UART-002와 동일; 실제 좌표값은 고정 기대값으로 사용하지 않음 |

### MDT-PRINTER-UART-004: M115 Multiline UART

| 항목 | 내용 |
|---|---|
| 목적 | firmware information의 여러 line을 첫 line에서 중단하지 않고 모두 수집 |
| 사전조건 | MDT-PRINTER-UART-002와 동일 |
| 입력/실행 방법 | CLI `printer m115` 또는 MQTT `script=M115` |
| 기대 결과 | 빈 line을 제외한 의미 있는 모든 line과 terminal `ok` line 보존 |
| 판정 기준 | PASS: terminal response까지 전체 수집; FAIL: 첫 line 조기 종료, timeout, `Error:`, overflow, UART 오류 |

### MDT-PRINTER-UART-005: CR/LF/CRLF Response Handling

| 항목 | 내용 |
|---|---|
| 목적 | CR, LF, CRLF 응답을 모두 line delimiter로 처리하고 빈 line을 무시 |
| 사전조건 | 실제 Printer 또는 UART response fixture |
| 입력/실행 방법 | 각 delimiter 형태의 multiline response 입력 |
| 기대 결과 | 의미 있는 line을 `\n`으로 구분해 보존; `Error:`는 기존처럼 Printer 오류; 성공은 정확히 `ok` 또는 뒤따르는 space/tab만 허용 |
| 판정 기준 | `okay`, `okerror`를 성공으로 처리하거나 의미 있는 line을 유실하면 FAIL |

### MDT-PRINTER-SAFETY-001: Unsafe G-code Default Deny

| 항목 | 내용 |
|---|---|
| 목적 | M105/M114/M115 외 입력이 UART write에 도달하지 않음을 확인 |
| 사전조건 | CLI 또는 MQTT RPC 사용 가능 |
| 입력/실행 방법 | `G28`, `G1 X10`, `M105\nG28`, 추가 token 포함 입력 |
| 기대 결과 | NOT_SUPPORTED 또는 `unclassified_gcode/rejected`; accepted progress와 UART write 없음 |
| 판정 기준 | MQTT validation과 mutex 내부 송신 직전 SAFE_READ 재검사를 모두 통과한 canonical 명령만 송신. unsafe input이 write에 도달하면 FAIL |

### MDT-PRINTER-CONCURRENCY-001: CLI/MQTT Mutex Serialization

| 항목 | 내용 |
|---|---|
| 목적 | CLI task와 MQTT worker의 Printer transaction이 같은 UART2에서 섞이지 않도록 직렬화 |
| 사전조건 | CLI와 MQTT RPC를 거의 동시에 실행할 수 있는 환경 |
| 입력/실행 방법 | 양쪽에서 허용 명령을 겹쳐 요청하고 UART/응답 순서 관찰 |
| 기대 결과 | 한 transaction이 terminal response 또는 timeout으로 끝난 뒤 다음 transaction 시작 |
| 판정 기준 | frame/response 혼합 또는 동시 write는 FAIL. busy 즉시 거부 정책은 이번 범위가 아님 |

### MDT-PRINTER-RPC-001: Accepted to Completed

| 항목 | 내용 |
|---|---|
| 목적 | 유효한 SAFE_READ RPC가 accepted 후 실제 UART 성공으로 completed되는지 확인 |
| 사전조건 | RPC subscribe PASS, `printer_id=prt_test001`, progress/response subscriber |
| 입력/실행 방법 | M105/M114/M115를 단일 또는 multiline script로 QoS 1 publish |
| 기대 결과 | UART 시작 전 `progress.status=accepted`; 모든 line 성공 후 `result.status=completed`, `source=uart`, 실제 raw_response |
| 판정 기준 | accepted → completed 순서, request id, boot_id/seq, line 순차 실행 확인 |

### MDT-PRINTER-RPC-002: Accepted to Failed

| 항목 | 내용 |
|---|---|
| 목적 | validation 이후 runtime 오류가 rejected가 아니라 accepted 이후 failed로 보고되는지 확인 |
| 사전조건 | 각 오류를 재현할 수 있는 Printer/fixture 또는 배선 상태 |
| 입력/실행 방법 | timeout, Printer `Error:`, UART 오류, response overflow 각각 실행 |
| 기대 결과 | `error.data.status=failed`, `source=uart`; code는 `printer_timeout`, `printer_error`, `uart_error`, `response_overflow`로 구분 |
| 판정 기준 | runtime 오류를 rejected로 발행하면 FAIL; Printer Error에서는 가능한 raw_response 보존 |

### MDT-PRINTER-RPC-003: Validation Rejection

| 항목 | 내용 |
|---|---|
| 목적 | UART transaction 시작 전 validation 실패만 rejected로 처리 |
| 사전조건 | RPC subscribe PASS |
| 입력/실행 방법 | invalid JSON/params/printer ID, unsupported method, unsafe G-code publish |
| 기대 결과 | accepted progress 없음, `error.data.status=rejected`, UART write 없음 |
| 판정 기준 | 기존 `invalid_json`, `unclassified_gcode`, `unsupported_method` 결과와 validation 순서 유지 |

### MDT-PRINTER-STATUS-001: Recent Transaction Printer/Status

| 최근 결과 | connection | reason |
|---|---|---|
| initialization 성공, transaction 없음 | `unknown` | `not_tested` |
| initialization 실패 | `disconnected` | `uart_initialization_error` |
| terminal `ok` 성공 | `connected` | `last_transaction_ok` |
| terminal `Error:` | `connected` | `printer_error` |
| response overflow | `connected` | `response_overflow` |
| response timeout | `disconnected` | `printer_timeout` |
| UART runtime 오류 | `disconnected` | 해당 UART reason |

모든 current payload는 `printer_id=prt_test001`, `source=uart`여야 한다. 별도 polling이나
health-check state machine은 만들지 않고 최근 transaction 결과만 반영한다.
실제 payload의 판정은 Test Results에 증적과 함께 기록한다.

## 5. Test Result Records

실제 PASS/FAIL/SKIP/NOT_SUPPORTED 결과, build 단계와 hardware verification 이력은
[Test Results](modulino-dev-test-results.md)에 날짜, commit 및 증적과 함께 기록한다.

## 6. Spec Compliance Gaps

다음 항목은 product firmware 전환 전에 해결해야 하는 v0.1 사양 차이다.

- Duplicate RPC command ID deduplication이 구현되지 않아 같은 ID가 다시 실행된다.
- MQTT auto reconnect와 제품 수준 장애 복구가 구현되지 않았다.
- 현재 `boot_id`는 `boot_` + 32자리 lowercase hexadecimal이며 v0.1의 lowercase
  base32/base36 식별자 정책과 다르다.
- Printer UART는 실제 대상 장비에서 이 문서의 hardware 시험 항목을 수행해야 한다.
- 실제 Parts Module discovery/version protocol은 구현되지 않았다.
