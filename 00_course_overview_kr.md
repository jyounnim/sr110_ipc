# Astra SR110 M4↔M55 IPC 실습 커리큘럼

Synaptics Astra SR110(board: `sr100_rdk`) — Cortex-M55와 Cortex-M4가 함께 들어있는 비대칭 듀얼코어(AMP) SoC — 에서, Zephyr RTOS의 raw `mbox` 드라이버 API를 이용해 두 코어 사이의 IPC(Inter-Processor Communication)를 처음부터 단계적으로 익히는 실습 커리큘럼입니다.

이 문서는 커리큘럼 전체의 지도 역할을 합니다. 각 랩의 상세한 내용은 랩별 폴더 안의 강의 문서를 참고하세요.

## 이 커리큘럼의 성격

- **raw `mbox` API를 직접 다룹니다.** Zephyr가 제공하는 `ipc_service`/OpenAMP 같은 상위 레벨 IPC 프레임워크는 이 커리큘럼의 범위가 아닙니다. `mbox_send_dt()`, `mbox_register_callback_dt()`, `mbox_set_enabled_dt()`라는 저수준 API를 직접 사용하면서, IPC가 하드웨어 레벨에서 어떻게 동작하는지부터 이해하는 것을 목표로 합니다.
- **모든 랩은 M4(remote)를 먼저 빌드하고, 그 결과물을 M55(host) 빌드에 포함시키는 동일한 구조**를 따릅니다. 랩이 진행될수록 메시지 프로토콜과 스레드 설계는 점점 복잡해지지만, 이 빌드 구조와 기본 프로젝트 레이아웃(`lab/`은 M55, `lab/remote/`는 M4)은 끝까지 동일하게 유지됩니다.
- **각 랩 폴더는 두 개의 문서로 구성됩니다.**
  - `NN_주제_kr.md`: 지금 완성된 코드를 설명하는 **강의 문서**입니다. 학습 목표, IPC 개념 설명, 코드 해설, 빌드/실행 방법을 다룹니다. 디버깅 히스토리는 없고, 이미 최종 확정된 정답 코드를 처음부터 이렇게 설계된 것처럼 설명합니다.
  - `NN_주제_troubleshooting_kr.md`: 실제 하드웨어 검증 과정에서 발견되고 해결된 문제들을 기록한 **트러블슈팅 노트**입니다. 강의 문서와 분리해 둔 이유는, 학습자가 처음 이 랩을 볼 때는 "정답이 무엇인가"에 집중하고, 막상 자신의 환경에서 문제가 생겼을 때만 "실제로 어떤 함정이 있었는가"를 찾아보도록 하기 위함입니다.

## 하드웨어 전제

- Board: `sr100_rdk` (Synaptics Astra SR110)
- M55: `sr100_rdk/sr100/m55` — 호스트(HOST) 코어. 대부분의 랩에서 시나리오를 시작시키는 쪽입니다.
- M4: `sr100_rdk/sr100/m4` — 클라이언트/리모트(CLIENT/REMOTE) 코어. 대부분의 랩에서 실제 주변장치(GPIO, I2C 센서, SPI 등)를 물리적으로 구동하는 쪽입니다.
- M4/M55는 보드 위에서 **I2C1 버스를 물리적으로 공유**합니다. 그래서 LED/버튼/가속도계처럼 I2C1에 물린 장치를 다루는 모든 랩의 M55 오버레이에는 `&i2c1`, `&gpio_exp0`(및 관련 자식 노드)를 `status = "disabled"`로 꺼서, 실제로 그 버스를 쓰는 M4에게만 소유권을 주는 설정이 공통으로 들어갑니다. 이 설정의 배경은 [Lab 01 트러블슈팅 문서](01_hello_ipc/doc/01_hello_ipc_troubleshooting_kr.md)에서 자세히 다룹니다.
- 두 코어는 각각 독립된 UART로 시리얼 콘솔을 출력합니다(230400bps, 8N1). 실습 중에는 M55/M4 콘솔을 동시에 열어두고 양쪽 로그를 함께 확인하는 것을 권장합니다.

## 빌드 방법 (모든 랩 공통)

이 커리큘럼의 모든 랩은 west 워크스페이스 루트(`zephyr/`가 보이는 디렉토리)에서 다음 순서로 빌드합니다. `<N>_<lab_name>`은 랩 폴더 이름으로 바꿔주세요.

```bash
# 1) M4 (remote) 이미지를 먼저 빌드
west build -p always -b sr100_rdk/sr100/m4 ./sr110_ipc/<N>_<lab_name>/lab/remote -d m4

# 2) M55 (host) 이미지를 빌드 — 방금 만든 M4 바이너리를 포함
west build -p always -b sr100_rdk/sr100/m55 ./sr110_ipc/<N>_<lab_name>/lab -d m55 \
    -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"
```

