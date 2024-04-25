#include "../lvgl/lvgl.h"
#include "../lvgl/drivers/display/fbdev.h"
#include "../lvgl/drivers/indev/evdev.h"
#include "ui/ui.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/reboot.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libgen.h>
#include "../common/common.h"
#include "../common/help.h"
#include "../common/options.h"
#include "../common/theme.h"
#include "../common/config.h"
#include "../common/glyph.h"
#include "../common/mini/mini.h"

static int js_fd;

int NAV_DPAD_HOR;
int NAV_ANLG_HOR;
int NAV_DPAD_VER;
int NAV_ANLG_VER;
int NAV_A;
int NAV_B;

int turbo_mode = 0;
int msgbox_active = 0;
int input_disable = 0;
int SD2_found = 0;
int nav_sound = 0;
int safe_quit = 0;
int bar_header = 0;
int bar_footer = 0;
char *osd_message;
struct mux_config config;

int nav_moved = 1;
char *current_wall = "";

lv_obj_t *labels[] = {NULL};
unsigned int label_count = sizeof(labels) / sizeof(labels[0]);

lv_obj_t *msgbox_element = NULL;

int progress_onscreen = -1;

typedef struct {
    char *name;
    char *value;
    lv_obj_t *widget;
} Option;

typedef struct {
    const char *name;
    const char *value;
} OptionSend;

Option *options = NULL;
int num_options = 0;

void show_help(lv_obj_t *element_focused) {
    char *message = "HELP NOT FOUND";

    if (element_focused == ui_lblClearFavourites) {
        message = MUXRESET_CLEARFAVOURITE;
    } else if (element_focused == ui_lblClearHistory) {
        message = MUXRESET_CLEARHISTORY;
    } else if (element_focused == ui_lblClearActivity) {
        message = MUXRESET_CLEARACTIVITY;
    } else if (element_focused == ui_lblClearConfigurations) {
        message = MUXRESET_CLEARCONFIG;
    } else if (element_focused == ui_lblClearCaches) {
        message = MUXRESET_CLEARCACHE;
    } else if (element_focused == ui_lblRestoreConfig) {
        message = MUXRESET_RESTORERA;
    } else if (element_focused == ui_lblRestoreNetwork) {
        message = MUXRESET_RESTORENET;
    } else if (element_focused == ui_lblRestorePortMaster) {
        message = MUXRESET_RESTOREPM;
    }

    show_help_msgbox(ui_pnlHelp, ui_lblHelpHeader, ui_lblHelpContent, lv_label_get_text(element_focused), message);
}

lv_group_t *ui_group;
lv_group_t *ui_group_glyph;

void init_navigation_groups() {
    lv_obj_t *ui_objects[] = {
            ui_lblClearFavourites,
            ui_lblClearHistory,
            ui_lblClearActivity,
            ui_lblClearConfigurations,
            ui_lblClearCaches,
            ui_lblRestoreConfig,
            ui_lblRestoreNetwork,
            ui_lblRestorePortMaster
    };

    lv_obj_t *ui_objects_icon[] = {
            ui_icoClearFavourites,
            ui_icoClearHistory,
            ui_icoClearActivity,
            ui_icoClearConfigurations,
            ui_icoClearCaches,
            ui_icoRestoreConfig,
            ui_icoRestoreNetwork,
            ui_icoRestorePortMaster
    };

    ui_group = lv_group_create();
    ui_group_glyph = lv_group_create();

    for (unsigned int i = 0; i < sizeof(ui_objects) / sizeof(ui_objects[0]); i++) {
        lv_group_add_obj(ui_group, ui_objects[i]);
    }

    for (unsigned int i = 0; i < sizeof(ui_objects_icon) / sizeof(ui_objects_icon[0]); i++) {
        lv_group_add_obj(ui_group_glyph, ui_objects_icon[i]);
    }
}

