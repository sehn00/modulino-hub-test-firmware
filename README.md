# Modulino-dev Test Firmware

ESP32-S3 Hub의 NVS, Wi-Fi, MQTT, Parts Module test UART와 실제 3D Printer
UART 경로를 검증하기 위한 ESP-IDF test firmware다. Printer 경로는 Marlin 기반
Ender-3 V3 SE를 대상으로 `M105`, `M114`, `M115` SAFE_READ 명령만 허용한다.

현재 상태는 다음과 같다.

- `BUILD_VERIFIED`: ESP-IDF 5.5.2, ESP32-S3 target에서 `idf.py build` 성공
- `READY_FOR_HW_TEST`: 실제 Printer UART 구현과 시험 절차 준비 완료
- `HW_VERIFIED` 아님: Ender-3 V3 SE 연결 시험은 담당자가 수행해야 함

빌드 성공이나 UART driver 초기화 성공을 실제 Printer 통신 PASS로 해석하면 안 된다.
`modules/discovery`는 기존 mock 동작(`source=mock`, 빈 module 목록)을 유지한다.

상세 판정 기준과 단계별 절차는 다음 문서를 참고한다.

- [Test plan](docs/modulino-dev-test-plan.md)
- [Test procedure](docs/modulino-dev-test-procedure.md)

## Printer UART configuration

baud rate를 제외한 설정은 firmware에 고정되어 있으며 menuconfig 항목이 아니다.

| 항목 | 설정 |
|---|---|
| UART controller | `UART_NUM_2` |
| Hub TX | GPIO7 |
| Hub RX | GPIO8 |
| frame | 8 data bits, parity none, 1 stop bit |
| flow control | none |
| TX line ending | LF (`\n`) |
| terminal response timeout | 3000 ms |
| default baud rate | 115200 |
| selectable baud rates | 9600, 19200, 38400, 57600, 115200, 250000 |
| printer ID | `prt_test001` |

UART0는 console, UART1은 Parts Module test용, UART2는 Printer 전용이다.

## Wiring

전원을 끈 상태에서 TX/RX를 교차하고 GND를 공통 연결한다.

| ESP32-S3 Hub | Printer UART side |
|---|---|
| GPIO7 TX | RX |
| GPIO8 RX | TX |
| GND | GND |

TX-TX 또는 RX-RX로 연결하지 않는다. Printer 쪽 connector pinout과 전기적 신호
레벨은 해당 장비/보드 자료와 실제 측정으로 별도 확인해야 하며, 확인되지 않은 전원
핀을 연결하지 않는다.

## Configure, build, flash, monitor

ESP-IDF 5.5.2 환경을 준비한다.

```bash
source "$HOME/esp/v5.5.2/esp-idf/export.sh"
cd <REPO_ROOT>
idf.py menuconfig
```

`Modulino test firmware` → `Printer UART baud rate`에서 Printer와 동일한 baud를
선택한다. 기본값은 115200이다. 저장 후 다음을 실행한다.

```bash
idf.py build
idf.py -p <HUB_PORT> flash monitor
```

monitor 종료는 `Ctrl+]`다. build 성공은 `BUILD_VERIFIED`이고, 실제 UART 시험의
정상 `ok` 응답을 확인해야 해당 명령을 hardware PASS로 기록할 수 있다.

## CLI Printer test

Hub monitor에서 다음 명령을 각각 실행한다.

```text
modulino> printer m105
modulino> printer m114
modulino> printer m115
```

세 명령은 mock이 아니라 `printer_comm_uart_query()`를 실행한다. 성공 예시의 응답
내용은 실제 Printer firmware와 상태에 따라 달라지므로 문서에서 값을 고정하지 않는다.

```text
[PRINTER UART] M105 PASS
<raw response ending in an ok line>
```

`M115`처럼 여러 줄인 응답도 terminal `ok` line까지 줄 내용을 보존해 출력한다.
CR, LF, CRLF는 모두 줄 구분자로 처리하고 빈 줄은 무시한다. 성공 terminal line은
정확히 `ok`이거나 `ok` 다음 문자가 space/tab인 경우만 허용하며, `okay`와
`okerror`는 성공이 아니다.

실패는 다음처럼 분리된다.

| code/상황 | 판정 |
|---|---|
| terminal line이 정확히 `ok` 또는 `ok` 뒤 space/tab | PASS |
| 3000 ms 내 terminal line 없음 / `printer_timeout` | FAIL |
| line이 `Error:`로 시작 / `printer_error` | FAIL; 수신한 raw response 출력 |
| caller response buffer 초과 / `response_overflow` | FAIL |
| initialization, RX flush, write, TX wait, read 오류 / `uart_error` | FAIL |

`test all`은 실제 G-code를 자동 송신하지 않는다. 아직 CLI 또는 RPC transaction을
실행하지 않았으면 `READY_FOR_HW_TEST`와 SKIP을 표시한다. 기존 mock query는 회귀
검사용으로만 남아 있고 `printer m105/m114/m115`의 기본 경로가 아니다.

