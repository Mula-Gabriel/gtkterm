# Graph Report - .  (2026-06-11)

## Corpus Check
- 79 files · ~72,637 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 870 nodes · 1878 edges · 66 communities (54 shown, 12 thin omitted)
- Extraction: 86% EXTRACTED · 14% INFERRED · 0% AMBIGUOUS · INFERRED: 272 edges (avg confidence: 0.8)
- Token cost: 360,547 input · 63,622 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Main Window & Menus (interface.c)|Main Window & Menus (interface.c)]]
- [[_COMMUNITY_Macro Config Dialog & TreeViews|Macro Config Dialog & TreeViews]]
- [[_COMMUNITY_Transport Layer & Port Lifecycle|Transport Layer & Port Lifecycle]]
- [[_COMMUNITY_Terminal Config Dialog & Settings|Terminal Config Dialog & Settings]]
- [[_COMMUNITY_Receive Buffer & Display Pipeline|Receive Buffer & Display Pipeline]]
- [[_COMMUNITY_Baud Rates & Buffer Func Pointers|Baud Rates & Buffer Func Pointers]]
- [[_COMMUNITY_Macro Button Panel & Polling|Macro Button Panel & Polling]]
- [[_COMMUNITY_Lua Script Engine (threading)|Lua Script Engine (threading)]]
- [[_COMMUNITY_Color Scheme & Menu Widgets|Color Scheme & Menu Widgets]]
- [[_COMMUNITY_Project Docs, Lua Examples & API|Project Docs, Lua Examples & API]]
- [[_COMMUNITY_parsecfg Data Structures|parsecfg Data Structures]]
- [[_COMMUNITY_Macro Argument Entry Widgets|Macro Argument Entry Widgets]]
- [[_COMMUNITY_Macro Config Dialog (screenshots)|Macro Config Dialog (screenshots)]]
- [[_COMMUNITY_Startup & Update Check Wiring|Startup & Update Check Wiring]]
- [[_COMMUNITY_Main Window UI (screenshots)|Main Window UI (screenshots)]]
- [[_COMMUNITY_Macro Send & Polling Callbacks|Macro Send & Polling Callbacks]]
- [[_COMMUNITY_Lua Tap & Idle Marshalling|Lua Tap & Idle Marshalling]]
- [[_COMMUNITY_Config SaveLoad via parsecfg|Config Save/Load via parsecfg]]
- [[_COMMUNITY_Search Bar|Search Bar]]
- [[_COMMUNITY_Macro & List Data Model|Macro & List Data Model]]
- [[_COMMUNITY_Device Hotplug & Signal Port Control|Device Hotplug & Signal Port Control]]
- [[_COMMUNITY_Macro Button Rebuild & List Models|Macro Button Rebuild & List Models]]
- [[_COMMUNITY_CLI Parsing & Config Defaults|CLI Parsing & Config Defaults]]
- [[_COMMUNITY_Macro Format Argument Parsing|Macro Format Argument Parsing]]
- [[_COMMUNITY_TCP Transport Config (screenshots)|TCP Transport Config (screenshots)]]
- [[_COMMUNITY_parsecfg INI Parser & Schema|parsecfg INI Parser & Schema]]
- [[_COMMUNITY_Self-Update Flow|Self-Update Flow]]
- [[_COMMUNITY_Config File Paths & Window State|Config File Paths & Window State]]
- [[_COMMUNITY_Baud Rate Tables|Baud Rate Tables]]
- [[_COMMUNITY_View Switching & Config Window|View Switching & Config Window]]
- [[_COMMUNITY_Macro Arg Save & Validation|Macro Arg Save & Validation]]
- [[_COMMUNITY_Lua Port API (opensendread)|Lua Port API (open/send/read)]]
- [[_COMMUNITY_i18n  locale conversion|i18n / locale conversion]]
- [[_COMMUNITY_Update Script|Update Script]]
- [[_COMMUNITY_Baudrates Generator Script|Baudrates Generator Script]]
- [[_COMMUNITY_Log File Start|Log File Start]]
- [[_COMMUNITY_Search Callbacks|Search Callbacks]]
- [[_COMMUNITY_Read Control Signals|Read Control Signals]]
- [[_COMMUNITY_Send Break|Send Break]]
- [[_COMMUNITY_Set Control Signals|Set Control Signals]]
- [[_COMMUNITY_Hex Display InitClear|Hex Display Init/Clear]]
- [[_COMMUNITY_Clear Func Hook|Clear Func Hook]]
- [[_COMMUNITY_Build Hook (settings.json)|Build Hook (settings.json)]]
- [[_COMMUNITY_Script Stop Callback|Script Stop Callback]]
- [[_COMMUNITY_Terminal Configuration|Terminal Configuration]]

