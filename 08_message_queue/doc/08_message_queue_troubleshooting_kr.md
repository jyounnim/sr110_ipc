# Lab 08 트러블슈팅: Multi-Type Message Queue

이 랩은 Lab 07(Echo Service)에서 확립된 **"mbox 콜백은 절대 블로킹 금지 — `mbox_send_dt()`도 예외 없음"** 원칙을 설계 단계부터 반영해 하드웨어 검증 과정에서 특별한 이슈 없이 통과했습니다. 아래에 그 배경과, 만약 유사한 증상을 겪을 경우 확인해 볼 점을 정리합니다.

## 이 랩이 처음부터 안전했던 이유

Lab 07에서는 "가벼운 API 정도는 mbox 콜백(ISR) 안에서 직접 호출해도 된다"는 가정이 있었으나, 실기 검증 결과 `mbox_send_dt()` 자체가 이 보드에서 블로킹될 수 있다는 사실이 확인되며 그 예외가 전면 폐기되었습니다. 이후 모든 랩은 "콜백 안에서는 큐에 넣는 것 외에 그 어떤 IPC 호출도 하지 않는다"는 원칙을 지켜야 합니다.

Lab 08의 M4 쪽 `mbox_rx_callback()`은 이 원칙과 정확히 일치합니다.

```c
static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
                              void *user_data, struct mbox_msg *data)
{
    struct ipc_pattern_msg msg;

    if (data->size < sizeof(msg)) {
        return;
    }
    memcpy(&msg, data->data, sizeof(msg));
    k_msgq_put(&pattern_msgq, &msg, K_NO_WAIT);
}
```

- 이 콜백은 **`mbox_send_dt()`를 호출하지 않습니다.** 이 랩은 애초에 M4→M55 응답이 없는 M55→M4 단방향 구조로 설계되어 있어서, Lab 07에서 문제가 됐던 "콜백에서 `mbox_send_dt()`를 호출했다가 블로킹된다"는 시나리오 자체가 성립하지 않습니다.
- 실제로 시간이 걸리는 작업(LED 패턴 실행, 다수의 `k_msleep` 포함)은 전부 `Pattern_Task`라는 별도 워커 스레드에서만 수행됩니다.
- `k_msgq_put(..., K_NO_WAIT)`는 큐가 가득 찬 경우에도 대기 없이 즉시 실패를 반환하므로, ISR이 큐잉 연산 때문에 블로킹될 가능성도 없습니다.

즉 이 랩은 "우연히 문제가 없었다"기보다, **큐잉이라는 주제 자체가 콜백-블로킹 금지 원칙과 자연스럽게 맞아떨어지도록 설계되어 있어** 검증 과정에서 별도의 수정 없이 통과했습니다.

## 확인된 이슈 없음

이 문서 작성 시점(v1.0.0 정리 기준) 기준으로 Lab 08 고유의 하드웨어/빌드 이슈는 보고된 바 없습니다. 다른 랩들과 공통으로 적용되는 아래 항목만 참고하면 됩니다.

- **I2C1 버스 공유 이슈**: M4와 M55가 물리적으로 같은 I2C1 버스를 공유하는 이 보드의 구조상, M55 오버레이에서 `&i2c1`, `&gpio_exp0`, `&ov02c10`을 `status = "disabled"`로 꺼야 합니다. 이 랩의 M55 오버레이(`lab/boards/sr100_rdk_sr100_m55.overlay`)에는 이미 반영되어 있습니다. 자세한 원인은 [Lab 01 트러블슈팅 문서](../../01_hello_ipc/doc/01_hello_ipc_troubleshooting_kr.md)를 참고하세요.
- **`M4_BUILD` 상대경로**: M55 빌드 시 `-DM4_BUILD="../m4"`는 M55 빌드 디렉토리(`m55/`) 기준 상대경로입니다. `m4/`와 `m55/`가 워크스페이스 루트의 형제 디렉토리가 아니라면 이 값을 실제 배치에 맞게 조정해야 합니다.

## 만약 큐잉이 기대와 다르게 동작한다면

이 랩 자체는 실기에서 문제없이 통과했지만, 이 코드를 변형해 실습하다가 아래와 같은 증상을 만날 수 있으므로 참고용으로 남겨둡니다.

- **`queue depth now`가 항상 0으로만 찍힌다**: M55가 4개를 연달아 보내는 타이밍과 M4의 처리 속도가 우연히 맞아떨어져 큐에 쌓일 겨를이 없는 경우입니다. `send_pattern()` 호출 사이에 지연이 없는지, `repeat` 값이 의도한 대로인지 확인하세요.
- **일부 메시지가 사라진 것처럼 보인다**: 큐 depth(`K_MSGQ_DEFINE`의 세 번째 인자, 현재 8)보다 한 번에 몰리는 메시지 수가 더 많아 `k_msgq_put()`이 `-ENOMSG`로 실패하는 경우입니다. 이 랩의 콜백은 이 반환값을 검사하지 않으므로, 원인 파악을 위해 실습 중 콜백에 반환값 검사와 드롭 카운터를 추가해 보는 것을 권장합니다.
- **LED가 패턴과 다르게 동작한다**: `run_pattern()`의 `switch (msg->pattern_id)` 분기가 `enum ipc_pattern_id`(1~4)와 정확히 일치하는지, M55와 M4 양쪽의 `ipc_common.h`가 동일한 버전인지 확인하세요.

---

문제가 새로 발견되면 이 문서에 증상/원인/해결 순으로 추가해 주세요.
</content>
