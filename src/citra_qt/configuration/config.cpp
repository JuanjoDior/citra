// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QSettings>
#include "citra_qt/configuration/config.h"
#include "citra_qt/ui_settings.h"
#include "common/file_util.h"
#include "input_common/main.h"
#include "input_common/udp/client.h"
#include "network/network.h"

Config::Config() {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    qt_config_loc = FileUtil::GetUserPath(D_CONFIG_IDX) + "qt-config.ini";
    FileUtil::CreateFullPath(qt_config_loc);
    qt_config = new QSettings(QString::fromStdString(qt_config_loc), QSettings::IniFormat);

    Reload();
}

const std::array<int, Settings::NativeButton::NumButtons> Config::default_buttons = {
    Qt::Key_A, Qt::Key_S, Qt::Key_Z, Qt::Key_X, Qt::Key_T, Qt::Key_G, Qt::Key_F, Qt::Key_H,
    Qt::Key_Q, Qt::Key_W, Qt::Key_M, Qt::Key_N, Qt::Key_1, Qt::Key_2, Qt::Key_B,
};

const std::array<std::array<int, 5>, Settings::NativeAnalog::NumAnalogs> Config::default_analogs{{
    {
        Qt::Key_Up,
        Qt::Key_Down,
        Qt::Key_Left,
        Qt::Key_Right,
        Qt::Key_D,
    },
    {
        Qt::Key_I,
        Qt::Key_K,
        Qt::Key_J,
        Qt::Key_L,
        Qt::Key_D,
    },
}};

void Config::ReadValues() {
    qt_config->beginGroup("ControlPanel");
    Settings::values.sp_enable_3d = qt_config->value("sp_enable_3d", false).toBool();
    Settings::values.p_adapter_connected = qt_config->value("p_adapter_connected", true).toBool();
    Settings::values.p_battery_charging = qt_config->value("p_battery_charging", true).toBool();
    Settings::values.p_battery_level =
        static_cast<u32>(qt_config->value("p_battery_level", 5).toInt());
    Settings::values.n_wifi_status = static_cast<u32>(qt_config->value("n_wifi_status", 0).toInt());
    Settings::values.n_wifi_link_level =
        static_cast<u8>(qt_config->value("n_wifi_link_level", 0).toInt());
    Settings::values.n_state = static_cast<u8>(qt_config->value("n_state", 0).toInt());
    qt_config->endGroup();

    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        Settings::values.buttons[i] =
            qt_config
                ->value(Settings::NativeButton::mapping[i], QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (Settings::values.buttons[i].empty())
            Settings::values.buttons[i] = default_param;
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_analogs[i][4], 0.5f);
        Settings::values.analogs[i] =
            qt_config
                ->value(Settings::NativeAnalog::mapping[i], QString::fromStdString(default_param))
                .toString()
                .toStdString();
        if (Settings::values.analogs[i].empty())
            Settings::values.analogs[i] = default_param;
    }

    Settings::values.motion_device =
        qt_config
            ->value("motion_device",
                    "engine:motion_emu,update_period:100,sensitivity:0.01,tilt_clamp:90.0")
            .toString()
            .toStdString();
    Settings::values.touch_device =
        qt_config->value("touch_device", "engine:emu_window").toString().toStdString();

    Settings::values.udp_input_address =
        qt_config->value("udp_input_address", InputCommon::CemuhookUDP::DEFAULT_ADDR)
            .toString()
            .toStdString();
    Settings::values.udp_input_port = static_cast<u16>(
        qt_config->value("udp_input_port", InputCommon::CemuhookUDP::DEFAULT_PORT).toInt());
    Settings::values.udp_pad_index = static_cast<u8>(qt_config->value("udp_pad_index", 0).toUInt());
    qt_config->endGroup();

    qt_config->beginGroup("Core");
    Settings::values.use_cpu_jit = qt_config->value("use_cpu_jit", true).toBool();
    Settings::values.keyboard_mode =
        static_cast<Settings::KeyboardMode>(qt_config->value("keyboard_mode", 1).toInt());
    qt_config->endGroup();

    qt_config->beginGroup("LLE");
    for (const auto& service_module : Service::service_module_map) {
        bool use_lle{qt_config->value(QString::fromStdString(service_module.name), false).toBool()};
        Settings::values.lle_modules.emplace(service_module.name, use_lle);
    }
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    Settings::values.use_hw_renderer = qt_config->value("use_hw_renderer", true).toBool();
#ifdef __APPLE__
    // Hardware shader is broken on macos thanks to poor drivers.
    // We still want to provide this option for test/development purposes, but disable it by
    // default.
    Settings::values.use_hw_shader = qt_config->value("use_hw_shader", false).toBool();
