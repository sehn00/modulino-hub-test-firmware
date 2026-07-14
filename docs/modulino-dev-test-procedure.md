# Modulino-dev Test Procedure

## Scope and Conventions

이 절차는 처음 프로젝트를 접한 개발자가 Windows + WSL2에서 ESP32-S3 Hub,
Marlin 기반 Ender-3 V3 SE, ESP32-C3 test-only mock module, Mosquitto 2.1.2를
사용해 MVP 시험을 재현하기 위한 순서다. Printer UART 구현은 현재
`READY_FOR_HW_TEST`이며 담당자가 실제 Printer에서 검증해야 한다.

placeholder:

| 표기 | 대체할 값 |
|---|---|
| `<REPO_ROOT>` | WSL에서 이 저장소의 절대 경로 |
| `<HUB_PORT>` | WSL에 attach된 ESP32-S3 console device |
| `<C3_PORT>` | WSL에 attach된 ESP32-C3 USB Serial/JTAG device |
| `<HUB_BUSID>` | `usbipd list`에 표시된 Hub USB BUSID |
| `<C3_BUSID>` | `usbipd list`에 표시된 C3 USB BUSID |
| `<BROKER_IP>` | ESP32-S3가 접근 가능한 Windows adapter의 실제 IPv4 |
| `<HUB_ID>` | menuconfig Hub ID, 기본 `hub_test001` |

검증 환경의 예시 broker 주소가 `172.20.10.3`이더라도 다른 환경에서 그대로
사용하지 않는다. Windows `ipconfig`로 ESP32-S3와 같은 네트워크의 실제 주소를
확인한다. 실제 Wi-Fi SSID/password, MAC address, 개인 credential은 문서나
시험 결과에 기록하지 않는다.

필요 도구:

- Windows 10/11, WSL2, usbipd-win
- ESP-IDF 5.5.2 CLI 환경
- Mosquitto 2.1.2 Windows binaries
- ESP32-S3 Hub와 ESP32-C3 mock board
- 실제 시험 대상 Ender-3 V3 SE와 확인된 Printer UART access point
- 3.3V logic UART용 TX/RX/GND 배선

## 1. ESP-IDF Export

WSL terminal에서 ESP-IDF 5.5.2를 export한다. 실제 설치 경로가 다르면 수정한다.

```bash
source "$HOME/esp/v5.5.2/esp-idf/export.sh"
idf.py --version
```

기대 결과:

```text
ESP-IDF v5.5.2
```

같은 terminal에서 다음 값을 준비하면 이후 명령이 간단해진다.

```bash
export REPO_ROOT=<REPO_ROOT>
cd "$REPO_ROOT"
```

새 WSL terminal을 열 때마다 ESP-IDF export를 다시 수행한다.

## 2. Hub and C3 Build

먼저 credential 없이도 source와 target 구성이 유효한지 두 프로젝트를 빌드한다.

Hub ESP32-S3:

```bash
cd "$REPO_ROOT"
idf.py build
```

기대 binary:

```text
<REPO_ROOT>/build/modulino_test_fw.bin
```

빌드 출력에서 target `esp32s3`, flash size 16MB, smallest app partition
`0x400000`을 확인한다.

Mock ESP32-C3:

```bash
cd "$REPO_ROOT/mock_module_fw"
idf.py build
```

기대 binary:

```text
<REPO_ROOT>/mock_module_fw/build/modulino_mock_module_fw.bin
```

