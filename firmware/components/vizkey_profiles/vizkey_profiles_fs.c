#include "vizkey_profiles.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#if defined(__has_include)
#if __has_include("esp_littlefs.h")
#include "esp_littlefs.h"
#define VIZKEY_HAVE_LITTLEFS 1
#else
#define VIZKEY_HAVE_LITTLEFS 0
#endif
#else
#define VIZKEY_HAVE_LITTLEFS 0
#endif

static const char *TAG = "vizkey_profiles_fs";
static const char *MOUNT_PATH = "/littlefs";
static const char *PARTITION_LABEL = "littlefs";

static bool s_mounted;

static esp_err_t vizkey_macro_path(char *buf, size_t len, uint16_t macro_id)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int wrote = snprintf(buf, len, "%s/macro_%u.bin", MOUNT_PATH, macro_id);
    if (wrote < 0 || (size_t)wrote >= len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t vizkey_profiles_fs_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

#if VIZKEY_HAVE_LITTLEFS
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT_PATH,
        .partition_label = PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "LittleFS mount failed");
    s_mounted = true;

    size_t total = 0;
    size_t used = 0;
    if (esp_littlefs_info(PARTITION_LABEL, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS ready: total=%u used=%u", (unsigned)total, (unsigned)used);
    }

    return ESP_OK;
#else
    ESP_LOGW(TAG, "esp_littlefs.h not available; macro file storage disabled in this build");
    return ESP_OK;
#endif
}

esp_err_t vizkey_profiles_fs_write_macro(uint16_t macro_id, const void *blob, size_t len)
{
#if !VIZKEY_HAVE_LITTLEFS
    (void)macro_id;
    (void)blob;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (blob == NULL && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[64];
    ESP_RETURN_ON_ERROR(vizkey_macro_path(path, sizeof(path), macro_id), TAG, "Path build failed");

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }

    if (len > 0 && fwrite(blob, 1, len, f) != len) {
        fclose(f);
        return ESP_FAIL;
    }

    fclose(f);
    return ESP_OK;
#endif
}

esp_err_t vizkey_profiles_fs_read_macro(uint16_t macro_id, void *blob, size_t len, size_t *bytes_read)
{
#if !VIZKEY_HAVE_LITTLEFS
    (void)macro_id;
    (void)blob;
    (void)len;
    if (bytes_read != NULL) {
        *bytes_read = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (blob == NULL && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char path[64];
    ESP_RETURN_ON_ERROR(vizkey_macro_path(path, sizeof(path), macro_id), TAG, "Path build failed");

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t count = 0;
    if (len > 0) {
        count = fread(blob, 1, len, f);
    }
    fclose(f);

    if (bytes_read != NULL) {
        *bytes_read = count;
    }
    return ESP_OK;
#endif
}
