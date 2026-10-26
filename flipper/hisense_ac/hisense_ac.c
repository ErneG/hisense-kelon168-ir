#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <infrared_transmit.h>

#include "hisense_codec.h"

#define STATE_FILE APP_DATA_PATH("state.bin")
#define STATE_MAGIC 0x48534143UL
#define STATE_VERSION 1

typedef enum {
    ItemLast,
    ItemPower,
    ItemMode,
    ItemTemp,
    ItemFan,
    ItemTurbo,
    ItemEconomy,
    ItemSleep,
    ItemVSwing,
    ItemHSwing,
    ItemDisplay,
    ItemResend,
    ItemCount,
} ItemId;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t power;
    uint8_t mode;
    uint8_t temp;
    uint8_t fan;
    uint8_t sleep;
    uint8_t turbo;
    uint8_t economy;
    uint8_t fan_stash_valid;
    uint8_t fan_stash;
    uint8_t temp_stash_valid;
    uint8_t temp_stash;
} SavedState;

typedef struct HisenseApp HisenseApp;

typedef struct {
    HisenseApp* app;
    ItemId id;
} ItemCtx;

struct HisenseApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    VariableItemList* list;
    NotificationApp* notifications;
    Storage* storage;
    VariableItem* items[ItemCount];
    ItemCtx ctx[ItemCount];
    hs_state_t state;
    bool fan_stash_valid;
    hs_fan_t fan_stash;
    bool temp_stash_valid;
    uint8_t temp_stash;
    uint32_t timings[HS_TIMINGS_COUNT];
    char temp_text[8];
    char last_text[24];
};

static const char* const mode_names[] = {"Heat", "Cool", "Dry", "Fan"};
static const hs_mode_t mode_values[] = {HsModeHeat, HsModeCool, HsModeDry, HsModeFan};
static const char* const fan_names[] = {"Auto", "High", "Mid-High", "Mid", "Mid-Low", "Low"};
static const char* const onoff_names[] = {"Off", "On"};

static const char* command_name(uint8_t cmd) {
    switch(cmd) {
    case HsCmdDisplay:
        return "Display";
    case HsCmdPower:
        return "Power";
    case HsCmdTemp:
        return "Temp";
    case HsCmdSleep:
        return "Sleep";
    case HsCmdTurbo:
        return "Turbo";
    case HsCmdMode:
        return "Mode";
    case HsCmdVSwing:
        return "V-Swing";
    case HsCmdHSwing:
        return "H-Swing";
    case HsCmdEconomy:
        return "Economy";
    case HsCmdFan:
        return "Fan";
    default:
        return "?";
    }
}

static uint8_t mode_index(hs_mode_t m) {
    for(uint8_t i = 0; i < COUNT_OF(mode_values); i++) {
        if(mode_values[i] == m) return i;
    }
    return 0;
}

