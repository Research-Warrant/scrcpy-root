#include "control_msg.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/binary.h"
#include "util/log.h"
#include "util/str.h"

/**
 * Map an enum value to a string based on an array, without crashing on an
 * out-of-bounds index.
 */
#define ENUM_TO_LABEL(labels, value) \
    ((size_t) (value) < ARRAY_LEN(labels) ? labels[value] : "???")

#define KEYEVENT_ACTION_LABEL(value) \
    ENUM_TO_LABEL(android_keyevent_action_labels, value)

#define MOTIONEVENT_ACTION_LABEL(value) \
    ENUM_TO_LABEL(android_motionevent_action_labels, value)

#define SCREEN_POWER_MODE_LABEL(value) \
    ENUM_TO_LABEL(screen_power_mode_labels, value)

static const char *const android_keyevent_action_labels[] = {
    "down",
    "up",
    "multi",
};

static const char *const android_motionevent_action_labels[] = {
    "down",
    "up",
    "move",
    "cancel",
    "outside",
    "pointer-down",
    "pointer-up",
    "hover-move",
    "scroll",
    "hover-enter",
    "hover-exit",
    "btn-press",
    "btn-release",
};

static const char *const screen_power_mode_labels[] = {
    "off",
    "doze",
    "normal",
    "doze-suspend",
    "suspend",
};

static const char *const copy_key_labels[] = {
    "none",
    "copy",
    "cut",
};

static inline const char *
get_well_known_pointer_id_name(uint64_t pointer_id) {
    switch (pointer_id) {
        case SC_POINTER_ID_MOUSE:
            return "mouse";
        case SC_POINTER_ID_GENERIC_FINGER:
            return "finger";
        case SC_POINTER_ID_VIRTUAL_FINGER:
            return "vfinger";
        default:
            return NULL;
    }
}

static bool
is_mouse_pointer(uint64_t pointer_id) {
    return pointer_id == SC_POINTER_ID_MOUSE;
}

static bool
is_finger_pointer(uint64_t pointer_id) {
    return pointer_id == SC_POINTER_ID_GENERIC_FINGER
        || pointer_id == SC_POINTER_ID_VIRTUAL_FINGER;
}

static const char *
get_touch_action_name_camel(enum android_motionevent_action action) {
    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            return "Down";
        case AMOTION_EVENT_ACTION_UP:
            return "Up";
        case AMOTION_EVENT_ACTION_MOVE:
            return "Move";
        case AMOTION_EVENT_ACTION_CANCEL:
            return "Cancel";
        case AMOTION_EVENT_ACTION_OUTSIDE:
            return "Outside";
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            return "PointerDown";
        case AMOTION_EVENT_ACTION_POINTER_UP:
            return "PointerUp";
        case AMOTION_EVENT_ACTION_HOVER_MOVE:
            return "Hover";
        case AMOTION_EVENT_ACTION_SCROLL:
            return "Scroll";
        case AMOTION_EVENT_ACTION_HOVER_ENTER:
            return "HoverEnter";
        case AMOTION_EVENT_ACTION_HOVER_EXIT:
            return "HoverExit";
        case AMOTION_EVENT_ACTION_BUTTON_PRESS:
            return "ButtonPress";
        case AMOTION_EVENT_ACTION_BUTTON_RELEASE:
            return "ButtonRelease";
        default:
            return "Unknown";
    }
}

static const char *
get_touch_target_name_camel(uint64_t pointer_id) {
    switch (pointer_id) {
        case SC_POINTER_ID_GENERIC_FINGER:
            return "finger";
        case SC_POINTER_ID_VIRTUAL_FINGER:
            return "virtualFinger";
        case SC_POINTER_ID_MOUSE:
            return "mouse";
        default:
            return "pointer";
    }
}

static const char *
get_mouse_button_name(enum android_motionevent_buttons button) {
    switch (button) {
        case AMOTION_EVENT_BUTTON_PRIMARY:
            return "left";
        case AMOTION_EVENT_BUTTON_SECONDARY:
            return "right";
        case AMOTION_EVENT_BUTTON_TERTIARY:
            return "middle";
        case AMOTION_EVENT_BUTTON_BACK:
            return "back";
        case AMOTION_EVENT_BUTTON_FORWARD:
            return "forward";
        default:
            return NULL;
    }
}

