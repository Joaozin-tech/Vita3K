// Vita3K emulator project
// Copyright (C) 2023 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "private.h"

#include <compat/functions.h>
#include <config/state.h>
#include <gui/functions.h>
#include <io/state.h>

#include <util/log.h>
#include <util/safe_time.h>

#include <SDL_misc.h>
#include <pugixml.hpp>

namespace gui {

static std::map<double, std::string> update_history_infos;

static bool get_update_history(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {
    update_history_infos.clear();
    const auto change_info_path{ fs::path(emuenv.pref_path) / "ux0/app" / app_path / "sce_sys/changeinfo/" };

    std::string fname = fs::exists(change_info_path / fmt::format("changeinfo_{:0>2d}.xml", emuenv.cfg.sys_lang)) ? fmt::format("changeinfo_{:0>2d}.xml", emuenv.cfg.sys_lang) : "changeinfo.xml";

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file((change_info_path.string() + fname).c_str());

    for (const auto &info : doc.child("changeinfo"))
        update_history_infos[info.attribute("app_ver").as_double()] = info.text().as_string();

    for (auto &update : update_history_infos) {
        const auto startpos = "<";
        const auto endpos = ">";

        if (update.second.find_first_of('\n') != std::string::npos)
            update.second.erase(update.second.begin() + update.second.find_first_of('\n'));

        while (update.second.find("</li> <li>") != std::string::npos)
            if (update.second.find("</li> <li>") != std::string::npos)
                update.second.replace(update.second.find("</li> <li>"), update.second.find(endpos) + 2 - update.second.find(startpos), "\n");
        while (update.second.find("<li>") != std::string::npos)
            if (update.second.find("<li>") != std::string::npos)
                update.second.replace(update.second.find("<li>"), update.second.find(endpos) + 1 - update.second.find(startpos), ". ");
        while (update.second.find(startpos) != std::string::npos)
            if (update.second.find(">") + 1 != std::string::npos)
                update.second.erase(update.second.find(startpos), update.second.find(endpos) + 1 - update.second.find(startpos));

        bool found_space = false;
        auto end{ std::remove_if(update.second.begin(), update.second.end(), [&found_space](unsigned ch) {
            bool is_space = std::isspace(ch);
            std::swap(found_space, is_space);
            return found_space && is_space;
        }) };

        if (end != update.second.begin() && std::isspace(static_cast<unsigned>(end[-1])))
            --end;

        update.second.erase(end, update.second.end());
    }

    return !update_history_infos.empty();
}

std::vector<TimeApp>::iterator get_time_app_index(GuiState &gui, EmuEnvState &emuenv, const std::string app) {
    const auto time_app_index = std::find_if(gui.time_apps[emuenv.io.user_id].begin(), gui.time_apps[emuenv.io.user_id].end(), [&](const TimeApp &t) {
        return t.app == app;
    });

    return time_app_index;
}

static std::string get_time_app_used(const int64_t &time_used) {
    static const uint32_t one_min = 60;
    static const uint32_t one_hour = one_min * 60;
    static const uint32_t twenty_four_hours = 24;
    static const uint32_t one_day = one_hour * twenty_four_hours;
    static const uint32_t seven_days = 7;
    static const uint32_t one_week = one_day * seven_days;

    if (time_used < one_min)
        return fmt::format("{}s", time_used);
    else {
        const std::chrono::seconds sec(time_used);
        const uint32_t minutes = std::chrono::duration_cast<std::chrono::minutes>(sec).count() % one_min;
        const uint32_t seconds = sec.count() % one_min;
        if (time_used < one_hour)
            return fmt::format("{}m:{}s", minutes, seconds);
        else {
            const uint32_t count_hours = std::chrono::duration_cast<std::chrono::hours>(sec).count();
            if (time_used < one_day)
                return fmt::format("{}h:{}m:{}s", count_hours, minutes, seconds);
            else {
                const uint32_t count_days = count_hours / twenty_four_hours;
                const uint32_t hours_per_day = count_hours - (count_days * twenty_four_hours);
                if (time_used < one_week)
                    return fmt::format("{}d:{}h:{}m:{}s", count_days, hours_per_day, minutes, seconds);
                else {
                    const uint32_t count_weeks = count_days / seven_days;
                    const uint32_t count_days_week = count_days - (count_weeks * seven_days);
                    return fmt::format("{}w:{}d:{}h:{}m:{}s", count_weeks, count_days_week, hours_per_day, minutes, seconds);
                }
            }
        }
    }
}

void get_time_apps(GuiState &gui, EmuEnvState &emuenv) {
    gui.time_apps.clear();
    const auto time_path{ fs::path(emuenv.pref_path) / "ux0/user/time.xml" };

    pugi::xml_document time_xml;
    if (fs::exists(time_path)) {
        if (time_xml.load_file(time_path.c_str())) {
            const auto time_child = time_xml.child("time");
            if (!time_child.child("user").empty()) {
                for (const auto &user : time_child) {
                    auto user_id = user.attribute("id").as_string();
                    for (const auto &app : user)
                        gui.time_apps[user_id].push_back({ app.text().as_string(), app.attribute("last-time-used").as_llong(), app.attribute("time-used").as_llong() });
                }
            }
        } else {
            LOG_ERROR("Time XML found is corrupted on path: {}", time_path.string());
            fs::remove(time_path);
        }
    }
}

static void save_time_apps(GuiState &gui, EmuEnvState &emuenv) {
    pugi::xml_document time_xml;
    auto declarationUser = time_xml.append_child(pugi::node_declaration);
    declarationUser.append_attribute("version") = "1.0";
    declarationUser.append_attribute("encoding") = "utf-8";

    auto time_child = time_xml.append_child("time");

    for (const auto &user : gui.time_apps) {
        // Sort by last time used
        std::sort(gui.time_apps[user.first].begin(), gui.time_apps[user.first].end(), [&](const TimeApp &ta, const TimeApp &tb) {
            return ta.last_time_used > tb.last_time_used;
        });

        auto user_child = time_child.append_child("user");
        user_child.append_attribute("id") = user.first.c_str();

        for (const auto &app : user.second) {
            auto app_child = user_child.append_child("app");
            app_child.append_attribute("last-time-used") = app.last_time_used;
            app_child.append_attribute("time-used") = app.time_used;
            app_child.append_child(pugi::node_pcdata).set_value(app.app.c_str());
        }
    }

    const auto time_path{ fs::path(emuenv.pref_path) / "ux0/user/time.xml" };
    const auto save_xml = time_xml.save_file(time_path.c_str());
    if (!save_xml)
        LOG_ERROR("Fail save xml");
}

void update_time_app_used(GuiState &gui, EmuEnvState &emuenv, const std::string &app) {
    const auto &time_app_index = get_time_app_index(gui, emuenv, app);
    time_app_index->time_used += std::time(nullptr) - time_app_index->last_time_used;

    save_time_apps(gui, emuenv);
}

void update_last_time_app_used(GuiState &gui, EmuEnvState &emuenv, const std::string &app) {
    const auto &time_app_index = get_time_app_index(gui, emuenv, app);
    if (time_app_index != gui.time_apps[emuenv.io.user_id].end())
        time_app_index->last_time_used = std::time(nullptr);
    else
        gui.time_apps[emuenv.io.user_id].push_back({ app, std::time(nullptr), 0 });

    get_app_index(gui, app)->last_time = std::time(nullptr);
    if (gui.users[emuenv.io.user_id].sort_apps_type == LAST_TIME) {
        const auto sorted = gui.app_selector.app_list_sorted[LAST_TIME];
        std::sort(gui.app_selector.user_apps.begin(), gui.app_selector.user_apps.end(), [&sorted](const App &lhs, const App &rhs) {
            switch (sorted) {
            case ASCENDANT:
                return lhs.last_time > rhs.last_time;
            case DESCENDANT:
                return lhs.last_time < rhs.last_time;
            default: break;
            }

            return false;
        });
    }

    save_time_apps(gui, emuenv);
}

void delete_app(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {
    const auto APP_INDEX = get_app_index(gui, app_path);
    const auto title_id = APP_INDEX->title_id;
    try {
        const auto PREF_PATH = fs::path(emuenv.pref_path);
        fs::remove_all(PREF_PATH / "ux0/app" / app_path);

        const auto BASE_PATH = fs::path(emuenv.base_path);
        const auto CUSTOM_CONFIG_PATH{ BASE_PATH / "config" / fmt::format("config_{}.xml", app_path) };
        if (fs::exists(CUSTOM_CONFIG_PATH))
            fs::remove_all(CUSTOM_CONFIG_PATH);
        const auto ADDCONT_PATH{ PREF_PATH / "ux0/addcont" / APP_INDEX->addcont };
        if (fs::exists(ADDCONT_PATH))
            fs::remove_all(ADDCONT_PATH);
        const auto LICENSE_PATH{ PREF_PATH / "ux0/license" / title_id };
        if (fs::exists(LICENSE_PATH))
            fs::remove_all(LICENSE_PATH);
        const auto PATCH_PATH{ PREF_PATH / "ux0/patch" / title_id };
        if (fs::exists(PATCH_PATH))
            fs::remove_all(PATCH_PATH);
        const auto SAVE_DATA_PATH{ PREF_PATH / "ux0/user" / emuenv.io.user_id / "savedata" / APP_INDEX->savedata };
        if (fs::exists(SAVE_DATA_PATH))
            fs::remove_all(SAVE_DATA_PATH);
        const auto SHADER_CACHE_PATH{ BASE_PATH / "cache/shaders" / title_id };
        if (fs::exists(SHADER_CACHE_PATH))
            fs::remove_all(SHADER_CACHE_PATH);
        const auto SHADER_LOG_PATH{ BASE_PATH / "shaderlog" / title_id };
        if (fs::exists(SHADER_LOG_PATH))
            fs::remove_all(SHADER_LOG_PATH);

        if (gui.app_selector.user_apps_icon.find(app_path) != gui.app_selector.user_apps_icon.end()) {
            gui.app_selector.user_apps_icon[app_path] = {};
            gui.app_selector.user_apps_icon.erase(app_path);
        }

        const auto time_app_index = get_time_app_index(gui, emuenv, app_path);
        if (time_app_index != gui.time_apps[emuenv.io.user_id].end()) {
            gui.time_apps[emuenv.io.user_id].erase(time_app_index);
            save_time_apps(gui, emuenv);
        }

        LOG_INFO("Application successfully deleted '{} [{}]'.", title_id, APP_INDEX->title);

        gui.app_selector.user_apps.erase(APP_INDEX);

        save_apps_cache(gui, emuenv);
    } catch (std::exception &e) {
        LOG_ERROR("Failed to delete '{} [{}]'.\n{}", title_id, APP_INDEX->title, e.what());
    }
}

void open_path(const std::string &path) {
    if (path.starts_with("http")) {
        SDL_OpenURL(path.c_str());
    } else {
#ifdef _WIN32
        static const char OS_PREFIX[] = "start \"Vita3K\" ";
#elif __APPLE__
        static const char OS_PREFIX[] = "open ";
#else
        static const char OS_PREFIX[] = "xdg-open ";
        system((OS_PREFIX + ("\"" + path + "\" & disown")).c_str());
        return;
#endif

        system((OS_PREFIX + ("\"" + path + "\"")).c_str());
    }
}

static std::string context_dialog;

void draw_app_context_menu(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {
    const auto APP_INDEX = get_app_index(gui, app_path);
    const auto title_id = APP_INDEX->title_id;

    const auto APP_PATH{ fs::path(emuenv.pref_path) / "ux0/app" / app_path };
    const auto CUSTOM_CONFIG_PATH{ fs::path(emuenv.base_path) / "config" / fmt::format("config_{}.xml", app_path) };
    const auto ADDCONT_PATH{ fs::path(emuenv.pref_path) / "ux0/addcont" / APP_INDEX->addcont };
    const auto LICENSE_PATH{ fs::path(emuenv.pref_path) / "ux0/license" / title_id };
    const auto MANUAL_PATH{ APP_PATH / "sce_sys/manual" };
    const auto SAVE_DATA_PATH{ fs::path(emuenv.pref_path) / "ux0/user" / emuenv.io.user_id / "savedata" / APP_INDEX->savedata };
    const auto SHADER_CACHE_PATH{ fs::path(emuenv.base_path) / "cache/shaders" / title_id };
    const auto SHADER_LOG_PATH{ fs::path(emuenv.base_path) / "shaderlog" / title_id };
    const auto ISSUES_URL = "https://github.com/Vita3K/compatibility/issues";

    const auto display_size = ImGui::GetIO().DisplaySize;
    const auto RES_SCALE = ImVec2(display_size.x / emuenv.res_width_dpi_scale, display_size.y / emuenv.res_height_dpi_scale);

    const auto is_12_hour_format = emuenv.cfg.sys_time_format == SCE_SYSTEM_PARAM_TIME_FORMAT_12HOUR;

    auto lang = gui.lang.app_context;
    auto app_str = gui.lang.content_manager.application;
    auto savedata_str = gui.lang.content_manager.saved_data;
    auto common = emuenv.common_dialog.lang.common;
    auto lang_compat = gui.lang.compatibility;

    const auto has_issue = gui.compat.compat_db_loaded ? gui.compat.app_compat_db.contains(title_id) : false;
    const auto compat_state = has_issue ? gui.compat.app_compat_db[title_id].state : Unknown;
    const auto compat_state_color = gui.compat.compat_color[compat_state];
    const auto compat_state_str = has_issue ? lang_compat.states[compat_state] : lang_compat.states[Unknown];

    // App Context Menu
    if (ImGui::BeginPopupContextItem("##app_context_menu")) {
        ImGui::SetWindowFontScale(1.1f);
        if (ImGui::MenuItem(lang["boot"].c_str()))
            pre_load_app(gui, emuenv, false, app_path);
        if (title_id.find("NPXS") == std::string::npos) {
            if (ImGui::BeginMenu(lang_compat.name.c_str())) {
                ImGui::Spacing();
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() / 2.f) - (ImGui::CalcTextSize(compat_state_str.c_str()).x / 2));
                ImGui::TextColored(compat_state_color, "%s", compat_state_str.c_str());
                ImGui::Spacing();
                if (has_issue) {
                    tm updated_at_tm = {};
                    SAFE_LOCALTIME(&gui.compat.app_compat_db[title_id].updated_at, &updated_at_tm);
                    auto UPDATED_AT = get_date_time(gui, emuenv, updated_at_tm);
                    ImGui::Spacing();
                    const auto updated_at_str = fmt::format("{} {} {} {}", lang["updated"].c_str(), UPDATED_AT[DateTime::DATE_MINI], UPDATED_AT[DateTime::CLOCK], is_12_hour_format ? UPDATED_AT[DateTime::DAY_MOMENT] : "");
                    ImGui::SetCursorPosX((ImGui::GetWindowWidth() / 2.f) - (ImGui::CalcTextSize(updated_at_str.c_str()).x / 2));
                    ImGui::TextColored(GUI_COLOR_TEXT, "%s", updated_at_str.c_str());
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::MenuItem(lang["check_app_compatibility"].c_str())) {
                    const std::string compat_url = title_id.find("PCS") != std::string::npos ? "https://vita3k.org/compatibility?g=" + title_id : "https://github.com/Vita3K/homebrew-compatibility/issues?q=" + APP_INDEX->title;
                    open_path(compat_url);
                }
                if (has_issue) {
                    if (ImGui::MenuItem(lang["open_issue"].c_str()))
                        open_path(fmt::format("{}/{}", ISSUES_URL, gui.compat.app_compat_db[title_id].issue_id));
                } else {
                    if (ImGui::MenuItem(lang["create_issue"].c_str()))
                        open_path(fmt::format("{}/new?title={} [{}]", ISSUES_URL, APP_INDEX->title, title_id));
                }
                if (ImGui::MenuItem(lang["update_database"].c_str()))
                    compat::update_compat_app_db(gui, emuenv);

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(lang["copy_app_info"].c_str())) {
                if (ImGui::MenuItem(lang["name_and_id"].c_str())) {
                    ImGui::LogToClipboard();
                    ImGui::LogText("%s [%s]", APP_INDEX->title.c_str(), title_id.c_str());
                    ImGui::LogFinish();
                }
                if (ImGui::MenuItem(lang["name"].c_str())) {
                    ImGui::LogToClipboard();
                    ImGui::LogText("%s", APP_INDEX->title.c_str());
                    ImGui::LogFinish();
                }
                if (ImGui::MenuItem(lang["title_id"].c_str())) {
                    ImGui::LogToClipboard();
                    ImGui::LogText("%s", title_id.c_str());
                    ImGui::LogFinish();
                }
                if (ImGui::MenuItem(lang["app_summary"].c_str())) {
                    ImGui::LogToClipboard();
                    ImGui::LogText("# App summary\n- App name: %s\n- App serial: %s\n- App version: %s", APP_INDEX->title.c_str(), title_id.c_str(), APP_INDEX->app_ver.c_str());
                    ImGui::LogFinish();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(lang["custom_config"].c_str())) {
                if (!fs::exists(CUSTOM_CONFIG_PATH)) {
                    if (ImGui::MenuItem(lang["create"].c_str(), nullptr, &gui.configuration_menu.custom_settings_dialog))
                        init_config(gui, emuenv, app_path);
                } else {
                    if (ImGui::MenuItem(lang["edit"].c_str(), nullptr, &gui.configuration_menu.custom_settings_dialog))
                        init_config(gui, emuenv, app_path);
                    if (ImGui::MenuItem(lang["remove"].c_str()))
                        fs::remove(CUSTOM_CONFIG_PATH);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(lang["open_folder"].c_str())) {
                if (ImGui::MenuItem(app_str["title"].c_str()))
                    open_path(APP_PATH.string());
                if (fs::exists(ADDCONT_PATH) && ImGui::MenuItem(lang["addcont"].c_str()))
                    open_path(ADDCONT_PATH.string());
                if (fs::exists(LICENSE_PATH) && ImGui::MenuItem(lang["license"].c_str()))
                    open_path(LICENSE_PATH.string());
                if (fs::exists(SAVE_DATA_PATH) && ImGui::MenuItem(savedata_str["title"].c_str()))
                    open_path(SAVE_DATA_PATH.string());
                if (fs::exists(SHADER_CACHE_PATH) && ImGui::MenuItem(lang["shader_cache"].c_str()))
                    open_path(SHADER_CACHE_PATH.string());
                if (fs::exists(SHADER_LOG_PATH) && ImGui::MenuItem(lang["shader_log"].c_str()))
                    open_path(SHADER_LOG_PATH.string());
                ImGui::EndMenu();
            }
            if (!emuenv.cfg.show_live_area_screen && ImGui::BeginMenu("Live Area")) {
                if (ImGui::MenuItem("Live Area", nullptr, &gui.live_area.live_area_screen))
                    open_live_area(gui, emuenv, app_path);
                if (ImGui::MenuItem("Search", nullptr))
                    open_search(APP_INDEX->title);
                if (fs::exists(MANUAL_PATH) && !fs::is_empty(MANUAL_PATH) && ImGui::MenuItem("Manual", nullptr))
                    open_manual(gui, emuenv, app_path);
                if (ImGui::MenuItem("Update"))
                    update_app(gui, emuenv, app_path);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(common["delete"].c_str())) {
                if (ImGui::MenuItem(app_str["title"].c_str()))
                    context_dialog = "app";
                if (fs::exists(ADDCONT_PATH) && ImGui::MenuItem(lang["addcont"].c_str()))
                    fs::remove_all(ADDCONT_PATH);
                if (fs::exists(LICENSE_PATH) && ImGui::MenuItem(lang["license"].c_str()))
                    fs::remove_all(LICENSE_PATH);
                if (fs::exists(SAVE_DATA_PATH) && ImGui::MenuItem(savedata_str["title"].c_str()))
                    context_dialog = "save";
                if (fs::exists(SHADER_CACHE_PATH) && ImGui::MenuItem(lang["shader_cache"].c_str()))
                    fs::remove_all(SHADER_CACHE_PATH);
                if (fs::exists(SHADER_LOG_PATH) && ImGui::MenuItem(lang["shader_log"].c_str()))
                    fs::remove_all(SHADER_LOG_PATH);
                ImGui::EndMenu();
            }
            if (fs::exists(APP_PATH / "sce_sys/changeinfo/") && ImGui::MenuItem(lang["update_history"].c_str())) {
                if (get_update_history(gui, emuenv, app_path))
                    context_dialog = "history";
                else
                    LOG_WARN("Patch note Error for title id: {} in path: {}", title_id, app_path);
            }
        }
        if (ImGui::MenuItem(lang["information"].c_str(), nullptr, &gui.live_area.app_information)) {
            if (title_id.find("NPXS") == std::string::npos) {
                get_app_info(gui, emuenv, app_path);
                const auto app_size = get_app_size(gui, emuenv, app_path);
                gui.app_selector.app_info.size = app_size;
            }
            gui.live_area.information_bar = false;
        }
        ImGui::EndPopup();
    }

    const auto SCALE = ImVec2(RES_SCALE.x * emuenv.dpi_scale, RES_SCALE.y * emuenv.dpi_scale);
    const auto WINDOW_SIZE = ImVec2(756.0f * SCALE.x, 436.0f * SCALE.y);

    const auto BUTTON_SIZE = ImVec2(320.f * SCALE.x, 46.f * SCALE.y);
    const auto PUPOP_ICON_SIZE = ImVec2(96.f * SCALE.x, 96.f * SCALE.y);
    const auto INFO_ICON_SIZE = ImVec2(128.f * SCALE.x, 128.f * SCALE.y);

    // Context Dialog
    if (!context_dialog.empty()) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(display_size, ImGuiCond_Always);
        ImGui::Begin("##context_dialog", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetNextWindowBgAlpha(0.999f);
        ImGui::SetNextWindowPos(ImVec2(display_size.x / 2.f, display_size.y / 2.f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f * SCALE.x);
        ImGui::BeginChild("##context_dialog_child", WINDOW_SIZE, true, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f * SCALE.x);
        // Update History
        if (context_dialog == "history") {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + 20.f * SCALE.x, ImGui::GetWindowPos().y + BUTTON_SIZE.y));
            ImGui::BeginChild("##info_update_list", ImVec2(WINDOW_SIZE.x - (30.f * SCALE.x), WINDOW_SIZE.y - (BUTTON_SIZE.y * 2.f) - (25.f * SCALE.y)), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
            for (const auto &update : update_history_infos) {
                ImGui::SetWindowFontScale(1.4f);
                const auto version_str = fmt::format(fmt::runtime(lang["history_version"].c_str()), update.first);
                ImGui::TextColored(GUI_COLOR_TEXT, "%s", version_str.c_str());
                ImGui::SetWindowFontScale(1.f);
                ImGui::PushTextWrapPos(WINDOW_SIZE.x - (80.f * SCALE.x));
                ImGui::TextColored(GUI_COLOR_TEXT, "%s\n", update.second.c_str());
                ImGui::PopTextWrapPos();
                ImGui::TextColored(GUI_COLOR_TEXT, "\n");
            }
            ImGui::EndChild();
            ImGui::SetWindowFontScale(1.4f * RES_SCALE.x);
            ImGui::SetCursorPos(ImVec2((WINDOW_SIZE.x / 2.f) - (BUTTON_SIZE.x / 2.f), WINDOW_SIZE.y - BUTTON_SIZE.y - (22.f * SCALE.y)));
        } else {
            // Delete Data
            const auto ICON_MARGIN = 24.f * SCALE.y;
            if (gui.app_selector.user_apps_icon.find(title_id) != gui.app_selector.user_apps_icon.end()) {
                ImGui::SetCursorPos(ImVec2((WINDOW_SIZE.x / 2.f) - (PUPOP_ICON_SIZE.x / 2.f), ICON_MARGIN));
                const auto POS_MIN = ImGui::GetCursorScreenPos();
                const ImVec2 POS_MAX(POS_MIN.x + PUPOP_ICON_SIZE.x, POS_MIN.y + PUPOP_ICON_SIZE.y);
                ImGui::GetWindowDrawList()->AddImageRounded(gui.app_selector.user_apps_icon[title_id], POS_MIN, POS_MAX, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, PUPOP_ICON_SIZE.x * SCALE.x, ImDrawFlags_RoundCornersAll);
            }
            ImGui::SetWindowFontScale(1.6f);
            ImGui::SetCursorPos(ImVec2((WINDOW_SIZE.x / 2.f) - (ImGui::CalcTextSize(APP_INDEX->stitle.c_str()).x / 2.f), ICON_MARGIN + PUPOP_ICON_SIZE.y + (4.f * SCALE.y)));
            ImGui::TextColored(GUI_COLOR_TEXT, "%s", APP_INDEX->stitle.c_str());
            ImGui::SetWindowFontScale(1.4f * RES_SCALE.x);
            const auto ask_delete = context_dialog == "save" ? lang["save_delete"].c_str() : lang["app_delete"].c_str();
            ImGui::SetCursorPos(ImVec2(WINDOW_SIZE.x / 2 - ImGui::CalcTextSize(ask_delete, 0, false, WINDOW_SIZE.x - (108.f * SCALE.x)).x / 2, (WINDOW_SIZE.y / 2) + 10));
            ImGui::PushTextWrapPos(WINDOW_SIZE.x - (54.f * SCALE.x));
            ImGui::TextColored(GUI_COLOR_TEXT, "%s", ask_delete);
            ImGui::PopTextWrapPos();
            if ((context_dialog == "app") && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", lang["app_delete_note"].c_str());
            ImGui::SetWindowFontScale(1.4f * RES_SCALE.x);
            ImGui::SetCursorPos(ImVec2((WINDOW_SIZE.x / 2) - (BUTTON_SIZE.x + (20.f * SCALE.x)), WINDOW_SIZE.y - BUTTON_SIZE.y - (24.0f * SCALE.y)));
            if (ImGui::Button(common["cancel"].c_str(), BUTTON_SIZE) || ImGui::IsKeyPressed(emuenv.cfg.keyboard_button_circle)) {
                context_dialog.clear();
            }
            ImGui::SameLine();
            ImGui::SetCursorPosX((WINDOW_SIZE.x / 2.f) + (20.f * SCALE.x));
        }
        if (ImGui::Button("OK", BUTTON_SIZE) || ImGui::IsKeyPressed(emuenv.cfg.keyboard_button_cross)) {
            if (context_dialog == "app")
                delete_app(gui, emuenv, app_path);
            else if (context_dialog == "save")
                fs::remove_all(SAVE_DATA_PATH);
            context_dialog.clear();
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();
    }

    // Information
    if (gui.live_area.app_information) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(display_size, ImGuiCond_Always);
        ImGui::Begin("##information", &gui.live_area.app_information, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.5f * RES_SCALE.x);
        ImGui::SetCursorPos(ImVec2(10.0f * SCALE.x, 10.0f * SCALE.y));
        if (ImGui::Button("X", ImVec2(40.f * SCALE.x, 40.f * SCALE.y)) || ImGui::IsKeyPressed(emuenv.cfg.keyboard_button_circle)) {
            gui.live_area.app_information = false;
            gui.live_area.information_bar = true;
        }
        if (get_app_icon(gui, title_id)->first == title_id) {
            ImGui::SetCursorPos(ImVec2((display_size.x / 2.f) - (INFO_ICON_SIZE.x / 2.f), 22.f * SCALE.x));
            const auto POS_MIN = ImGui::GetCursorScreenPos();
            const ImVec2 POS_MAX(POS_MIN.x + INFO_ICON_SIZE.x, POS_MIN.y + INFO_ICON_SIZE.y);
            ImGui::GetWindowDrawList()->AddImageRounded(get_app_icon(gui, title_id)->second, POS_MIN, POS_MAX, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, INFO_ICON_SIZE.x * SCALE.x, ImDrawFlags_RoundCornersAll);
        }
        ImGui::SetCursorPos(ImVec2((display_size.x / 2.f) - ImGui::CalcTextSize((lang["name"] + "  ").c_str()).x, INFO_ICON_SIZE.y + (50.f * SCALE.y)));
        ImGui::TextColored(GUI_COLOR_TEXT, "%s ", lang["name"].c_str());
        ImGui::SameLine();
        ImGui::PushTextWrapPos(display_size.x - (85.f * SCALE.x));
        ImGui::TextColored(GUI_COLOR_TEXT, "%s", APP_INDEX->title.c_str());
        ImGui::PopTextWrapPos();
        if (title_id.find("NPXS") == std::string::npos) {
            ImGui::Spacing();
            ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang["trophy_earning"] + "  ").c_str()).x);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s  %s", lang["trophy_earning"].c_str(), gui.app_selector.app_info.trophy.c_str());
            ImGui::Spacing();
            ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang["parental_controls"] + "  ").c_str()).x);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s  %s", lang["parental_controls"].c_str(), lang["level"].c_str());
            ImGui::SameLine();
            ImGui::TextColored(GUI_COLOR_TEXT, "%d", *reinterpret_cast<const uint16_t *>(APP_INDEX->parental_level.c_str()));
            ImGui::Spacing();
            ImGui::SetCursorPosX(((display_size.x / 2.f) - ImGui::CalcTextSize((lang["updated"] + "  ").c_str()).x));
            auto DATE_TIME = get_date_time(gui, emuenv, gui.app_selector.app_info.updated);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s  %s %s", lang["updated"].c_str(), DATE_TIME[DateTime::DATE_MINI].c_str(), DATE_TIME[DateTime::CLOCK].c_str());
            if (is_12_hour_format) {
                ImGui::SameLine();
                ImGui::TextColored(GUI_COLOR_TEXT, "%s", DATE_TIME[DateTime::DAY_MOMENT].c_str());
            }
            ImGui::Spacing();
            ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang["size"] + "  ").c_str()).x);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s", (lang["size"] + "  " + get_unit_size(gui.app_selector.app_info.size)).c_str());
            ImGui::Spacing();
            ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang["version"] + "  ").c_str()).x);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s  %s", lang["version"].c_str(), APP_INDEX->app_ver.c_str());
            ImGui::Spacing();
            ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang["title_id"] + "  ").c_str()).x);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s  %s", lang["title_id"].c_str(), APP_INDEX->title_id.c_str());
            ImGui::Spacing();
            ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang_compat.name + "  ").c_str()).x);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s", lang_compat.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(compat_state_color, " %s", compat_state_str.c_str());
            ImGui::Spacing();
            ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang["last_time_used"] + "  ").c_str()).x);
            ImGui::TextColored(GUI_COLOR_TEXT, "%s ", lang["last_time_used"].c_str());
            ImGui::SameLine();
            const auto time_app_index = get_time_app_index(gui, emuenv, app_path);
            if (time_app_index != gui.time_apps[emuenv.io.user_id].end()) {
                tm date_tm = {};
                SAFE_LOCALTIME(&time_app_index->last_time_used, &date_tm);
                auto LAST_TIME = get_date_time(gui, emuenv, date_tm);
                ImGui::TextColored(GUI_COLOR_TEXT, "%s %s", LAST_TIME[DateTime::DATE_MINI].c_str(), LAST_TIME[DateTime::CLOCK].c_str());
                if (is_12_hour_format) {
                    ImGui::SameLine();
                    ImGui::TextColored(GUI_COLOR_TEXT, "%s", LAST_TIME[DateTime::DAY_MOMENT].c_str());
                }
                ImGui::Spacing();
                ImGui::SetCursorPosX((display_size.x / 2.f) - ImGui::CalcTextSize((lang["time_used"] + "  ").c_str()).x);
                ImGui::TextColored(GUI_COLOR_TEXT, "%s  %s", lang["time_used"].c_str(), get_time_app_used(time_app_index->time_used).c_str());
            } else
                ImGui::TextColored(GUI_COLOR_TEXT, "%s", lang["never"].c_str());
        }
        ImGui::End();
    }
}

} // namespace gui
