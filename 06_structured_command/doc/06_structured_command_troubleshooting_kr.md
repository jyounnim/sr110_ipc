# Lab 06 트러블슈팅: 구조화된 커맨드 메시지

이 랩은 Lab 02/05에서 이미 확인된 M4 쪽 `user_button` 인터럽트 미지원 이슈를, 처음부터 **선제적으로 polling-mode로 적용**한 상태로 시작했습니다. 그 결과 하드웨어 검증 과정에서 새로 발생한 이슈는 없었습니다.

### 증상/원인/조치 — (선제 조치, 이슈 아님) M4 `user_button` 인터럽트 미지원

**증상**: 해당 없음 — 이 랩에서는 발생하지 않았습니다. (Lab 02에서 처음 관찰된 증상은 M4 부팅 로그에 `interrupt configuration failed: -134`가 출력되고 버튼 입력이 전혀 감지되지 않는 것이었습니다.)

**원인**: `user_button`이 매달린 `gpio_exp0`(PCAL6416A I2C GPIO expander)에는 M4 쪽 base devicetree에 `int-gpios`가 배선돼 있지 않습니다. `gpio_pcal64xxa.c` 드라이버는 `int-gpios`가 없는 인스턴스에서 `pin_interrupt_configure()`가 항상 `-ENOTSUP`을 반환하도록 되어 있어, 인터럽트 기반 `gpio-keys` 모드는 M4 쪽에서 구조적으로 동작할 수 없습니다.

**조치**: Lab 02/05에서 확립된 해결책을 이 랩에서는 코드 작성 시점부터 미리 적용했습니다. `lab/remote/boards/sr100_rdk_sr100_m4.overlay`에 아래와 같이 부모 노드(`&buttons`)에 `polling-mode`를 지정했습니다.

```dts
&buttons {
	polling-mode;
};
```

이 설정 덕분에 Lab 06은 하드웨어 검증 단계에서 버튼 관련 이슈 없이 곧바로 통과했습니다. `button_press_count`는 폴링 주기(기본 `debounce-interval-ms` 30ms)만큼의 지연 후 `GET_STATUS` 응답에 정상 반영되는 것을 확인했습니다.

### 참고: 이 랩에도 적용되어 있는 공통 보드 이슈

아래 두 가지는 이 랩 고유의 이슈가 아니라, Lab 01부터 모든 IPC 랩에 공통으로 적용된 조치입니다. 새로운 문제가 발생했을 때 오해하지 않도록 참고용으로 남겨둡니다.

- **M4/M55 I2C1 버스 공유**: M4와 M55가 물리적으로 같은 I2C1 버스를 공유하므로, `lab/boards/sr100_rdk_sr100_m55.overlay`에서 M55 쪽 `&i2c1`(및 자식 노드 `&gpio_exp0`, `&ov02c10`)을 `disabled`로 설정해 M4만 이 버스를 구동하도록 되어 있습니다. 자세한 원인은 [Lab 01 트러블슈팅 문서](../../01_hello_ipc/doc/01_hello_ipc_troubleshooting_kr.md) 참고.
- **`M4_BUILD` 상대경로**: M55 빌드 시 `-DM4_BUILD="../m4"`는 M55 빌드 디렉토리(`m55/`) 기준 상대경로입니다. `m4/`와 `m55/`가 워크스페이스 루트의 형제 디렉토리 구조일 때 `../m4`가 맞으며, `./m4`로 잘못 지정하면 최종 이미지에 M4 펌웨어가 조용히 누락됩니다. 자세한 원인은 [Lab 01 트러블슈팅 문서](../../01_hello_ipc/doc/01_hello_ipc_troubleshooting_kr.md) 참고.

문제가 이 문서로 해결되지 않는다면, 위에서 링크한 Lab 01/02 README의 관련 섹션을 함께 확인해주세요.