`M4_BUILD`는 CWD가 아니라 **M55 빌드 디렉토리(`-d m55`) 기준 상대경로**로 해석됩니다. `m4/`와 `m55/`가 워크스페이스 루트 아래 형제 디렉토리인 구조에서는 `../m4`가 정확한 값입니다 — `./m4`로 잘못 지정하면 CMake가 에러 없이 조용히 M4 이미지를 찾지 못해, 최종 이미지에 M4 펌웨어가 빠진 채로(M55만 정상 동작하고 M4는 부팅조차 안 되는 것처럼 보이는) 조용히 실패합니다. 각 랩 문서에도 이 명령이 랩별 경로로 다시 제시되어 있으니, 그대로 복사해 사용해도 됩니다. 보드 플래시 절차는 사용 중인 툴체인/보드의 표준 절차를 따르세요(이 문서의 범위 밖입니다).

## IPC 핵심 개념 총정리

각 랩의 강의 문서에서 개념이 처음 등장할 때마다 자세히 설명하지만, 커리큘럼 전체를 관통하는 핵심 개념만 여기 모아 요약해 둡니다. 처음 시작하기 전에 훑어보고, 랩을 진행하며 다시 돌아와 참고하세요.

**mbox (mailbox)** — SoC에 내장된 프로세서 간 통신용 하드웨어 블록입니다. 한쪽 코어가 레지스터에 값을 쓰면 상대 코어에 하드웨어 인터럽트(doorbell)가 발생합니다. mbox 자체는 "데이터가 도착했다"는 신호만 전달하고, 실제 데이터(payload)는 devicetree로 정의된 공유 메모리 영역에 놓입니다.

**AMP (Asymmetric Multi-Processing)** — M4와 M55가 하나의 OS 인스턴스를 공유하는 것이 아니라, 각자 독립된 Zephyr 이미지를 부팅해 실행하는 구조입니다. 두 코어는 사실상 한 칩 안의 별개의 컴퓨터이며, 그래서 함수 호출이나 전역 변수 공유가 아니라 IPC가 필요합니다.

**ISR 컨텍스트와 블로킹 금지 — 이 커리큘럼에서 가장 중요한 규칙** — `mbox_register_callback_dt()`로 등록한 콜백은 메시지가 도착하면 **인터럽트 서비스 루틴(ISR) 컨텍스트**에서 실행됩니다. ISR 안에서는 스케줄러가 개입하는 블로킹 호출(`k_msleep()` 등)을 쓸 수 없고, Lab 07에서 실기로 확인되었듯 `mbox_send_dt()` 자체도 이 보드의 mbox 백엔드에서는 블로킹될 수 있어 **예외 없이** 금지됩니다. 그래서 이 커리큘럼의 모든 랩은 다음 패턴을 따릅니다.

1. mbox 콜백(ISR)은 도착한 메시지를 `k_msgq_put(..., K_NO_WAIT)`로 큐에 넣기만 하고 즉시 반환합니다.
2. 별도의 워커 스레드가 `k_msgq_get()`으로 큐를 기다리다가, 메시지가 들어오면 실제 처리(하드웨어 제어, 응답 전송 등 시간이 걸릴 수 있는 모든 작업)를 스레드 컨텍스트에서 수행합니다.

이 "ISR → `k_msgq` → 워커 스레드" 패턴은 Lab 01에서 처음 등장한 뒤, 이후 모든 랩에서 형태만 바뀌어 반복됩니다. 이 원칙이 왜 절대적인지, 실제로 지켜지지 않았을 때 어떤 일이 벌어지는지는 [Lab 03 트러블슈팅](03_full_duplex_ping_pong/doc/03_full_duplex_ping_pong_troubleshooting_kr.md)과, 특히 [Lab 07 트러블슈팅](07_echo_service/doc/07_echo_service_troubleshooting_kr.md)(`mbox_send_dt()`가 ISR에서 시스템 전체를 멈춰버린 실제 사례)에 상세히 기록되어 있습니다.

**메시지 프로토콜 설계** — 초반 랩(01~04)은 메시지 하나에 필드 하나뿐인 단순한 구조지만, Lab 06부터는 커맨드 타입 필드로 여러 종류의 요청/응답을 하나의 채널에 다중화하는 구조화된 프로토콜을 다룹니다. 뒤로 갈수록(특히 Lab 18) 여러 메시지 종류를 하나의 태그된 유니온(envelope)으로 통합하는 패턴까지 발전합니다.

**주기적 전송 vs 이벤트 기반 전송** — Lab 09는 센서 값을 일정 주기마다 무조건 전송하는 "텔레메트리 푸시" 패턴을, Lab 10은 조건(임계값 초과)을 만족할 때만 전송하는 "이벤트 기반" 패턴을 다룹니다. 두 패턴의 트레이드오프(대역폭·지연시간·놓친 이벤트 처리)를 대비해서 이해하면 이후 랩(15 텔레메트리 허브, 16 하트비트)의 설계 의도가 더 명확해집니다.

## 랩 목록 (Lab 01~10 — 실기 검증 완료)