static void
append_button_name(char *buf, size_t buf_size, const char *name, bool *first) {
    size_t len = strlen(buf);
    if (len >= buf_size) {
        return;
    }

    int w = snprintf(buf + len, buf_size - len, "%s%s", *first ? "" : "|",
                     name);
    if (w > 0) {
        *first = false;
    }
}

static void
write_buttons_human(char *buf, size_t buf_size,
                    enum android_motionevent_buttons buttons) {
    if (!buttons) {
        snprintf(buf, buf_size, "none");
        return;
    }

    buf[0] = '\0';
    bool first = true;
    uint32_t remaining = buttons;

    static const struct {
        enum android_motionevent_buttons button;
        const char *name;
    } known_buttons[] = {
        { AMOTION_EVENT_BUTTON_PRIMARY, "left" },
        { AMOTION_EVENT_BUTTON_SECONDARY, "right" },
        { AMOTION_EVENT_BUTTON_TERTIARY, "middle" },
        { AMOTION_EVENT_BUTTON_BACK, "back" },
        { AMOTION_EVENT_BUTTON_FORWARD, "forward" },
    };

    for (size_t i = 0; i < ARRAY_LEN(known_buttons); ++i) {
        if (buttons & known_buttons[i].button) {
            append_button_name(buf, buf_size, known_buttons[i].name, &first);
            remaining &= ~known_buttons[i].button;
        }
    }

    if (remaining) {
        char unknown[16];
        snprintf(unknown, sizeof(unknown), "0x%lx", (long) remaining);
        append_button_name(buf, buf_size, unknown, &first);
    }
}

static const char *
get_mouse_action_button_name(enum android_motionevent_buttons action_button,
                             enum android_motionevent_buttons buttons,
                             char *buf, size_t buf_size) {
    const char *name = get_mouse_button_name(action_button);
    if (name) {
        return name;
    }

    name = get_mouse_button_name(buttons);
    if (name) {
        return name;
    }

    write_buttons_human(buf, buf_size, buttons);
    return buf;
}

static void
write_position(uint8_t *buf, const struct sc_position *position) {
    sc_write32be(&buf[0], position->point.x);
    sc_write32be(&buf[4], position->point.y);
    sc_write16be(&buf[8], position->screen_size.width);
    sc_write16be(&buf[10], position->screen_size.height);
}

// Write truncated string, and return the size
static size_t
write_string_payload(uint8_t *payload, const char *utf8, size_t max_len) {
    if (!utf8) {
        return 0;
    }
    size_t len = sc_str_utf8_truncation_index(utf8, max_len);
    memcpy(payload, utf8, len);
    return len;
}

// Write length (4 bytes) + string (non null-terminated)
static size_t
write_string(uint8_t *buf, const char *utf8, size_t max_len) {
    size_t len = write_string_payload(buf + 4, utf8, max_len);
    sc_write32be(buf, len);
    return 4 + len;
}

// Write length (1 byte) + string (non null-terminated)
static size_t
write_string_tiny(uint8_t *buf, const char *utf8, size_t max_len) {
    assert(max_len <= 0xFF);
    size_t len = write_string_payload(buf + 1, utf8, max_len);
    buf[0] = len;
    return 1 + len;
}

