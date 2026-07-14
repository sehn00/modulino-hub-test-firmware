# Modulino Hub Test Firmware

Modulino Hub Test Firmware는 ESP32-S3 기반 `modulino-dev` Hub board의 bring-up과
통신 경로 검증을 위한 test firmware다. 제품 펌웨어가 아니며, Hub와 Host PC,
Parts Module, 3D Printer 사이의 연결 및 기본 통신 경로를 진단하는 데 사용한다.

제품 수준 보안, 자동 복구, OTA, 실제 Parts Module production protocol은 이
저장소의 범위에 포함하지 않는다.

## 1. Project Overview

이 프로젝트는 다음 항목을 독립적으로 확인할 수 있는 최소 진단 환경을 제공한다.

- ESP32-S3 Hub의 boot, log, NVS 및 Serial CLI
- Wi-Fi station 연결과 MQTT publish/subscribe/LWT
- Host에서 MQTT JSON-RPC 요청을 보내고 progress/response를 관찰하는 경로
- UART1을 사용하는 test-only Parts Module PING/PONG handshake
- UART2를 사용하는 Marlin 기반 3D Printer read-only 통신

## 2. System Architecture

```text
Parts Modules ↔ Hub Board(ESP32-S3) ↔ 3D Printer
                     ↕
                 Host PC
             MQTT Broker
```

Host PC는 UART0 console과 MQTT broker/subscriber를 통해 Hub를 제어하고 결과를
관찰한다. Parts Module 경로와 Printer 경로는 서로 다른 UART controller를 사용한다.

## 3. Supported Test Paths

- ESP32-S3 boot 및 serial runtime log
- line-oriented Serial CLI
- NVS initialization
- Wi-Fi STA 연결과 IPv4 할당
- MQTT connect, publish, subscribe 및 LWT
- MQTT RPC request → progress → response
- UART1 Parts Module test-only mock handshake (`MODULINO_PING`/`MODULINO_PONG`)
- UART2 actual Printer transaction
- Printer G-code SAFE_READ allowlist: `M105`, `M114`, `M115`

`modules/discovery`는 실제 Parts Module discovery가 아니라 기존 mock payload를
발행한다. Printer 명령은 각각 명시적으로 실행하며 `test all`이 자동 전송하지 않는다.

## 4. Hardware Interfaces

| 경로 | Controller | Hub TX | Hub RX | 용도 |
|---|---|---:|---:|---|
| Host console | UART0 | GPIO43 | GPIO44 | ESP-IDF log 및 Serial CLI |
| Parts test | UART1 | GPIO17 | GPIO18 | test-only mock PING/PONG |
| 3D Printer | UART2 | GPIO7 | GPIO8 | actual Printer read-only G-code |

Printer UART는 GPIO Matrix를 통해 UART2에 연결되며 다음 설정은 firmware에 고정된다.

| 항목 | 값 |
|---|---|
| Data bits | 8 |
| Parity | none |
| Stop bits | 1 |
| Flow control | none |
| TX line ending | LF (`\n`) |
| Response timeout | 3000 ms |
| Baud rate | menuconfig 선택, 기본 115200 |

장치 간 UART는 TX/RX를 교차 연결하고 GND를 공통 연결한다. 실제 connector pinout과
전기적 신호 레벨은 보드 및 장비 자료와 측정으로 별도 확인해야 한다. 확인되지 않은
전원 핀은 연결하지 않는다.

## 5. Build and Flash

ESP-IDF 5.5.2 환경에서 다음 순서로 실행한다.

```bash
source "$HOME/esp/v5.5.2/esp-idf/export.sh"
cd <REPO_ROOT>
idf.py menuconfig
idf.py build
idf.py -p <HUB_PORT> flash monitor
```

`<REPO_ROOT>`와 `<HUB_PORT>`는 로컬 환경에 맞게 바꾼다. ESP-IDF monitor는
`Ctrl+]`로 종료한다.

## 6. Menuconfig

`idf.py menuconfig`의 `Modulino test firmware` 메뉴에서 다음 항목을 설정한다.

| 항목 | 용도 | 기본값/예시 |
|---|---|---|
| Wi-Fi station SSID | 시험 AP의 SSID | 비어 있으면 Wi-Fi 시험 생략 |
| Wi-Fi station password | 시험 AP의 password | 비어 있음 |
| Wi-Fi connection timeout | IPv4 대기 시간 | 15000 ms |
| Hub ID | MQTT topic과 payload의 Hub 식별자 | `hub_test001` |
| MQTT broker URI | 시험 broker 주소 | `mqtt://<BROKER_IP>:1883` |
| MQTT connection timeout | broker 연결 대기 시간 | 10000 ms |
| MQTT keepalive | LWT 감지용 keepalive | 10 seconds |
| Printer UART baud rate | Printer와 동일한 baud | 115200 |

Printer baud 선택지는 9600, 19200, 38400, 57600, 115200, 250000이다. 실제
Printer baud가 다르면 menuconfig에서 변경한다. SSID, password, 개인 IP, MAC
address와 credential은 README, source, commit 또는 log에 기록하지 않는다.