## God Nodes (most connected - your core abstractions)
1. `gpointer` - 27 edges
2. `GtkWidget` - 24 edges
3. `show_message()` - 24 edges
4. `gpointer` - 22 edges
5. `rebuild_macro_buttons()` - 21 edges
6. `gboolean` - 19 edges
7. `main()` - 17 edges
8. `gchar` - 17 edges
9. `gchar` - 16 edges
10. `really_save_config()` - 16 edges

## Surprising Connections (you probably didn't know these)
- `memory-safety-reviewer subagent` --references--> `gtkterm.* Lua API surface`  [INFERRED]
  .claude/agents/memory-safety-reviewer.md → scripts/01_hello.lua
- `Handle 0 = main port (always open) convention` --rationale_for--> `gtkterm.* Lua API surface`  [EXTRACTED]
  .claude/skills/lua-script/SKILL.md → scripts/01_hello.lua
- `README.md — GTKTerm fork features` --references--> `gtkterm.* Lua API surface`  [EXTRACTED]
  README.md → scripts/01_hello.lua
- `GTKTerm Main Window with Macro Panel` --semantically_similar_to--> `GTKTerm Main Window (Serial Boot Log)`  [INFERRED] [semantically similar]
  Capture1.png → screenshot.png
- `Menu Bar (File, Edition, Log, Configuration, Control signals, View, Help)` --semantically_similar_to--> `Menu Bar (File, Edit, Log, Configuration, Control signals, View, Help)`  [INFERRED] [semantically similar]
  Capture1.png → screenshot.png

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Receive path: device to screen** — transport_transport_read_cb, buffer_put_chars, terminal_display_put_text, terminal_display_put_hexadecimal [INFERRED 0.85]
- **serial.c delegates all I/O to transport.c** — serial_send_chars, serial_config_port, serial_close_port, transport_transport_send, transport_transport_open, transport_transport_close [EXTRACTED 0.95]
- **transport_open dispatches to three transport modes** — transport_transport_open, transport_transport_open_serial, transport_transport_open_tcp_client, transport_transport_open_tcp_server [EXTRACTED 0.95]
- **Macro send pipeline: format args, parse escapes, write to port** — macros_send_macro_with_args, macros_format_format_action_with_args, macros_format_parse_macro_string, interface_send_serial [EXTRACTED 1.00]
- **Lua-thread-to-GTK-main-loop marshalling via g_idle_add** — script_engine_lua_thread_func, script_engine_engine_log, script_engine_log_idle_cb, script_engine_send_macro_idle, script_engine_done_idle_cb [INFERRED 0.85]
- **Named-list rendering into macro arg combo boxes** — macro_panel_rebuild_macro_buttons, macros_format_macro_get_arg_infos, macros_list_macro_list_find, macros_list_macro_list_entry_value [EXTRACTED 1.00]
- **Self-update flow (check, revision, script, config url)** — update_update_check, update_git_revision, data_gtkterm_update_sh, term_config_config [EXTRACTED 1.00]
- **GKeyFile section strip/restore around parsecfg save** — term_config_cfg_strip_kf_sections, term_config_cfg_restore_kf_sections, term_config_save_config_silent, term_config_really_save_config [EXTRACTED 0.85]
- **External port open/close triggers (udev + signals)** — device_monitor_device_monitor_status, user_signals_handle_usr1, user_signals_handle_usr2, interface_interface_open_port, interface_interface_close_port [INFERRED 0.85]
- **Example Lua scripts exercise the gtkterm.* API surface** — scripts_01_hello, scripts_02_at_commands, scripts_07_tcp_client, scripts_06_hex_protocol, gtkterm_lua_api [EXTRACTED 1.00]
- **Auto-update feature: plan implements design spec per CLAUDE.md constraints** — plans_2026_06_08_auto_update, specs_2026_06_08_auto_update_design, claude_md [INFERRED 0.85]

