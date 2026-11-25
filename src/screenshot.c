/*
 * スクリーンショット: 押したときの接続先に合わせて、スクリーンショットのショートカットを送る。
 * キーマップの &screenshot（かなキーの長押し &ss_kana）から呼ばれる。
 *
 * - Duo（BT 1）              : Command + Shift + 3（画面全体）
 * - Windows（Windows モード）: Win + Shift + S（範囲を選んでクリップボードへ）
 * - Mac（それ以外）          : Command + Control + Shift + 4（範囲を選んでクリップボードへ）
 *   Mac は CLine の Control と Command を入れ替えているが、両方を同時に送るので影響しない。
 * - USB でつないでいるときは Duo とはみなさず、Windows モードかどうかで Mac / Windows を決める。
 */

#define DT_DRV_COMPAT zmk_behavior_cline46_screenshot

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define DUO_PROFILE CONFIG_CLINE46_SCREENSHOT_DUO_PROFILE
#define WIN_MODE_LAYER CONFIG_CLINE46_WIN_MODE_LAYER

// 押したときに送ったキー（離すときに同じキーを離す）
static uint32_t pressed_keycode = 0;

static uint32_t choose_keycode(void) {
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();

    if (ep.transport == ZMK_TRANSPORT_BLE && ep.ble.profile_index == DUO_PROFILE) {
        return LG(LS(N3));
    }
    if (zmk_keymap_layer_active(WIN_MODE_LAYER)) {
        return LG(LS(S));
    }
    return LG(LC(LS(N4)));
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    pressed_keycode = choose_keycode();
    LOG_DBG("Screenshot: sending 0x%08X", pressed_keycode);
    return raise_zmk_keycode_state_changed_from_encoded(pressed_keycode, true, event.timestamp);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    if (pressed_keycode == 0) {
        return ZMK_BEHAVIOR_OPAQUE;
    }
    uint32_t keycode = pressed_keycode;
    pressed_keycode = 0;
    return raise_zmk_keycode_state_changed_from_encoded(keycode, false, event.timestamp);
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
