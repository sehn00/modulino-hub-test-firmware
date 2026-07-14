# Modulino Hub Test Results

이 문서는 실제로 실행한 시험 결과와 증적만 기록한다. 시험 항목과 판정 기준은
[Test Plan](modulino-dev-test-plan.md), 실행 순서는 [Test Procedure](modulino-dev-test-procedure.md)를 따른다.

## 1. Record Rules

실행 결과 표는 다음 schema를 사용한다.

| Date | Commit | Test | Result | Evidence / Notes | Tester |
|---|---|---|---|---|---|

- Result: `PASS`=기대 결과 확인, `FAIL`=실행 오류/불일치, `SKIP`=미실행/증적 없음, `NOT_SUPPORTED`=의도적 미지원
- Stage: `BUILD_VERIFIED`=build 확인, `READY_FOR_HW_TEST`=실기기 시험 준비, `HW_VERIFIED`=실기기 증적 확인

Result는 실행 판정이고 Stage는 구현·검증 진행 단계이므로 같은 열에 섞지 않는다.
기존 기록 중 날짜, tester 또는 raw artifact가 보존되지 않은 값은 표에서 `-`로 표시한다.
SSID, password, private IP, MAC address, credential 등 민감정보는 기록하지 않는다.

## 2. Current Status


| Scope | Commit | Stage | Notes |
|---|---|---|---|
| Printer UART 포함 firmware build | `28379d2a542f84d6c8e54602a1368300a57e6fef` | BUILD_VERIFIED | ESP-IDF 5.5.2 build 성공, binary 931552 bytes |
| Actual Printer UART | `28379d2a542f84d6c8e54602a1368300a57e6fef` | READY_FOR_HW_TEST | 실제 Ender-3 V3 SE의 M105/M114/M115 미실행; HW_VERIFIED 아님 |

## 3. Verified Results

| Date | Commit | Test | Result | Evidence / Notes | Tester |
|---|---|---|---|---|---|
| - | `2d1aab9` | Boot / Serial log / CLI / NVS init | PASS | Actual ESP32-S3 observation recorded in the versioned Test Plan; raw capture not preserved | - |
| - | `2d1aab9` | Wi-Fi IPv4 / MQTT connect / LWT / heartbeat | PASS | Actual board, broker and Host observations recorded in the versioned Test Plan; raw capture not preserved | - |
| - | `2d1aab9` | RPC subscribe/request / invalid JSON / unsafe G-code / unsupported method | PASS | Actual RPC and rejection paths recorded in the versioned Test Plan; raw payload not preserved | - |
| - | `2d1aab9` | ESP32-S3 ↔ ESP32-C3 Parts UART PING/PONG | PASS | Test-only mock handshake 5회 기록; actual Parts Module protocol 검증 아님 | - |
| - | `2d1aab9` | status / birth / logs / modules/discovery Host reception | SKIP | Firmware enqueue만 확인, subscriber 수신 증적 없음 | - |
| - | `2d1aab9` | Actual Parts Module production protocol | NOT_SUPPORTED | Discovery/version/production protocol 미구현 | - |

## 4. Printer UART Verification

세부 시험 항목은 Test Plan에 정의되어 있다. 현재 Stage는 `READY_FOR_HW_TEST`이며,
실제 장비 결과가 없으므로 `PASS` 또는 `HW_VERIFIED`로 기록하지 않는다.

| Date | Commit | Test | Result | Evidence / Notes | Tester |
|---|---|---|---|---|---|
| 2026-07-14 | `28379d2` | Actual Printer UART M105/M114/M115 | SKIP | Implementation and procedure ready; actual Ender-3 V3 SE test not executed | - |
| YYYY-MM-DD | `<commit>` | M105 / M114 / M115 | PASS/FAIL | Raw CLI response or MQTT payload reference | `<tester>` |

## 5. Historical Mock Results

아래 결과는 UART2 actual Printer 구현 전 mock 이력이며 현재 actual UART의 PASS 증적으로
사용할 수 없다.

| Date | Commit | Test | Result | Evidence / Notes | Tester |
|---|---|---|---|---|---|
| - | `2d1aab9` | Mock printer/status Host reception / mock M105 RPC accepted → completed | PASS | `prt_mock001`, `source=mock`; versioned Test Plan record, raw payload not preserved | - |
| - | `2d1aab9` | Actual Printer path before UART implementation | NOT_SUPPORTED | 당시 `ESP_ERR_NOT_SUPPORTED`; current UART implementation 결과와 무관 | - |

## 6. Evidence Guidelines

새 실행 기록에는 다음 증적을 가능한 한 함께 남긴다.

- 실행 날짜, commit SHA와 tester
- raw Serial CLI output 또는 ESP-IDF monitor log
- MQTT request/progress/response payload
- Printer baud 설정
- 필요한 경우 배선 사진 또는 확인된 pin reference

증적이 없으면 결과를 추측해 `PASS`로 기록하지 않는다.
