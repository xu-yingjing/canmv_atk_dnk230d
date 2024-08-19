#include "py/obj.h"
#include "py/runtime.h"
#include "k_vicap_comm.h"

#ifndef ALIGN_UP
#define ALIGN_UP(addr, size)        ((addr) + ((size) - 1)) & (~((size) - 1))
#endif  

extern const mp_obj_type_t media_manager_type;
extern const mp_obj_type_t mp_display_type;
extern const mp_obj_type_t mp_sensor_type;



STATIC const mp_rom_map_elem_t media_media_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_MediaManager), MP_ROM_PTR(&media_manager_type) },
};
STATIC MP_DEFINE_CONST_DICT(media_media_module_globals, media_media_module_globals_table);

const mp_obj_module_t media_media_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&media_media_module_globals,
};

static mp_obj_t mp_aligh_up(mp_obj_t base, mp_obj_t size) 
{
    return mp_obj_new_int(ALIGN_UP(mp_obj_get_int(base), mp_obj_get_int(size)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mp_aligh_up_obj,mp_aligh_up);

STATIC const mp_rom_map_elem_t media_display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_Display), MP_ROM_PTR(&mp_display_type) },
    { MP_ROM_QSTR(MP_QSTR_ALIGN_UP), MP_ROM_PTR(&mp_aligh_up_obj) },
};
STATIC MP_DEFINE_CONST_DICT(media_display_module_globals, media_display_module_globals_table);

const mp_obj_module_t media_display_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&media_display_module_globals,
};

// extern const mp_obj_module_t image_module;       ///demo: 请自行添加import image

STATIC const mp_rom_map_elem_t media_sensor_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_Sensor), MP_ROM_PTR(&mp_sensor_type) },
    // { MP_ROM_QSTR(MP_QSTR_image), MP_ROM_PTR(&image_module) },
    { MP_ROM_QSTR(MP_QSTR_CAM_CHN_ID_0), MP_ROM_INT(VICAP_DEV_ID_0) },
    { MP_ROM_QSTR(MP_QSTR_CAM_CHN_ID_1), MP_ROM_INT(VICAP_DEV_ID_1) },
    { MP_ROM_QSTR(MP_QSTR_CAM_CHN_ID_2), MP_ROM_INT(VICAP_DEV_ID_2) },
};
STATIC MP_DEFINE_CONST_DICT(media_sensor_module_globals, media_sensor_module_globals_table);

const mp_obj_module_t media_sensor_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&media_sensor_module_globals,
};



// 定义模块全局方法表
STATIC const mp_rom_map_elem_t media_module_globals_table[] = {
    // { MP_ROM_QSTR(MP_QSTR_mediaManager), MP_ROM_PTR(&media_manager_type) },
    { MP_ROM_QSTR(MP_QSTR_media), MP_ROM_PTR(&media_media_user_cmodule) },
    { MP_ROM_QSTR(MP_QSTR_display), MP_ROM_PTR(&media_display_user_cmodule) },
    { MP_ROM_QSTR(MP_QSTR_sensor), MP_ROM_PTR(&media_sensor_user_cmodule) },
};
STATIC MP_DEFINE_CONST_DICT(media_module_globals, media_module_globals_table);

const mp_obj_module_t media_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&media_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_Cmedia, media_user_cmodule);