빌드 출력에서 target이 `esp32c3`인지 확인한다. C3의 generated `sdkconfig`에는
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`가 설정되어야 한다.

이 단계의 성공은 `BUILD_VERIFIED`이며 실제 Printer UART/MQTT 동작 PASS 또는
`HW_VERIFIED`를 의미하지 않는다.

## 3. Windows usbipd List, Bind, Attach

두 보드를 Windows USB에 연결한다. PowerShell에서 장치를 확인한다.

```powershell
usbipd list
```

보드를 하나씩 연결/분리하며 목록 변화를 비교해 Hub와 C3의 BUSID를 식별한다.
관리자 PowerShell에서 각 장치를 최초 1회 bind한다.

```powershell
usbipd bind --busid <HUB_BUSID>
usbipd bind --busid <C3_BUSID>
```

그 다음 WSL에 attach한다.

```powershell
usbipd attach --wsl --busid <HUB_BUSID>
usbipd attach --wsl --busid <C3_BUSID>
usbipd list
```

`Attached` 상태를 확인한다. USB 분리, Windows 재부팅 또는 WSL 종료 후에는 attach를
다시 수행해야 할 수 있다.

## 4. Identify `/dev/ttyUSB*` and `/dev/ttyACM*`

WSL에서 attach 전후의 device 목록을 비교한다.

```bash
lsusb
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
dmesg | tail -n 40
```

일반적으로 USB-UART bridge를 사용하는 Hub는 `/dev/ttyUSB*`, C3 native USB
Serial/JTAG는 `/dev/ttyACM*`로 나타나지만 보드와 driver에 따라 다를 수 있다.
추정하지 말고 하나씩 attach하거나 다음 명령으로 속성을 확인한다.

```bash
udevadm info --query=property --name=<HUB_PORT>
udevadm info --query=property --name=<C3_PORT>
```

현재 shell user가 serial port를 열 수 없다면 `dialout` group을 확인한다.

```bash
groups
sudo usermod -aG dialout "$USER"
```

group 변경은 WSL session을 완전히 다시 시작한 뒤 적용된다. 이후 예시에서는 다음과
같이 환경 변수를 사용한다.

```bash
export HUB_PORT=<HUB_PORT>
export C3_PORT=<C3_PORT>
```

## 5. Flash ESP32-C3 Mock Firmware First

C3 project에서 flash하고 monitor를 연다.

```bash
cd "$REPO_ROOT/mock_module_fw"
idf.py -p "$C3_PORT" flash monitor
```

다음 ready log를 확인한다.

```text
test-only Parts Module UART mock ready
```

이 log는 USB Serial/JTAG console에만 출력된다. GPIO20/21 UART1에는
`MODULINO_PONG\n` 같은 handshake data만 출력된다. monitor를 종료하려면
`Ctrl+]`를 누른다.

## 6. Flash Hub After C3 Is Ready

C3가 ready 상태임을 확인한 뒤 별도 WSL terminal에서 Hub를 flash한다. 새 terminal은
ESP-IDF export가 필요하다.

```bash
source "$HOME/esp/v5.5.2/esp-idf/export.sh"
cd <REPO_ROOT>
idf.py -p <HUB_PORT> flash monitor
```

Hub boot log와 `modulino>` prompt를 확인한다. Hub console은 UART0 GPIO43/44다.
이 단계에서 Wi-Fi SSID가 아직 설정되지 않았다면 Wi-Fi/MQTT SKIP은 정상이다.

## 7. Parts Module UART Wiring and Power Safety

배선 변경 전 두 보드의 USB를 분리해 전원을 끈다. 다음과 같이 교차 연결한다.

| ESP32-S3 Hub | ESP32-C3 mock |
|---|---|
| GPIO17 TX | GPIO20 RX |
| GPIO18 RX | GPIO21 TX |
| GND | GND |

주의사항:

- GND는 반드시 공통으로 연결한다.
- VCC, 5V, 3.3V는 보드 사이에 연결하지 않는다.
- 두 보드는 각자의 USB cable로 별도 전원을 공급한다.
- TX-TX 또는 RX-RX로 연결하지 않는다.
- 이 배선은 3.3V logic UART를 전제로 한다.

배선 후 C3 USB를 먼저 연결하고 ready log를 확인한다. 그 다음 Hub USB를 연결하거나
Hub를 reset한다. 두 보드를 동시에 reset하면 C3 ready 이전 첫 handshake가 timeout될
수 있다.

## 8. Run `test module uart`

Hub monitor의 CLI에서 실행한다.

```text
modulino> test module uart
```

Hub 동작:

1. UART1 RX buffer flush
2. GPIO17 TX로 `MODULINO_PING\n` 전송
3. GPIO18 RX에서 최대 1000ms 대기
4. CR/LF 제거 후 `MODULINO_PONG` 비교

정상 출력:

```text
[TEST] parts_module_uart handshake      PASS - response=MODULINO_PONG
```

연속 안정성을 확인하려면 C3 ready 상태에서 5회 실행하고 모두 PASS인지 확인한다.

Timeout:

```text
[TEST] parts_module_uart handshake      FAIL - timeout after 1000 ms
```

C3 reset 직후 첫 시도만 timeout이면 C3 ready log 후 다시 실행한다. 반복 timeout이면
TX/RX 교차, GND, port와 pin을 점검한다. mismatch는 수신 문자열과 baud/8N1 설정을
점검한다. 이 시험은 test-only PING/PONG이며 실제 Parts Module protocol 검증이 아니다.

### 8A. Printer UART wiring

Printer와 Hub 전원을 끈 뒤 Printer UART를 다음과 같이 교차 연결한다.

| ESP32-S3 Hub | Printer UART side |
|---|---|
| GPIO7 TX (UART2) | RX |
| GPIO8 RX (UART2) | TX |
| GND | GND |

TX-TX 또는 RX-RX로 연결하지 않고 GND를 반드시 공통 연결한다. Printer connector의
pinout과 전기적 신호 레벨은 장비 자료와 실제 측정으로 확인한다. 확인되지 않은 전원
핀은 연결하지 않는다. UART0 console과 UART1 Parts Module 경로는 Printer에 사용하지
않는다.

## 9. Run `test all`

Hub CLI에서 실행한다.

```text
modulino> test all
```

`test all`은 boot 때 저장한 NVS/Wi-Fi/MQTT 결과, RPC subscribe 결과, G-code safety,
Printer mock 회귀 helper, Printer UART 초기화와 최근 transaction, Parts UART의 최근
결과를 출력한다. 실제 Printer 명령은 자동 실행하지 않는다.
Parts UART를 먼저 실행하지 않았다면 다음은 정상이다.

```text
[TEST] parts_module_uart handshake      SKIP - test not run
```

`test all` 자체는 Parts UART handshake를 새로 실행하지 않는다. 가장 최근
`test module uart` 결과만 표시한다.

Printer transaction을 아직 실행하지 않았으면 `printer_uart hardware`는
`SKIP - READY_FOR_HW_TEST`로 표시된다. `printer_uart init PASS`는 UART driver
초기화 성공과 `hardware not verified`를 표시할 뿐 Printer 통신 PASS가 아니다. init
자체가 FAIL이면 hardware 시험을 진행하지 않고 firmware/pin resource 설정을 먼저
수정한다.

`mqtt initial publish PASS - status,birth,modules,logs queued`는 네 publish 요청이
`esp_mqtt_client_enqueue()`를 통해 client queue에 등록됐다는 뜻이다. Broker 수신,
retain 적용 또는 subscriber 수신을 의미하지 않으며 wildcard subscriber에서 별도로
확인해야 한다.

## 10. Configure Wi-Fi and MQTT with Menuconfig

Windows PowerShell의 `ipconfig`로 ESP32-S3가 접속할 Wi-Fi/hotspot adapter의 IPv4를
확인하고 이를 `<BROKER_IP>`로 정한다.

```powershell
ipconfig
```

WSL의 Hub root에서 menuconfig를 실행한다.

```bash
cd "$REPO_ROOT"
idf.py menuconfig
```

`Modulino test firmware` menu에서 설정한다.

- Wi-Fi station SSID: 실제 test AP SSID
- Wi-Fi station password: 실제 test AP password
- Wi-Fi connection timeout: 기본 15000ms 또는 시험값
- Hub ID: 기본 `hub_test001` 또는 test ID
- MQTT broker URI: `mqtt://<BROKER_IP>:1883`
- MQTT connection timeout: 기본 10000ms
- MQTT keepalive: 기본 10초
- Printer UART baud rate: 실제 Printer와 동일한 값. 기본 115200이며 선택지는
  9600, 19200, 38400, 57600, 115200, 250000

