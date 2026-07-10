# Modulino-dev Test Firmware

ESP32-S3 Hub의 부팅, 저장소, 네트워크, MQTT, mock Printer RPC, test-only
Parts UART 경로를 실제 보드에서 검증하기 위한 ESP-IDF CLI 프로젝트다.

이 저장소의 firmware는 **test firmware**다. 제품용 장애 복구, 보안, 실제
Printer 제어, 실제 Parts Module protocol을 제공하는 product firmware가 아니다.
mock 결과를 실제 장치 통신 검증 결과로 해석해서는 안 된다.

상세 시험 항목과 재현 절차는 다음 문서를 참고한다.

- [Test plan](docs/modulino-dev-test-plan.md)
- [Test procedure](docs/modulino-dev-test-procedure.md)

## System Overview

```text
Windows Host
  Mosquitto 2.1.2 :1883
          ^
          | Wi-Fi / plain MQTT (local development only)
          v
ESP32-S3 Hub                                      ESP32-C3 mock module
  test firmware                                     test-only firmware
  UART0 console GPIO43/44                           USB Serial/JTAG console
  UART1 TX GPIO17  ------------------------------>  UART1 RX GPIO20
  UART1 RX GPIO18  <------------------------------  UART1 TX GPIO21
  GND              ------------------------------   GND
```

두 보드는 각자의 USB로 전원을 공급한다. 보드 사이에는 TX, RX, GND만 연결하며
VCC, 5V, 3.3V 전원 핀은 연결하지 않는다.

## Confirmed Environment

| 구분 | 환경 |
|---|---|
| Hub | ESP32-S3, ESP-IDF 5.5.2, physical flash 16MB |
| Hub partition | NVS 24KB, `phy_init` 4KB, factory app 4MB, OTA 없음 |
| Hub console | UART0, GPIO43/44 |
| Mock module | ESP32-C3, 독립 프로젝트 `mock_module_fw/` |
| Mock console | USB Serial/JTAG |
| Parts UART | UART1, 115200 baud, 8N1, parity 없음, flow control 없음 |
| Host | Windows + WSL2, Mosquitto 2.1.2, development port 1883 |

문서의 `<BROKER_IP>`, `<HUB_PORT>`, `<C3_PORT>`, `<BUSID>`는 환경별
placeholder다. `172.20.10.3`과 같은 주소는 검증 환경의 예시일 뿐이며 실제
Windows 네트워크 adapter 주소를 확인해 대체해야 한다.

## MVP Scope

현재 구현 범위:

- ESP32-S3 boot log와 interactive Serial CLI
- NVS 초기화 결과 저장과 조회, 자동 erase 없음
- Wi-Fi station 연결 및 IPv4 할당 제한시간 시험
- plain MQTT broker 연결, QoS 1 retained LWT, keepalive 기본 10초
- MQTT 초기 `status`, `birth`, mock `modules/discovery`, `logs` publish
- 5초 주기 `heartbeat`, 3초 주기 mock `printer/status` publish
- boot마다 생성되는 `boot_id`와 모든 Hub-originated publish가 공유하는 전역 `seq`
- MQTT RPC subscribe와 queue/worker 기반 request 처리
- `printer.gcode.run`의 `M105`, `M114`, `M115` SAFE_READ mock 실행
- ESP32-C3와 `MODULINO_PING`/`MODULINO_PONG` test-only UART handshake

현재 제외 범위:

- 실제 Printer UART 송수신 및 실제 측정값
- 실제 Parts Module discovery/version/production protocol
- duplicate RPC command ID deduplication
- TLS, MQTT ACL, provisioning, credential NVS 저장
- 제품 수준 자동 reconnect와 장애 복구
- device time synchronization
- OTA partition과 OTA update

## Repository Layout

| 경로 | 역할 |
|---|---|
| `main/app_main.c` | Hub boot 순서: NVS, Wi-Fi, MQTT, publish, RPC, CLI 시작 |
| `main/test_result.c/.h` | PASS, FAIL, SKIP, NOT_SUPPORTED 결과 출력 API |
| `main/serial_cli.c/.h` | console line input, echo, 명령 dispatch, `test all` |
| `main/nvs_test.c/.h` | `nvs_flash_init()` 1회 실행과 결과 저장 |
| `main/wifi_test.c/.h` | Wi-Fi STA 연결 및 IPv4 결과 저장 |
| `main/mqtt_test.c/.h` | MQTT 연결, LWT, keepalive, client 보관 |
| `main/mqtt_publish.c/.h` | 초기/주기 topic publish, `boot_id`, 공유 `seq` |
| `main/mqtt_rpc.c/.h` | RPC subscribe, fragment 재조립, validation, worker, 응답 |
| `main/gcode_safety.c/.h` | M105/M114/M115만 SAFE_READ로 분류 |
| `main/printer_comm.c/.h` | Printer mock 응답, 실제 UART는 NOT_SUPPORTED |
| `main/module_uart_test.c/.h` | ESP32-S3 UART1 PING/PONG handshake 시험과 최근 결과 |
| `partitions.csv` | 4MB factory app을 포함한 Hub custom partition table |
| `sdkconfig.defaults` | Hub 16MB flash와 custom partition 기본 설정 |
| `mock_module_fw/` | ESP32-C3 test-only mock Parts Module 독립 프로젝트 |
| `docs/` | test plan과 단계별 재현 절차 |

