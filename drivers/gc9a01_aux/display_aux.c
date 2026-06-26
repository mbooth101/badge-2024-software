#include "py/runtime.h"
#include "py/mpprint.h"
#include "gc9a01.h"
#include "mp_uctx.h"

#define TILDAGON_DISPLAY_WIDTH  240
#define TILDAGON_DISPLAY_HEIGHT 240


flow3r_bsp_gc9a01_config_t display_config = {
    .host = 1,
};

static flow3r_bsp_gc9a01_t display;

static uint8_t initialised_port = 0;

// High speed hexpansion pins
static uint8_t pin_mappings[24] = {39, 40, 41, 42, 35, 36, 37, 38, 34, 33, 47, 48, 11, 14, 13, 12, 18, 16, 15, 17, 3, 4, 5, 6};

static Ctx *display_aux_gfx_ctx(void);
static void display_aux_start_frame(Ctx *ctx);
static void display_aux_end_frame(Ctx *ctx);

static mp_obj_t gfx_init(mp_obj_t hexpansion_conf_obj) {
    mp_obj_t port_dest[2];
    mp_load_method(hexpansion_conf_obj, MP_QSTR_port, port_dest);
    int port = mp_obj_get_int(port_dest[0]);

    if (!initialised_port) {
        uint8_t* pins = pin_mappings + (port - 1) * 4;
        display_config.pin_sck = pins[1];
        display_config.pin_mosi = pins[0];
        display_config.pin_dc = pins[2];
        display_config.pin_cs = pins[3];
        esp_err_t ret = gc9a01_init(&display, &display_config);
        if (ret == ESP_OK) {
            initialised_port = port;
        }
    }
    ////////START
    Ctx *ctx = display_aux_gfx_ctx();
    display_aux_start_frame (ctx);
    ctx_rgb(ctx, 1, 0, 0);
    ctx_rectangle(ctx, -20, -20, 40, 40);
    ctx_fill(ctx);
    display_aux_end_frame(ctx);
    ////////END
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gfx_init_obj, gfx_init);

static mp_obj_t gfx_deinit(mp_obj_t port_obj) {
    mp_int_t port = mp_obj_get_int(port_obj);

    if (initialised_port == port) {
        gc9a01_deinit(&display);
        initialised_port = 0;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gfx_deinit_obj, gfx_deinit);
///////////////////////////START


EXT_RAM_BSS_ATTR
static uint8_t tildagon_fb[TILDAGON_DISPLAY_WIDTH * TILDAGON_DISPLAY_HEIGHT * 2];
static Ctx *tildagon_ctx = NULL;

static Ctx *display_aux_gfx_ctx(void) {
  if (tildagon_ctx == NULL)
  {
    tildagon_ctx = ctx_new_for_framebuffer (tildagon_fb, TILDAGON_DISPLAY_WIDTH, TILDAGON_DISPLAY_HEIGHT, TILDAGON_DISPLAY_WIDTH * 2, CTX_FORMAT_RGB565_BYTESWAPPED);
  }
  return tildagon_ctx;
}

static void display_aux_start_frame(Ctx *ctx) {
  int32_t offset_x = TILDAGON_DISPLAY_WIDTH / 2;
  int32_t offset_y = TILDAGON_DISPLAY_HEIGHT / 2;

  ctx_save (ctx);
  ctx_identity (ctx);
  ctx_apply_transform (ctx, 1.0f, 0.0f, offset_x, 0.0f, 1.0f, offset_y, 0.0f, 0.0f, 1.0f);
}

static void display_aux_end_frame(Ctx *ctx)
{
    ctx_restore(ctx);

    if (initialised_port) {
        static bool had_error = false;
        
        int ret = gc9a01_blit(&display, tildagon_fb);
        if (ret != 0) {
            if (!had_error) {
                mp_printf(&mp_plat_print, "display blit failed: %d\n", ret);
                had_error = true;
            }
        } else {
            if (had_error) {
                mp_printf(&mp_plat_print, "display blit success!\n");
                had_error = false;
            }
        }
    }
    // display.end_frame() cannot call ctx_end_frame() directly here: that resets
    // rasterizer state, including the framebuffer clip bounds, which leaves
    // subsequent frames blank. Advance only the texture eviction clock.
    ctx_set_textureclock(ctx, ctx_textureclock(ctx) + 1);
}

///////////////////////END
static const mp_rom_map_elem_t display_aux_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display_aux) },
    { MP_ROM_QSTR(MP_QSTR_gfx_init), MP_ROM_PTR(&gfx_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_gfx_deinit), MP_ROM_PTR(&gfx_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(display_aux_module_globals, display_aux_module_globals_table);

const mp_obj_module_t display_aux_user_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_aux_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_display_aux, display_aux_user_module);