SSID/password를 source나 문서에 기록하지 않는다. 설정 저장 후 Hub를 build한다.

```bash
idf.py build
```

Broker를 시작하기 전까지 Hub를 reset하지 않아도 된다. 다음 단계에서 broker와
subscriber를 준비한 후 Hub를 다시 flash/reset한다.

### 10A. CLI M105/M114/M115 actual Printer test

baud 설정을 저장하고 build/flash한 뒤 Hub monitor에서 실제 UART2 경로를 각각
실행한다.

```text
modulino> printer m105
modulino> printer m114
modulino> printer m115
```

정상 형식:

```text
[PRINTER UART] M105 PASS
<actual raw response ending in an ok line>
```

응답 값은 Printer firmware와 현재 상태에 따라 달라지므로 고정값과 비교하지 않는다.
M115는 여러 줄 전체와 terminal `ok` line이 출력되어야 한다. CR, LF, CRLF는 모두
line delimiter로 인식하고 빈 line은 무시한다. terminal 성공은 정확히 `ok`이거나
`ok` 다음 문자가 space/tab인 경우만 허용하며 `okay`, `okerror`는 성공이 아니다.

다음은 모두 FAIL이다.

- UART initialization, RX flush, write, TX completion wait 또는 read 오류
- 3000 ms 안에 유효한 `ok`/`Error:` terminal line이 없는 `printer_timeout`
- `Error:`로 시작하는 terminal line의 `printer_error`
- response buffer를 넘는 `response_overflow`