size_t
sc_control_msg_serialize(const struct sc_control_msg *msg, uint8_t *buf) {
    buf[0] = msg->type;
    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_KEYCODE:
            buf[1] = msg->inject_keycode.action;
            sc_write32be(&buf[2], msg->inject_keycode.keycode);
            sc_write32be(&buf[6], msg->inject_keycode.repeat);
            sc_write32be(&buf[10], msg->inject_keycode.metastate);
            return 14;
        case SC_CONTROL_MSG_TYPE_INJECT_TEXT: {
            size_t len = write_string(&buf[1], msg->inject_text.text,
                                      SC_CONTROL_MSG_INJECT_TEXT_MAX_LENGTH);
            return 1 + len;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT:
            buf[1] = msg->inject_touch_event.action;
            sc_write64be(&buf[2], msg->inject_touch_event.pointer_id);
            write_position(&buf[10], &msg->inject_touch_event.position);
            uint16_t pressure =
                sc_float_to_u16fp(msg->inject_touch_event.pressure);
            sc_write16be(&buf[22], pressure);
            sc_write32be(&buf[24], msg->inject_touch_event.action_button);
            sc_write32be(&buf[28], msg->inject_touch_event.buttons);
            return 32;
        case SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT:
            write_position(&buf[1], &msg->inject_scroll_event.position);
            int16_t hscroll =
                sc_float_to_i16fp(msg->inject_scroll_event.hscroll);
            int16_t vscroll =
                sc_float_to_i16fp(msg->inject_scroll_event.vscroll);
            sc_write16be(&buf[13], (uint16_t) hscroll);
            sc_write16be(&buf[15], (uint16_t) vscroll);
            sc_write32be(&buf[17], msg->inject_scroll_event.buttons);
            return 21;
        case SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
            buf[1] = msg->inject_keycode.action;
            return 2;
        case SC_CONTROL_MSG_TYPE_GET_CLIPBOARD:
            buf[1] = msg->get_clipboard.copy_key;
            return 2;
        case SC_CONTROL_MSG_TYPE_SET_CLIPBOARD:
            sc_write64be(&buf[1], msg->set_clipboard.sequence);
            buf[9] = !!msg->set_clipboard.paste;
            size_t len = write_string(&buf[10], msg->set_clipboard.text,
                                      SC_CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH);
            return 10 + len;
        case SC_CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE:
            buf[1] = msg->set_screen_power_mode.mode;
            return 2;
        case SC_CONTROL_MSG_TYPE_UHID_CREATE:
            sc_write16be(&buf[1], msg->uhid_create.id);

            size_t index = 3;
            index += write_string_tiny(&buf[index], msg->uhid_create.name, 127);

            sc_write16be(&buf[index], msg->uhid_create.report_desc_size);
            index += 2;

            memcpy(&buf[index], msg->uhid_create.report_desc,
                                msg->uhid_create.report_desc_size);
            index += msg->uhid_create.report_desc_size;

            return index;
        case SC_CONTROL_MSG_TYPE_UHID_INPUT:
            sc_write16be(&buf[1], msg->uhid_input.id);
            sc_write16be(&buf[3], msg->uhid_input.size);
            memcpy(&buf[5], msg->uhid_input.data, msg->uhid_input.size);
            return 5 + msg->uhid_input.size;
        case SC_CONTROL_MSG_TYPE_UHID_DESTROY:
            sc_write16be(&buf[1], msg->uhid_destroy.id);
            return 3;
        case SC_CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
        case SC_CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
        case SC_CONTROL_MSG_TYPE_COLLAPSE_PANELS:
        case SC_CONTROL_MSG_TYPE_ROTATE_DEVICE:
        case SC_CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS:
            // no additional data
            return 1;
        default:
            LOGW("Unknown message type: %u", (unsigned) msg->type);
            return 0;
    }
}

