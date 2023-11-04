/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "properties"
// #define LOG_NDEBUG 0

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <cutils/sockets.h>
#include <errno.h>
#include <assert.h>

#include <cutils/properties.h>
#include <stdbool.h>
#include <inttypes.h>
#include <log/log.h>
#include <stdio.h>

static int property_map_current_index = -1;
static struct property_map_entry {
    char key[PROPERTY_KEY_MAX];
    char value[PROPERTY_VALUE_MAX];
} property_map[PROPERTY_MAX_ENTRY];


static bool property_ensure_loaded() {
    ALOGI("Loading property file: %s", ADB_DAEMON_PROPS);
    if (property_map_current_index >= 0) {
        return true;
    }

    FILE *file = fopen(ADB_DAEMON_PROPS, "r");
    if (file == NULL) {
        ALOGE("Failed to open property file: %s", ADB_DAEMON_PROPS);
        property_map_current_index = 0;
        return false;
    }

    char line[PROPERTY_KEY_MAX + PROPERTY_VALUE_MAX + 2];
    char key[PROPERTY_KEY_MAX + 1];
    char value[PROPERTY_VALUE_MAX + 1];
    int line_num = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        line_num++;

        // Skip empty lines and comments
        if (line[0] == '\n' || line[0] == '#') {
            continue;
        }

        int len = sscanf(line, "%" STRINGIFY(PROPERTY_KEY_MAX) "[^=]=%" STRINGIFY(PROPERTY_VALUE_MAX) "s",
                         key, value);
        if (len != 2) {
            ALOGE("Invalid property format in %s at line %d", ADB_DAEMON_PROPS, line_num);
            property_map_current_index = 0;
            return false;
        }

        // Remove trailing newline character from the value
        size_t value_len = strlen(value);
        if (value_len > 0 && value[value_len - 1] == '\n') {
            value[value_len - 1] = '\0';
        }

        // Store the key-value pair in the property map
        strncpy(property_map[line_num - 1].key, key, PROPERTY_KEY_MAX);
        strncpy(property_map[line_num - 1].value, value, PROP_VALUE_MAX);
    }

    fclose(file);
    property_map_current_index = line_num - 1;
    return true;
}

int8_t property_get_bool(const char *key, int8_t default_value) {
    if (!key) {
        return default_value;
    }

    int8_t result = default_value;
    char buf[PROPERTY_VALUE_MAX] = {'\0',};

    int len = property_get(key, buf, "");
    if (len == 1) {
        char ch = buf[0];
        if (ch == '0' || ch == 'n') {
            result = false;
        } else if (ch == '1' || ch == 'y') {
            result = true;
        }
    } else if (len > 1) {
         if (!strcmp(buf, "no") || !strcmp(buf, "false") || !strcmp(buf, "off")) {
            result = false;
        } else if (!strcmp(buf, "yes") || !strcmp(buf, "true") || !strcmp(buf, "on")) {
            result = true;
        }
    }

    return result;
}

// Convert string property to int (default if fails); return default value if out of bounds
static intmax_t property_get_imax(const char *key, intmax_t lower_bound, intmax_t upper_bound,
        intmax_t default_value) {
    if (!key) {
        return default_value;
    }

    intmax_t result = default_value;
    char buf[PROPERTY_VALUE_MAX] = {'\0',};
    char *end = NULL;

    int len = property_get(key, buf, "");
    if (len > 0) {
        int tmp = errno;
        errno = 0;

        // Infer base automatically
        result = strtoimax(buf, &end, /*base*/0);
        if ((result == INTMAX_MIN || result == INTMAX_MAX) && errno == ERANGE) {
            // Over or underflow
            result = default_value;
            ALOGV("%s(%s,%" PRIdMAX ") - overflow", __FUNCTION__, key, default_value);
        } else if (result < lower_bound || result > upper_bound) {
            // Out of range of requested bounds
            result = default_value;
            ALOGV("%s(%s,%" PRIdMAX ") - out of range", __FUNCTION__, key, default_value);
        } else if (end == buf) {
            // Numeric conversion failed
            result = default_value;
            ALOGV("%s(%s,%" PRIdMAX ") - numeric conversion failed",
                    __FUNCTION__, key, default_value);
        }

        errno = tmp;
    }

    return result;
}

int64_t property_get_int64(const char *key, int64_t default_value) {
    return (int64_t)property_get_imax(key, INT64_MIN, INT64_MAX, default_value);
}

int32_t property_get_int32(const char *key, int32_t default_value) {
    return (int32_t)property_get_imax(key, INT32_MIN, INT32_MAX, default_value);
}

int property_set(const char *key, const char *value) {
    // Add the key-value pair to the property_map
    if (property_map_current_index < PROPERTY_MAX_ENTRY - 1) {
        strncpy(property_map[property_map_current_index].key, key, PROPERTY_KEY_MAX);
        strncpy(property_map[property_map_current_index].value, value, PROPERTY_VALUE_MAX);
        property_map_current_index++;
    } else {
        return -1; // Property map is full
    }

    return 0;

    FILE *file = fopen(ADB_DAEMON_PROPS, "w");
    if (file == NULL) {
        printf("Failed to open property file for writing: %s\n", ADB_DAEMON_PROPS);
        return -2;
    }

    for (int i = 0; i <= property_map_current_index; i++) {
        fprintf(file, "%s=%s\n", property_map[i].key, property_map[i].value);
    }

    fclose(file);
    return 0;
}

int property_get(const char *key, char *value, const char *default_value) {
    if (!property_ensure_loaded() || !key) {
        return -1;
    }

    for (int i = 0; i <= property_map_current_index; i++) {
        if (strncmp(property_map[i].key, key, PROPERTY_KEY_MAX) == 0) {
            strncpy(value, property_map[i].value, PROPERTY_VALUE_MAX);
            return strlen(value);
        }
    }

    if (default_value) {
        size_t len = strlen(default_value);
        if (len >= PROPERTY_VALUE_MAX) {
            len = PROPERTY_VALUE_MAX - 1;
        }
        strncpy(value, default_value, PROPERTY_VALUE_MAX);
        value[len] = '\0';
        return len;
    }

    return 0;
}

struct property_list_callback_data
{
    void (*propfn)(const char *key, const char *value, void *cookie);
    void *cookie;
};

#if 0
static void property_list_callback(const prop_info *pi, void *cookie)
{
    char name[PROP_NAME_MAX];
    char value[PROP_VALUE_MAX];
    struct property_list_callback_data *data = cookie;

    __system_property_read(pi, name, value);
    data->propfn(name, value, data->cookie);
}
#endif
int property_list(
        void (*propfn)(const char *key, const char *value, void *cookie),
        void *cookie)
{
//FIXME
#if 0
    struct property_list_callback_data data = { propfn, cookie };
    return __system_property_foreach(property_list_callback, &data);
#endif
    return 0;
}
