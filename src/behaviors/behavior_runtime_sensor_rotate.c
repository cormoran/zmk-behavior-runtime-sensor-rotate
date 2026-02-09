/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_runtime_sensor_rotate

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/sensors.h>
#include <zmk/behavior_queue.h>
#include <zmk/virtual_key_position.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/behaviors/runtime_sensor_rotate.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_runtime_sensor_rotate_config {
    int tap_ms;
    uint8_t max_layers;
};

struct behavior_runtime_sensor_rotate_data {
    struct sensor_value remainder[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS][ZMK_KEYMAP_LAYERS_LEN];
    int triggers[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS][ZMK_KEYMAP_LAYERS_LEN];
    struct runtime_sensor_rotate_layer_bindings bindings[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS]
                                                        [ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS];
    bool data_accepted[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS][ZMK_KEYMAP_LAYERS_LEN];
};

static struct behavior_runtime_sensor_rotate_data global_data = {0};

// Settings storage key
#define SETTINGS_KEY "runtime_sr"

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, "bindings", &next) && !next) {
        if (len != sizeof(global_data.bindings)) {
            LOG_ERR("Invalid settings data size: %d vs %d", len, sizeof(global_data.bindings));
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &global_data.bindings, sizeof(global_data.bindings));
        if (rc < 0) {
            LOG_ERR("Failed to read settings: %d", rc);
            return rc;
        }

        LOG_INF("Loaded runtime sensor rotate bindings from settings");
        return 0;
    }

    return -ENOENT;
}

static struct settings_handler settings_handler = {
    .name = SETTINGS_KEY,
    .h_set = settings_set,
};

int zmk_runtime_sensor_rotate_get_layer_bindings(
    uint8_t sensor_index, uint8_t layer, struct runtime_sensor_rotate_layer_bindings *bindings) {

    if (sensor_index >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS) {
        return -EINVAL;
    }
    if (layer >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS) {
        return -EINVAL;
    }

    *bindings = global_data.bindings[sensor_index][layer];
    return 0;
}

int zmk_runtime_sensor_rotate_set_layer_bindings(
    uint8_t sensor_index, uint8_t layer,
    const struct runtime_sensor_rotate_layer_bindings *bindings) {

    if (sensor_index >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS) {
        return -EINVAL;
    }
    if (layer >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS) {
        return -EINVAL;
    }

    global_data.bindings[sensor_index][layer] = *bindings;

    // Save to settings
    int rc = settings_save_one(SETTINGS_KEY "/bindings", &global_data.bindings,
                               sizeof(global_data.bindings));
    if (rc != 0) {
        LOG_ERR("Failed to save settings: %d", rc);
        return rc;
    }

    LOG_DBG("Saved bindings for sensor %d layer %d", sensor_index, layer);
    return 0;
}

int zmk_runtime_sensor_rotate_get_all_layer_bindings(
    uint8_t sensor_index, uint8_t max_layers,
    struct runtime_sensor_rotate_layer_bindings *bindings_array, uint8_t *actual_layers) {

    if (sensor_index >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS) {
        return -EINVAL;
    }

    uint8_t layers = ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS;
    if (max_layers < layers) {
        layers = max_layers;
    }

    for (uint8_t i = 0; i < layers; i++) {
        bindings_array[i] = global_data.bindings[sensor_index][i];
    }

    if (actual_layers) {
        *actual_layers = layers;
    }

    return 0;
}

