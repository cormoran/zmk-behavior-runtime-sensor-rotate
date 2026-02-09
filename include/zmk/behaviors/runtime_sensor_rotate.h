/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/behavior.h>
#include <zmk/keymap.h>

#define ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS 8
#define ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS 2

struct runtime_sensor_rotate_layer_bindings {
    struct zmk_behavior_binding cw_binding;
    struct zmk_behavior_binding ccw_binding;
};

/**
 * Get the layer bindings for a specific sensor and layer
 */
int zmk_runtime_sensor_rotate_get_layer_bindings(
    uint8_t sensor_index, uint8_t layer, struct runtime_sensor_rotate_layer_bindings *bindings);

/**
 * Set the layer bindings for a specific sensor and layer
 */
int zmk_runtime_sensor_rotate_set_layer_bindings(
    uint8_t sensor_index, uint8_t layer,
    const struct runtime_sensor_rotate_layer_bindings *bindings);

/**
 * Get all layer bindings for a specific sensor
 */
int zmk_runtime_sensor_rotate_get_all_layer_bindings(
    uint8_t sensor_index, uint8_t max_layers,
    struct runtime_sensor_rotate_layer_bindings *bindings_array, uint8_t *actual_layers);