| # | 제목 | 방향 | 이 랩에서 새로 등장하는 핵심 개념 |
|---|------|------|-----------------------------------|
| 01 | [Hello IPC](01_hello_ipc/doc/01_hello_ipc_kr.md) | M55 → M4 | mbox 기초, devicetree `mbox-consumer`, ISR→msgq→워커스레드 패턴 |
| 02 | [Button Pong](02_button_pong/doc/02_button_pong_kr.md) | M4 → M55 | 디바이스 이벤트를 호스트로 전달, `gpio-keys` polling-mode |
| 03 | [Full-Duplex Ping-Pong](03_full_duplex_ping_pong/doc/03_full_duplex_ping_pong_kr.md) | M55 ↔ M4 | 양방향 동시 통신, 콜백 블로킹 금지 원칙의 확립 |
| 04 | [Shared Counter](04_shared_counter/doc/04_shared_counter_kr.md) | M55 → M4 | 메시지 패싱으로 상태를 동기화하는 것과 진짜 공유메모리의 차이 |
| 05 | [Button Press Counter](05_button_press_counter/doc/05_button_press_counter_kr.md) | M4 → M55 | 반복 이벤트를 누적 카운트로 상태화 |
| 06 | [Structured Command](06_structured_command/doc/06_structured_command_kr.md) | M55 ↔ M4 | 커맨드 타입 필드로 채널을 다중화하는 구조화된 프로토콜 |
| 07 | [Echo Service](07_echo_service/doc/07_echo_service_kr.md) | M55 ↔ M4 | **`mbox_send_dt()`도 ISR에서 호출 금지** — 커리큘럼 최종 규칙 확정 |
| 08 | [Message Queue](08_message_queue/doc/08_message_queue_kr.md) | M4 → M55 | 연속 메시지 스트림 큐잉, 큐 depth 설계 |
| 09 | [Accel Telemetry](09_accel_telemetry/doc/09_accel_telemetry_kr.md) | M4 → M55 | 주기적 센서 텔레메트리, Zephyr 센서 서브시스템 사용법 |
| 10 | [Threshold Event](10_threshold_event/doc/10_threshold_event_kr.md) | M4 → M55 | 이벤트 기반 전송, baseline 캘리브레이션, 커맨드+주기작업 단일 스레드 처리 |

## 앞으로의 랩 (Lab 11~18 — 준비 중)

아래 랩들은 아직 이번 문서 정비 작업(및 실기 검증)의 대상이 아니며, 현재 초기 설계 노트 상태입니다. 앞으로 Lab 01~10과 동일한 절차(실기 검증 → 강의 문서/트러블슈팅 문서 분리 작성)를 거쳐 정식으로 편입될 예정입니다.

| # | 제목 | 개요 |
|---|------|------|
| 11 | OLED Display Control | M55 → M4 → SSD1306 디스플레이 제어 |
| 12 | AHT20 Temp/Humidity Logging | M4 → M55, 온습도 센서 로깅 |
| 13 | SPI ADC Control Loop | M55(센서) ↔ M4(액추에이터) — 이전 랩들과 반대의 하드웨어 소유권 구조 |
| 14 | UART Bridge | 외부 UART → M4 → M55 브리지 |
| 15 | Telemetry Hub | Lab 09 + Lab 12 통합, Lab 10의 단일 스레드 패턴 재사용 |
| 16 | Heartbeat Watchdog | M4 생존 감시 |
| 17 | Low Power Sync (실험적) | M55 Sleep / M4 Wake Trigger — 미검증 |
| 18 | Capstone Gateway | 지금까지의 모든 패턴을 하나의 태그된 유니온(envelope) 프로토콜로 통합하는 최종 종합 실습 |

## 학습 순서 권장

번호 순서대로 진행하는 것을 권장합니다. 각 랩은 이전 랩에서 확립된 패턴 위에 새 개념 하나씩을 더하는 방식으로 설계되어 있고, 특히 아래 세 랩은 커리큘럼 전체의 설계 원칙이 확정되는 분기점이므로 건너뛰지 말고 순서대로 짚고 넘어가는 것을 권장합니다.

- **Lab 01**: mbox·devicetree·ISR/워커스레드 패턴의 기초가 여기서 전부 확립됩니다.
- **Lab 03**: 양방향 통신에서 콜백 블로킹 금지 원칙이 왜 필요한지 실기 사례로 체감합니다.
- **Lab 07**: `mbox_send_dt()`조차 ISR에서 호출하면 안 된다는, 이 프로젝트 특유의 가장 중요한 교훈이 확정됩니다.

각 랩을 마칠 때마다 실기에서 막히는 부분이 있다면, 먼저 해당 랩의 트러블슈팅 문서를 확인하세요. 같은 보드/사이트를 공유하는 랩들 사이에는 이미 알려진 이슈(I2C1 버스 공유, `M4_BUILD` 상대경로, GPIO expander의 polling-mode 등)가 반복해서 등장하며, 대부분 처음 발견된 랩의 트러블슈팅 문서에 근본 원인까지 정리되어 있습니다.