## 7. Serial CLI Quick Start

monitor에 `modulino>` prompt가 표시되면 다음 명령을 사용할 수 있다.

| 명령 | 동작 |
|---|---|
| `help` | 지원 명령 목록 출력 |
| `test all` | 저장된 Hub 진단 결과 요약; actual G-code는 전송하지 않음 |
| `test module uart` | UART1 Parts mock PING/PONG 실행 |
| `printer m105` | UART2로 온도 조회 `M105`를 보내고 응답 수신 |
| `printer m114` | UART2로 현재 좌표 조회 `M114`를 보내고 응답 수신 |
| `printer m115` | UART2로 firmware information `M115`를 보내고 multiline 응답 수신 |

실제 Printer UART 통신은 `printer m105`, `printer m114`, `printer m115`를 각각
명시적으로 실행해야 한다. 허용된 세 명령 외의 이동, 가열, 설정 G-code는
default-deny 정책으로 차단된다. 단계별 배선, 실행 및 판정 방법은 Test Procedure를
따른다.

## 8. MQTT RPC Quick Start

MQTT topic prefix는 다음과 같다.

```text
modulino/local/v1/{hub_id}/...
```

| 용도 | Topic |
|---|---|
| Request | `modulino/local/v1/{hub_id}/rpc/request` |
| Progress | `modulino/local/v1/{hub_id}/rpc/progress` |
| Response | `modulino/local/v1/{hub_id}/rpc/response` |

지원 method는 `printer.gcode.run`, Printer ID는 `prt_test001`이다. Script에는
`M105`, `M114`, `M115`만 사용할 수 있다. Validation을 통과하면 `accepted` progress
후 actual UART 결과에 따라 `completed` 또는 `failed` response가 발행된다.
Validation 단계에서 거부된 요청은 `rejected` response가 되며 `accepted` progress가
발행되지 않는다.

단일 M105 request 예시:

```json
{
  "jsonrpc": "2.0",
  "method": "printer.gcode.run",
  "params": {
    "printer_id": "prt_test001",
    "script": "M105"
  },
  "id": "cmd_m105_001"
}
```

상세 payload, error code, subscriber 명령과 PASS/FAIL 기준은 Test Plan과 Test
Procedure를 참고한다.

## 9. Documentation

- [Test plan](docs/modulino-dev-test-plan.md): 시험 범위, 기대 결과와 판정 기준
- [Test procedure](docs/modulino-dev-test-procedure.md): 환경 준비와 단계별 실행 절차
- [Test results](docs/modulino-dev-test-results.md): 실제 실행 결과와 증적 기록

README는 프로젝트 소개와 빠른 시작만 다룬다. 실행 시점에 따라 바뀌는 결과와
hardware verification 기록은 Test Results에서 관리한다.

## 10. Repository Layout

| 경로 | 역할 |
|---|---|
| `main/app_main.c` | Hub boot sequence와 subsystem 초기화 |
| `main/test_result.c/.h` | 공통 test result와 detail 저장 |
| `main/serial_cli.c/.h` | UART0 console CLI |
| `main/gcode_safety.c/.h` | Printer SAFE_READ allowlist |
| `main/printer_comm.c/.h` | UART2 Printer transaction과 최근 결과 |
| `main/nvs_test.c/.h` | NVS initialization test |
| `main/wifi_test.c/.h` | Wi-Fi STA connection test |
| `main/mqtt_test.c/.h` | MQTT connection 및 RPC subscription test |
| `main/mqtt_publish.c/.h` | Hub status/event topic publish |
| `main/mqtt_rpc.c/.h` | JSON-RPC validation과 Printer 실행 worker |
| `main/module_uart_test.c/.h` | UART1 Parts mock handshake |
| `main/Kconfig.projbuild` | Wi-Fi, MQTT, Hub ID 및 Printer baud 설정 |
| `main/CMakeLists.txt` | Hub component source와 dependency 등록 |
| `partitions.csv` | ESP32-S3 flash partition layout |
| `sdkconfig.defaults` | Hub ESP-IDF 기본 설정 |
| `mock_module_fw/` | ESP32-C3 test-only Parts mock firmware |
| `docs/` | Test Plan, Procedure 및 Results |

## 11. Scope and Limitations

- 이 저장소는 bring-up 및 통신 경로 검증용이며 제품 펌웨어가 아니다.
- 실제 Parts Module discovery/version/production protocol은 구현하지 않는다.
- MQTT RPC duplicate command ID deduplication은 구현하지 않는다.
- 제품 수준 자동 reconnect, 장애 복구 또는 health-check state machine을 제공하지 않는다.
- TLS, broker ACL, credential provisioning 등 제품 수준 보안을 제공하지 않는다.
- OTA와 device time synchronization을 제공하지 않는다.
- `M105`, `M114`, `M115` 외의 Printer G-code는 차단한다.
- 실제 connector pinout과 허용 voltage level은 별도 hardware 자료로 확인해야 한다.
