# Lab 09: 가속도계 텔레메트리 (M4 → M55)

온보드 MC3419 가속도계를 M4가 주기적으로 샘플링해 M55로 스트리밍하는, 이 커리큘럼 최초의 "센서 텔레메트리" 랩입니다.

## 학습 목표

- Zephyr **센서 서브시스템 표준 API**(`sensor_sample_fetch()` / `sensor_channel_get()` / `sensor_attr_set()`)로 실제 하드웨어 센서를 다루는 방법을 익힌다.
- devicetree 노드가 기본적으로 `disabled` 상태로 꺼져 있는 경우를 오버레이로 재활성화하는 관례를 이해한다.
- 이벤트 기반(버튼 인터럽트 등) IPC와 구분되는 **주기적 텔레메트리 푸시(periodic telemetry push)** 패턴을 M4→M55 방향으로 구현한다.

## 이전 랩과의 연결

Lab 01~08까지는 버튼 입력, 카운터, 커맨드 응답처럼 **디지털·이산적인 이벤트**를 주고받는 IPC였습니다. 무언가 "일어났을 때"(버튼이 눌렸다, 커맨드가 도착했다) 한 번 메시지를 보내는 구조였죠.

Lab 09부터는 성격이 다릅니다. 가속도계는 항상 값을 갖고 있는 **아날로그(연속) 센서**이고, 이 랩에서는 특정 이벤트를 기다리는 대신 **일정한 주기(500ms)마다 무조건 값을 읽어 보내는** 방식을 씁니다. 이를 "텔레메트리 푸시(telemetry push)" 패턴이라고 부릅니다 — 수신 측(M55)이 요청하지 않아도 송신 측(M4)이 스스로 정한 주기로 데이터를 밀어 넣는 구조입니다.

> 이 패턴은 바로 다음 랩인 Lab 10에서 "이벤트 기반"으로 대비됩니다. Lab 10은 같은 센서를 쓰면서도 값이 임계값을 넘을 때만 알림을 보내는 구조로 바뀌는데, 두 패턴의 트레이드오프(단순함 vs 대역폭/전력 효율)를 비교하며 이해하면 좋습니다.

## 핵심 IPC 개념

### 1. 주기적 텔레메트리 푸시 vs 이벤트 기반

| | 이 랩 (Lab 09) | Lab 10 (예고) |
|---|---|---|
| 전송 시점 | 고정 주기(500ms)마다 항상 | 값이 임계값을 넘을 때만 |
| 장점 | 구현이 단순, 수신 측이 최신 상태를 놓칠 일이 없음 | IPC 트래픽/전력 소모 최소화 |
| 단점 | 값이 안 바뀌어도 계속 전송(대역폭 낭비) | "값이 바뀌었는지" 판단 로직이 추가로 필요 |

두 패턴 모두 실무에서 흔히 쓰이며, 어떤 걸 고를지는 "수신 측이 최신값을 항상 알아야 하는가" 대 "변화가 있을 때만 알면 되는가"에 달려 있습니다.

### 2. Zephyr 센서 서브시스템 사용법

Zephyr의 표준 센서 API는 크게 세 함수로 구성됩니다.

- `sensor_sample_fetch(dev)` — 센서에서 최신 원시 측정값을 드라이버 내부 버퍼로 읽어옵니다.
- `sensor_channel_get(dev, chan, &val)` — 방금 fetch한 값에서 특정 채널(X축, Y축 …)의 값을 꺼냅니다.
- `sensor_attr_set(dev, chan, attr, &val)` — 센서의 동작 속성(샘플링 주파수, 측정 범위 등)을 설정합니다.

여기까지는 잘 알려진 사용법이지만, 실제로 센서를 "제대로" 동작시키려면 초기화 시점에 다음 세 가지가 갖춰져야 합니다. 이 세 가지는 MC3419 하나에 국한된 이야기가 아니라, **Zephyr 센서 드라이버를 다룰 때 일반적으로 확인해야 할 체크리스트**로 기억해 두면 이후 랩(10, 12, 13, 15, 18)에서도 그대로 적용됩니다.

**① devicetree 노드가 활성화(`status = "okay"`)되어 있는지 확인한다.**
많은 보드 base devicetree는 온보드 주변장치 노드를 기본값 `status = "disabled"`로 정의해 둡니다. 그 주변장치를 쓰지 않는 애플리케이션에서 불필요하게 드라이버가 컴파일/초기화되거나, 다른 주변장치와 핀/버스를 두고 충돌하는 일을 막기 위한 흔한 관례입니다. 실제로 이 센서를 쓰려면 애플리케이션(오버레이) 쪽에서 명시적으로 다시 켜줘야 합니다.