void *joystick_task() {
    struct input_event ev;
    int epoll_fd;
    struct epoll_event event, events[MAX_EVENTS];

    int JOYHOTKEY_pressed = 0;

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("Error creating EPOLL instance");
        return NULL;
    }

    event.events = EPOLLIN;
    event.data.fd = js_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, js_fd, &event) == -1) {
        perror("Error with EPOLL controller");
        return NULL;
    }

    while (1) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 64);
        if (num_events == -1) {
            perror("Error with EPOLL wait event timer");
            continue;
        }

        for (int i = 0; i < num_events; i++) {
            if (events[i].data.fd == js_fd) {
                int ret = read(js_fd, &ev, sizeof(struct input_event));
                if (ret == -1) {
                    perror("Error reading input");
                    continue;
                }

                struct _lv_obj_t *element_focused = lv_group_get_focused(ui_group);
                switch (ev.type) {
                    case EV_KEY:
                        if (ev.value == 1) {
                            if (msgbox_active) {
                                if (ev.code == NAV_B || ev.code == JOY_MENU) {
                                    play_sound("confirm", nav_sound);
                                    msgbox_active = 0;
                                    progress_onscreen = 0;
                                    lv_obj_add_flag(msgbox_element, LV_OBJ_FLAG_HIDDEN);
                                }
                            } else {
                                if (ev.code == JOY_MENU) {
                                    JOYHOTKEY_pressed = 1;
                                } else if (ev.code == JOY_X) {
                                    if (element_focused == ui_lblClearFavourites) {
                                        play_sound("confirm", nav_sound);

                                        char favourites[MAX_BUFFER_SIZE];
                                        snprintf(favourites, sizeof(favourites), "/%s/favourites", MUOS_INFO_PATH);
                                        delete_files_of_type(favourites, "cfg");

                                        osd_message = "Favourites Cleared";
                                    } else if (element_focused == ui_lblClearHistory) {
                                        play_sound("confirm", nav_sound);

                                        char history[MAX_BUFFER_SIZE];
                                        snprintf(history, sizeof(history), "/%s/history", MUOS_INFO_PATH);
                                        delete_files_of_type(history, "cfg");

                                        osd_message = "History Cleared";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);
                                    } else if (element_focused == ui_lblClearActivity) {
                                        play_sound("confirm", nav_sound);

                                        char activity[MAX_BUFFER_SIZE];
                                        snprintf(activity, sizeof(activity), "/%s/activity", MUOS_INFO_PATH);
                                        delete_files_of_type(activity, "act");

                                        osd_message = "Activity Cleared";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);
                                    } else if (element_focused == ui_lblClearConfigurations) {
                                        play_sound("confirm", nav_sound);

                                        delete_files_of_type(MUOS_CORE_DIR, "cfg");

                                        osd_message = "Configurations Cleared";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);
                                    } else if (element_focused == ui_lblClearCaches) {
                                        play_sound("confirm", nav_sound);

                                        delete_files_of_type(MUOS_CACHE_DIR, "json");

                                        osd_message = "Caches Cleared";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);
                                    } else if (element_focused == ui_lblRestoreConfig) {
                                        play_sound("confirm", nav_sound);

                                        system("cp -f /opt/muos/backup/retroarch.cfg /mnt/mmc/MUOS/.retroarch/retroarch.cfg");

                                        osd_message = "RetroArch Configuration Restored";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);
                                    } else if (element_focused == ui_lblRestoreNetwork) {
                                        play_sound("confirm", nav_sound);

                                        mini_t * muos_config = mini_try_load(MUOS_CONFIG_FILE);

                                        set_ini_int(muos_config, "network", "enabled", 0);
                                        set_ini_string(muos_config, "network", "interface", "wlan0");
                                        set_ini_int(muos_config, "network", "type", 0);
                                        set_ini_string(muos_config, "network", "ssid", "");
                                        set_ini_string(muos_config, "network", "address", "192.168.0.123");
                                        set_ini_string(muos_config, "network", "gateway", "192.168.0.1");
                                        set_ini_int(muos_config, "network", "subnet", 24);
                                        set_ini_string(muos_config, "network", "dns", "1.1.1.1");

                                        mini_save(muos_config, MINI_FLAGS_SKIP_EMPTY_GROUPS);
                                        mini_free(muos_config);

                                        system("/opt/muos/script/system/network.sh");

                                        osd_message = "Network Configuration Restored";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);
                                    } else if (element_focused == ui_lblRestorePortMaster) {
                                        play_sound("confirm", nav_sound);

                                        osd_message = "Restoring PortMaster";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);

                                        // TODO: Add PortMaster archival restore here

                                        osd_message = "PortMaster Restored";
                                        lv_label_set_text(ui_lblMessage, osd_message);
                                        lv_obj_clear_flag(ui_pnlMessage, LV_OBJ_FLAG_HIDDEN);
                                    }
                                } else if (ev.code == NAV_B) {
                                    play_sound("back", nav_sound);
                                    safe_quit = 1;
                                }
                            }
                        } else {
                            if (ev.code == JOY_MENU) {
                                JOYHOTKEY_pressed = 0;
                                if (progress_onscreen == -1) {
                                    play_sound("confirm", nav_sound);
                                    show_help(element_focused);
                                }
                            }
                        }
                    case EV_ABS:
                        if (msgbox_active) {
                            break;
                        }
                        if (ev.code == NAV_DPAD_VER || ev.code == NAV_ANLG_VER) {
                            switch (ev.value) {
                                case -4096:
                                case -1:
                                    nav_prev(ui_group, 1);
                                    nav_prev(ui_group_glyph, 1);
                                    play_sound("navigate", nav_sound);
                                    nav_moved = 1;
                                    break;
                                case 1:
                                case 4096:
                                    nav_next(ui_group, 1);
                                    nav_next(ui_group_glyph, 1);
                                    play_sound("navigate", nav_sound);
                                    nav_moved = 1;
                                    break;
                                default:
                                    break;
                            }
                        }
                    default:
                        break;
                }
            }
        }

        if (ev.type == EV_KEY && ev.value == 1 && (ev.code == JOY_MINUS || ev.code == JOY_PLUS)) {
            progress_onscreen = 1;
            if (lv_obj_has_flag(ui_pnlProgress, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_clear_flag(ui_pnlProgress, LV_OBJ_FLAG_HIDDEN);
            }
            if (JOYHOTKEY_pressed) {
                lv_label_set_text(ui_icoProgress, "\uF185");
                lv_bar_set_value(ui_barProgress, get_brightness_percentage(get_brightness()), LV_ANIM_OFF);
            } else {
                int volume = get_volume_percentage();
                switch (volume) {
                    case 0:
                        lv_label_set_text(ui_icoProgress, "\uF6A9");
                        break;
                    case 1 ... 46:
                        lv_label_set_text(ui_icoProgress, "\uF026");
                        break;
                    case 47 ... 71:
                        lv_label_set_text(ui_icoProgress, "\uF027");
                        break;
                    case 72 ... 100:
                        lv_label_set_text(ui_icoProgress, "\uF028");
                        break;
                }
                lv_bar_set_value(ui_barProgress, volume, LV_ANIM_OFF);
            }
        }

        lv_task_handler();
        usleep(SCREEN_WAIT);
    }
}

