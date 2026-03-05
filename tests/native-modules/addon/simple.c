// Simple C N-API addon for Node-API testing
#include <node_api.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static napi_value Greet(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    
    if (argc < 1) {
        napi_throw_type_error(env, NULL, "String expected");
        return NULL;
    }
    
    size_t str_len;
    napi_get_value_string_utf8(env, args[0], NULL, 0, &str_len);
    
    char* name = malloc(str_len + 1);
    napi_get_value_string_utf8(env, args[0], name, str_len + 1, &str_len);
    
    char result[512];
    snprintf(result, sizeof(result), "Hello from .node: %s!", name);
    
    napi_value napi_result;
    napi_create_string_utf8(env, result, NAPI_AUTO_LENGTH, &napi_result);
    
    free(name);
    return napi_result;
}

static napi_value Multiply(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "Two numbers expected");
        return NULL;
    }
    
    double a, b;
    napi_get_value_double(env, args[0], &a);
    napi_get_value_double(env, args[1], &b);
    
    napi_value result;
    napi_create_double(env, a * b, &result);
    return result;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        { "greet", 0, Greet, 0, 0, 0, napi_default, 0 },
        { "multiply", 0, Multiply, 0, 0, 0, napi_default, 0 },
    };
    napi_define_properties(env, exports, 2, desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