```dts
&mc3479 {
	status = "okay";
};
```

노드를 켜지 않으면 `DEVICE_DT_GET_OR_NULL()`이 `NULL`을 반환하고, `device_is_ready()` 검사에서 "device not ready"로 실패합니다.

**② `SENSOR_ATTR_SAMPLING_FREQUENCY`로 ODR(출력 데이터 속도)을 설정해 센서를 깨운다.**
많은 저전력 센서는 전원이 들어와도 곧바로 측정을 시작하지 않고 대기(standby) 상태를 유지하도록 설계되어 있습니다. `sensor_attr_set()`으로 ODR(Output Data Rate)을 지정하는 호출이 바로 이 센서를 활성 측정 모드로 전환하는 트리거 역할을 합니다.

```c
struct sensor_value odr = {.val1 = 50, .val2 = 0}; /* 50 Hz */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
		 SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
```

**③ `SENSOR_ATTR_FULL_SCALE`로 측정 범위를 설정해 스케일(sensitivity)을 활성화한다.**
센서 드라이버는 보통 레지스터에서 읽은 원시 정수값을 실제 물리 단위(예: mg 단위 가속도)로 변환하기 위한 배율(스케일 계수)을 내부에 갖고 있습니다. 이 배율은 초기화 시 자동으로 채워지는 것이 아니라, 애플리케이션이 `SENSOR_ATTR_FULL_SCALE`로 원하는 측정 범위를 명시적으로 설정할 때 계산되어 채워지는 경우가 많습니다.

```c
struct sensor_value range = {.val1 = 0, .val2 = 0}; /* 가장 민감한(최소) 범위 */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
		 SENSOR_ATTR_FULL_SCALE, &range);
```

②와 ③ 둘 다 빠뜨리면 "센서가 잠들어 있다"는 것과 "원시값에 곱해지는 배율이 0이다"는 것이 겉보기 증상(항상 0)이 똑같기 때문에, 어느 쪽이 원인인지 구분하려면 실제 드라이버 소스를 확인하는 것이 가장 확실합니다.

## 아키텍처

```
M4 (CLIENT)                         M55 (HOST)
─────────────                       ─────────────
mc3419 노드 활성화(overlay)
device_is_ready()
sensor_attr_set(ODR)      ─┐
sensor_attr_set(FULL_SCALE)│  초기화 (1회)
                            ┘
루프(500ms마다):
  sensor_sample_fetch()
  sensor_channel_get() x3 (X/Y/Z)
  msg = {x, y, z, seq++}
  mbox_send_dt(&tx_channel, &msg) ──mbox──▶ rx_cb(ISR)
                                             LOG_INF(x, y, z, seq)
```

- **M4 (`lab/remote/src/main.c`, CLIENT)**: `mc3479` 노드를 열고, 부팅 시 ODR과 측정 범위를 한 번 설정한 뒤, 500ms 주기로 X/Y/Z를 읽어 `struct ipc_accel_msg`에 담아 `mbox_send_dt()`로 전송합니다.
- **M55 (`lab/src/main.c`, HOST)**: `rx_cb()`가 메시지를 수신하는 즉시 `LOG_INF()`로 로그만 남기고 반환합니다. 이 랩은 M4→M55 단방향이라 M55가 M4로 응답을 보낼 필요가 없고, 로그 출력 자체가 블로킹 호출이 아니므로 별도의 메시지 큐/워커 스레드 없이 ISR 콜백 안에서 바로 처리해도 안전합니다.

메시지 구조체(`lab/include/ipc_common.h`, `lab/remote/include/ipc_common.h`)는 다음과 같이 X/Y/Z와 시퀀스 번호를 담습니다.

```c
struct ipc_accel_msg {
	int32_t x;
	int32_t y;
	int32_t z;
	uint32_t seq;
};
```

> **설계 노트 — mbox 콜백은 절대 블로킹 금지**: M55의 `rx_cb()`는 로그만 남기고 반환하며, `mbox_send_dt()`도 이 콜백 안에서 호출하지 않습니다(단방향 랩이라 M55가 M4로 되돌려 보낼 데이터가 없습니다). 이 원칙은 Lab 03/07에서 확립된, 이 커리큘럼 전체에 적용되는 규칙입니다.

## devicetree 설정