## Build, Flash, Monitor

ESP-IDF 5.5.2 환경을 export한다. 설치 위치는 로컬 환경에 맞게 바꾼다.

```bash
source "$HOME/esp/v5.5.2/esp-idf/export.sh"
idf.py --version
```

Hub ESP32-S3:

```bash
cd /path/to/modulino_test_fw
idf.py build
idf.py -p <HUB_PORT> flash monitor
```

Mock ESP32-C3:

```bash
cd /path/to/modulino_test_fw/mock_module_fw
idf.py build
idf.py -p <C3_PORT> flash monitor
```

ESP32-C3 target은 `mock_module_fw/CMakeLists.txt`에 `esp32c3`로 고정되어 있다.
monitor 종료 키는 `Ctrl+]`다. 두 장치를 동시에 monitor할 경우 별도 WSL terminal을
사용한다.

## Menuconfig

Hub root에서 실행한다.

```bash
idf.py menuconfig
```

`Modulino test firmware` menu의 설정:

| 설정 | 기본값 | 설명 |
|---|---:|---|
| Wi-Fi station SSID | 빈 문자열 | 비어 있으면 Wi-Fi test SKIP |
| Wi-Fi station password | 빈 문자열 | firmware가 로그에 출력하거나 NVS에 저장하지 않음 |
| Wi-Fi connection timeout | 15000ms | IPv4 할당 대기 제한시간 |
| Hub ID | `hub_test001` | MQTT topic과 payload의 Hub 식별자 |
| MQTT broker URI | 예시 `mqtt://172.20.10.3:1883` | 실제 값은 `mqtt://<BROKER_IP>:1883`; 비어 있으면 SKIP |
| MQTT connection timeout | 10000ms | `MQTT_EVENT_CONNECTED` 대기 제한시간 |
| MQTT keepalive | 10초 | 비정상 연결 종료와 LWT 감지에 사용 |

실제 SSID/password는 문서, source, commit에 넣지 않는다. Hub `sdkconfig`는
`.gitignore` 대상이며 credential은 Wi-Fi RAM storage로 사용된다.

## Serial CLI

| 명령 | 동작 |
|---|---|
| `help` | 지원 명령 출력 |
| `test all` | 부팅 시 저장된 결과와 Parts UART의 최근 실행 결과 출력 |
| `test module uart` | RX flush 후 PING/PONG handshake를 최대 1000ms 수행 |
| `printer m105` | M105 mock 응답 출력 |
| `printer m114` | M114 mock 응답 출력 |
| `printer m115` | M115 mock 응답 출력 |

`test module uart`를 실행하지 않은 상태에서 `test all`은 Parts UART를
`SKIP - test not run`으로 출력한다. 실제 Printer UART test는 항상
`NOT_SUPPORTED`다.

## MQTT Topics

모든 topic prefix는 `modulino/local/v1/{hub_id}`다.

| Suffix | 방향 | QoS | Retain | 동작 |
|---|---|---:|---:|---|
| `status` | Hub -> Host | 1 | true | 연결 후 online; 비정상 종료 시 broker가 offline LWT 발행 |
| `birth` | Hub -> Host | 1 | true | firmware/protocol/reset 정보, boot당 1회 |
| `modules/discovery` | Hub -> Host | 1 | true | `source=mock`, `scan_id=scan_mock001`, `modules=[]` |
| `logs` | Hub -> Host | 0 | false | `mqtt_connected` info event, 연결 직후 1회 |
| `heartbeat` | Hub -> Host | 0 | false | 5초 주기 uptime, free heap, Wi-Fi RSSI |
| `printer/status` | Hub -> Host | 0 | false | 3초 주기, `prt_mock001`, `connection=disconnected`, `source=mock` |
| `rpc/request` | Host -> Hub | 1 | 사용 금지 | retained request는 실행하지 않고 폐기 |
| `rpc/progress` | Hub -> Host | 1 | false | 안전한 요청의 `accepted` progress |
| `rpc/response` | Hub -> Host | 1 | false | `completed` result 또는 `rejected` error |

정상 Hub payload에는 동일 boot 동안 유지되는 `boot_id`, 전역 단조 증가 `seq`,
`device_ts=null`, `ts_quality=unsynced`가 포함된다. 현재 `boot_id`는
`boot_` 뒤에 lowercase random hex identifier가 붙는다. LWT는 broker가 대신
발행하므로 `boot_id`와 `seq`를 포함하지 않는다.

## RPC Overview

현재 지원 method는 `printer.gcode.run` 하나다. 요청은 JSON-RPC 2.0 style이며
Host-originated request이므로 Hub 공통 envelope를 요구하지 않는다.

