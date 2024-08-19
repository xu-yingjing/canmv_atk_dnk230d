#ifndef _SENSOR_PY_H_
#define _SENSOR_PY_H_

#include "py/obj.h"
#include "py/runtime.h"
#include "k_vicap_comm.h"
#include "mpconfigboard.h"
#include "mpi_sensor_api.h"
#include "mpi_vicap_api.h"
#include "media.h"
#include "imlib.h"
#include <string.h>

#define FRAME_SIZE_INVAILD      31
#define ALIGN_UP(addr, size)        ((addr) + ((size) - 1)) & (~((size) - 1))
#define VICAP_ALIGN_4K              (0x1000)
#define CAM_OUT_WIDTH_MIN       64
#define CAM_OUT_HEIGHT_MIN      64
#define CAMERA_MOD_ID           K_ID_VI

#define sensor_each(...)                                            \
    for (size_t i = 0; i < CAM_DEV_ID_MAX; i++)\
    {\
        sensor_t *sensor = Sensor._devs[i];\
        if (sensor == NULL)\
            continue;\
        if ((!sensor->_dev_attr.dev_enable) || sensor->_is_started)\
            continue;\
        __VA_ARGS__\
    }\

enum
{
    CAM_DEV_ID_0 = VICAP_DEV_ID_0,
    CAM_DEV_ID_1 = VICAP_DEV_ID_1,
    CAM_DEV_ID_2 = VICAP_DEV_ID_2,
    CAM_DEV_ID_MAX = VICAP_DEV_ID_MAX,
};

enum
{
    CAM_CHN_ID_0 = VICAP_CHN_ID_0,
    CAM_CHN_ID_1 = VICAP_CHN_ID_1,
    CAM_CHN_ID_2 = VICAP_CHN_ID_2,
    CAM_CHN_ID_MAX = VICAP_CHN_ID_MAX
};

enum
{
    SENSOR_RGB565   = PIXEL_FORMAT_RGB_565,
    SENSOR_RGB888   = PIXEL_FORMAT_RGB_888,
    SENSOR_RGBP888  = PIXEL_FORMAT_RGB_888_PLANAR,
    SENSOR_YUV420SP = PIXEL_FORMAT_YUV_SEMIPLANAR_420,
    SENSOR_GRAYSCALE = 303
};

typedef struct
{
    bool is_init;
    int id;
    int chn;
    int size;
    long phys;
    long virt;
}dumped_image_t;

typedef struct 
{
    int _dft_input_buff_num;
    int _dft_output_buff_num;
    int _dev_id;
    int _type;

    k_vicap_dev_attr _dev_attr;
    k_vicap_chn_attr _chn_attr[VICAP_CHN_ID_MAX];
    bool _buf_init[VICAP_CHN_ID_MAX];
    bool _buf_in_init;
    bool _is_started;

    dumped_image_t _imgs[VICAP_CHN_ID_MAX];
    bool _is_rgb565[VICAP_CHN_ID_MAX];
    bool _is_grayscale[VICAP_CHN_ID_MAX];
    int _framesize[VICAP_CHN_ID_MAX];
}sensor_t;

typedef struct 
{
    mp_obj_base_t base;
    sensor_t sensor;
}mp_sensor_t;

typedef struct 
{
    sensor_t *_devs[CAM_DEV_ID_MAX];
}sensor_class_methods;

sensor_class_methods Sensor = {0};

extern const mp_obj_type_t mp_sensor_type;
extern media_manager media_class_method_obj;

static const struct {uint16_t width;uint16_t height;} parse_framesize[] = {
    {88, 72},           //QQCIF
    {176, 144},         //QCIF
    {352, 288},         //CIF
    {176, 120},         //QSIF
    {352, 240},         //SIF
    {160, 120},         //QQVGA
    {320, 240},         //QVGA
    {640, 480},         //VGA
    {120, 80},          //HQQVGA
    {240, 160},         //HQVGA
    {480, 320},         //HVGA
    {64, 64},           //B64X64
    {128, 64},          //B128X64
    {128, 128},         //B128X128
    {160, 160},         //B160X160
    {320, 320},         //B320X320
    {128, 160},         //QQVGA2
    {720, 480},         //WVGA
    {752, 480},         //WVGA2
    {800, 600},         //SVGA
    {1024, 768},        //XGA
    {1280, 768},        //WXGA
    {1280, 1024},       //SXGA
    {1280, 960},        //SXGAM
    {1600, 1200},       //UXGA
    {1280, 720},        //HD
    {1920, 1080},       //FHD
    {2560, 1440},       //QHD
    {2048, 1536},       //QXGA
    {2560, 1600},       //WQXGA
    {2592, 1944}        //WQXGA2
};

#define dumped_image_push_phys(p_dumped_image,phys_val)          do{(p_dumped_image)->phys = phys_val;}while (0)
#define dumped_image_push_virt(p_dumped_image, virt_val, size_val) do{(p_dumped_image)->virt = virt_val; (p_dumped_image)->size = size_val;}while (0)

static inline void dumped_image_init(dumped_image_t *obj,int dev_id, int chn)
{
    obj->is_init = true;
    obj->id = dev_id;
    obj->chn = chn;
    obj->phys = 0;
    obj->virt = 0;
    obj->size = 0;
}
static inline void dumped_image_release(dumped_image_t *obj)
{
    if (obj->virt > 0 && obj->size > 0) 
    {
        int ret = kd_mpi_sys_munmap((void *)obj->virt, obj->size);
        obj->virt = 0;
        obj->size = 0;
        if (ret)
            mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("release image failed (1)"));
        
    }

    if (obj->phys > 0) 
    {
        k_video_frame_info frame_info = {0};
        frame_info.v_frame.phys_addr[0] = obj->phys;
        int ret = kd_mpi_vicap_dump_release(obj->id, obj->chn, &frame_info);
        if(ret)
            mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("release image failed (2)"));
        obj->phys = 0;
    }
}





#endif 

