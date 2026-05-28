// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Westberry Technology (ChangZhou) Corp., Ltd
// SPDX-License-Identifier: GPL-2.0-or-later*/

#include QMK_KEYBOARD_H
#include "wireless.h"

// External declarations needed by wireless_send_nkro override
extern void wireless_task(void);
extern bool smsg_is_busy(void);
extern host_driver_t wireless_driver;
extern host_driver_t wireless_driver;

typedef union {
    uint32_t raw;
    struct {
        uint8_t flag : 1;
        uint8_t devs : 3;
    };
} confinfo_t;
confinfo_t confinfo;

uint32_t post_init_timer = 0x00;

uint8_t blink_index      = 0;
bool    blink_fast       = true;
bool    blink_slow       = true;
static uint32_t blink_timer = 0;
#define BLINK_UPDATE_INTERVAL_MS 50  // Update blink state every 50ms instead of every call

// Implement a circular linked list of devices to support FN+TAB device
// selection
struct devs_list {
    int               devs;
    struct devs_list *next;
};

struct devs_list devs[] = {
    {.devs = DEVS_USB, .next = &devs[1]},
    {.devs = DEVS_BT1, .next = &devs[2]},
    {.devs = DEVS_BT2, .next = &devs[3]},
    {.devs = DEVS_BT3, .next = &devs[4]},
    {.devs = DEVS_2G4, .next = &devs[0]}
};

struct devs_list *current_dev = &devs[0]; // Default circular linked list to USB device

// Hack
void md_send_devinfo(const char *name);

// We use per-key tapping term to allow the wireless keys to have a much
// longer tapping term, therefore a longer hold, to match the default
// firmware behaviour.
uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case LT(0, KC_BT1):
        case LT(0, KC_BT2):
        case LT(0, KC_BT3):
        case LT(0, KC_2G4):
            return WIRELESS_TAPPING_TERM;
        default:
            return TAPPING_TERM;
    }
}

void keyboard_post_init_kb(void) {
    confinfo.raw = eeconfig_read_kb();
    if (!confinfo.raw) {
        confinfo.flag = true;
        confinfo.devs = DEVS_USB;
        eeconfig_update_kb(confinfo.raw);
    }

    gpio_write_pin_low(LED_POWER_EN_PIN);
    gpio_set_pin_output_open_drain(LED_POWER_EN_PIN);

    //gpio_set_pin_output(ESCAPE_PIN);
    //gpio_set_pin_output(DEVS_BT1_PIN);
    //gpio_set_pin_output(DEVS_BT2_PIN);
    //gpio_set_pin_output(DEVS_BT3_PIN);
    //gpio_set_pin_output(DEVS_2G4_PIN);

    // Set GPIO as high input for battery charging state
    gpio_set_pin_input_high(BT_CABLE_PIN);
    gpio_set_pin_input_high(BT_CHARGE_PIN);

    // Set USB_POWER_EN_PIN state before enabling the output to avoid instability
    if (confinfo.devs == DEVS_USB && gpio_read_pin(BT_CABLE_PIN)) {
        gpio_write_pin_low(USB_POWER_EN_PIN);
    } else {
        gpio_write_pin_high(USB_POWER_EN_PIN);
    }
    gpio_set_pin_output(USB_POWER_EN_PIN);

    wireless_init();
    md_send_devinfo(MD_BT_NAME);
    wait_ms(10);
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);
    post_init_timer = timer_read32();

    keyboard_post_init_user();
}

void usb_power_connect(void) {
    gpio_write_pin_low(USB_POWER_EN_PIN);
}

void usb_power_disconnect(void) {
    gpio_write_pin_high(USB_POWER_EN_PIN);
}

void suspend_power_down_kb(void) {
    gpio_write_pin_high(LED_POWER_EN_PIN);

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    gpio_write_pin_low(LED_POWER_EN_PIN);

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    suspend_wakeup_init_user();
}

// lpwr_is_allow_timeout_hook removed — the DEVS_USB check is already done in
// lpwr_is_allow_timeout() in keyboards/neo/wireless/lowpower.c, so this override
// was silently redundant. Keeping the code path clear (Bug 12 closure).

void wireless_post_task(void) {
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 100) {
        // Send commands with small delays to ensure SMSG queue doesn't overflow
        // and CH582F has time to process each command before the next one arrives
        md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
        wait_ms(5);
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        wait_ms(5);
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        wait_ms(5);
        wireless_devs_change(!confinfo.devs, confinfo.devs, false);
        post_init_timer = 0x00;
    }
}

void md_devs_change(uint8_t devs, bool reset) {
    switch (devs) {
        case DEVS_USB: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
        } break;
        case DEVS_2G4: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_2G4);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        case DEVS_BT1: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_BT1);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        case DEVS_BT2: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_BT2);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        case DEVS_BT3: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_BT3);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        default:
            break;
    }
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (process_record_user(keycode, record) != true) {
        return false;
    }

    switch (keycode) {
        case KC_USB: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
            return false;
        }
        case KC_NXT: {
            if (record->event.pressed) {
                current_dev = current_dev->next;
                wireless_devs_change(wireless_get_current_devs(), current_dev->devs, false);
                return false;
            }
        }
        case LT(0, KC_BT1): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, true);
            }
            return false;
        }
        case LT(0, KC_BT2): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, true);
            }
            return false;
        }
        case LT(0, KC_BT3): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, true);
            }
            return false;
        }
        case LT(0, KC_2G4): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, true);
            }
            return false;
        }
        default:
            return true;
    }
}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {
    if (confinfo.devs != wireless_get_current_devs()) {
        confinfo.devs = wireless_get_current_devs();
        eeconfig_update_kb(confinfo.raw);
    }
}

