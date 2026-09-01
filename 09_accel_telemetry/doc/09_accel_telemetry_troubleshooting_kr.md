# Lab 09 트러블슈팅: 가속도계 텔레메트리

이 문서는 Lab 09를 실제 SR110 보드(`sr100_rdk`)에서 검증하는 과정에서 순차적으로 발견된 세 가지 실제 이슈와, 각각의 진단 과정을 정리한 것입니다. 메인 문서([`09_accel_telemetry_kr.md`](./09_accel_telemetry_kr.md))에서 "정상 사용법"으로 설명한 세 가지 필수 단계(노드 재활성화, ODR wake, FULL_SCALE 설정)는 사실 아래 세 버그를 하나씩 겪으며 밝혀진 것들입니다.

## 이슈 1 — accel device not ready

### 증상

M4 콘솔에 다음 에러가 뜨고 `main()`이 `-ENODEV`로 조기 종료됩니다.

```
[00:00:00.007,000] <inf> lab09_client: Lab09 Accel Telemetry CLIENT - sr100_rdk/sr100/m4
[00:00:00.007,000] <err> lab09_client: accel device not ready (check DT_NODELABEL(accel0))
```

### 원인 진단 과정

최초 코드는 가속도계 devicetree 노드 레이블을 `accel0`으로 추정하고 작성되었습니다(Zephyr 업스트림에 `mc3419` 드라이버가 존재한다는 것과 `CONFIG_MC3419`/`SENSOR_CHAN_ACCEL_X/Y/Z` API는 확인했지만, 실제 보드 dts의 노드 레이블까지는 확인하지 못한 상태로 전달됨).

실기 테스트에서 위 에러가 재현된 뒤, 실제 보드 dts 파일(`sr100_rdk_m4.dts` / `sr100_rdk_m55.dts`)을 직접 열어 확인한 결과 두 가지가 최초 추정과 달랐습니다.

```dts
&i2c1 {
	...
	mc3479: mc3419@4c {
		compatible = "memsic,mc3419";
		status = "disabled";
		reg = <0x4c>;
		int-gpios = <&gpio_exp0 4 0>;
		lpf-fc-sel = <0>;
	};
};
```

1. 노드 레이블이 `accel0`이 아니라 **`mc3479`**였습니다.
2. 이 노드가 M4/M55 base dts 양쪽 모두 **기본값이 `status = "disabled"`**였는데, 오버레이에서 다시 켜주는 코드가 애초에 빠져 있었습니다. `DEVICE_DT_GET_OR_NULL()`은 비활성 노드에 대해 `NULL`을 반환하므로, `device_is_ready()` 검사 이전에 이미 디바이스 포인터 자체가 없는 상태였고 이게 "not ready" 에러로 직결되었습니다.

`CONFIG_MC3419` Kconfig 심볼과 `SENSOR_CHAN_ACCEL_X/Y/Z` 표준 sensor API 채널은 원래 추정이 맞았습니다(Zephyr 업스트림 `drivers/sensor/memsic/mc3419/` 드라이버로 재확인).

### 조치

- `lab/remote/boards/sr100_rdk_sr100_m4.overlay`에 `&mc3479 { status = "okay"; };` 추가해 M4 쪽에서 노드를 명시적으로 활성화.
- `lab/remote/src/main.c`의 `DT_NODELABEL(accel0)` → `DT_NODELABEL(mc3479)`로 수정.
- 참고: `mc3479` 노드의 `int-gpios`는 `gpio_exp0`의 4번 핀을 가리키는데, 이건 드라이버의 인터럽트 기반 트리거 모드(`CONFIG_MC3419_TRIGGER_OWN_THREAD`)를 켰을 때만 쓰입니다. 이 랩은 `sensor_sample_fetch()`로 폴링만 하므로(트리거 모드 미사용, 기본값 `CONFIG_MC3419_TRIGGER=none`), Lab 02/05/06에서 확인된 "M4 쪽 `gpio_exp0`는 `int-gpios`가 없어 인터럽트 미지원" 이슈와는 무관합니다.
- M55 쪽 base dts도 `mc3479`가 동일하게 기본 disabled라, M55 오버레이는 추가 조치 불필요(원래 disabled 상태 그대로 두면 됩니다).

## 이슈 2 — x=y=z=0 고정, 보드를 흔들어도 변화 없음

### 증상

이슈 1 해결 후 IPC 자체(seq 증가)는 정상 동작하지만, M55에 도착하는 `x/y/z` 값이 계속 0으로 고정됩니다. 보드를 흔들어도 변화가 없습니다.

### 원인 진단 과정

Zephyr MC3419 드라이버(`mc3419_init()`)는 **의도적으로 초기화 직후 저전력 standby 상태를 유지**합니다. 드라이버 소스 코드 주석에 다음과 같이 명시되어 있습니다.

> "Leave the sensor in default power on state, will be enabled by configure attr or setting trigger"

즉 ODR(Output Data Rate)을 `sensor_attr_set(SENSOR_ATTR_SAMPLING_FREQUENCY)`로 설정해야 그 내부에서 센서를 wake 모드로 전환하는 레지스터 쓰기가 일어나도록 설계되어 있습니다. 이 호출이 코드에 빠져 있었기 때문에, `sensor_sample_fetch()`는 에러 없이 "성공"하지만 실제로는 측정이 시작되지 않은 상태의 레지스터(전부 0)를 읽어온 것이었습니다.

