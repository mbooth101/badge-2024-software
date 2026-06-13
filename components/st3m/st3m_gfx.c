#include "st3m_gfx.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task.h"
#include "esp_timer.h"

// clang-format off
#include "ctx_config.h"
#include "ctx.h"
// clang-format on


#include "flow3r_bsp.h"
#include "st3m_counter.h"

uint8_t st3m_pal[256 * 3]; // XXX - unused

static float smoothed_fps = 0.0f;

static st3m_counter_rate_t rast_rate;

float st3m_gfx_fps(void) { return smoothed_fps; }
void st3m_gfx_fps_update (void)
{
        st3m_counter_rate_sample(&rast_rate);
        float rate = 1000000.0 / st3m_counter_rate_average(&rast_rate);
        smoothed_fps = smoothed_fps * 0.6 + 0.4 * rate;
}


void st3m_gfx_init(void) {
    st3m_counter_rate_init(&rast_rate);
    flow3r_bsp_display_init();
}
