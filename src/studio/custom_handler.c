/**
 * Template Feature - Custom Studio RPC Handler
 *
 * This file implements a custom RPC subsystem for ZMK Studio.
 * It demonstrates the minimum code needed to handle custom RPC requests.
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/template/custom.pb.h>
#include <zmk/behaviors/runtime_sensor_rotate.h>
#include <zmk/behavior.h>
#include <zmk/sensors.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/**
 * Metadata for the custom subsystem.
 * - ui_urls: URLs where the custom UI can be loaded from
 * - security: Security level for the RPC handler
 */
static struct zmk_rpc_custom_subsystem_meta template_feature_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("http://localhost:5173"),
    // Unsecured is suggested by default to avoid unlocking in un-reliable
    // environments.
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

/**
 * Register the custom RPC subsystem.
 * The first argument is the subsystem name used to route requests from the
 * frontend. Format: <namespace>__<feature> (double underscore)
 */
ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__template, &template_feature_meta, template_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__template, zmk_template_Response);

static int handle_set_layer_bindings(const zmk_template_SetLayerBindingsRequest *req,
                                     zmk_template_Response *resp);
static int handle_get_all_layer_bindings(const zmk_template_GetAllLayerBindingsRequest *req,
                                         zmk_template_Response *resp);
static int handle_get_sensors(const zmk_template_GetSensorsRequest *req,
                              zmk_template_Response *resp);

/**
 * Main request handler for the custom RPC subsystem.
 * Sets up the encoding callback for the response.
 */
static bool template_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                        pb_callback_t *encode_response) {
    zmk_template_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__template, encode_response);

    zmk_template_Request req = zmk_template_Request_init_zero;

    // Decode the incoming request from the raw payload
    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, zmk_template_Request_fields, &req)) {
        LOG_WRN("Failed to decode template request: %s", PB_GET_ERROR(&req_stream));
        zmk_template_ErrorResponse err = zmk_template_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to decode request");
        resp->which_response_type = zmk_template_Response_error_tag;
        resp->response_type.error = err;
        return true;
    }

    int rc = 0;
    switch (req.which_request_type) {
    case zmk_template_Request_set_layer_bindings_tag:
        rc = handle_set_layer_bindings(&req.request_type.set_layer_bindings, resp);
        break;
    case zmk_template_Request_get_all_layer_bindings_tag:
        rc = handle_get_all_layer_bindings(&req.request_type.get_all_layer_bindings, resp);
        break;
    case zmk_template_Request_get_sensors_tag:
        rc = handle_get_sensors(&req.request_type.get_sensors, resp);
        break;
    default:
        LOG_WRN("Unsupported template request type: %d", req.which_request_type);
        rc = -1;
    }

    if (rc != 0) {
        zmk_template_ErrorResponse err = zmk_template_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to process request");
        resp->which_response_type = zmk_template_Response_error_tag;
        resp->response_type.error = err;
    }
    return true;
}

static int handle_set_layer_bindings(const zmk_template_SetLayerBindingsRequest *req,
                                     zmk_template_Response *resp) {
    LOG_DBG("Set layer bindings: sensor=%d layer=%d", req->sensor_index, req->layer);

    struct runtime_sensor_rotate_layer_bindings bindings;

    // Convert behavior_id to local_id
    bindings.cw_binding.behavior_local_id = req->cw_binding.behavior_id;
    bindings.cw_binding.param1 = req->cw_binding.param1;
    bindings.cw_binding.param2 = req->cw_binding.param2;
    bindings.cw_binding.tap_ms = req->cw_binding.tap_ms;

    bindings.ccw_binding.behavior_local_id = req->ccw_binding.behavior_id;
    bindings.ccw_binding.param1 = req->ccw_binding.param1;
    bindings.ccw_binding.param2 = req->ccw_binding.param2;
    bindings.ccw_binding.tap_ms = req->ccw_binding.tap_ms;

    int rc = zmk_runtime_sensor_rotate_set_layer_bindings(req->sensor_index, req->layer, &bindings);

    zmk_template_SetLayerBindingsResponse result = zmk_template_SetLayerBindingsResponse_init_zero;
    result.success = (rc == 0);

    resp->which_response_type = zmk_template_Response_set_layer_bindings_tag;
    resp->response_type.set_layer_bindings = result;
    return rc;
}

static int handle_get_all_layer_bindings(const zmk_template_GetAllLayerBindingsRequest *req,
                                         zmk_template_Response *resp) {
    LOG_DBG("Get all layer bindings: sensor=%d", req->sensor_index);

    struct runtime_sensor_rotate_layer_bindings bindings[ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS];
    uint8_t actual_layers;

    int rc = zmk_runtime_sensor_rotate_get_all_layer_bindings(
        req->sensor_index, ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS, bindings, &actual_layers);

    if (rc != 0) {
        LOG_ERR("Failed to get all layer bindings: %d", rc);
        return rc;
    }

    zmk_template_GetAllLayerBindingsResponse result =
        zmk_template_GetAllLayerBindingsResponse_init_zero;
    result.bindings_count = actual_layers;

    for (uint8_t i = 0; i < actual_layers; i++) {
        result.bindings[i].layer = i;

        // Convert local_id to behavior_id (they are the same)
        result.bindings[i].cw_binding.behavior_id = bindings[i].cw_binding.behavior_local_id;
        result.bindings[i].cw_binding.param1 = bindings[i].cw_binding.param1;
        result.bindings[i].cw_binding.param2 = bindings[i].cw_binding.param2;
        result.bindings[i].cw_binding.tap_ms = bindings[i].cw_binding.tap_ms;

        result.bindings[i].ccw_binding.behavior_id = bindings[i].ccw_binding.behavior_local_id;
        result.bindings[i].ccw_binding.param1 = bindings[i].ccw_binding.param1;
        result.bindings[i].ccw_binding.param2 = bindings[i].ccw_binding.param2;
        result.bindings[i].ccw_binding.tap_ms = bindings[i].ccw_binding.tap_ms;
    }

    resp->which_response_type = zmk_template_Response_get_all_layer_bindings_tag;
    resp->response_type.get_all_layer_bindings = result;
    return 0;
}

#if ZMK_KEYMAP_HAS_SENSORS

#define _SENSOR_NAME(idx, node) DT_NODE_FULL_NAME(node)
#define SENSOR_NAME(idx, _i) _SENSOR_NAME(idx, ZMK_KEYMAP_SENSORS_BY_IDX(idx))

static const char *sensor_names[] = {LISTIFY(ZMK_KEYMAP_SENSORS_LEN, SENSOR_NAME, (, ), 0)};

#endif

static int handle_get_sensors(const zmk_template_GetSensorsRequest *req,
                              zmk_template_Response *resp) {
    LOG_DBG("Get sensors");

    zmk_template_GetSensorsResponse result = zmk_template_GetSensorsResponse_init_zero;

#if ZMK_KEYMAP_HAS_SENSORS
    result.sensors_count = ZMK_KEYMAP_SENSORS_LEN;

    for (uint8_t i = 0; i < ZMK_KEYMAP_SENSORS_LEN; i++) {
        result.sensors[i].index = i;
        strncpy(result.sensors[i].name, sensor_names[i], sizeof(result.sensors[i].name) - 1);
    }
#else
    result.sensors_count = 0;
#endif

    resp->which_response_type = zmk_template_Response_get_sensors_tag;
    resp->response_type.get_sensors = result;
    return 0;
}
