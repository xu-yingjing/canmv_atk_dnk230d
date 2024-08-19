#ifndef _DISPLAY_PY_H_
#define _DISPLAY_PY_H_

#include "py/obj.h"
#include "py/runtime.h"
#include "media.h"
#include "mpi_connector_api.h"
#include "mpi_vo_api.h"
#include "imlib.h"
#include <string.h>
#include <stdbool.h>

#define DISPLAY_MOD_ID              K_ID_VO
#define DISPLAY_DEV_ID              K_VO_DISPLAY_DEV_ID

enum
{
    LT9611 = 300,
    HX8377,
    ST7701,
    VIRT
};


typedef struct 
{
    int layer;
    rectangle_t rect;    //(x, y, w, h)
    k_pixel_format pix_format;
    int flag;
    int alpha;
}LayerConfig;

typedef struct 
{
    LayerConfig layer_config;
    media_linker *link;
}bindConfig_t;

typedef enum
{
    DISPLAY_LT9611 = 300,
    DISPLAY_HX8377 = 301,
    DISPLAY_ST7701 = 302,
    DISPLAY_VIRT = 303,
}display_connector_type;

typedef enum
{
    DISPLAY_FLAG_ROTATION_0        = (1 << 0),
    DISPLAY_FLAG_ROTATION_90       = (1 << 1),
    DISPLAY_FLAG_ROTATION_180      = (1 << 2),
    DISPLAY_FLAG_ROTATION_270      = (1 << 3),
    DISPLAY_FLAG_MIRROR_NONE       = (1 << 4),
    DISPLAY_FLAG_MIRROR_HOR        = (1 << 5),
    DISPLAY_FLAG_MIRROR_VER        = (1 << 6),
    DISPLAY_FLAG_MIRROR_BOTH       = (1 << 7)
}display_flag;

typedef enum
{
    DISPLAY_LAYER_VIDEO1 = K_VO_DISPLAY_CHN_ID1,
    DISPLAY_LAYER_VIDEO2 = K_VO_DISPLAY_CHN_ID2,
    DISPLAY_LAYER_OSD0 = K_VO_DISPLAY_CHN_ID3,
    DISPLAY_LAYER_OSD1 = K_VO_DISPLAY_CHN_ID4,
    DISPLAY_LAYER_OSD2 = K_VO_DISPLAY_CHN_ID5,
    DISPLAY_LAYER_OSD3 = K_VO_DISPLAY_CHN_ID6
}display_layer_osd;

typedef struct 
{
    bool _is_inited;
    int _osd_layer_num;
    bool _write_back_to_ide;
    display_flag _ide_vo_wbc_flag;
    k_connector_type _connector_type;
    int _width;
    int _height;
    bool _connector_is_st7701;
    k_connector_info _connector_info;

    LayerConfig *_layer_cfgs[K_VO_MAX_CHN_NUMS];
    bool _layer_configured[K_VO_MAX_CHN_NUMS];

    bindConfig_t *_layer_bind_cfg[K_VO_MAX_CHN_NUMS];
    media_buffer *_layer_rotate_buffer;
    media_buffer *_layer_disp_buffers[K_VO_MAX_CHN_NUMS];
}display_t;

typedef struct {
    mp_obj_base_t base;
    display_t display;
} mp_obj_display_t;

extern media_manager media_class_method_obj;

static inline bindConfig_t * bind_config_new(k_mpp_chn *src,k_mpp_chn *dst,LayerConfig *layer_config)
{
    bindConfig_t *obj = malloc(sizeof(bindConfig_t));
    if (obj == NULL)
    {
        mp_printf(&mp_plat_print,"malloc failed");
        return NULL;
    }

    obj->link = media_manager_link(&media_class_method_obj,src, dst);
    
    memcpy(&obj->layer_config, layer_config, sizeof(LayerConfig));

    return obj;
}

static inline void bind_config_del(bindConfig_t *obj)
{
    if (obj->link != NULL)
    {
        media_manager_del_link(&media_class_method_obj,obj->link);
        obj->link = NULL;
    }
    free(obj);
}


#endif // !