void blink(uint8_t key_index, uint8_t r, uint8_t g, uint8_t b, bool blink) {
    if (blink) {
        rgb_matrix_set_color(key_index, r, g, b);
    } else {
        rgb_matrix_set_color(key_index, RGB_OFF);
    }
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    // Throttle blink updates to avoid burning CPU cycles on every mainloop iteration
    // This significantly reduces overhead when RGB matrix is active
    if (sync_timer_elapsed32(blink_timer) >= BLINK_UPDATE_INTERVAL_MS) {
        blink_index++;
        blink_fast = (blink_index % 64 == 0) ? !blink_fast : blink_fast;
        blink_slow = (blink_index % 128 == 0) ? !blink_slow : blink_slow;
        blink_timer = sync_timer_read32();
    }

    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }

    switch (confinfo.devs) {
        case DEVS_USB: {
            rgb_matrix_set_color(DEVS_USB_INDEX, RGB_ADJ_WHITE);
        } break;
        case DEVS_BT1: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_BT1_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_BT1_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_BT1_INDEX, RGB_ADJ_WHITE);
            }
        } break;
        case DEVS_BT2: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_BT2_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_BT2_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_BT2_INDEX, RGB_ADJ_WHITE);
            }
        } break;
        case DEVS_BT3: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_BT3_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_BT3_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_BT3_INDEX, RGB_ADJ_WHITE);
            }
        } break;
        case DEVS_2G4: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_2G4_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_2G4_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_2G4_INDEX, RGB_ADJ_WHITE);
            }
        } break;
    }

    if (host_keyboard_led_state().caps_lock) {
        for (uint8_t j = 0; j <= 15; j++) {
            rgb_matrix_set_color(j, RGB_ADJ_WHITE);
        }
    }

    return true;
}

// Temporary work around for WS2812 pin init
void board_init(void) {
    gpio_set_pin_output(WS2812_DI_PIN);
    gpio_write_pin_low(WS2812_DI_PIN);
}

// Force MCU reset on unhandled_exception
void _unhandled_exception(void) {
    mcu_reset();
}

// Experimental change to fix duplicate and hung key presses on wireless
void wireless_send_nkro(report_nkro_t *report) {
    static report_keyboard_t temp_report_keyboard                 = {0};
    uint8_t                  wls_report_nkro[MD_SND_CMD_NKRO_LEN] = {0};

#ifdef NKRO_ENABLE
    if (report != NULL) {
        report_nkro_t temp_report_nkro = *report;
        uint8_t       key_count        = 0;

        temp_report_keyboard.mods = temp_report_nkro.mods;
        for (uint8_t i = 0; i < NKRO_REPORT_BITS; i++) {
            key_count += __builtin_popcount(temp_report_nkro.bits[i]);
        }

        /*
         * Use NKRO for sending when more than 6 keys are pressed
         * to solve the issue of the lack of a protocol flag in wireless mode.
         */

        for (uint8_t i = 0; i < key_count; i++) {
            uint8_t usageid;
            uint8_t idx, n = 0;

            for (n = 0; n < NKRO_REPORT_BITS && !temp_report_nkro.bits[n]; n++) {
            }
            usageid = (n << 3) | biton(temp_report_nkro.bits[n]);
            del_key_bit(&temp_report_nkro, usageid);

            for (idx = 0; idx < WLS_KEYBOARD_REPORT_KEYS; idx++) {
                if (temp_report_keyboard.keys[idx] == usageid) {
                    goto next;
                }
            }

            for (idx = 0; idx < WLS_KEYBOARD_REPORT_KEYS; idx++) {
                if (temp_report_keyboard.keys[idx] == 0x00) {
                    temp_report_keyboard.keys[idx] = usageid;
                    break;
                }
            }
        next:
            if (idx == WLS_KEYBOARD_REPORT_KEYS && (usageid < (MD_SND_CMD_NKRO_LEN * 8))) {
                wls_report_nkro[usageid / 8] |= 0x01 << (usageid % 8);
            }
        }

        temp_report_nkro = *report;

        // Detect released keys by comparing current report against previous state.
        // Any key that was in temp_report_keyboard but is now absent in temp_report_nkro
        // is a released key — clear its slot in the 6KRO buffer so the host gets a
        // proper release event. (Bug 2c fix)
        for (uint8_t i = 0; i < WLS_KEYBOARD_REPORT_KEYS; i++) {
            uint8_t prev_key = temp_report_keyboard.keys[i];
            if (prev_key == 0x00) {
                continue;
            }

            // Check if this previously-held key is still pressed in the current report
            uint8_t byte_idx = prev_key >> 3;
            uint8_t bit_idx  = prev_key & 7;
            if (byte_idx >= NKRO_REPORT_BITS || (temp_report_nkro.bits[byte_idx] & (1 << bit_idx)) == 0) {
                // Key was released — detach it from the slot
                temp_report_keyboard.keys[i] = 0x00;
            }
        }

    } else {
        memset(&temp_report_keyboard, 0, sizeof(temp_report_keyboard));
    }
#endif
    // Wait for SMSG queue to drain before sending.
    // Without a timeout, a stalled CH582F would block this loop infinitely.
    // Accept up to ~16ms wait (~16 sync_ticks at 1MHz); continue anyway
    // rather than freeze the keyboard input on a misbehaving module.
    uint32_t smsg_timeout = sync_timer_read32();
    while (smsg_is_busy()) {
        wireless_task();
        if (sync_timer_elapsed32(smsg_timeout) > 16) {
            break;
        }
    }
    wireless_driver.send_keyboard(&temp_report_keyboard);
    md_send_nkro(wls_report_nkro);
} 