```json
{
  "jsonrpc": "2.0",
  "method": "printer.gcode.run",
  "params": {
    "printer_id": "prt_mock001",
    "script": "M105"
  },
  "id": "cmd_test001"
}
```

M105, M114, M115만 기존 G-code safety와 Printer mock 경로로 실행한다. 안전한
요청은 `accepted` progress 뒤 `completed` response를 발행한다. G1, M104, G28
등은 `unclassified_gcode`, 미지원 method는 `unsupported_method`, 잘못된 JSON은
`invalid_json`으로 progress 없이 거부한다. ID를 파싱하지 못하면 response의
`id`는 `null`이다.

M105는 MQTT request publish부터 progress/response의 Host 수신까지 mock RPC
end-to-end로 실제 검증됐다. M114/M115는 구현되어 있고 Serial CLI의 Printer mock
경로도 확인됐지만, MQTT RPC end-to-end Host 수신 증적은 아직 없다. M105 검증
결과를 M114/M115까지 확대 해석하지 않는다.

제한값은 request payload 8KB, script 2048 bytes, 최대 10 lines, line당 128
bytes다. MQTT fragmented payload는 재조립 후 worker task에서 처리한다.

## Test Result States

| 상태 | 의미 |
|---|---|
| PASS | 해당 실행에서 기대 동작을 관찰함 |
| FAIL | 실행했으나 초기화, 연결, timeout, 응답 또는 validation 결과가 기대와 다름 |
| SKIP | 필수 설정/선행 연결이 없거나 아직 해당 runtime test를 실행하지 않음 |
| NOT_SUPPORTED | 현재 test firmware에 의도적으로 구현되지 않은 기능 |

빌드 성공은 compile/link/partition 적합성만 의미한다. PASS로 기록된 실제 보드
시험과 동일한 의미가 아니다.

## Verified on Hardware

다음 연결 및 보드 동작이 실제 환경에서 확인됐다.

- Wi-Fi IPv4 할당 PASS
- MQTT broker connect PASS
- Parts UART PING/PONG 5회 연속 PASS

다음 항목은 Host MQTT subscriber에서 payload를 실제 수신해 확인했다.

- MQTT LWT offline actual publish PASS
- heartbeat 5초 주기 수신 PASS
- mock printer/status 3초 주기 수신 PASS
- 동일 boot에서 공통 `boot_id`와 전역 단조 증가 `seq` 확인
- M105 RPC `accepted` -> `completed` PASS
- invalid JSON의 `invalid_json`, `id=null` rejection PASS
- G1 `unclassified_gcode` rejection PASS
- 미지원 method의 `unsupported_method` rejection PASS

초기 `status` online, `birth`, mock `modules/discovery`, `logs`는 firmware의
`mqtt initial publish PASS - status,birth,modules,logs queued`를 통해 네 publish
요청의 client queue 등록 성공까지 확인했다. 현재 문서 작성 기준으로 각 topic의
개별 Host subscriber 수신 증적은 확보되지 않았다. `mqtt initial publish PASS`는
`esp_mqtt_client_enqueue()` 성공을 뜻하며 broker 수신, retain 적용 또는 subscriber
수신 PASS를 의미하지 않는다.

ESP32-C3 reset 직후 Hub가 C3 ready 이전에 handshake를 시작하면 첫 시도에서
1회 timeout이 발생할 수 있다. C3 ready 확인 후 재실행하면 된다.

## Spec Deviations / Product Firmware Blockers

다음 항목은 단순한 test firmware 편의사항이 아니다. v0.1 통신 사양을 준수하는
product firmware로 넘어가기 전에 해결해야 하는 사양 차이와 blocker다.

- v0.1 사양에서 요구하는 duplicate RPC command ID deduplication이 구현되지 않았다.
  동일한 command ID를 다시 보내면 현재 test firmware는 다시 실행한다.
- v0.1 제품 동작에 필요한 MQTT 자동 재연결과 장애 복구가 구현되지 않았다.
- 현재 `boot_id`는 `boot_` + 32자리 lowercase hexadecimal이며, v0.1 식별자
  정책의 lowercase base32/base36 형식과 다르다.
- 실제 Printer 통신과 실제 Parts Module discovery/version protocol은 구현되지 않았다.
- TLS, MQTT ACL, provisioning이 구현되지 않아 product security 및 credential
  lifecycle 요구사항을 충족하지 않는다.

## Known Limitations

- M105/M114/M115 응답과 printer/status는 test-only Printer mock이다.
- Parts UART PING/PONG은 test-only handshake이며 실제 Parts Module protocol이 아니다.
- device time synchronization이 없어 `device_ts=null`, `ts_quality=unsynced`다.
- OTA partition이 없다.
- mock discovery는 항상 `modules=[]`다.
- Windows broker의 `allow_anonymous true`는 격리된 local development에서만 허용한다.
- C3와 Hub 동시 부팅 시 C3 ready 이전 handshake는 timeout될 수 있다.