### 조치

`main()`에서 `device_is_ready()` 확인 직후, 센서를 깨우기 위해 ODR을 50Hz로 설정하는 `sensor_attr_set()` 호출을 추가했습니다.

```c
struct sensor_value odr = {.val1 = 50, .val2 = 0}; /* 50 Hz */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
```

이 호출이 실패하면 로그(`sensor_attr_set(SAMPLING_FREQUENCY) failed`)로 알리도록 했습니다.

## 이슈 3 — ODR 설정 후에도 x=y=z=0 계속됨 (같은 날 두 번째 원인)

### 증상

이슈 2의 ODR 수정본을 적용해도, M4에는 에러 로그(`sensor_attr_set` 실패 등)가 전혀 없이 정상 초기화되는데 M55에 도착하는 x/y/z는 여전히 0으로 고정되었습니다. 콘솔만 봐서는 "센서가 여전히 잠들어 있는 것"과 구분이 되지 않는 증상이었습니다.

### 원인 진단 과정

콘솔 로그만으로는 원인을 좁힐 수 없어 Zephyr MC3419 드라이버 소스(`drivers/sensor/memsic/mc3419/mc3419.c`)를 직접 확인했습니다.

```c
static int mc3419_to_sensor_value(double sensitivity, int16_t *raw_data,
                                  struct sensor_value *val)
{
        double value = sys_le16_to_cpu(*raw_data);
        value *= sensitivity * SENSOR_GRAVITY_DOUBLE / 1000;
        return sensor_value_from_double(val, value);
}
```

raw 값에 곱해지는 `sensitivity`는 **`mc3419_set_accel_range()` 안에서만 대입**되며, 이 함수는 `sensor_attr_set(..., SENSOR_ATTR_FULL_SCALE, ...)`을 명시적으로 호출할 때만 실행됩니다. `mc3419_init()`은 이 함수를 호출하지 않고, 드라이버의 정적 데이터 구조체는 0으로 초기화되므로 `sensitivity`가 계속 0으로 남아 `raw_value * 0 = 0`이 되어 실제 측정값과 무관하게 항상 0이 나온 것이었습니다.

이슈 2(ODR/wake 문제)와는 **완전히 별개의 버그**였는데, 콘솔에 나타나는 증상(항상 0)이 완전히 동일해서 하나씩 순차적으로 발견되었습니다.

### 조치

`main()`에 ODR 설정에 이어 full-scale range 설정 `sensor_attr_set()` 호출을 추가했습니다.

```c
struct sensor_value range = {.val1 = 0, .val2 = 0}; /* smallest/most sensitive range */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &range);
```

정리하면 이 센서를 실제로 쓰려면 **ODR과 range 둘 다** `sensor_attr_set()`으로 명시적으로 설정해야 합니다 — 둘 중 하나만 빠져도 겉보기 증상(값이 0)은 동일해서 로그만 봐서는 구분이 쉽지 않습니다.

> **전 랩 공통 교훈**: 이후 센서를 다루는 랩(10/12/13/15/18)에서도 "device ready + IPC 정상인데 값이 0/고정"이라면, wake-up(ODR)뿐 아니라 range/scale 등 드라이버가 요구하는 다른 필수 `attr_set` 호출이 더 있는지 실제 드라이버 소스를 직접 확인할 것 — devicetree/Kconfig 문제가 아니라 런타임 API 호출 누락(그것도 여러 개일 수 있음)인 경우가 있습니다.

## 캘리브레이션/정밀도는 스코프 밖으로 결정

위 세 이슈를 모두 해결한 뒤 재검증한 결과, IPC와 센서 읽기 자체는 정상 동작하지만 부팅 직후 정지 상태에서도 값이 정확히 0이 아니라 중력가속도(~1g)에 해당하는 값이 어느 한 축에 항상 실려 있는 것이 확인되었습니다.

이는 드라이버 버그가 아니라, 공장 출고 캘리브레이션(오프셋/스케일 보정)이 적용되지 않은 원시(raw) 값을 그대로 사용하고 있기 때문입니다. 이 커리큘럼은 정밀 가속도계를 만드는 것이 아니라 M4↔M55 IPC 패턴을 학습하는 것이 목적이므로, 캘리브레이션 및 측정 정밀도 개선은 이 랩의 스코프 밖으로 명시적으로 결정하고 원시값 그대로 완료 처리했습니다.

> 참고로 이 "정지 상태에서도 항상 값이 실려 있다"는 특성은 Lab 10에서 다른 각도로 다시 등장합니다. Lab 10은 같은 센서로 임계값 기반 이벤트를 만드는데, 원점(0) 기준으로 임계값을 비교하면 중력 오프셋 때문에 정지 상태에서도 이벤트가 계속 발생하는 문제가 생깁니다. 이때는 (캘리브레이션이 아니라) 부팅 시점의 값을 baseline으로 저장해 그 편차만 비교하는 방식으로 해결합니다 — 목적이 "측정값을 정확하게 만드는 것"이 아니라 "오탐(false positive) 이벤트를 막는 것"이라는 점에서 이번 랩의 캘리브레이션 논의와는 별개입니다.