## Communities (66 total, 12 thin omitted)

### Community 0 - "Main Window & Menus (interface.c)"
Cohesion: 0.06
Nodes (82): GCallback, GParamSpec, GSimpleAction, GVariant, action_state_sync_to_item(), Autoreconnect_toggled_callback(), build_submenu(), gboolean (+74 more)

### Community 1 - "Macro Config Dialog & TreeViews"
Cohesion: 0.08
Nodes (75): GdkEventFocus, GtkCellEditable, GtkCellRenderer, GtkCellRendererText, GtkTreeModel, GtkTreeView, macro_t, action_edited() (+67 more)

### Community 2 - "Transport Layer & Port Lifecycle"
Cohesion: 0.07
Nodes (65): GUdevClient, GUdevDevice, gchar, device_monitor_handle(), device_monitor_start(), device_monitor_status(), event_udev(), add_input() (+57 more)

### Community 3 - "Terminal Config Dialog & Settings"
Cohesion: 0.08
Nodes (64): gfloat, GPtrArray, GtkAdjustment, GtkDialog, GtkEditable, GtkFontButton, GtkSwitch, GtkTreeSelection (+56 more)

### Community 4 - "Receive Buffer & Display Pipeline"
Cohesion: 0.07
Nodes (45): display_func_t, gboolean, clear_buffer(), create_buffer(), delete_buffer(), get_display_func(), insert_timestamp(), put_chars() (+37 more)

### Community 5 - "Baud Rates & Buffer Func Pointers"
Cohesion: 0.05
Nodes (47): set_port_baudrate, baudrate_list, find_standard_baudrate, baudrates.sh baud table generator, speed_t_to_baud, clear_buffer, create_buffer, get_display_func (+39 more)

### Community 6 - "Macro Button Panel & Polling"
Cohesion: 0.13
Nodes (42): GtkComboBox, GtkToggleButton, macro_polling_t, apply_polling_css(), gboolean, gchar, GdkEvent, GdkEventButton (+34 more)

### Community 7 - "Lua Script Engine (threading)"
Cohesion: 0.11
Nodes (36): lua_State, ScriptDoneFunc, ScriptLogFunc, baud_constant(), gboolean, GIOChannel, GIOCondition, gpointer (+28 more)

### Community 8 - "Color Scheme & Menu Widgets"
Cohesion: 0.14
Nodes (31): GtkMenuItem, GtkOrientation, apply_color_scheme(), gboolean, gchar, GdkEvent, GdkEventButton, GdkRGBA (+23 more)

### Community 9 - "Project Docs, Lua Examples & API"
Cohesion: 0.08
Nodes (14): memory-safety-reviewer subagent, CircleCI build config (debian:buster), CLAUDE.md project instructions, gtkterm.* Lua API surface, Handle 0 = main port (always open) convention, Auto-update Implementation Plan, README.md — GTKTerm fork features, bytes_to_hex() (+6 more)

### Community 10 - "parsecfg Data Structures"
Cohesion: 0.24
Nodes (26): cfgErrorCode, cfgFileType, cfgKeywordValue, cfgStruct, cfgValueType, alloc_for_new_section(), FILE, cfgAllocForNewSection() (+18 more)