static void state_save(HisenseApp* app) {
    SavedState sv = {
        .magic = STATE_MAGIC,
        .version = STATE_VERSION,
        .power = app->state.power,
        .mode = app->state.mode,
        .temp = app->state.temp,
        .fan = app->state.fan,
        .sleep = app->state.sleep,
        .turbo = app->state.turbo,
        .economy = app->state.economy,
        .fan_stash_valid = app->fan_stash_valid,
        .fan_stash = app->fan_stash,
        .temp_stash_valid = app->temp_stash_valid,
        .temp_stash = app->temp_stash,
    };
    storage_common_mkdir(app->storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, STATE_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, &sv, sizeof(sv));
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void state_load(HisenseApp* app) {
    hs_state_default(&app->state);
    app->fan_stash_valid = false;
    app->temp_stash_valid = false;
    SavedState sv;
    File* file = storage_file_alloc(app->storage);
    bool ok = storage_file_open(file, STATE_FILE, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_read(file, &sv, sizeof(sv)) == sizeof(sv) &&
              sv.magic == STATE_MAGIC && sv.version == STATE_VERSION;
    storage_file_close(file);
    storage_file_free(file);
    if(!ok) return;

    hs_state_t s = app->state;
    s.power = sv.power != 0;
    s.mode = (hs_mode_t)sv.mode;
    s.temp = sv.temp;
    s.fan = (hs_fan_t)sv.fan;
    s.sleep = sv.sleep != 0;
    s.turbo = sv.turbo != 0;
    s.economy = sv.economy != 0;
    if(!hs_state_valid(&s)) return;
    app->state = s;
    if(sv.fan_stash_valid && sv.fan_stash <= (uint8_t)HsFanLow) {
        app->fan_stash_valid = true;
        app->fan_stash = (hs_fan_t)sv.fan_stash;
    }
    if(sv.temp_stash_valid && sv.temp_stash >= 16 && sv.temp_stash <= 30) {
        app->temp_stash_valid = true;
        app->temp_stash = sv.temp_stash;
    }
}

static void refresh_items(HisenseApp* app) {
    const hs_state_t* s = &app->state;
    variable_item_set_current_value_index(app->items[ItemPower], s->power ? 1 : 0);
    variable_item_set_current_value_text(app->items[ItemPower], onoff_names[s->power ? 1 : 0]);
    uint8_t mi = mode_index(s->mode);
    variable_item_set_current_value_index(app->items[ItemMode], mi);
    variable_item_set_current_value_text(app->items[ItemMode], mode_names[mi]);
    variable_item_set_current_value_index(app->items[ItemTemp], s->temp - 16);
    snprintf(app->temp_text, sizeof(app->temp_text), "%u C", s->temp);
    variable_item_set_current_value_text(app->items[ItemTemp], app->temp_text);
    variable_item_set_current_value_index(app->items[ItemFan], (uint8_t)s->fan);
    variable_item_set_current_value_text(app->items[ItemFan], fan_names[s->fan]);
    variable_item_set_current_value_index(app->items[ItemTurbo], s->turbo ? 1 : 0);
    variable_item_set_current_value_text(app->items[ItemTurbo], onoff_names[s->turbo ? 1 : 0]);
    variable_item_set_current_value_index(app->items[ItemEconomy], s->economy ? 1 : 0);
    variable_item_set_current_value_text(
        app->items[ItemEconomy], onoff_names[s->economy ? 1 : 0]);
    variable_item_set_current_value_index(app->items[ItemSleep], s->sleep ? 1 : 0);
    variable_item_set_current_value_text(app->items[ItemSleep], onoff_names[s->sleep ? 1 : 0]);
    variable_item_set_current_value_text(app->items[ItemLast], app->last_text);
}

static void send_command(HisenseApp* app, uint8_t cmd) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    app->state.clock_h = dt.hour;
    app->state.clock_m = dt.minute;

    uint8_t frame[HS_FRAME_BYTES];
    if(!hs_build_frame(&app->state, cmd, frame)) {
        snprintf(app->last_text, sizeof(app->last_text), "Bad state");
        refresh_items(app);
        return;
    }
    size_t n = hs_frame_to_timings(frame, app->timings, COUNT_OF(app->timings));
    if(n == 0) return;

    notification_message(app->notifications, &sequence_blink_blue_100);
    infrared_send_raw_ext(
        app->timings, (uint32_t)n, true, HS_CARRIER_HZ, (float)HS_DUTY_PERCENT / 100.0f);

    snprintf(
        app->last_text,
        sizeof(app->last_text),
        "%s %02u:%02u",
        command_name(cmd),
        app->state.clock_h,
        app->state.clock_m);
    state_save(app);
    refresh_items(app);
}

static void apply_sleep_economy_change(HisenseApp* app, bool turned_on) {
    hs_state_t* s = &app->state;
    if(turned_on) {
        if(!app->fan_stash_valid) {
            app->fan_stash = s->fan;
            app->fan_stash_valid = true;
        }
        s->fan = HsFanLow;
    } else if(!s->sleep && !s->economy && app->fan_stash_valid) {
        s->fan = app->fan_stash;
        app->fan_stash_valid = false;
    }
}

static void apply_turbo_change(HisenseApp* app, bool turned_on) {
    hs_state_t* s = &app->state;
    if(turned_on) {
        if(s->mode == HsModeHeat) {
            if(!app->temp_stash_valid) {
                app->temp_stash = s->temp;
                app->temp_stash_valid = true;
            }
            s->temp = 30;
        }
    } else if(app->temp_stash_valid) {
        s->temp = app->temp_stash;
        app->temp_stash_valid = false;
    }
}

static void item_changed(VariableItem* item) {
    ItemCtx* ctx = variable_item_get_context(item);
    HisenseApp* app = ctx->app;
    hs_state_t* s = &app->state;
    uint8_t idx = variable_item_get_current_value_index(item);

    switch(ctx->id) {
    case ItemPower:
        s->power = idx != 0;
        send_command(app, HsCmdPower);
        break;
    case ItemMode:
        s->mode = mode_values[idx];
        send_command(app, HsCmdMode);
        break;
    case ItemTemp:
        s->temp = 16 + idx;
        app->temp_stash_valid = false;
        send_command(app, HsCmdTemp);
        break;
    case ItemFan:
        s->fan = (hs_fan_t)idx;
        app->fan_stash_valid = false;
        send_command(app, HsCmdFan);
        break;
    case ItemTurbo:
        s->turbo = idx != 0;
        apply_turbo_change(app, s->turbo);
        send_command(app, HsCmdTurbo);
        break;
    case ItemEconomy:
        s->economy = idx != 0;
        apply_sleep_economy_change(app, s->economy);
        send_command(app, HsCmdEconomy);
        break;
    case ItemSleep:
        s->sleep = idx != 0;
        apply_sleep_economy_change(app, s->sleep);
        send_command(app, HsCmdSleep);
        break;
    default:
        break;
    }
}

static void enter_callback(void* context, uint32_t index) {
    HisenseApp* app = context;
    switch(index) {
    case ItemVSwing:
        send_command(app, HsCmdVSwing);
        break;
    case ItemHSwing:
        send_command(app, HsCmdHSwing);
        break;
    case ItemDisplay:
        send_command(app, HsCmdDisplay);
        break;
    case ItemResend:
        send_command(app, HsCmdTemp);
        break;
    default:
        break;
    }
}

static uint32_t exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static VariableItem* add_item(
    HisenseApp* app,
    ItemId id,
    const char* label,
    uint8_t count,
    bool has_change) {
    app->ctx[id].app = app;
    app->ctx[id].id = id;
    VariableItem* item = variable_item_list_add(
        app->list, label, count, has_change ? item_changed : NULL, &app->ctx[id]);
    app->items[id] = item;
    return item;
}

static HisenseApp* app_alloc(void) {
    HisenseApp* app = malloc(sizeof(HisenseApp));
    memset(app, 0, sizeof(HisenseApp));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);
    state_load(app);
    snprintf(app->last_text, sizeof(app->last_text), "none");

    app->view_dispatcher = view_dispatcher_alloc();
    app->list = variable_item_list_alloc();
    View* view = variable_item_list_get_view(app->list);
    view_set_previous_callback(view, exit_callback);

    add_item(app, ItemLast, "Last sent", 1, false);
    add_item(app, ItemPower, "Power", 2, true);
    add_item(app, ItemMode, "Mode", COUNT_OF(mode_names), true);
    add_item(app, ItemTemp, "Temp", 15, true);
    add_item(app, ItemFan, "Fan", COUNT_OF(fan_names), true);
    add_item(app, ItemTurbo, "Turbo", 2, true);
    add_item(app, ItemEconomy, "Economy", 2, true);
    add_item(app, ItemSleep, "Sleep", 2, true);
    VariableItem* item = add_item(app, ItemVSwing, "V-Swing", 1, false);
    variable_item_set_current_value_text(item, "OK");
    item = add_item(app, ItemHSwing, "H-Swing", 1, false);
    variable_item_set_current_value_text(item, "OK");
    item = add_item(app, ItemDisplay, "Display", 1, false);
    variable_item_set_current_value_text(item, "OK");
    item = add_item(app, ItemResend, "Resend state", 1, false);
    variable_item_set_current_value_text(item, "OK");
    variable_item_list_set_enter_callback(app->list, enter_callback, app);
    refresh_items(app);

    view_dispatcher_add_view(app->view_dispatcher, 0, view);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);
    return app;
}

static void app_free(HisenseApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, 0);
    variable_item_list_free(app->list);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t hisense_ac_app(void* p) {
    UNUSED(p);
    HisenseApp* app = app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    app_free(app);
    return 0;
}