Printer Error와 overflow에서는 출력된 partial raw response도 증적으로 저장한다.
정상 `ok`를 실제로 관찰한 명령만 hardware PASS로 기록한다.

## 11. Configure Windows Mosquitto for Local Development

아래 설정은 격리된 local development network 전용이다. Internet-facing 또는
공용 network에서 `allow_anonymous true`를 사용하지 않는다.

PowerShell에서 ASCII config 파일을 만든다.

```powershell
$MosquittoDevDir = "$HOME\mosquitto-dev"
$MosquittoConfig = "$MosquittoDevDir\mosquitto-dev.conf"
New-Item -ItemType Directory -Force -Path $MosquittoDevDir | Out-Null

@'
listener 1883 0.0.0.0
allow_anonymous true
persistence false
'@ | Set-Content -Path $MosquittoConfig -Encoding ascii
```

이미 Mosquitto Windows service가 1883을 사용 중인지 확인한다.

```powershell
Get-NetTCPConnection -LocalPort 1883 -ErrorAction SilentlyContinue
```

중복 broker를 실행하지 않는다. foreground broker를 사용할 경우 Mosquitto 설치
경로를 실제 경로에 맞게 설정하고 실행한다.

```powershell
$MosquittoBin = "C:\Program Files\mosquitto"
& "$MosquittoBin\mosquitto.exe" -c $MosquittoConfig -v
```

Windows Firewall이 차단하면 신뢰할 수 있는 local test network와 TCP 1883에만
inbound rule을 허용하고 시험 후 제거한다. broker verbose log에서 ESP32 연결을
확인할 수 있어야 한다.

Broker가 준비된 후 WSL에서 설정된 Hub image를 flash하거나 Hub를 reset한다.

```bash
cd "$REPO_ROOT"
idf.py -p "$HUB_PORT" flash monitor
```

`test all`에서 다음을 확인한다.