void init_elements() {
    lv_obj_move_foreground(ui_pnlFooter);
    lv_obj_move_foreground(ui_pnlHeader);
    lv_obj_move_foreground(ui_pnlHelp);
    lv_obj_move_foreground(ui_pnlProgress);

    if (bar_footer) {
        lv_obj_set_style_bg_opa(ui_pnlFooter, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (bar_header) {
        lv_obj_set_style_bg_opa(ui_pnlHeader, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    process_visual_element(CLOCK, ui_lblDatetime);
    process_visual_element(BLUETOOTH, ui_staBluetooth);
    process_visual_element(NETWORK, ui_staNetwork);
    process_visual_element(BATTERY, ui_staCapacity);

    lv_label_set_text(ui_lblMessage, osd_message);

    lv_label_set_text(ui_lblNavX, "Confirm");
    lv_label_set_text(ui_lblNavB, "Back");
    lv_label_set_text(ui_lblNavMenu, "Help");

    lv_obj_t *nav_hide[] = {
            ui_lblNavAGlyph,
            ui_lblNavA,
            ui_lblNavCGlyph,
            ui_lblNavC,
            ui_lblNavYGlyph,
            ui_lblNavY,
            ui_lblNavZGlyph,
            ui_lblNavZ
    };

    for (int i = 0; i < sizeof(nav_hide) / sizeof(nav_hide[0]); i++) {
        lv_obj_add_flag(nav_hide[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_user_data(ui_lblClearFavourites, "favourite");
    lv_obj_set_user_data(ui_lblClearHistory, "history");
    lv_obj_set_user_data(ui_lblClearActivity, "activity");
    lv_obj_set_user_data(ui_lblClearConfigurations, "config");
    lv_obj_set_user_data(ui_lblClearCaches, "cache");
    lv_obj_set_user_data(ui_lblRestoreConfig, "retroarch");
    lv_obj_set_user_data(ui_lblRestoreNetwork, "network");
    lv_obj_set_user_data(ui_lblRestorePortMaster, "portmaster");
}

void glyph_task() {
    // TODO: Bluetooth connectivity!

/*
    if (is_network_connected() > 0) {
        lv_obj_set_style_text_color(ui_staNetwork, lv_color_hex(theme.NETWORK.ACTIVE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(ui_staNetwork, theme.NETWORK.ACTIVE_ALPHA, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else {
        lv_obj_set_style_text_color(ui_staNetwork, lv_color_hex(theme.NETWORK.NORMAL), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(ui_staNetwork, theme.NETWORK.NORMAL_ALPHA, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
*/

    if (atoi(read_text_from_file(BATT_CHARGER))) {
        lv_obj_set_style_text_color(ui_staCapacity, lv_color_hex(theme.BATTERY.ACTIVE),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(ui_staCapacity, theme.BATTERY.ACTIVE_ALPHA, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (read_battery_capacity() <= 15) {
        lv_obj_set_style_text_color(ui_staCapacity, lv_color_hex(theme.BATTERY.LOW), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(ui_staCapacity, theme.BATTERY.LOW_ALPHA, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_text_color(ui_staCapacity, lv_color_hex(theme.BATTERY.NORMAL),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(ui_staCapacity, theme.BATTERY.NORMAL_ALPHA, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (progress_onscreen > 0) {
        progress_onscreen -= 1;
    } else {
        if (!lv_obj_has_flag(ui_pnlProgress, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(ui_pnlProgress, LV_OBJ_FLAG_HIDDEN);
        }
        if (!msgbox_active) {
            progress_onscreen = -1;
        }
    }
}

void ui_refresh_task() {
    if (nav_moved) {
        if (lv_group_get_obj_count(ui_group) > 0) {
            static char old_wall[MAX_BUFFER_SIZE];
            static char new_wall[MAX_BUFFER_SIZE];

            snprintf(old_wall, sizeof(old_wall), "%s", current_wall);
            snprintf(new_wall, sizeof(new_wall), "%s", load_wallpaper(
                    ui_scrReset, ui_group, theme.MISC.ANIMATED_BACKGROUND));

            if (strcmp(new_wall, old_wall) != 0) {
                strcpy(current_wall, new_wall);
                if (strlen(new_wall) > 3) {
                    printf("LOADING WALLPAPER: %s\n", new_wall);
                    if (theme.MISC.ANIMATED_BACKGROUND) {
                        lv_obj_t * img = lv_gif_create(ui_pnlWall);
                        lv_gif_set_src(img, new_wall);
                    } else {
                        lv_img_set_src(ui_imgWall, new_wall);
                    }
                    lv_obj_invalidate(ui_pnlWall);
                } else {
                    lv_img_set_src(ui_imgWall, &ui_img_nothing_png);
                }
            }

            static char static_image[MAX_BUFFER_SIZE];
            snprintf(static_image, sizeof(static_image), "%s",
                     load_static_image(ui_scrReset, ui_group));

            if (strlen(static_image) > 0) {
                printf("LOADING STATIC IMAGE: %s\n", static_image);

                switch (theme.MISC.STATIC_ALIGNMENT) {
                    case 0: // Bottom + Front
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_BOTTOM_RIGHT);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                    case 1: // Middle + Front
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_RIGHT_MID);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                    case 2: // Top + Front
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_TOP_RIGHT);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                    case 3: // Fullscreen + Behind
                        lv_obj_set_height(ui_pnlBox, SCREEN_HEIGHT);
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_BOTTOM_RIGHT);
                        lv_obj_move_background(ui_pnlBox);
                        lv_obj_move_background(ui_pnlWall);
                        break;
                    case 4: // Fullscreen + Front
                        lv_obj_set_height(ui_pnlBox, SCREEN_HEIGHT);
                        lv_obj_set_align(ui_imgBox, LV_ALIGN_BOTTOM_RIGHT);
                        lv_obj_move_foreground(ui_pnlBox);
                        break;
                }

                lv_img_set_src(ui_imgBox, static_image);
            } else {
                lv_img_set_src(ui_imgBox, &ui_img_nothing_png);
            }
        }
        lv_obj_invalidate(ui_pnlContent);
        lv_task_handler();
        nav_moved = 0;
    }
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/system/bin", 1);
    setenv("NO_COLOR", "1", 1);

    lv_init();
    fbdev_init();

    static lv_color_t buf1[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    lv_disp_drv_register(&disp_drv);

    load_config(&config);

    ui_init();
    init_elements();

    lv_obj_set_user_data(ui_scrReset, basename(argv[0]));

    lv_label_set_text(ui_lblDatetime, get_datetime());
    lv_label_set_text(ui_staCapacity, get_capacity());

    load_theme(&theme, basename(argv[0]));
    apply_theme();

    switch (theme.MISC.NAVIGATION_TYPE) {
        case 1:
            NAV_DPAD_HOR = ABS_HAT0Y;
            NAV_ANLG_HOR = ABS_RX;
            NAV_DPAD_VER = ABS_HAT0X;
            NAV_ANLG_VER = ABS_Z;
            break;
        default:
            NAV_DPAD_HOR = ABS_HAT0X;
            NAV_ANLG_HOR = ABS_Z;
            NAV_DPAD_VER = ABS_HAT0Y;
            NAV_ANLG_VER = ABS_RX;
    }

    switch (config.SETTINGS.ADVANCED.SWAP) {
        case 1:
            NAV_A = JOY_B;
            NAV_B = JOY_A;
            lv_label_set_text(ui_lblNavAGlyph, "\u21D2");
            lv_label_set_text(ui_lblNavBGlyph, "\u21D3");
            break;
        default:
            NAV_A = JOY_A;
            NAV_B = JOY_B;
            lv_label_set_text(ui_lblNavAGlyph, "\u21D3");
            lv_label_set_text(ui_lblNavBGlyph, "\u21D2");
            break;
    }

    current_wall = load_wallpaper(ui_scrReset, NULL, theme.MISC.ANIMATED_BACKGROUND);
    if (strlen(current_wall) > 3) {
        if (theme.MISC.ANIMATED_BACKGROUND) {
            lv_obj_t * img = lv_gif_create(ui_pnlWall);
            lv_gif_set_src(img, current_wall);
        } else {
            lv_img_set_src(ui_imgWall, current_wall);
        }
    } else {
        lv_img_set_src(ui_imgWall, &ui_img_nothing_png);
    }

    load_font_text(basename(argv[0]), ui_scrReset);

    if (config.SETTINGS.GENERAL.SOUND == 2) {
        nav_sound = 1;
    }

    init_navigation_groups();

    struct dt_task_param dt_par;
    struct bat_task_param bat_par;
    struct osd_task_param osd_par;

    dt_par.lblDatetime = ui_lblDatetime;
    bat_par.staCapacity = ui_staCapacity;
    osd_par.lblMessage = ui_lblMessage;
    osd_par.pnlMessage = ui_pnlMessage;
    osd_par.count = 0;

    js_fd = open(JOY_DEVICE, O_RDONLY);
    if (js_fd < 0) {
        perror("Failed to open joystick device");
        return 1;
    }

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = evdev_read;
    indev_drv.user_data = (void *) (intptr_t) js_fd;

    lv_indev_drv_register(&indev_drv);

    lv_timer_t *datetime_timer = lv_timer_create(datetime_task, UINT16_MAX / 2, &dt_par);
    lv_timer_ready(datetime_timer);

    lv_timer_t *capacity_timer = lv_timer_create(capacity_task, UINT16_MAX / 2, &bat_par);
    lv_timer_ready(capacity_timer);

    lv_timer_t *osd_timer = lv_timer_create(osd_task, UINT16_MAX / 32, &osd_par);
    lv_timer_ready(osd_timer);

    lv_timer_t *glyph_timer = lv_timer_create(glyph_task, UINT16_MAX / 64, NULL);
    lv_timer_ready(glyph_timer);

    lv_timer_t *ui_refresh_timer = lv_timer_create(ui_refresh_task, UINT8_MAX / 4, NULL);
    lv_timer_ready(ui_refresh_timer);

    pthread_t joystick_thread;
    pthread_create(&joystick_thread, NULL, (void *(*)(void *)) joystick_task, NULL);

    init_elements();
    while (!safe_quit) {
        usleep(SCREEN_WAIT);
    }

    pthread_cancel(joystick_thread);

    close(js_fd);

    return 0;
}

uint32_t mux_tick(void) {
    static uint64_t start_ms = 0;

    if (start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}