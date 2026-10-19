#include "workload.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void);
void app_main(void)
{
    fixture_checkpoint(0);
    for (;;) {
        fixture_cycle();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
