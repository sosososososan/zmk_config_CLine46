/*
 * Windows モード（レイヤー）と Unicode の入力方式を、選んでいる BT 接続先に合わせ続ける。
 *
 * どちらもキーボードの中に保存されないので、次のときに Mac 用の状態に戻ってしまう:
 *   - 再起動（深いスリープからの復帰を含む）。BT 接続先は保存されるので、Windows のままなのに Mac 用になる
 *   - &to 0（英数・かなキーなど）。レイヤー0以外のレイヤーを全部オフにするので、Windows モードも消える
 * そこで、起動直後・接続先が変わったとき・レイヤーが切り替わったときに、接続先を見て状態を直す。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>

#include <dt-bindings/zmk-unicode/uc.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define WIN_PROFILE CONFIG_CLINE46_WIN_PROFILE
#define WIN_MODE_LAYER CONFIG_CLINE46_WIN_MODE_LAYER

// 最後に設定した Unicode の入力方式（-1 = まだ設定していない）
static int applied_uc_mode = -1;

static void set_unicode_mode(int mode) {
    if (mode == applied_uc_mode) {
        return;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(uc)),
        .param1 = UC_SELECT_INPUT_MODE,
        .param2 = mode,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    if (zmk_behavior_invoke_binding(&binding, event, true) == 0) {
        zmk_behavior_invoke_binding(&binding, event, false);
        applied_uc_mode = mode;
    }
}

static void apply(struct k_work *work) {
    bool windows = zmk_ble_active_profile_index() == WIN_PROFILE;

    if (windows && !zmk_keymap_layer_active(WIN_MODE_LAYER)) {
        LOG_DBG("Windows profile selected: turning Windows mode on");
        zmk_keymap_layer_activate(WIN_MODE_LAYER);
    } else if (!windows && zmk_keymap_layer_active(WIN_MODE_LAYER)) {
        LOG_DBG("Non-Windows profile selected: turning Windows mode off");
        zmk_keymap_layer_deactivate(WIN_MODE_LAYER);
    }

    set_unicode_mode(windows ? UC_MODE_WIN_COMPOSE : UC_MODE_MACOS);
}

static K_WORK_DEFINE(apply_work, apply);
static K_WORK_DELAYABLE_DEFINE(boot_apply_work, apply);

// イベントの処理中にレイヤーを変えないよう、実際の変更は後で（ワークキューで）行う
static int on_event(const zmk_event_t *eh) {
    if (as_zmk_ble_active_profile_changed(eh) != NULL) {
        // 接続先が変わったら Unicode の入力方式も設定し直す（キーマップのマクロが変えている場合に備える）
        applied_uc_mode = -1;
    }
    k_work_submit(&apply_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(win_mode_follows_profile, on_event);
ZMK_SUBSCRIPTION(win_mode_follows_profile, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(win_mode_follows_profile, zmk_layer_state_changed);

// 起動直後は接続先の記録（settings）を読み込み終わってから合わせる
static int win_mode_follows_profile_init(void) {
    k_work_schedule(&boot_apply_work, K_SECONDS(3));
    return 0;
}

SYS_INIT(win_mode_follows_profile_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