```text
[TEST] wifi connect                     PASS - IP=<assigned IP>
[TEST] mqtt connect                     PASS - broker=<BROKER_IP>:1883
[TEST] mqtt initial publish             PASS - status,birth,modules,logs queued
[TEST] mqtt rpc subscribe               PASS - topic=modulino/local/v1/<HUB_ID>/rpc/request
```

여기서 `mqtt initial publish PASS`는 네 초기 publish 요청의 client enqueue 성공만
판정한다. Broker 또는 subscriber 수신 결과는 아니다.

## 12. Start a Wildcard MQTT Subscriber

Hub가 연결되기 전에 별도 Windows PowerShell에서 subscriber를 시작하면 non-retained
`logs` event까지 관찰할 수 있다.

```powershell
$MosquittoBin = "C:\Program Files\mosquitto"
$BrokerIp = "<BROKER_IP>"
& "$MosquittoBin\mosquitto_sub.exe" `
  -h $BrokerIp -p 1883 `
  -t "modulino/local/v1/#" -v
```

subscriber가 준비된 상태에서 Hub를 reset한다. topic과 payload가 한 줄씩 출력된다.
동일 Windows host에서 broker에 접속할 때 `localhost`를 쓸 수도 있지만, firmware의
broker URI에는 ESP32-S3가 접근할 수 있는 `<BROKER_IP>`를 사용해야 한다.

## 13. Verify Status, Birth, Discovery, Logs, Heartbeat, Printer Status

Hub 연결 직후 예상 초기 publish 순서:

1. `{hub_id}/status`
2. `{hub_id}/birth`
3. `{hub_id}/modules/discovery`
4. `{hub_id}/logs`

각 초기 topic은 wildcard subscriber 출력에서 실제 topic과 payload를 관찰하고 증적을
남긴 경우에만 Host 수신 PASS로 기록한다. Firmware의 `mqtt initial publish PASS`만
있거나 broker log/subscriber 출력이 없다면 status, birth, modules/discovery, logs의
runtime 상태는 PASS로 기록하지 않는다.

topic별 확인사항:

| Topic suffix | 확인사항 |
|---|---|
| `status` | `status=online`, `reason=connected` |
| `birth` | fw `0.1.0`, proto `1.0`, reset_reason |
| `modules/discovery` | `source=mock`, `modules=[]`; 실제 module 발견으로 기록하지 않음 |
| `logs` | `level=info`, `event=mqtt_connected`, 연결 직후 1회 |
| `heartbeat` | 약 5초 주기, uptime/free heap/RSSI |
| `printer/status` | 약 3초 주기, `printer_id=prt_test001`, `source=uart`; 최근 transaction 반영 |

정상 Hub-originated payload에서 다음을 함께 확인한다.

- 같은 boot 동안 모든 topic의 `boot_id`가 동일
- `seq`가 topic 종류와 관계없이 전역으로 단조 증가
- `device_ts`가 `null`
- `ts_quality`가 `unsynced`
- password, SSID 등 credential이 payload/log에 없음

UART initialization 성공 후 Printer transaction 전에는 `connection=unknown`,
`reason=not_tested`다. initialization 실패 시에는 `connection=disconnected`,
`reason=uart_initialization_error`다. 최근 유효한 `ok`는
`connected/last_transaction_ok`, timeout 또는 UART runtime 오류는 `disconnected`와
원인별 reason으로 바뀐다. Printer `Error:`와 overflow는 통신 응답이 있었으므로
connection은 connected지만 transaction 판정은 FAIL이다. 자율 polling은 수행하지 않는다.

Retained topic만 확인하려면 subscriber를 늦게 시작해도 status, birth,
modules/discovery를 받을 수 있다. `logs`, heartbeat, printer/status는 retain false다.

## 14. Verify LWT by Blocking the Hotspot Path

