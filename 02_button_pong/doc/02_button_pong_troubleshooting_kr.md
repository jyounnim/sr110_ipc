# Lab 02: Button Pong — 트러블슈팅 노트

이 문서는 Lab 02 개발/실기 검증 과정에서 실제로 겪었던 문제와 해결 과정을 기록합니다. 현재 최종 코드에는 이미 모든 수정이 반영되어 있으며, 이 문서는 참고용 기록입니다.

## 이슈 1: gpio_exp0 인터럽트 미지원 (2026-08-31)

### 증상

M4 부팅 로그에 다음 에러가 뜨고, 버튼을 눌러도 아무 반응이 없음.

```
[00:00:00.010,000] <err> gpio_keys: interrupt configuration failed: -134
[00:00:00.010,000] <err> gpio_keys: Pin 0 interrupt configuration failed: -134
```

`-134`는 Zephyr의 `-ENOTSUP`입니다.

### 원인

`user_button`은 `gpio_exp0`(PCAL6416A, I2C GPIO expander) 뒤에 달려있는데, **M4 쪽 base dts의 `gpio_exp0` 정의에는 `int-gpios` 프로퍼티가 없습니다** (M55 쪽 정의에는 `int-gpios = <&gpioa 3 GPIO_ACTIVE_LOW>;`가 있지만, M55는 i2c1을 disable했으므로 무의미). `gpio_pcal64xxa.c` 드라이버는 `int-gpios`가 없는 인스턴스에 대해 `pin_interrupt_configure()`가 항상 `-ENOTSUP`을 반환하도록 되어 있어서, 기본 인터럽트 기반 `gpio-keys` 모드는 이 보드의 M4 쪽에서 원천적으로 동작할 수 없습니다.

### 조치

`zephyr/dts/bindings/input/gpio-keys.yaml`에 정확히 이 상황을 위한 `polling-mode` 불리언 프로퍼티가 있어서, M4 오버레이(`remote/boards/sr100_rdk_sr100_m4.overlay`)에 다음을 추가해 인터럽트 대신 주기적 폴링(기본 `debounce-interval-ms` 30ms)으로 전환했습니다.

```dts
&buttons {
	polling-mode;
};
```

## 확인 이력 (검증 완료 항목)

개발 중 다음 세 가지가 "미검증" 상태였으나, 모두 실기 테스트를 거쳐 확인 완료되었습니다.

1. **`led1` 노드**: 원래 보드 dts의 `aliases`에 `led1` 별칭이 없어서 `DT_NODELABEL(led1)`을 썼었는데, 이후 M4/M55 dts의 `aliases`에 `led1 = &led1;`이 직접 추가되어 이제 `led0`와 동일하게 `DT_ALIAS(led1)`로 바로 접근합니다 (2026-08-30 반영).
2. **`user_button`이 Zephyr Input 서브시스템(`CONFIG_INPUT_GPIO_KEYS`)으로 노출되는지**: 정상 노출됨을 확인. 다만 인터럽트가 아니라 폴링 모드로 동작해야 함 (위 이슈 1 참고).
3. **gpio_exp0(PCA6416A)가 인터럽트 방식으로 버튼 이벤트를 올려주는지**: (2026-08-31) M4 쪽은 `int-gpios` 미배선으로 인터럽트 불가로 확인, `polling-mode`로 해결.

## 참고: M4/M55 I2C1 버스 공유 이슈

Lab 02의 M4/M55 오버레이에 있는 `i2c1` disable, `gpio_exp0`/`ov02c10` disable 설정은 Lab 02에서 새로 발견된 문제가 아니라 Lab 01 하드웨어 테스트 중 발견되어 이후 모든 랩에 공통 적용된 사항입니다. 자세한 원인 분석은 Lab 01의 트러블슈팅 문서를 참고하세요.
