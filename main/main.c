#include "bsp_display.h"
#include "bsp_battery.h"
#include "csi_scope.h"
#include "esp_log.h"

void app_main(void)
{
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE("csi_scope", "Display initialization failed");
        return;
    }
    if (!csi_scope_ui_init()) ESP_LOGW("csi_scope", "Button/UI initialization incomplete");
    bsp_display_backlight(65);
    if (bsp_battery_init() != ESP_OK) ESP_LOGW("csi_scope", "Battery gauge unavailable");
    csi_scope_run();
}