Wildcard subscriber와 broker는 계속 실행한 상태로 둔다. 먼저 retained online
status를 확인한다. 그 다음 Hub 전원을 정상 종료하거나 MQTT disconnect API를
호출하지 말고, ESP32-S3와 Windows broker 사이의 hotspot/network path를 갑자기
차단한다.

keepalive 기본값은 10초다. broker가 비정상 종료를 감지할 때까지 일반적으로
15~30초 정도 기다리고 subscriber에서 다음을 확인한다.

```text
modulino/local/v1/<HUB_ID>/status
```

payload 필수 값:

```json
{
  "schema": "modulino.hub_status.v1",
  "hub_id": "<HUB_ID>",
  "status": "offline",
  "reason": "mqtt_lwt"
}
```

LWT에는 `boot_id`와 `seq`가 없어야 한다. 이는 firmware가 직접 publish한 PASS가
아니라 broker가 비정상 연결 종료를 감지해 대신 발행한 결과다.

네트워크를 복구한 뒤 Hub를 reset한다. 제품 수준 자동 reconnect가 구현되지
않았으므로 네트워크 복구만으로 재연결되지 않을 수 있다. 새 online status가 retained
offline LWT를 대체하는지 확인한다.

## 15. Create ASCII RPC Files Instead of PowerShell Inline JSON

PowerShell inline JSON은 quote/escape 처리로 payload가 변형될 수 있다. 모든 RPC
시험은 ASCII 파일을 만든 뒤 `mosquitto_pub -f`로 전송한다.

PowerShell에서 공통 변수를 설정한다.

```powershell
$MosquittoBin = "C:\Program Files\mosquitto"
$BrokerIp = "<BROKER_IP>"
$HubId = "<HUB_ID>"
$RpcTopic = "modulino/local/v1/$HubId/rpc/request"
```

파일은 `Set-Content -Encoding ascii`로 만든다. publish 시 QoS 1을 지정하고 retained
option `-r`은 절대로 사용하지 않는다. firmware는 retained RPC request를 실행하지
않는다.

## 16. Actual Printer UART RPC

ASCII request 파일을 만든다.

```powershell
@'
{
  "jsonrpc": "2.0",
  "method": "printer.gcode.run",
  "params": {
    "printer_id": "prt_test001",
    "script": "M105"
  },
  "id": "cmd_m105_001"
}
'@ | Set-Content -Path .\rpc-m105.json -Encoding ascii

& "$MosquittoBin\mosquitto_pub.exe" `
  -h $BrokerIp -p 1883 -q 1 `
  -t $RpcTopic -f .\rpc-m105.json
```

Wildcard subscriber에서 같은 request id로 다음 순서를 확인한다.

1. `rpc/progress`: `progress.status=accepted`, `elapsed_ms=0`
2. UART2에서 실제 M105 transaction 실행
3. 성공 시 `rpc/response`: `result.status=completed`, `source=uart`, 실제
   `raw_response`

두 MQTT payload는 Hub의 기존 `boot_id`와 전역 증가 `seq`를 사용한다. 실제 terminal
`ok` response를 받은 경우에만 M105 hardware PASS다.

세 명령의 순차 실행을 확인하려면 별도 request id로 다음 payload도 전송한다.

```json
{
  "jsonrpc": "2.0",
  "method": "printer.gcode.run",
  "params": {
    "printer_id": "prt_test001",
    "script": "M105\nM114\nM115"
  },
  "id": "cmd_printer_reads_001"
}
```

firmware는 validation을 모두 끝낸 뒤 accepted를 발행하고 각 line을 같은 UART mutex로
순차 실행한다. 모두 성공해야 completed가 발행된다. 어느 한 transaction에서 runtime
오류가 나면 rejected가 아니라 accepted 이후 다음 failed response가 와야 한다.

| 원인 | `error.code` | 추가 확인 |
|---|---|---|
| 3000 ms terminal timeout | `printer_timeout` | `error.data.status=failed` |
| Printer `Error:` | `printer_error` | 가능한 raw_response 보존 |
| init/flush/write/TX wait/read 오류 | `uart_error` | message로 stage 구분 |
| response buffer 초과 | `response_overflow` | partial raw_response 확인 |

