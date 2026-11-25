/*
 * スクリーンショット: 押したときの接続先に合わせて、スクリーンショットのショートカットを送る。
 * キーマップの &screenshot（かなキーの長押し &ss_kana）から呼ばれる。
 *
 * - Duo（BT 1）              : Command + Shift + 3（画面全体）
 * - Windows（Windows モード）: Win + Shift + S（範囲を選んでクリップボードへ）
 * - Mac（それ以外）          : Command + Control + Shift + 4（範囲を選んでクリップボードへ）
 *   Mac は CLine の Control と Command を入れ替えているが、両方を同時に送るので影響しない。
 * - USB でつないでいるときは Duo とはみなさず、Windows モードかどうかで Mac / Windows を決める。
 * - 長押しと判定した瞬間に一度だけ送る（キーを押し続けても、押しっぱなしにはしない）。
 */

#define DT_DRV_COMPAT zmk_behavior_cline46_screenshot

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define DUO_PROFILE CONFIG_CLINE46_SCREENSHOT_DUO_PROFILE
#define WIN_MODE_LAYER CONFIG_CLINE46_WIN_MODE_LAYER

// 修飾キーを1つずつ押してから本体のキーを押し、すぐ全部離す（マクロと同じ送り方）。
// - 修飾キーと本体を同じ瞬間に送ると、Windows の Win+Shift+S が反応しない
// - 押しっぱなしにすると、範囲を選ぶときのクリックに修飾キーが付いてしまう（Mac の Control+クリック＝右クリック）
#define STEP_WAIT_MS 20
#define KEY_HOLD_MS 30

struct shortcut {
    uint32_t keys[4]; // 修飾キー…, 本体のキー（0 で終わり）
};

static const struct shortcut DUO = {{LGUI, LSHFT, N3, 0}};
static const struct shortcut WINDOWS = {{LGUI, LSHFT, S, 0}};
static const struct shortcut MAC = {{LGUI, LCTRL, LSHFT, N4}};

static const struct shortcut *choose_shortcut(void) {
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();

    if (ep.transport == ZMK_TRANSPORT_BLE && ep.ble.profile_index == DUO_PROFILE) {
        return &DUO;
    }
    if (zmk_keymap_layer_active(WIN_MODE_LAYER)) {
        return &WINDOWS;
    }
    return &MAC;
}

static void queue_key(struct zmk_behavior_binding_event *event, uint32_t keycode, bool press,
                      uint32_t wait) {
    struct zmk_behavior_binding kp = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)),
        .param1 = keycode,
    };
    zmk_behavior_queue_add(event, kp, press, wait);
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct shortcut *sc = choose_shortcut();
    int n = 0;
    while (n < ARRAY_SIZE(sc->keys) && sc->keys[n] != 0) {
        n++;
    }
    LOG_DBG("Screenshot: %s", sc == &DUO ? "Duo" : (sc == &WINDOWS ? "Windows" : "Mac"));

    for (int i = 0; i < n; i++) {
        queue_key(&event, sc->keys[i], true, i == n - 1 ? KEY_HOLD_MS : STEP_WAIT_MS);
    }
    for (int i = n - 1; i >= 0; i--) {
        queue_key(&event, sc->keys[i], false, i == 0 ? 0 : STEP_WAIT_MS);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_screenshot_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_screenshot_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