#else
    Settings::values.use_hw_shader = qt_config->value("use_hw_shader", true).toBool();
#endif
    Settings::values.shaders_accurate_gs = qt_config->value("shaders_accurate_gs", true).toBool();
    Settings::values.shaders_accurate_mul =
        qt_config->value("shaders_accurate_mul", false).toBool();
    Settings::values.use_shader_jit = qt_config->value("use_shader_jit", true).toBool();
    Settings::values.resolution_factor =
        static_cast<u16>(qt_config->value("resolution_factor", 1).toInt());
    Settings::values.use_vsync = qt_config->value("use_vsync", false).toBool();
    Settings::values.use_frame_limit = qt_config->value("use_frame_limit", true).toBool();
    Settings::values.frame_limit = qt_config->value("frame_limit", 100).toInt();

    Settings::values.bg_red = qt_config->value("bg_red", 0.0).toFloat();
    Settings::values.bg_green = qt_config->value("bg_green", 0.0).toFloat();
    Settings::values.bg_blue = qt_config->value("bg_blue", 0.0).toFloat();
    qt_config->endGroup();

    qt_config->beginGroup("Layout");
    Settings::values.toggle_3d = qt_config->value("toggle_3d", false).toBool();
    Settings::values.factor_3d = qt_config->value("factor_3d", 0).toInt();
    Settings::values.layout_option =
        static_cast<Settings::LayoutOption>(qt_config->value("layout_option").toInt());
    Settings::values.swap_screen = qt_config->value("swap_screen", false).toBool();
    Settings::values.custom_layout = qt_config->value("custom_layout", false).toBool();
    Settings::values.custom_top_left = qt_config->value("custom_top_left", 0).toInt();
    Settings::values.custom_top_top = qt_config->value("custom_top_top", 0).toInt();
    Settings::values.custom_top_right = qt_config->value("custom_top_right", 400).toInt();
    Settings::values.custom_top_bottom = qt_config->value("custom_top_bottom", 240).toInt();
    Settings::values.custom_bottom_left = qt_config->value("custom_bottom_left", 40).toInt();
    Settings::values.custom_bottom_top = qt_config->value("custom_bottom_top", 240).toInt();
    Settings::values.custom_bottom_right = qt_config->value("custom_bottom_right", 360).toInt();
    Settings::values.custom_bottom_bottom = qt_config->value("custom_bottom_bottom", 480).toInt();
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    Settings::values.sink_id = qt_config->value("output_engine", "auto").toString().toStdString();
    Settings::values.enable_audio_stretching =
        qt_config->value("enable_audio_stretching", true).toBool();
    Settings::values.audio_device_id =
        qt_config->value("output_device", "auto").toString().toStdString();
    Settings::values.volume = qt_config->value("volume", 1).toFloat();
    Settings::values.headphones_connected =
        qt_config->value("headphones_connected", false).toBool();
    qt_config->endGroup();

    using namespace Service::CAM;
    qt_config->beginGroup("Camera");
    Settings::values.camera_name[OuterRightCamera] =
        qt_config->value("camera_outer_right_name", "blank").toString().toStdString();
    Settings::values.camera_config[OuterRightCamera] =
        qt_config->value("camera_outer_right_config", "").toString().toStdString();
    Settings::values.camera_flip[OuterRightCamera] =
        qt_config->value("camera_outer_right_flip", 0).toInt();
    Settings::values.camera_name[InnerCamera] =
        qt_config->value("camera_inner_name", "blank").toString().toStdString();
    Settings::values.camera_config[InnerCamera] =
        qt_config->value("camera_inner_config", "").toString().toStdString();
    Settings::values.camera_flip[InnerCamera] = qt_config->value("camera_inner_flip", 0).toInt();
    Settings::values.camera_name[OuterLeftCamera] =
        qt_config->value("camera_outer_left_name", "blank").toString().toStdString();
    Settings::values.camera_config[OuterLeftCamera] =
        qt_config->value("camera_outer_left_config", "").toString().toStdString();
    Settings::values.camera_flip[OuterLeftCamera] =
        qt_config->value("camera_outer_left_flip", 0).toInt();
    qt_config->endGroup();

    qt_config->beginGroup("Data Storage");
    Settings::values.use_virtual_sd = qt_config->value("use_virtual_sd", true).toBool();
    Settings::values.sd_card_root = qt_config->value("sd_card_root", "").toString().toStdString();
    qt_config->endGroup();

    qt_config->beginGroup("System");
    Settings::values.region_value =
        qt_config->value("region_value", Settings::REGION_VALUE_AUTO_SELECT).toInt();
    Settings::values.init_clock = static_cast<Settings::InitClock>(
        qt_config->value("init_clock", static_cast<u32>(Settings::InitClock::SystemTime)).toInt());
    Settings::values.init_time = qt_config->value("init_time", 946681277ULL).toULongLong();
    Settings::values.enable_new_mode = qt_config->value("enable_new_mode", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    Settings::values.log_filter = qt_config->value("log_filter", "*:Info").toString().toStdString();
    qt_config->endGroup();

    qt_config->beginGroup("Hacks");
    Settings::values.priority_boost = qt_config->value("priority_boost", false).toBool();
    Settings::values.ticks_mode =
        static_cast<Settings::TicksMode>(qt_config->value("ticks_mode", 0).toInt());
    Settings::values.ticks = qt_config->value("ticks", 0).toULongLong();
    Settings::values.use_bos = qt_config->value("use_bos", false).toBool();
    qt_config->endGroup();

    qt_config->beginGroup("UI");
    UISettings::values.theme = qt_config->value("theme", UISettings::themes[0].second).toString();

    qt_config->beginGroup("UILayout");
    UISettings::values.geometry = qt_config->value("geometry").toByteArray();
    UISettings::values.state = qt_config->value("state").toByteArray();
    UISettings::values.renderwindow_geometry =
        qt_config->value("geometryRenderWindow").toByteArray();
    UISettings::values.gamelist_header_state =
        qt_config->value("gameListHeaderState").toByteArray();
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    UISettings::values.roms_path = qt_config->value("romsPath").toString();
    UISettings::values.game_dir_deprecated = qt_config->value("gameListRootDir", ".").toString();
    UISettings::values.game_dir_deprecated_deepscan =
        qt_config->value("gameListDeepScan", false).toBool();
    int size = qt_config->beginReadArray("gamedirs");
    for (int i = 0; i < size; ++i) {
        qt_config->setArrayIndex(i);
        UISettings::GameDir game_dir;
        game_dir.path = qt_config->value("path").toString();
        game_dir.deep_scan = qt_config->value("deep_scan", false).toBool();
        game_dir.expanded = qt_config->value("expanded", true).toBool();
        UISettings::values.game_dirs.append(game_dir);
    }
    qt_config->endArray();
    // create NAND and SD card directories if empty, these are not removable through the UI, also
    // carries over old game list settings if present
    if (UISettings::values.game_dirs.isEmpty()) {
        UISettings::GameDir game_dir;
        game_dir.path = "INSTALLED";
        game_dir.expanded = true;
        UISettings::values.game_dirs.append(game_dir);
        game_dir.path = "SYSTEM";
        UISettings::values.game_dirs.append(game_dir);
        if (UISettings::values.game_dir_deprecated != ".") {
            game_dir.path = UISettings::values.game_dir_deprecated;
            game_dir.deep_scan = UISettings::values.game_dir_deprecated_deepscan;
            UISettings::values.game_dirs.append(game_dir);
        }
    }
    UISettings::values.recent_files = qt_config->value("recentFiles").toStringList();
    UISettings::values.language = qt_config->value("language", "").toString();
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    QStringList groups = qt_config->childGroups();
    for (auto group : groups) {
        qt_config->beginGroup(group);

        QStringList hotkeys = qt_config->childGroups();
        for (auto hotkey : hotkeys) {
            qt_config->beginGroup(hotkey);
            UISettings::values.shortcuts.emplace_back(UISettings::Shortcut(
                group + "/" + hotkey,
                UISettings::ContextualShortcut(qt_config->value("KeySeq").toString(),
                                               qt_config->value("Context").toInt())));
            qt_config->endGroup();
        }

        qt_config->endGroup();
    }
    qt_config->endGroup();

    UISettings::values.single_window_mode = qt_config->value("singleWindowMode", true).toBool();
    UISettings::values.fullscreen = qt_config->value("fullscreen", false).toBool();
    UISettings::values.display_titlebar = qt_config->value("displayTitleBars", true).toBool();
    UISettings::values.show_filter_bar = qt_config->value("showFilterBar", true).toBool();
    UISettings::values.show_status_bar = qt_config->value("showStatusBar", true).toBool();
    UISettings::values.confirm_before_closing = qt_config->value("confirmClose", true).toBool();
    UISettings::values.show_console = qt_config->value("showConsole", false).toBool();

    qt_config->beginGroup("Multiplayer");
    UISettings::values.ip = qt_config->value("ip", "").toString();
    UISettings::values.port = qt_config->value("port", Network::DefaultRoomPort).toString();
    UISettings::values.port_host =
        qt_config->value("port_host", Network::DefaultRoomPort).toString();
    qt_config->endGroup();

    qt_config->endGroup();
}

