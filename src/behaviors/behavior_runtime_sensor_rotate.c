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
};

struct behavior_runtime_sensor_rotate_data {
    struct sensor_value remainder[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS][ZMK_KEYMAP_LAYERS_LEN];
    int triggers[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS][ZMK_KEYMAP_LAYERS_LEN];
    struct runtime_sensor_rotate_layer_bindings bindings[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS]
                                                        [ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS];
    bool data_accepted[ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS][ZMK_KEYMAP_LAYERS_LEN];
};

static struct behavior_runtime_sensor_rotate_data global_data = {};

// Settings storage key
#define SETTINGS_KEY "rsr"

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    int rc;
    int sensor_index, layer;

    // Parse key format: s<sensor_index>/l<layer>
    // Example: "s0/l1" for sensor 0, layer 1
    if (sscanf(name, "s%d/l%d", &sensor_index, &layer) == 2) {
        if (sensor_index < 0 || sensor_index >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS) {
            LOG_WRN("Invalid sensor index in settings: %d", sensor_index);
            return -EINVAL;
        }
        if (layer < 0 || layer >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS) {
            LOG_WRN("Invalid layer in settings: %d", layer);
            return -EINVAL;
        }

        if (len != sizeof(struct runtime_sensor_rotate_layer_bindings)) {
            LOG_ERR("Invalid settings data size for s%d/l%d: %d vs %d", sensor_index, layer, len,
                    sizeof(struct runtime_sensor_rotate_layer_bindings));
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &global_data.bindings[sensor_index][layer],
                     sizeof(struct runtime_sensor_rotate_layer_bindings));
        if (rc < 0) {
            LOG_ERR("Failed to read settings for s%d/l%d: %d", sensor_index, layer, rc);
            return rc;
        }

        LOG_DBG("Loaded bindings for sensor %d layer %d", sensor_index, layer);
        return 0;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(behavior_runtime_sensor_rotate, SETTINGS_KEY, NULL, settings_set,
                               NULL, NULL);

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

    // Save to settings with per-sensor, per-layer key
    char key[32];
    snprintf(key, sizeof(key), SETTINGS_KEY "/s%d/l%d", sensor_index, layer);

    int rc = settings_save_one(key, &global_data.bindings[sensor_index][layer],
                               sizeof(struct runtime_sensor_rotate_layer_bindings));
    if (rc != 0) {
        LOG_ERR("Failed to save settings for sensor %d layer %d: %d", sensor_index, layer, rc);
        return rc;
    }

    LOG_DBG("Saved bindings (local_id=%d) for sensor %d layer %d",
            bindings->cw_binding.behavior_local_id, sensor_index, layer);
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

    struct runtime_sensor_rotate_binding triggered_binding_data;
    if (triggers > 0) {
        triggered_binding_data = global_data.bindings[sensor_index][event.layer].cw_binding;
    } else if (triggers < 0) {
        triggers = -triggers;
        triggered_binding_data = global_data.bindings[sensor_index][event.layer].ccw_binding;
    } else {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    // Check if binding is configured
    if (triggered_binding_data.behavior_local_id == 0) {
        LOG_DBG("No binding configured for sensor %d layer %d", sensor_index, event.layer);
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    // Get behavior name from local_id
    const char *behavior_name =
        zmk_behavior_find_behavior_name_from_local_id(triggered_binding_data.behavior_local_id);
    if (!behavior_name) {
        LOG_ERR("Failed to find behavior for local_id %d",
                triggered_binding_data.behavior_local_id);
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    // Create the zmk_behavior_binding for execution
    struct zmk_behavior_binding triggered_binding = {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS)
        .local_id = triggered_binding_data.behavior_local_id,
#endif
        .behavior_dev = behavior_name,
        .param1 = triggered_binding_data.param1,
        .param2 = triggered_binding_data.param2,
    };

    // Check if the behavior is transparent
    const struct device *behavior_dev = zmk_behavior_get_binding(triggered_binding.behavior_dev);
    if (!behavior_dev) {
        LOG_ERR("Behavior device not found: %s", triggered_binding.behavior_dev);
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    LOG_DBG("Runtime sensor binding: %s (triggers=%d, tap_ms=%d)", triggered_binding.behavior_dev,
            triggers, triggered_binding_data.tap_ms);

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    event.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
#endif

    for (int i = 0; i < triggers; i++) {
        zmk_behavior_queue_add(&event, triggered_binding, true, triggered_binding_data.tap_ms);
        zmk_behavior_queue_add(&event, triggered_binding, false, 0);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_runtime_sensor_rotate_driver_api = {
    .sensor_binding_accept_data = behavior_runtime_sensor_rotate_accept_data,
    .sensor_binding_process = behavior_runtime_sensor_rotate_process};

// Macro to load a single default binding from DTS child node
#define LOAD_DEFAULT_BINDING(node_id)                                                              \
    do {                                                                                           \
        uint8_t sensor_index = DT_PROP(node_id, sensor_index);                                     \
        uint32_t layer_mask = DT_PROP(node_id, layer_mask);                                        \
        /* Get tap_ms from child, falling back to parent's tap_ms */                               \
        uint32_t tap_ms = DT_PROP_OR(node_id, tap_ms, DT_PROP_OR(DT_PARENT(node_id), tap_ms, 5));  \
                                                                                                   \
        if (sensor_index >= ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS) {                               \
            LOG_ERR("Invalid sensor_index %d in default binding", sensor_index);                   \
        } else {                                                                                   \
            struct runtime_sensor_rotate_layer_bindings binding = {0};                             \
                                                                                                   \
            /* Parse CW binding if present */                                                      \
            if (DT_NODE_HAS_PROP(node_id, cw_binding_behavior)) {                                  \
                const char *behavior_name = DT_PROP(node_id, cw_binding_behavior);                 \
                binding.cw_binding.behavior_local_id = zmk_behavior_get_local_id(behavior_name);   \
                binding.cw_binding.param1 = DT_PROP_OR(node_id, cw_binding_param1, 0);             \
                binding.cw_binding.param2 = DT_PROP_OR(node_id, cw_binding_param2, 0);             \
                binding.cw_binding.tap_ms = tap_ms;                                                \
            }                                                                                      \
                                                                                                   \
            /* Parse CCW binding if present */                                                     \
            if (DT_NODE_HAS_PROP(node_id, ccw_binding_behavior)) {                                 \
                const char *behavior_name = DT_PROP(node_id, ccw_binding_behavior);                \
                binding.ccw_binding.behavior_local_id = zmk_behavior_get_local_id(behavior_name);  \
                binding.ccw_binding.param1 = DT_PROP_OR(node_id, ccw_binding_param1, 0);           \
                binding.ccw_binding.param2 = DT_PROP_OR(node_id, ccw_binding_param2, 0);           \
                binding.ccw_binding.tap_ms = tap_ms;                                               \
            }                                                                                      \
                                                                                                   \
            /* Apply to all layers specified in the mask */                                        \
            for (uint8_t layer = 0; layer < ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS; layer++) {       \
                if (layer_mask & (1U << layer)) {                                                  \
                    /* Only set if not already configured (first wins on conflict) */              \
                    if (global_data.bindings[sensor_index][layer].cw_binding.behavior_local_id ==  \
                            0 &&                                                                   \
                        global_data.bindings[sensor_index][layer].ccw_binding.behavior_local_id == \
                            0) {                                                                   \
                        global_data.bindings[sensor_index][layer] = binding;                       \
                        LOG_DBG("Loaded default binding for sensor %d layer %d "                   \
                                "(behavior=%s,cw_local_id=%d)",                                    \
                                sensor_index, layer,                                               \
                                DT_PROP_OR(node_id, cw_binding_behavior, "none"),                  \
                                binding.cw_binding.behavior_local_id);                             \
                    }                                                                              \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
    } while (0);

#define RUNTIME_SENSOR_ROTATE_INST(n)                                                              \
    static struct behavior_runtime_sensor_rotate_config                                            \
        behavior_runtime_sensor_rotate_config_##n = {                                              \
            .tap_ms = DT_INST_PROP_OR(n, tap_ms, 5),                                               \
    };                                                                                             \
                                                                                                   \
    static int behavior_runtime_sensor_rotate_init_##n(const struct device *dev) {                 \
        /* Process all child nodes to load default bindings */                                     \
        DT_INST_FOREACH_CHILD_STATUS_OKAY(n, LOAD_DEFAULT_BINDING)                                 \
        return 0;                                                                                  \
    }                                                                                              \
                                                                                                   \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_runtime_sensor_rotate_init_##n, NULL, &global_data,        \
                            &behavior_runtime_sensor_rotate_config_##n, POST_KERNEL,               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_runtime_sensor_rotate_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SENSOR_ROTATE_INST)