실제 Printer 응답값을 다른 장비의 예시값과 비교하지 않는다. M105/M114/M115는 각각
증적을 확보해 개별 판정한다.

## 17. G1 X10 Unsafe G-code Rejection

```powershell
@'
{
  "jsonrpc": "2.0",
  "method": "printer.gcode.run",
  "params": {
    "printer_id": "prt_test001",
    "script": "G1 X10"
  },
  "id": "cmd_g1_001"
}
'@ | Set-Content -Path .\rpc-g1.json -Encoding ascii

& "$MosquittoBin\mosquitto_pub.exe" `
  -h $BrokerIp -p 1883 -q 1 `
  -t $RpcTopic -f .\rpc-g1.json
```

기대 결과:

- accepted progress 없음
- `rpc/response`의 id는 `cmd_g1_001`
- `error.code=unclassified_gcode`
- `error.data.status=rejected`
- 실제 Printer UART 송신 없음

## 18. Invalid JSON Rejection

의도적으로 끝나지 않은 ASCII JSON 파일을 만든다.

```powershell
@'
{"jsonrpc":"2.0","method":"printer.gcode.run","params":
'@ | Set-Content -Path .\rpc-invalid.json -Encoding ascii

& "$MosquittoBin\mosquitto_pub.exe" `
  -h $BrokerIp -p 1883 -q 1 `
  -t $RpcTopic -f .\rpc-invalid.json
```

기대 결과:

- accepted progress 없음
- `error.code=invalid_json`
- `error.data.status=rejected`
- request id를 파싱할 수 없으므로 response `id=null`

## 19. Unsupported Method Rejection

`params={}`인 유효한 미지원 method를 전송해 validation 순서를 확인한다.

```powershell
@'
{
  "jsonrpc": "2.0",
  "method": "hub.reboot",
  "params": {},
  "id": "cmd_unsupported_001"
}
'@ | Set-Content -Path .\rpc-unsupported.json -Encoding ascii

& "$MosquittoBin\mosquitto_pub.exe" `
  -h $BrokerIp -p 1883 -q 1 `
  -t $RpcTopic -f .\rpc-unsupported.json
```

기대 결과:

- accepted progress 없음
- response id `cmd_unsupported_001`
- `error.code=unsupported_method`
- `invalid_params`가 반환되면 FAIL
- Hub reboot가 실제로 실행되지 않음

## 20. Shutdown and Cleanup

1. ESP-IDF monitor는 각 terminal에서 `Ctrl+]`로 종료한다.
2. wildcard subscriber와 foreground broker는 PowerShell에서 `Ctrl+C`로 종료한다.
3. 시험용 RPC 파일을 삭제한다.
4. 임시 Windows Firewall rule을 만들었다면 제거한다.
5. USB device를 WSL에서 detach한다.

```powershell
usbipd detach --busid <HUB_BUSID>
usbipd detach --busid <C3_BUSID>
usbipd list
```

더 이상 공유하지 않을 장치는 관리자 PowerShell에서 선택적으로 unbind한다.

```powershell
usbipd unbind --busid <HUB_BUSID>
usbipd unbind --busid <C3_BUSID>
```

보드 간 신호선을 변경하거나 제거할 때는 두 보드 USB 전원을 먼저 분리한다. 시험
기록에는 `BUILD_VERIFIED`, `READY_FOR_HW_TEST`, `HW_VERIFIED`를 분리한다. 현재
Printer 상태는 `READY_FOR_HW_TEST`이며 담당자가 실제 Ender-3 V3 SE에서 위 절차를
수행하기 전에는 `HW_VERIFIED`로 기록하지 않는다. mock discovery와 Parts UART
handshake도 actual Parts Module 검증으로 기록하지 않는다.
