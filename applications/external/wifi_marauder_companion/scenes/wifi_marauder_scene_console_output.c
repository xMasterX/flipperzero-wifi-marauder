#include "../wifi_marauder_app_i.h"

void wifi_marauder_console_output_handle_rx_data_cb(uint8_t* buf, size_t len, void* context) {
    furi_assert(context);
    WifiMarauderApp* app = context;

    if(app->ok_to_save_logs) {
        if(!app->is_writing_log) {
            app->is_writing_log = true;
            app->has_saved_logs_this_session = true;
            if(!app->log_file || !storage_file_is_open(app->log_file)) {
                wifi_marauder_create_log_file(app);
            }
        }

        storage_file_write(app->log_file, buf, len);
    }

    // If text box store gets too big, then truncate it
    app->text_box_store_strlen += len;
    if(app->text_box_store_strlen >= WIFI_MARAUDER_TEXT_BOX_STORE_SIZE - 1) {
        furi_string_right(app->text_box_store, app->text_box_store_strlen / 2);
        app->text_box_store_strlen = furi_string_size(app->text_box_store) + len;
    }

    // Null-terminate buf and append to text box store
    buf[len] = '\0';
    furi_string_cat_printf(app->text_box_store, "%s", buf);
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventRefreshConsoleOutput);
}

void wifi_marauder_console_output_handle_rx_packets_cb(uint8_t* buf, size_t len, void* context) {
    furi_assert(context);
    WifiMarauderApp* app = context;

    if(!app->ok_to_save_pcaps) {
        // user has declined this feature
        return;
    }

    // If it is a sniff function, open the pcap file for recording
    if(strncmp("sniff", app->selected_tx_string, strlen("sniff")) == 0 && !app->is_writing_pcap) {
        app->is_writing_pcap = true;
        if(!app->capture_file || !storage_file_is_open(app->capture_file)) {
            wifi_marauder_create_pcap_file(app);
        }
    }

    if(app->is_writing_pcap) {
        storage_file_write(app->capture_file, buf, len);
    }
}

void wifi_marauder_scene_console_output_on_enter(void* context) {
    WifiMarauderApp* app = context;

    TextBox* text_box = app->text_box;
    text_box_reset(app->text_box);
    text_box_set_font(text_box, TextBoxFontText);
    if(app->focus_console_start) {
        text_box_set_focus(text_box, TextBoxFocusStart);
    } else {
        text_box_set_focus(text_box, TextBoxFocusEnd);
    }
    if(app->is_command) {
        furi_string_reset(app->text_box_store);
        app->text_box_store_strlen = 0;
        if(0 == strncmp("help", app->selected_tx_string, strlen("help"))) {
            const char* help_msg = "Marauder companion " WIFI_MARAUDER_APP_VERSION "\n";
            furi_string_cat_str(app->text_box_store, help_msg);
            app->text_box_store_strlen += strlen(help_msg);
        }

        if(app->show_stopscan_tip) {
            const char* help_msg = "Press BACK to send stopscan\n";
            furi_string_cat_str(app->text_box_store, help_msg);
            app->text_box_store_strlen += strlen(help_msg);
        }
    }

    // Set starting text - for "View Log", this will just be what was already in the text box store
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
    if(furi_string_empty(app->text_box_store)) {
        char help_msg[256];
        snprintf(
            help_msg,
            sizeof(help_msg),
            "The log is empty! :(\nTry sending a command?\n\nSaving pcaps to flipper sdcard: %s\nSaving logs to flipper sdcard: %s",
            app->ok_to_save_pcaps ? "ON" : "OFF",
            app->ok_to_save_logs ? "ON" : "OFF");
        text_box_set_text(app->text_box, help_msg);
    }

    scene_manager_set_scene_state(app->scene_manager, WifiMarauderSceneConsoleOutput, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewConsoleOutput);

    // Register callback to receive data
    wifi_marauder_uart_set_handle_rx_data_cb(
        app->uart,
        wifi_marauder_console_output_handle_rx_data_cb); // setup callback for general log rx thread
    wifi_marauder_uart_set_handle_rx_data_cb(
        app->lp_uart,
        wifi_marauder_console_output_handle_rx_packets_cb); // setup callback for packets rx thread

    // Send command with newline '\n'
    if(app->is_command && app->selected_tx_string) {
        wifi_marauder_uart_tx(
            (uint8_t*)(app->selected_tx_string), strlen(app->selected_tx_string));
        wifi_marauder_uart_tx((uint8_t*)("\n"), 1);
    }
}

bool wifi_marauder_scene_console_output_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;

    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
        consumed = true;
    } else if(event.type == SceneManagerEventTypeTick) {
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_console_output_on_exit(void* context) {
    WifiMarauderApp* app = context;

    // Unregister rx callback
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    wifi_marauder_uart_set_handle_rx_data_cb(app->lp_uart, NULL);

    // Automatically stop the scan when exiting view
    if(app->is_command) {
        wifi_marauder_uart_tx((uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    }

    app->is_writing_pcap = false;
    if(app->capture_file && storage_file_is_open(app->capture_file)) {
        storage_file_close(app->capture_file);
    }

    app->is_writing_log = false;
    if(app->log_file && storage_file_is_open(app->log_file)) {
        storage_file_close(app->log_file);
    }
}