void
sc_control_msg_log(const struct sc_control_msg *msg) {
#define LOG_CMSG(fmt, ...) LOGV("input: " fmt, ## __VA_ARGS__)
    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_KEYCODE:
            LOG_CMSG("key %-4s code=%d repeat=%" PRIu32 " meta=%06lx",
                     KEYEVENT_ACTION_LABEL(msg->inject_keycode.action),
                     (int) msg->inject_keycode.keycode,
                     msg->inject_keycode.repeat,
                     (long) msg->inject_keycode.metastate);
            break;
        case SC_CONTROL_MSG_TYPE_INJECT_TEXT:
            LOG_CMSG("text \"%s\"", msg->inject_text.text);
            break;
        case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
            int action = msg->inject_touch_event.action
                       & AMOTION_EVENT_ACTION_MASK;
            uint64_t id = msg->inject_touch_event.pointer_id;
            const char *pointer_name = get_well_known_pointer_id_name(id);
            if (pointer_name) {
                // string pointer id
                LOG_CMSG("touch [id=%s] %-4s position=%" PRIi32 ",%" PRIi32
                             " pressure=%f action_button=%06lx buttons=%06lx",
                         pointer_name,
                         MOTIONEVENT_ACTION_LABEL(action),
                         msg->inject_touch_event.position.point.x,
                         msg->inject_touch_event.position.point.y,
                         msg->inject_touch_event.pressure,
                         (long) msg->inject_touch_event.action_button,
                         (long) msg->inject_touch_event.buttons);
            } else {
                // numeric pointer id
                LOG_CMSG("touch [id=%" PRIu64_ "] %-4s position=%" PRIi32 ",%"
                             PRIi32 " pressure=%f action_button=%06lx"
                             " buttons=%06lx",
                         id,
                         MOTIONEVENT_ACTION_LABEL(action),
                         msg->inject_touch_event.position.point.x,
                         msg->inject_touch_event.position.point.y,
                         msg->inject_touch_event.pressure,
                         (long) msg->inject_touch_event.action_button,
                         (long) msg->inject_touch_event.buttons);
            }
            break;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT:
            LOG_CMSG("scroll position=%" PRIi32 ",%" PRIi32 " hscroll=%f"
                         " vscroll=%f buttons=%06lx",
                     msg->inject_scroll_event.position.point.x,
                     msg->inject_scroll_event.position.point.y,
                     msg->inject_scroll_event.hscroll,
                     msg->inject_scroll_event.vscroll,
                     (long) msg->inject_scroll_event.buttons);
            break;
        case SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
            LOG_CMSG("back-or-screen-on %s",
                     KEYEVENT_ACTION_LABEL(msg->inject_keycode.action));
            break;
        case SC_CONTROL_MSG_TYPE_GET_CLIPBOARD:
            LOG_CMSG("get clipboard copy_key=%s",
                     copy_key_labels[msg->get_clipboard.copy_key]);
            break;
        case SC_CONTROL_MSG_TYPE_SET_CLIPBOARD:
            LOG_CMSG("clipboard %" PRIu64_ " %s \"%s\"",
                     msg->set_clipboard.sequence,
                     msg->set_clipboard.paste ? "paste" : "nopaste",
                     msg->set_clipboard.text);
            break;
        case SC_CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE:
            LOG_CMSG("power mode %s",
                     SCREEN_POWER_MODE_LABEL(msg->set_screen_power_mode.mode));
            break;
        case SC_CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
            LOG_CMSG("expand notification panel");
            break;
        case SC_CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
            LOG_CMSG("expand settings panel");
            break;
        case SC_CONTROL_MSG_TYPE_COLLAPSE_PANELS:
            LOG_CMSG("collapse panels");
            break;
        case SC_CONTROL_MSG_TYPE_ROTATE_DEVICE:
            LOG_CMSG("rotate device");
            break;
        case SC_CONTROL_MSG_TYPE_UHID_CREATE: {
            // Quote only if name is not null
            const char *name = msg->uhid_create.name;
            const char *quote = name ? "\"" : "";
            LOG_CMSG("UHID create [%" PRIu16 "] name=%s%s%s "
                     "report_desc_size=%" PRIu16, msg->uhid_create.id,
                     quote, name, quote, msg->uhid_create.report_desc_size);
            break;
        }
        case SC_CONTROL_MSG_TYPE_UHID_INPUT: {
            char *hex = sc_str_to_hex_string(msg->uhid_input.data,
                                             msg->uhid_input.size);
            if (hex) {
                LOG_CMSG("UHID input [%" PRIu16 "] %s",
                         msg->uhid_input.id, hex);
                free(hex);
            } else {
                LOG_CMSG("UHID input [%" PRIu16 "] size=%" PRIu16,
                         msg->uhid_input.id, msg->uhid_input.size);
            }
            break;
        }
        case SC_CONTROL_MSG_TYPE_UHID_DESTROY:
            LOG_CMSG("UHID destroy [%" PRIu16 "]", msg->uhid_destroy.id);
            break;
        case SC_CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS:
            LOG_CMSG("open hard keyboard settings");
            break;
        default:
            LOG_CMSG("unknown type: %u", (unsigned) msg->type);
            break;
    }
}