void Config::SaveValues() {
    qt_config->beginGroup("ControlPanel");
    qt_config->setValue("sp_enable_3d", Settings::values.sp_enable_3d);
    qt_config->setValue("p_adapter_connected", Settings::values.p_adapter_connected);
    qt_config->setValue("p_battery_charging", Settings::values.p_battery_charging);
    qt_config->setValue("p_battery_level", Settings::values.p_battery_level);
    qt_config->setValue("n_wifi_status", Settings::values.n_wifi_status);
    qt_config->setValue("n_wifi_link_level", Settings::values.n_wifi_link_level);
    qt_config->setValue("n_state", Settings::values.n_state);
    qt_config->endGroup();

    qt_config->beginGroup("Controls");
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeButton::mapping[i]),
                            QString::fromStdString(Settings::values.buttons[i]));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        qt_config->setValue(QString::fromStdString(Settings::NativeAnalog::mapping[i]),
                            QString::fromStdString(Settings::values.analogs[i]));
    }
    qt_config->setValue("motion_device", QString::fromStdString(Settings::values.motion_device));
    qt_config->setValue("touch_device", QString::fromStdString(Settings::values.touch_device));
    qt_config->setValue("udp_input_address",
                        QString::fromStdString(Settings::values.udp_input_address));
    qt_config->setValue("udp_input_port", Settings::values.udp_input_port);
    qt_config->setValue("udp_pad_index", Settings::values.udp_pad_index);
    qt_config->endGroup();

    qt_config->beginGroup("Core");
    qt_config->setValue("use_cpu_jit", Settings::values.use_cpu_jit);
    qt_config->setValue("keyboard_mode", static_cast<int>(Settings::values.keyboard_mode));
    qt_config->endGroup();

    qt_config->beginGroup("LLE");
    for (const auto& service_module : Settings::values.lle_modules) {
        qt_config->setValue(QString::fromStdString(service_module.first), service_module.second);
    }
    qt_config->endGroup();

    qt_config->beginGroup("Renderer");
    qt_config->setValue("use_hw_renderer", Settings::values.use_hw_renderer);
    qt_config->setValue("use_hw_shader", Settings::values.use_hw_shader);
    qt_config->setValue("shaders_accurate_gs", Settings::values.shaders_accurate_gs);
    qt_config->setValue("shaders_accurate_mul", Settings::values.shaders_accurate_mul);
    qt_config->setValue("use_shader_jit", Settings::values.use_shader_jit);
    qt_config->setValue("resolution_factor", Settings::values.resolution_factor);
    qt_config->setValue("use_vsync", Settings::values.use_vsync);
    qt_config->setValue("use_frame_limit", Settings::values.use_frame_limit);
    qt_config->setValue("frame_limit", Settings::values.frame_limit);

    // Cast to double because Qt's written float values are not human-readable
    qt_config->setValue("bg_red", (double)Settings::values.bg_red);
    qt_config->setValue("bg_green", (double)Settings::values.bg_green);
    qt_config->setValue("bg_blue", (double)Settings::values.bg_blue);
    qt_config->endGroup();

    qt_config->beginGroup("Layout");
    qt_config->setValue("toggle_3d", Settings::values.toggle_3d);
    qt_config->setValue("factor_3d", Settings::values.factor_3d);
    qt_config->setValue("layout_option", static_cast<int>(Settings::values.layout_option));
    qt_config->setValue("swap_screen", Settings::values.swap_screen);
    qt_config->setValue("custom_layout", Settings::values.custom_layout);
    qt_config->setValue("custom_top_left", Settings::values.custom_top_left);
    qt_config->setValue("custom_top_top", Settings::values.custom_top_top);
    qt_config->setValue("custom_top_right", Settings::values.custom_top_right);
    qt_config->setValue("custom_top_bottom", Settings::values.custom_top_bottom);
    qt_config->setValue("custom_bottom_left", Settings::values.custom_bottom_left);
    qt_config->setValue("custom_bottom_top", Settings::values.custom_bottom_top);
    qt_config->setValue("custom_bottom_right", Settings::values.custom_bottom_right);
    qt_config->setValue("custom_bottom_bottom", Settings::values.custom_bottom_bottom);
    qt_config->endGroup();

    qt_config->beginGroup("Audio");
    qt_config->setValue("output_engine", QString::fromStdString(Settings::values.sink_id));
    qt_config->setValue("enable_audio_stretching", Settings::values.enable_audio_stretching);
    qt_config->setValue("output_device", QString::fromStdString(Settings::values.audio_device_id));
    qt_config->setValue("volume", Settings::values.volume);
    qt_config->setValue("headphones_connected", Settings::values.headphones_connected);
    qt_config->endGroup();

    using namespace Service::CAM;
    qt_config->beginGroup("Camera");
    qt_config->setValue("camera_outer_right_name",
                        QString::fromStdString(Settings::values.camera_name[OuterRightCamera]));
    qt_config->setValue("camera_outer_right_config",
                        QString::fromStdString(Settings::values.camera_config[OuterRightCamera]));
    qt_config->setValue("camera_inner_name",
                        QString::fromStdString(Settings::values.camera_name[InnerCamera]));
    qt_config->setValue("camera_inner_config",
                        QString::fromStdString(Settings::values.camera_config[InnerCamera]));
    qt_config->setValue("camera_outer_left_name",
                        QString::fromStdString(Settings::values.camera_name[OuterLeftCamera]));
    qt_config->setValue("camera_outer_left_config",
                        QString::fromStdString(Settings::values.camera_config[OuterLeftCamera]));
    qt_config->endGroup();

    qt_config->beginGroup("Data Storage");
    qt_config->setValue("use_virtual_sd", Settings::values.use_virtual_sd);
    qt_config->setValue("sd_card_root", QString::fromStdString(Settings::values.sd_card_root));
    qt_config->endGroup();

    qt_config->beginGroup("System");
    qt_config->setValue("region_value", Settings::values.region_value);
    qt_config->setValue("init_clock", static_cast<u32>(Settings::values.init_clock));
    qt_config->setValue("init_time", static_cast<unsigned long long>(Settings::values.init_time));
    qt_config->setValue("enable_new_mode", Settings::values.enable_new_mode);
    qt_config->endGroup();

    qt_config->beginGroup("Miscellaneous");
    qt_config->setValue("log_filter", QString::fromStdString(Settings::values.log_filter));
    qt_config->endGroup();

    qt_config->beginGroup("Hacks");
    qt_config->setValue("priority_boost", Settings::values.priority_boost);
    qt_config->setValue("ticks_mode", static_cast<int>(Settings::values.ticks_mode));
    qt_config->setValue("ticks", static_cast<unsigned long long>(Settings::values.ticks));
    qt_config->setValue("use_bos", Settings::values.use_bos);
    qt_config->endGroup();

    qt_config->beginGroup("UI");
    qt_config->setValue("theme", UISettings::values.theme);

    qt_config->beginGroup("UILayout");
    qt_config->setValue("geometry", UISettings::values.geometry);
    qt_config->setValue("state", UISettings::values.state);
    qt_config->setValue("geometryRenderWindow", UISettings::values.renderwindow_geometry);
    qt_config->setValue("gameListHeaderState", UISettings::values.gamelist_header_state);
    qt_config->endGroup();

    qt_config->beginGroup("Paths");
    qt_config->setValue("romsPath", UISettings::values.roms_path);
    qt_config->beginWriteArray("gamedirs");
    for (int i = 0; i < UISettings::values.game_dirs.size(); ++i) {
        qt_config->setArrayIndex(i);
        const auto& game_dir = UISettings::values.game_dirs.at(i);
        qt_config->setValue("path", game_dir.path);
        qt_config->setValue("deep_scan", game_dir.deep_scan);
        qt_config->setValue("expanded", game_dir.expanded);
    }
    qt_config->endArray();
    qt_config->setValue("recentFiles", UISettings::values.recent_files);
    qt_config->setValue("language", UISettings::values.language);
    qt_config->endGroup();

    qt_config->beginGroup("Shortcuts");
    for (auto shortcut : UISettings::values.shortcuts) {
        qt_config->setValue(shortcut.first + "/KeySeq", shortcut.second.first);
        qt_config->setValue(shortcut.first + "/Context", shortcut.second.second);
    }
    qt_config->endGroup();

    qt_config->setValue("singleWindowMode", UISettings::values.single_window_mode);
    qt_config->setValue("fullscreen", UISettings::values.fullscreen);
    qt_config->setValue("displayTitleBars", UISettings::values.display_titlebar);
    qt_config->setValue("showFilterBar", UISettings::values.show_filter_bar);
    qt_config->setValue("showStatusBar", UISettings::values.show_status_bar);
    qt_config->setValue("confirmClose", UISettings::values.confirm_before_closing);
    qt_config->setValue("showConsole", UISettings::values.show_console);

    qt_config->beginGroup("Multiplayer");
    qt_config->setValue("ip", UISettings::values.ip);
    qt_config->setValue("port", UISettings::values.port);
    qt_config->setValue("port_host", UISettings::values.port_host);
    qt_config->endGroup();

    qt_config->endGroup();
}

void Config::Reload() {
    ReadValues();
    Settings::Apply();
}

void Config::Save() {
    SaveValues();
}

Config::~Config() {
    Save();

    delete qt_config;
}