static int behavior_runtime_sensor_rotate_accept_data(
    struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event,
    const struct zmk_sensor_config *sensor_config, size_t channel_data_size,
    const struct zmk_sensor_channel_data *channel_data) {

    const struct sensor_value value = channel_data[0].value;
    int triggers;
    int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);

    if (sensor_index >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS) {
        LOG_ERR("Sensor index %d out of bounds", sensor_index);
        return -EINVAL;
    }

    // Check if we already accepted data for this sensor/layer combination
    if (global_data.data_accepted[sensor_index][event.layer]) {
        LOG_DBG("Already accepted data for sensor %d layer %d", sensor_index, event.layer);
        return 0;
    }

    // Mark as accepted to prevent duplicate processing
    global_data.data_accepted[sensor_index][event.layer] = true;

    // Same logic as behavior_sensor_rotate_common
    if (value.val1 == 0) {
        triggers = value.val2;
    } else {
        struct sensor_value remainder = global_data.remainder[sensor_index][event.layer];

        remainder.val1 += value.val1;
        remainder.val2 += value.val2;

        if (remainder.val2 >= 1000000 || remainder.val2 <= -1000000) {
            remainder.val1 += remainder.val2 / 1000000;
            remainder.val2 %= 1000000;
        }

        int trigger_degrees = 360 / sensor_config->triggers_per_rotation;
        triggers = remainder.val1 / trigger_degrees;
        remainder.val1 %= trigger_degrees;

        global_data.remainder[sensor_index][event.layer] = remainder;
    }

    LOG_DBG("Sensor %d layer %d: val1=%d val2=%d remainder=%d/%d triggers=%d", sensor_index,
            event.layer, value.val1, value.val2,
            global_data.remainder[sensor_index][event.layer].val1,
            global_data.remainder[sensor_index][event.layer].val2, triggers);

    global_data.triggers[sensor_index][event.layer] = triggers;
    return 0;
}

static int behavior_runtime_sensor_rotate_process(struct zmk_behavior_binding *binding,
                                                  struct zmk_behavior_binding_event event,
                                                  enum behavior_sensor_binding_process_mode mode) {

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_runtime_sensor_rotate_config *cfg = dev->config;

    const int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);

    if (sensor_index >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS) {
        LOG_ERR("Sensor index %d out of bounds", sensor_index);
        return -EINVAL;
    }

    if (mode != BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER) {
        // Reset triggers and accepted flag
        global_data.triggers[sensor_index][event.layer] = 0;
        global_data.data_accepted[sensor_index][event.layer] = false;
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    int triggers = global_data.triggers[sensor_index][event.layer];

    // Reset accepted flag after processing
    global_data.data_accepted[sensor_index][event.layer] = false;

    if (event.layer >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS) {
        LOG_WRN("Layer %d exceeds max layers, skipping", event.layer);
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    struct zmk_behavior_binding triggered_binding;
    if (triggers > 0) {
        triggered_binding = global_data.bindings[sensor_index][event.layer].cw_binding;
    } else if (triggers < 0) {
        triggers = -triggers;
        triggered_binding = global_data.bindings[sensor_index][event.layer].ccw_binding;
    } else {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    // Check if binding is configured
    if (triggered_binding.behavior_dev == NULL || strlen(triggered_binding.behavior_dev) == 0) {
        LOG_DBG("No binding configured for sensor %d layer %d", sensor_index, event.layer);
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    LOG_DBG("Runtime sensor binding: %s (triggers=%d)", triggered_binding.behavior_dev, triggers);

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    event.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
#endif

    for (int i = 0; i < triggers; i++) {
        zmk_behavior_queue_add(&event, triggered_binding, true, cfg->tap_ms);
        zmk_behavior_queue_add(&event, triggered_binding, false, 0);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_runtime_sensor_rotate_driver_api = {
    .sensor_binding_accept_data = behavior_runtime_sensor_rotate_accept_data,
    .sensor_binding_process = behavior_runtime_sensor_rotate_process};

static int behavior_runtime_sensor_rotate_init(const struct device *dev) {
    // Register settings handler on first init
    static bool settings_registered = false;
    if (!settings_registered) {
        int rc = settings_subsys_init();
        if (rc != 0) {
            LOG_ERR("Failed to init settings subsystem: %d", rc);
        }

        rc = settings_register(&settings_handler);
        if (rc != 0) {
            LOG_ERR("Failed to register settings handler: %d", rc);
            return rc;
        }

        rc = settings_load();
        if (rc != 0) {
            LOG_WRN("Failed to load settings: %d", rc);
        }

        settings_registered = true;
        LOG_INF("Runtime sensor rotate settings registered");
    }

    return 0;
}

#define RUNTIME_SENSOR_ROTATE_INST(n)                                                              \
    static struct behavior_runtime_sensor_rotate_config                                            \
        behavior_runtime_sensor_rotate_config_##n = {                                              \
            .tap_ms = DT_INST_PROP_OR(n, tap_ms, 5),                                               \
            .max_layers = DT_INST_PROP_OR(n, layers, ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS),        \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_runtime_sensor_rotate_init, NULL, &global_data,            \
                            &behavior_runtime_sensor_rotate_config_##n, POST_KERNEL,               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_runtime_sensor_rotate_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SENSOR_ROTATE_INST)