void
sc_control_msg_log_human(const struct sc_control_msg *msg) {
    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_KEYCODE:
            if (msg->inject_keycode.action == AKEY_EVENT_ACTION_DOWN) {
                LOGI_USER_ACTION("send: keyDown code=%d",
                                 (int) msg->inject_keycode.keycode);
            } else if (msg->inject_keycode.action == AKEY_EVENT_ACTION_UP) {
                LOGI_USER_ACTION("send: keyUp code=%d",
                                 (int) msg->inject_keycode.keycode);
            } else {
                LOGI_USER_ACTION("send: keyMulti code=%d repeat=%" PRIu32,
                                 (int) msg->inject_keycode.keycode,
                                 msg->inject_keycode.repeat);
            }
            break;
        case SC_CONTROL_MSG_TYPE_INJECT_TEXT:
            LOGI_USER_ACTION("send: text \"%s\"", msg->inject_text.text);
            break;
        case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
            int32_t x = msg->inject_touch_event.position.point.x;
            int32_t y = msg->inject_touch_event.position.point.y;
            int action = msg->inject_touch_event.action
                       & AMOTION_EVENT_ACTION_MASK;

            if (is_mouse_pointer(msg->inject_touch_event.pointer_id)) {
                char buttons[32];
                write_buttons_human(buttons, sizeof(buttons),
                                    msg->inject_touch_event.buttons);

                switch (action) {
                    case AMOTION_EVENT_ACTION_HOVER_MOVE:
                        LOGI_USER_ACTION("send: mouseHover at (%" PRIi32
                                         ", %" PRIi32 ")", x, y);
                        break;
                    case AMOTION_EVENT_ACTION_MOVE:
                        if (msg->inject_touch_event.buttons) {
                            LOGI_USER_ACTION("send: mouseDrag at (%" PRIi32
                                             ", %" PRIi32 ") buttons=%s",
                                             x, y, buttons);
                        } else {
                            LOGI_USER_ACTION("send: mouseMove at (%" PRIi32
                                             ", %" PRIi32 ")", x, y);
                        }
                        break;
                    case AMOTION_EVENT_ACTION_DOWN:
                    case AMOTION_EVENT_ACTION_UP:
                    case AMOTION_EVENT_ACTION_BUTTON_PRESS:
                    case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
                        char action_button[32];
                        const char *button = get_mouse_action_button_name(
                            msg->inject_touch_event.action_button,
                            msg->inject_touch_event.buttons, action_button,
                            sizeof(action_button));
                        const char *suffix =
                            action == AMOTION_EVENT_ACTION_DOWN ? "ClickDown" :
                            action == AMOTION_EVENT_ACTION_UP ? "ClickUp" :
                            action == AMOTION_EVENT_ACTION_BUTTON_PRESS ?
                                "ButtonPress" :
                                "ButtonRelease";
                        LOGI_USER_ACTION("send: %s%s at (%" PRIi32 ", %" PRIi32
                                         ")", button, suffix, x, y);
                        break;
                    }
                    default:
                        LOGI_USER_ACTION("send: mouse%s at (%" PRIi32
                                         ", %" PRIi32 ") buttons=%s",
                                         get_touch_action_name_camel(action), x,
                                         y, buttons);
                        break;
                }
            } else if (is_finger_pointer(msg->inject_touch_event.pointer_id)) {
                const char *target_camel = get_touch_target_name_camel(
                    msg->inject_touch_event.pointer_id);
                switch (action) {
                    case AMOTION_EVENT_ACTION_DOWN:
                    case AMOTION_EVENT_ACTION_POINTER_DOWN:
                        LOGI_USER_ACTION("send: %sPress at (%" PRIi32 ", %"
                                         PRIi32 ") pressure=%f",
                                         target_camel, x, y,
                                         msg->inject_touch_event.pressure);
                        break;
                    case AMOTION_EVENT_ACTION_UP:
                    case AMOTION_EVENT_ACTION_POINTER_UP:
                        LOGI_USER_ACTION("send: %sRelease at (%" PRIi32 ", %"
                                         PRIi32 ") pressure=%f",
                                         target_camel, x, y,
                                         msg->inject_touch_event.pressure);
                        break;
                    case AMOTION_EVENT_ACTION_MOVE:
                        LOGI_USER_ACTION("send: %sMove at (%" PRIi32 ", %"
                                         PRIi32 ") pressure=%f",
                                         target_camel, x, y,
                                         msg->inject_touch_event.pressure);
                        break;
                    default:
                        LOGI_USER_ACTION("send: %s%s at (%" PRIi32 ", %"
                                         PRIi32 ") pressure=%f",
                                         target_camel,
                                         get_touch_action_name_camel(action), x,
                                         y,
                                         msg->inject_touch_event.pressure);
                        break;
                }
            } else {
                LOGI_USER_ACTION("send: pointer id=%" PRIu64_ " %s at (%"
                                 PRIi32 ", %" PRIi32 ") pressure=%f",
                                 msg->inject_touch_event.pointer_id,
                                 get_touch_action_name_camel(action), x, y,
                                 msg->inject_touch_event.pressure);
            }
            break;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT: {
            char buttons[32];
            write_buttons_human(buttons, sizeof(buttons),
                                msg->inject_scroll_event.buttons);
            LOGI_USER_ACTION("send: scroll at (%" PRIi32 ", %" PRIi32
                             ") h=%f v=%f buttons=%s",
                             msg->inject_scroll_event.position.point.x,
                             msg->inject_scroll_event.position.point.y,
                             msg->inject_scroll_event.hscroll,
                             msg->inject_scroll_event.vscroll,
                             buttons);
            break;
        }
        case SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
            LOGI_USER_ACTION("send: backOrScreenOn %s",
                             KEYEVENT_ACTION_LABEL(
                                 msg->back_or_screen_on.action));
            break;
        case SC_CONTROL_MSG_TYPE_GET_CLIPBOARD:
            LOGI_USER_ACTION("send: clipboardGet copyKey=%s",
                             copy_key_labels[msg->get_clipboard.copy_key]);
            break;
        case SC_CONTROL_MSG_TYPE_SET_CLIPBOARD:
            LOGI_USER_ACTION("send: clipboardSet sequence=%" PRIu64_
                             " %s \"%s\"",
                             msg->set_clipboard.sequence,
                             msg->set_clipboard.paste ? "paste" : "nopaste",
                             msg->set_clipboard.text);
            break;
        case SC_CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE:
            LOGI_USER_ACTION("send: displayPower %s",
                             SCREEN_POWER_MODE_LABEL(
                                 msg->set_screen_power_mode.mode));
            break;
        case SC_CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
            LOGI_USER_ACTION("send: expandNotificationPanel");
            break;
        case SC_CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
            LOGI_USER_ACTION("send: expandSettingsPanel");
            break;
        case SC_CONTROL_MSG_TYPE_COLLAPSE_PANELS:
            LOGI_USER_ACTION("send: collapsePanels");
            break;
        case SC_CONTROL_MSG_TYPE_ROTATE_DEVICE:
            LOGI_USER_ACTION("send: rotateDevice");
            break;
        case SC_CONTROL_MSG_TYPE_UHID_CREATE: {
            const char *name = msg->uhid_create.name ? msg->uhid_create.name
                                                     : "";
            LOGI_USER_ACTION("send: uhidCreate id=%" PRIu16 " name=\"%s\" "
                             "reportDescSize=%" PRIu16,
                             msg->uhid_create.id, name,
                             msg->uhid_create.report_desc_size);
            break;
        }
        case SC_CONTROL_MSG_TYPE_UHID_INPUT:
            LOGI_USER_ACTION("send: uhidInput id=%" PRIu16 " size=%" PRIu16,
                             msg->uhid_input.id, msg->uhid_input.size);
            break;
        case SC_CONTROL_MSG_TYPE_UHID_DESTROY:
            LOGI_USER_ACTION("send: uhidDestroy id=%" PRIu16,
                             msg->uhid_destroy.id);
            break;
        case SC_CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS:
            LOGI_USER_ACTION("send: openHardKeyboardSettings");
            break;
        default:
            LOGI_USER_ACTION("send: unknownType=%u", (unsigned) msg->type);
            break;
    }
}

bool
sc_control_msg_is_droppable(const struct sc_control_msg *msg) {
    // Cannot drop UHID_CREATE messages, because it would cause all further
    // UHID_INPUT messages for this device to be invalid.
    // Cannot drop UHID_DESTROY messages either, because a further UHID_CREATE
    // with the same id may fail.
    return msg->type != SC_CONTROL_MSG_TYPE_UHID_CREATE
        && msg->type != SC_CONTROL_MSG_TYPE_UHID_DESTROY;
}

void
sc_control_msg_destroy(struct sc_control_msg *msg) {
    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_TEXT:
            free(msg->inject_text.text);
            break;
        case SC_CONTROL_MSG_TYPE_SET_CLIPBOARD:
            free(msg->set_clipboard.text);
            break;
        default:
            // do nothing
            break;
    }
}