### Community 11 - "Macro Argument Entry Widgets"
Cohesion: 0.20
Nodes (28): GByteArray, guchar, macro_arg_info_t, apply_entry_validation(), GtkEntry, rebuild_macro_buttons(), update_entry_width(), apply_spec() (+20 more)

### Community 12 - "Macro Config Dialog (screenshots)"
Cohesion: 0.12
Nodes (19): Configure Macros Dialog, Macro Edit Buttons (Add/Delete/Capture Shortcut/Move), Macro Action Format Argument (%#NewList, \r\n), Macro Tab Grouping (General/LED), Macros Tab, Macros Table (Label/Shortcut/Action/Tab Columns), GTKTerm Configure Macros - Macros Tab Screenshot, List Entry Buttons (Add Entry/Delete Entry/Move) (+11 more)

### Community 13 - "Startup & Update Check Wiring"
Cohesion: 0.16
Nodes (16): GAsyncResult, GObject, gboolean, gpointer, startup_update_check(), gboolean, gchar, gpointer (+8 more)

### Community 14 - "Main Window UI (screenshots)"
Cohesion: 0.15
Nodes (17): GTKTerm Main Window with Macro Panel, Control Signal Indicators (DTR RTS CTS CD DSR RI), Macro Cmd Row with argument dropdown, Macro Button Panel (right side dock), Macro Tab Groups (General, LED), test Macro Button, Menu Bar (File, Edition, Log, Configuration, Control signals, View, Help), Status Bar with port config and control-signal indicators (+9 more)

### Community 15 - "Macro Send & Polling Callbacks"
Cohesion: 0.16
Nodes (17): ecriture, send_serial, create_macro_panel, on_list_action_button_clicked, on_macro_arg_button_clicked, on_macro_button_clicked_with_polling, polling_blink_callback, polling_timer_callback (+9 more)

### Community 16 - "Lua Tap & Idle Marshalling"
Cohesion: 0.14
Nodes (14): set_tap_func, unset_tap_func, done_idle_cb, engine_log, log_idle_cb, lua_main_port_tap, lua_thread_func, script_engine_run (+6 more)

### Community 17 - "Config Save/Load via parsecfg"
Cohesion: 0.22
Nodes (14): cfgAllocForNewSection, cfgDump, cfgParse, cfgSectionNumberToName, cfg_strip_kf_sections, delete_config, load_macros_file_callback, really_save_config (+6 more)

### Community 18 - "Search Bar"
Cohesion: 0.22
Nodes (12): gboolean, GdkEventKey, gpointer, GtkEntry, GtkWidget, GtkWindow, VteTerminal, entry_key_press_event_callback() (+4 more)

### Community 19 - "Macro & List Data Model"
Cohesion: 0.18
Nodes (13): Config_macros, create_shortcuts, list_entry_t, macro_list_add, macro_list_find, macro_list_t, macro_lists_free, macro_lists_init (+5 more)

### Community 20 - "Device Hotplug & Signal Port Control"
Cohesion: 0.23
Nodes (12): device_monitor_handle, device_monitor_start, device_monitor_status, event_udev, interface_close_port, interface_open_port, Set_status_message, Set_window_title (+4 more)

### Community 21 - "Macro Button Rebuild & List Models"
Cohesion: 0.21
Nodes (12): rebuild_macro_buttons, restore_macro_polling, create_lists_model, macro_type_is_float, macro_list_count, macro_list_entry_count, macro_list_entry_display, macro_list_entry_value (+4 more)

### Community 22 - "CLI Parsing & Config Defaults"
Cohesion: 0.33
Nodes (11): display_help, read_command_line, Check_configuration_file, config (configuration_port), ConfigFlags, Copy_configuration, Hard_default_configuration, load_config (+3 more)

### Community 23 - "Macro Format Argument Parsing"
Cohesion: 0.25
Nodes (11): apply_spec, escape_arg_backslashes, format_action_with_args, macro_arg_info_t, macro_arg_infos_free, macro_count_format_args, macro_get_arg_infos, parse_one_spec (+3 more)

### Community 24 - "TCP Transport Config (screenshots)"
Cohesion: 0.33
Nodes (10): Configuration Dialog, Control Signals Status Bar (DTR RTS CTS CD DSR RI), Macro Button Panel with General and LED Tabs, Main Menu Bar (File Edit Log Configuration Control signals View Help), GTKTerm TCP Client Configuration Dialog Screenshot, TCP Client Transport Mode, TCP Host and Port Fields, Transport Type Selector (+2 more)

### Community 25 - "parsecfg INI Parser & Schema"
Cohesion: 0.25
Nodes (9): cfgStoreValue, cfgStruct, parse_ini, parse_simple, parse_word, store_value, cfg[] config schema table, config_bg_color (+1 more)

### Community 26 - "Self-Update Flow"
Cohesion: 0.29
Nodes (8): gtkterm-update.sh self-update script, help_about_callback, find_terminal, GIT_REVISION (baked commit), offer_update, on_ls_remote_done, run_update_script, update_check

### Community 27 - "Config File Paths & Window State"
Cohesion: 0.33
Nodes (6): save_script_colors, cfg_restore_kf_sections, config_file_init, get_config_file_path, load_window_state, save_window_state

### Community 28 - "Baud Rate Tables"
Cohesion: 0.47
Nodes (4): speed_t, set_port_baudrate(), find_standard_baudrate(), speed_t_to_baud()

### Community 29 - "View Switching & Config Window"
Cohesion: 0.40
Nodes (5): set_view, show_message, check_text_input, Config_Port_Fenetre, transport_toggle_cb

### Community 30 - "Macro Arg Save & Validation"
Cohesion: 0.40
Nodes (5): save_arg_from_widget, save_entry_arg, macro_type_value_valid, macro_set_arg, macros_file_save

### Community 31 - "Lua Port API (open/send/read)"
Cohesion: 0.40
Nodes (5): l_open, l_send, lua_port_t, port_read_cb, register_api

### Community 32 - "i18n / locale conversion"
Cohesion: 0.50
Nodes (4): i18n_perror, i18n_printf, iconv_from_utf8_to_locale, strerror_utf8

## Knowledge Gaps
- **152 isolated node(s):** `gtkterm-update.sh script`, `baudrates.sh script`, `gboolean`, `GUdevClient`, `gchar` (+147 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **12 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Save_shortcuts` connect `Macro & List Data Model` to `Macro Format Argument Parsing`, `Macro Button Rebuild & List Models`, `Macro Arg Save & Validation`, `Macro Send & Polling Callbacks`?**
  _High betweenness centrality (0.283) - this node is a cross-community bridge._
- **Why does `macro_polling_t` connect `Macro Button Panel & Polling` to `Macro & List Data Model`?**
  _High betweenness centrality (0.280) - this node is a cross-community bridge._
- **Why does `macro_t` connect `Macro & List Data Model` to `Macro Button Panel & Polling`?**
  _High betweenness centrality (0.279) - this node is a cross-community bridge._
- **Are the 21 inferred relationships involving `show_message()` (e.g. with `ecriture()` and `save_ascii_file()`) actually correct?**
  _`show_message()` has 21 INFERRED edges - model-reasoned connections that need verification._
- **Are the 12 inferred relationships involving `rebuild_macro_buttons()` (e.g. with `macro_arg_infos_free()` and `macro_count_format_args()`) actually correct?**
  _`rebuild_macro_buttons()` has 12 INFERRED edges - model-reasoned connections that need verification._
- **What connects `gtkterm-update.sh script`, `baudrates.sh script`, `gboolean` to the rest of the system?**
  _154 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Main Window & Menus (interface.c)` be split into smaller, more focused modules?**
  _Cohesion score 0.05909351692484223 - nodes in this community are weakly interconnected._