## Safety and transaction behavior

- `gcode_safety`는 `M105`, `M114`, `M115`만 SAFE_READ로 분류한다.
- MQTT가 미리 validation하더라도 `printer_comm`이 mutex 안에서 송신 직전에 다시
  검사한다.
- 허용 명령은 canonical command로 바뀌므로 추가 token이나 여러 명령이 UART write에
  포함되지 않는다.
- transaction 시작 전 UART2 RX input을 flush한다.
- CLI task와 MQTT RPC worker는 하나의 FreeRTOS mutex를 공유해 동시에 UART를
  사용하지 못한다.
- write 길이와 `uart_wait_tx_done()` 결과를 확인한 뒤 수신을 시작한다.
- 여러 줄을 모으고 유효한 `ok` 경계 또는 `Error:...` terminal line에서 종료한다.

## MQTT RPC Printer test

Request topic:

```text
modulino/local/v1/<HUB_ID>/rpc/request
```

단일 명령 payload 예시:

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

`script`에는 CR, LF 또는 CRLF로 구분한 최대 10개 명령을 넣을 수 있지만 각 line은
정확히 M105/M114/M115 중 하나여야 한다. validation 성공 후 다음 순서로 발행한다.

1. `rpc/progress`: `progress.status="accepted"`
2. UART2 transaction을 line 순서대로 실행
3. 모두 성공하면 `rpc/response`: `result.status="completed"`,
   `result.source="uart"`, `result.raw_response=<실제 응답>`

실행 중 실패는 accepted 이후 `error.data.status="failed"` response로 발행한다.
`error.code`는 `printer_timeout`, `printer_error`, `uart_error`,
`response_overflow` 등으로 구분한다. Printer `Error:`인 경우 가능한 raw response를
`error.data.raw_response`에 보존한다. validation 전에 발견한 invalid JSON, 잘못된
printer ID, unsafe G-code만 `rejected`이며 accepted progress가 없어야 한다.

unsafe 예시:

```json
{
  "jsonrpc": "2.0",
  "method": "printer.gcode.run",
  "params": {
    "printer_id": "prt_test001",
    "script": "G28"
  },
  "id": "cmd_unsafe_001"
}
```

기대 결과는 `error.code="unclassified_gcode"`,
`error.data.status="rejected"`, accepted progress 없음, UART write 없음이다.

## Printer status

`printer/status`와 RPC는 공통 Printer ID `prt_test001`을 사용하고 status source는
`uart`다. 별도 polling이나 health-check는 수행하지 않고 UART initialization 실패
또는 최근 UART transaction 결과만 반영한다.

| 최근 결과 | connection | reason 예시 |
|---|---|---|
| initialization 성공, transaction 없음 | `unknown` | `not_tested` |
| initialization 실패 | `disconnected` | `uart_initialization_error` |
| terminal `ok` | `connected` | `last_transaction_ok` |
| terminal `Error:` | `connected` | `printer_error` |
| response overflow | `connected` | `response_overflow` |
| timeout | `disconnected` | `printer_timeout` |
| UART initialization/runtime error | `disconnected` | 해당 UART error reason |

Printer가 명시적인 `Error:`를 보낸 경우 통신 연결은 성립했으므로 connection은
`connected`이지만 transaction 판정은 FAIL이다.

## Project layout

| 경로 | 역할 |
|---|---|
| `main/app_main.c` | boot sequence와 Printer UART 명시적 초기화 |
| `main/printer_comm.c/.h` | UART2 transaction, mutex, result/status API, mock 회귀 helper |
| `main/gcode_safety.c/.h` | M105/M114/M115 SAFE_READ allowlist |
| `main/serial_cli.c/.h` | console CLI와 실제 Printer 명령 |
| `main/mqtt_rpc.c/.h` | JSON-RPC validation, accepted/completed/failed 처리 |
| `main/mqtt_publish.c/.h` | Hub publish와 최근 UART 기반 printer/status |
| `main/Kconfig.projbuild` | Wi-Fi/MQTT 설정과 Printer baud choice |
| `main/module_uart_test.c/.h` | 별도 UART1 Parts Module test handshake |
| `mock_module_fw/` | 기존 ESP32-C3 Parts Module test-only mock firmware |

## Verification record rule

시험 기록에는 다음을 분리한다.

- `BUILD_VERIFIED`: `idf.py build` 성공과 binary 생성
- `READY_FOR_HW_TEST`: 코드와 절차 준비, 실기기 결과 없음
- `HW_VERIFIED`: 담당자가 실제 Ender-3 V3 SE에서 명령별 raw response와 판정을 기록한
  뒤에만 사용

현재 저장소 상태는 `BUILD_VERIFIED` + `READY_FOR_HW_TEST`이며 `HW_VERIFIED`가 아니다.
