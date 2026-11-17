/*
 * スリープ防止モード: マウスカーソルを1ドットだけ小刻みに揺らして、接続先（Mac・Windows・Duo）が
 * スリープしないようにする。キーマップの &jiggle でオン・オフを切り替える。
 *
 * - オンの間、15〜30秒（設定で変更可）のランダムな間隔で、8方向のどれかに1ドット動かし、
 *   約0.1秒後に元へ戻す（カーソルが少しずつ流れていかないように）。
 * - キーを押している間・最後のキー操作から10秒間・AML（オートマウスレイヤー）がオンの間は揺らさない。
 * - 揺らしの信号は PC に直接送る（トラックボールの処理を通さないので、AML は入らない）。
 * - オンの間はキーボードの深いスリープを止める（止めないと2時間後に揺らしも止まってしまう）。
 * - LED: オンで水色2回、オフで紫1回、揺らすたびに水色で一瞬光る。
 * - 再起動するとオフに戻る。
 */

#define DT_DRV_COMPAT zmk_behavior_cline46_jiggle

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#if __has_include(<zephyr/random/random.h>)
#include <zephyr/random/random.h>
#else
#include <zephyr/random/rand32.h>
#endif

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define MIN_INTERVAL_MS CONFIG_CLINE46_JIGGLE_MIN_INTERVAL_MS
#define MAX_INTERVAL_MS CONFIG_CLINE46_JIGGLE_MAX_INTERVAL_MS
#define TYPING_PAUSE_MS CONFIG_CLINE46_JIGGLE_TYPING_PAUSE_MS
#define AML_LAYER CONFIG_CLINE46_JIGGLE_AML_LAYER
#define RETURN_DELAY_MS 100

static bool enabled = false;
// 押されたままのキーの数と、最後にキーを押した・離した時刻
static int keys_held = 0;
static int64_t last_key_time = 0;
// 最後に動かした量（元へ戻すときに逆向きに送る）
static int8_t moved_x = 0, moved_y = 0;

#if IS_ENABLED(CONFIG_CLINE46_JIGGLE_LED)
// LED は rgbled-widget の点滅の受付口（led_msgq）に直接入れる。公開 API ではないので、
// rgbled-widget を更新したら struct blink_item の形が変わっていないか確かめること。
struct blink_item {
    uint8_t color;
    uint16_t duration_ms;
    uint16_t sleep_ms;
};
extern struct k_msgq led_msgq;

#define LED_MAGENTA 5
#define LED_CYAN 6

static void blink(uint8_t color, uint16_t duration_ms) {
    struct blink_item item = {.color = color, .duration_ms = duration_ms};
    k_msgq_put(&led_msgq, &item, K_NO_WAIT);
}
#else
static void blink(uint8_t color, uint16_t duration_ms) {}
#endif

static void send_movement(int8_t x, int8_t y) {
    zmk_hid_mouse_movement_set(x, y);
    zmk_endpoints_send_mouse_report();
    zmk_hid_mouse_movement_set(0, 0);
}

// キーボードの「最後に操作された時刻」を更新して、深いスリープに入らないようにする。
// ZMK の activity.c は、どの機器からの入力イベントでも操作ありとみなす。ほかに機器を問わず
// 入力イベントを受け取る処理はないので、カーソルやスクロールには影響しない。
static void keep_awake(void) { input_report(NULL, INPUT_EV_MSC, INPUT_MSC_SCAN, 0, true, K_NO_WAIT); }

static void return_back(struct k_work *work) {
    if (moved_x != 0 || moved_y != 0) {
        send_movement(-moved_x, -moved_y);
        moved_x = 0;
        moved_y = 0;
    }
}
static K_WORK_DELAYABLE_DEFINE(return_work, return_back);

static void jiggle(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(jiggle_work, jiggle);

static void schedule_next(void) {
    uint32_t span = MAX_INTERVAL_MS - MIN_INTERVAL_MS + 1;
    k_work_reschedule(&jiggle_work, K_MSEC(MIN_INTERVAL_MS + sys_rand32_get() % span));
}

static void jiggle(struct k_work *work) {
    if (!enabled) {
        return;
    }
    keep_awake();

    bool typing = keys_held > 0 || k_uptime_get() - last_key_time < TYPING_PAUSE_MS;
    bool aml = zmk_keymap_layer_active(AML_LAYER);
    if (typing || aml) {
        LOG_DBG("Jiggle skipped (typing %d, AML %d)", typing, aml);
        schedule_next();
        return;
    }

    // 8方向（上下左右と斜め）からランダムに選ぶ
    static const int8_t dirs[8][2] = {{1, 0},  {1, 1},   {0, 1},  {-1, 1},
                                      {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    const int8_t *d = dirs[sys_rand32_get() % 8];
    moved_x = d[0];
    moved_y = d[1];
    send_movement(moved_x, moved_y);
    k_work_reschedule(&return_work, K_MSEC(RETURN_DELAY_MS));
    blink(LED_CYAN, 40);
    LOG_DBG("Jiggled (%d, %d)", moved_x, moved_y);

    schedule_next();
}

static void set_enabled(bool on) {
    enabled = on;
    LOG_INF("Jiggle mode %s", on ? "on" : "off");
    if (on) {
        blink(LED_CYAN, 200);
        blink(LED_CYAN, 200);
        schedule_next();
    } else {
        blink(LED_MAGENTA, 400);
        k_work_cancel_delayable(&jiggle_work);
    }
}

static int on_position_changed(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev != NULL) {
        if (ev->state) {
            keys_held++;
        } else if (keys_held > 0) {
            keys_held--;
        }
        last_key_time = k_uptime_get();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cline46_jiggle, on_position_changed);
ZMK_SUBSCRIPTION(cline46_jiggle, zmk_position_state_changed);

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    set_enabled(!enabled);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_jiggle_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_jiggle_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