M4/M55는 물리적으로 같은 I2C1 버스를 공유합니다. 이 커리큘럼 전체에서 M4만 이 버스를 소유하도록 M55 오버레이에서 `&i2c1`을 비활성화해 두었습니다(자세한 배경은 Lab 01 문서 참고). 가속도계 관련 설정은 M4 오버레이(`lab/remote/boards/sr100_rdk_sr100_m4.overlay`)에만 필요합니다.

```dts
/* ipc0 shared-memory-size는 M55 오버레이와 정확히 일치해야 합니다. */
&ipc0 {
	shared-memory-size = <0x400>;
};

/* 온보드 MC3419 가속도계는 base dts에서 기본 status = "disabled".
 * M4가 I2C1을 소유하므로 여기서 명시적으로 재활성화. */
&mc3479 {
	status = "okay";
};
```

`mc3479` 노드의 `int-gpios`는 드라이버의 인터럽트 기반 트리거 모드(`CONFIG_MC3419_TRIGGER_OWN_THREAD`)를 켰을 때만 쓰입니다. 이 랩은 `sensor_sample_fetch()`로 폴링만 하므로(`CONFIG_MC3419_TRIGGER`는 기본값 `none`) 인터럽트 배선 여부는 신경 쓰지 않아도 됩니다.

`lab/remote/prj.conf`에는 다음 설정이 필요합니다.

```
CONFIG_MBOX=y
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_MC3419=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_PRINTK=y
```

## 빌드 방법

```bash
# 1) M4(remote) 먼저 빌드
west build -p always -b sr100_rdk/sr100/m4 ./09_accel_telemetry/lab/remote -d m4

# 2) M55(host, M4 바이너리를 M4_BUILD로 포함)
west build -p always -b sr100_rdk/sr100/m55 ./09_accel_telemetry/lab -d m55 \
    -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"
```

> `M4_BUILD`는 M55 빌드 디렉토리(`m55/`) 기준 상대 경로입니다. `m4/`와 `m55/`가 워크스페이스 루트 아래 형제 디렉토리로 만들어졌다면 `../m4`가 맞습니다.

## 실행 및 결과 확인

M55 이미지를 플래시하고 보드를 리셋하면, M55는 부팅 후 M4를 reset에서 해제하고 mbox 수신을 준비합니다. M4는 부팅 즉시 가속도계를 초기화하고 500ms 주기로 값을 전송하기 시작합니다.

M55 콘솔에는 다음과 같이 시퀀스 번호가 증가하며 X/Y/Z 값이 계속 출력됩니다.

```
[00:00:00.512,000] <inf> lab09_host: accel seq=1 x=... y=... z=...
[00:00:01.012,000] <inf> lab09_host: accel seq=2 x=... y=... z=...
[00:00:01.512,000] <inf> lab09_host: accel seq=3 x=... y=... z=...
```

보드를 움직이거나 기울이면 값이 변하는 것을 확인할 수 있습니다.

### 참고 — 캘리브레이션되지 않은 원시값

이 랩에서 나오는 X/Y/Z 값은 공장 출고 캘리브레이션이 적용되지 않은 원시(raw) 값입니다. 보드를 가만히 두어도 중력가속도(~1g)가 항상 어느 한 축에 실려 있으므로, 정지 상태의 값이 정확히 0이 아닌 것은 정상입니다. 이 커리큘럼은 정밀 계측이 아니라 IPC 패턴 학습이 목적이므로 캘리브레이션/정밀도는 다루지 않습니다.

## 정리

- 가속도계처럼 항상 값을 갖는 아날로그 센서는 "주기적 텔레메트리 푸시" 패턴으로 다루는 것이 자연스럽습니다.
- Zephyr 센서를 제대로 쓰려면 `sensor_sample_fetch()`/`sensor_channel_get()`만으로는 부족하고, 초기화 시점에 devicetree 노드 활성화 + ODR 설정(wake) + 측정 범위 설정(scale) 세 가지를 함께 챙겨야 합니다.
- 단방향 텔레메트리는 수신 측 콜백이 로그만 남기면 되므로 메시지 큐/워커 스레드 없이도 안전하게 구현할 수 있습니다.

## 다음 랩 예고

Lab 10은 같은 MC3419 센서를 그대로 사용하면서, "매번 값을 보낸다"가 아니라 "값이 임계값을 넘었을 때만 알린다"는 **이벤트 기반 알림** 패턴으로 바꿔봅니다. 같은 센서로 두 가지 IPC 패턴을 비교해 보는 것이 이 랩과 Lab 10을 잇는 핵심 학습 포인트입니다.

---

문제가 발생했다면 → [`09_accel_telemetry_troubleshooting_kr.md`](./09_accel_telemetry_troubleshooting_kr.md) 참고
