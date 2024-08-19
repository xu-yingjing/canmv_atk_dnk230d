#include "display_py.h"

extern const mp_obj_type_t mp_display_type;
extern int ide_dbg_set_vo_wbc(int enable, int width, int height);
extern int ide_dbg_vo_wbc_deinit(void);
extern int kd_mpi_vo_osd_rotation(int flag, k_video_frame_info *in, k_video_frame_info *out);
extern void memcpy_fast(void *dst, void *src, size_t size);

display_t *display_class_method = NULL;

static void _disable_layer(display_t *obj,int layer)
{
    if (layer > K_VO_MAX_CHN_NUMS)
    {
        mp_raise_ValueError(MP_ERROR_TEXT("layer({layer}) out of range"));
    }

    if (obj->_layer_cfgs[layer] != NULL)
    {
        if (obj->_layer_configured[layer])
        {
            if (DISPLAY_LAYER_VIDEO1 <= layer && layer <= DISPLAY_LAYER_VIDEO2) 
                kd_mpi_vo_disable_video_layer(layer);
            else if (DISPLAY_LAYER_OSD0 <= layer && layer <= DISPLAY_LAYER_OSD3) 
                kd_mpi_vo_osd_disable(layer - DISPLAY_LAYER_OSD0);
        }
        else
        {
            mp_printf(&mp_plat_print,"error state on _disable_layer {layer}");
        }
        
        free(obj->_layer_cfgs[layer]);
        obj->_layer_cfgs[layer] = NULL;
    }
    
    obj->_layer_configured[layer] = false;
}

static void __config_layer(display_t *obj,LayerConfig *layer_config)
{
	if (layer_config == NULL)
    {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid layer configuration"));
    }

    if (obj->_layer_cfgs[layer_config->layer] != NULL && 
        memcmp(obj->_layer_cfgs[layer_config->layer],layer_config,sizeof(LayerConfig)) == 0)
    {
        // mp_printf(&mp_plat_print,"layer({layer_config.layer}) config not changed.");
        return;
    }
	
	if ( !(layer_config->layer >= DISPLAY_LAYER_VIDEO1 && layer_config->layer <= DISPLAY_LAYER_OSD3) )
    {
        mp_raise_ValueError(MP_ERROR_TEXT("layer({layer}) out of range"));
    }
	
	if (layer_config->layer <= DISPLAY_LAYER_VIDEO2 && layer_config->pix_format != PIXEL_FORMAT_YUV_SEMIPLANAR_420)
    {
        mp_raise_ValueError(MP_ERROR_TEXT("bind video layer only support format PIXEL_FORMAT_YUV_SEMIPLANAR_420"));
    }
	
	_disable_layer(obj,layer_config->layer);
	
	int width = layer_config->rect.w != 0? layer_config->rect.w : obj->_width;
    int height = layer_config->rect.h != 0? layer_config->rect.h : obj->_height;
	
	if(layer_config->layer <= DISPLAY_LAYER_VIDEO2)
	{
		if (obj->_connector_is_st7701) 
		{
			if (layer_config->flag == 0) 
				layer_config->flag = obj->_connector_type;
		}

		if ((layer_config->flag & DISPLAY_FLAG_ROTATION_90) == DISPLAY_FLAG_ROTATION_90 || 
			(layer_config->flag & DISPLAY_FLAG_ROTATION_270) == DISPLAY_FLAG_ROTATION_270) 
		{
			if (layer_config->layer == DISPLAY_LAYER_VIDEO2) 
			{
				mp_raise_ValueError(MP_ERROR_TEXT("Bind to video layer enable rotation 90/180 only support Display.LAYER_VIDEO1"));
			}
			int temp = width;
			width = height;
			height = temp;
		}
	}
	
	if (width == 0 || height == 0 || (width & 7)) 
    {
        mp_raise_ValueError(MP_ERROR_TEXT("width must be an integral multiple of 8 pixels"));
    }
	
	k_vo_point offset = {layer_config->rect.x,layer_config->rect.y};
    k_vo_size img_size = {width, height};
	
	if(layer_config->layer <= DISPLAY_LAYER_VIDEO2)
	{
		k_vo_video_layer_attr video_attr = {0};
		video_attr.pixel_format = layer_config->pix_format;

		video_attr.func = layer_config->flag;

		if (layer_config->pix_format == PIXEL_FORMAT_YUV_SEMIPLANAR_420)
		{
			video_attr.stride = (width / 8 - 1) + ((height - 1) << 16);
		}
		else
		{
			mp_raise_ValueError(MP_ERROR_TEXT("video layer not support pix_format ({layer_config.pix_format})"));
		}

		memcpy(&video_attr.display_rect, &offset, sizeof(k_vo_point));
		memcpy(&video_attr.img_size, &img_size, sizeof(k_vo_size));
		
		kd_mpi_vo_set_video_layer_attr(layer_config->layer, &video_attr);
		kd_mpi_vo_enable_video_layer(layer_config->layer);
	}
	else
	{
		int pixelformat = layer_config->pix_format;
		k_vo_video_osd_attr osd_attr = {0};
		osd_attr.global_alptha = layer_config->alpha;
		osd_attr.pixel_format = pixelformat;

		if (pixelformat == PIXEL_FORMAT_ARGB_8888 || pixelformat == PIXEL_FORMAT_ABGR_8888)
			osd_attr.stride = (width * 4) / 8;
		else if (pixelformat == PIXEL_FORMAT_RGB_888 || pixelformat == PIXEL_FORMAT_BGR_888)
			osd_attr.stride = (width * 3) / 8;
		else if (pixelformat == PIXEL_FORMAT_RGB_565_LE || pixelformat == PIXEL_FORMAT_BGR_565_LE)
			osd_attr.stride = (width * 2) / 8;
		else if (pixelformat == PIXEL_FORMAT_RGB_MONOCHROME_8BPP)
			osd_attr.stride = (width) / 8;
		else
			mp_raise_ValueError(MP_ERROR_TEXT("osd layer not support pix_format"));
		
		memcpy(&osd_attr.display_rect, &offset, sizeof(k_vo_point));
		memcpy(&osd_attr.img_size, &img_size, sizeof(k_vo_size));
		kd_mpi_vo_set_video_osd_attr(layer_config->layer - DISPLAY_LAYER_OSD0, &osd_attr);
	}
	
	
	if (obj->_layer_cfgs[layer_config->layer] == NULL)
    {
        LayerConfig *layer_cfg = (LayerConfig *)malloc(sizeof(LayerConfig));
        memcpy(layer_cfg, layer_config, sizeof(LayerConfig));
        obj->_layer_cfgs[layer_config->layer] = layer_cfg;
    }
    else
    {
        memcpy(obj->_layer_cfgs[layer_config->layer], layer_config, sizeof(LayerConfig));
    }
	
	if (layer_config->layer <= DISPLAY_LAYER_VIDEO2)
		obj->_layer_configured[layer_config->layer] = true;
}

static inline void _config_layer(display_t *obj,int layer,rectangle_t rect,int pix_format, int flag, int alpha)
{
    LayerConfig layer_config = {.layer = layer,.alpha = alpha,.flag = flag,.pix_format = pix_format};
    memcpy(&layer_config.rect,&rect,sizeof(rectangle_t));
    __config_layer(obj,&layer_config);
}

void display_make_init(display_t *obj)
{
    obj->_is_inited = false;
    obj->_osd_layer_num = 1;
    obj->_write_back_to_ide = false;
    obj->_ide_vo_wbc_flag = 0;

    obj->_width = 0;
    obj->_height = 0;
    memset(&obj->_connector_info,0, sizeof(k_connector_info));
    obj->_connector_type = 0;
    obj->_connector_is_st7701 = false;

    for (size_t i = 0; i < K_VO_MAX_CHN_NUMS; i++)
        obj->_layer_cfgs[i] = NULL;
    for (size_t i = 0; i < K_VO_MAX_CHN_NUMS; i++)
        obj->_layer_bind_cfg[i] = NULL;
    
    obj->_layer_rotate_buffer = NULL;
    for (size_t i = 0; i < K_VO_MAX_CHN_NUMS; i++)
        obj->_layer_disp_buffers[i] = NULL;
    for (size_t i = 0; i < K_VO_MAX_CHN_NUMS; i++)
        obj->_layer_configured[i] = false;
}

static void display_class_method_check(void)
{
    if (display_class_method == NULL)
    {
        display_class_method = malloc(sizeof(display_t));
        display_make_init(display_class_method);
    }
}

STATIC mp_obj_t display_bind_layer(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) 
{
    display_t *display = NULL;
    mp_obj_t *rect_items;
    size_t len;

    if (mp_obj_is_type(pos_args[0], &mp_type_type)) 
    {
        // 第一个参数是类类型，即类方法
        printf("Called as class method\n");
        // 处理类方法的逻辑
        display_class_method_check();
        display = display_class_method;
    }
    else
    {
        display = &((mp_obj_display_t *)pos_args[0])->display;
    }

    mp_map_elem_t *kw_src = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_src), MP_MAP_LOOKUP);
    if (kw_src == NULL)
        mp_raise_ValueError(MP_ERROR_TEXT("src should be set"));
    if (!mp_obj_is_type(kw_src->value, &mp_type_tuple) || MP_OBJ_SMALL_INT_VALUE(mp_obj_len(kw_src->value)) != 3) 
    {
        mp_raise_ValueError(MP_ERROR_TEXT("src should be (mod, dec, layer)"));
    }
    mp_obj_tuple_get(kw_src->value,&len, &rect_items);
    k_mpp_chn src = {.mod_id = mp_obj_get_int(rect_items[0]),
                     .dev_id = mp_obj_get_int(rect_items[1]),
                     .chn_id = mp_obj_get_int(rect_items[2])};

    int layer = DISPLAY_LAYER_VIDEO1;
    mp_map_elem_t *kw_layer = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_layer), MP_MAP_LOOKUP);
    if (kw_layer != NULL)layer = mp_obj_get_int(kw_layer->value);

    if (layer > K_VO_MAX_CHN_NUMS)
        mp_raise_msg(&mp_type_IndexError,MP_ERROR_TEXT("layer out of range"));

    k_mpp_chn dst = {.mod_id = DISPLAY_MOD_ID,.dev_id = DISPLAY_DEV_ID,.chn_id = layer};
    
    rectangle_t rect = {0,0,0,0};
    mp_map_elem_t *kw_rect = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_rect), MP_MAP_LOOKUP);
    if (kw_rect != NULL) 
    {
        if (!mp_obj_is_type(kw_rect->value, &mp_type_tuple) || MP_OBJ_SMALL_INT_VALUE(mp_obj_len(kw_rect->value)) != 4)
            mp_raise_ValueError(MP_ERROR_TEXT("rect should be (x, y, w, h)"));
        
        mp_obj_tuple_get(kw_rect->value,&len, &rect_items);
        rect.x = mp_obj_get_int(rect_items[0]);
        rect.y = mp_obj_get_int(rect_items[1]);
        rect.w = mp_obj_get_int(rect_items[2]);
        rect.h = mp_obj_get_int(rect_items[3]);
    }

    int pix_format = PIXEL_FORMAT_YVU_PLANAR_420;
    mp_map_elem_t *kw_pix_format = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_pix_format), MP_MAP_LOOKUP);
    if (kw_pix_format != NULL)pix_format = mp_obj_get_int(kw_pix_format->value);

    int alpha = 255;
    mp_map_elem_t *kw_alpha = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_alpha), MP_MAP_LOOKUP);
    if (kw_alpha != NULL) 
    {
        alpha = mp_obj_get_int(kw_alpha->value);
        if ((DISPLAY_LAYER_VIDEO1 <= layer) && (layer <= DISPLAY_LAYER_VIDEO2)) 
            mp_printf(&mp_plat_print, "layer(%d) not support alpha, ignore it.\n", layer);
    }

    int flag = 0;
    mp_map_elem_t *kw_flag = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_flag), MP_MAP_LOOKUP);
    if (kw_flag != NULL) flag = mp_obj_get_int(kw_flag->value);

    printf("bind_layer: src=(%d, %d, %d), layer=%d, rect=(%d, %d, %d, %d), pix_format=%d, alpha=%d, flag=%d\n", 
            src.mod_id, src.dev_id, src.chn_id, layer,rect.x, rect.y, rect.w, rect.h, pix_format,alpha, flag);

    if (display->_layer_bind_cfg[layer] != NULL)
    {
        bind_config_del(display->_layer_bind_cfg[layer]);
        display->_layer_bind_cfg[layer] = NULL;
        mp_printf(&mp_plat_print, "bin_layer: layer({layer}) haver been bind, auto unbind it.\n", layer);
    }

    LayerConfig layer_config = {.layer = layer,.alpha = alpha,.flag = flag,.pix_format = pix_format};
    memcpy(&layer_config.rect,&rect,sizeof(rectangle_t));
    display->_layer_bind_cfg[layer] = bind_config_new(&src,&dst,&layer_config);

    if (display->_is_inited)
        __config_layer(display,&layer_config);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_bind_layer_obj, 1, display_bind_layer);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(display_bind_layer_class_method, MP_ROM_PTR(&display_bind_layer_obj));


STATIC mp_obj_t display_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) 
{
    display_t *display = NULL;

    if (mp_obj_is_type(pos_args[0], &mp_type_type)) 
    {
        // 第一个参数是类类型，即类方法
        printf("Called as class method\n");
        // 处理类方法的逻辑
        display_class_method_check();
        display = display_class_method;
    }
    else
    {
        display = &((mp_obj_display_t *)pos_args[0])->display;
    }

    // 定义参数
    enum { ARG_type, ARG_width, ARG_height, ARG_osd_num, ARG_to_ide, ARG_flag, ARG_fps,ARG_quality };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_type, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_width, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_height, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_osd_num, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_to_ide, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_flag, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_fps, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_quality, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 90} }
    };

    // 解析参数
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // 解析参数值
    mp_obj_t type_obj = args[ARG_type].u_obj;
    mp_obj_t width_obj = args[ARG_width].u_obj;
    mp_obj_t height_obj = args[ARG_height].u_obj;
    int osd_num = args[ARG_osd_num].u_int;
    bool to_ide = args[ARG_to_ide].u_bool;
    mp_obj_t flag_obj = args[ARG_flag].u_obj;
    mp_obj_t fps_obj = args[ARG_fps].u_obj;
    int quality = args[ARG_quality].u_int;
    
    int type = mp_obj_get_int(type_obj);

    // 检查是否已经初始化
    if (display->_is_inited) {
        mp_printf(&mp_plat_print, "Already run Display.init()\n");
        return mp_const_none;
    }

    if (type_obj == mp_const_none) {
        mp_raise_ValueError(MP_ERROR_TEXT("please run Display.init(type=)"));
    }

    display->_osd_layer_num = osd_num;
    display->_write_back_to_ide = to_ide;
    display->_ide_vo_wbc_flag = 0;

    int _width = 0, _height = 0, _fps = 0, _flag = 0;

    if (type >= DISPLAY_LT9611) 
    {
        if (type == DISPLAY_LT9611) 
        {
            _width = (width_obj != mp_const_none) ? mp_obj_get_int(width_obj) : 1920;
            _height = (height_obj != mp_const_none) ? mp_obj_get_int(height_obj) : 1080;

            if (_width == 1920 && _height == 1080) 
            {
                display->_connector_type = LT9611_MIPI_4LAN_1920X1080_30FPS;
            } 
            else if (_width == 1280 && _height == 720) 
            {
                display->_connector_type = LT9611_MIPI_4LAN_1280X720_30FPS;
            } 
            else if (_width == 640 && _height == 480) 
            {
                display->_connector_type = LT9611_MIPI_4LAN_640X480_60FPS;
            } 
            else 
            {
                mp_raise_ValueError(MP_ERROR_TEXT("LT9611 unsupported resolution"));
            }
        } 
        else if (type == DISPLAY_HX8377) 
        {
            display->_connector_type = HX8377_V2_MIPI_4LAN_1080X1920_30FPS;
        } 
        else if (type == DISPLAY_ST7701) 
        {
            _width = (width_obj != mp_const_none) ? mp_obj_get_int(width_obj) : 800;
            _height = (height_obj != mp_const_none) ? mp_obj_get_int(height_obj) : 480;
            _flag = (flag_obj != mp_const_none) ? mp_obj_get_int(flag_obj) : DISPLAY_FLAG_ROTATION_90;

            display->_connector_is_st7701 = true;

            if (_width == 800 && _height == 480) 
            {
                display->_ide_vo_wbc_flag = _flag;
                display->_connector_type = ST7701_V1_MIPI_2LAN_480X800_30FPS;
            } 
            else if (_width == 480 && _height == 800) 
            {
                display->_connector_type = ST7701_V1_MIPI_2LAN_480X800_30FPS;
            } 
            else if (_width == 854 && _height == 480) 
            {
                display->_ide_vo_wbc_flag = _flag;
                display->_connector_type = ST7701_V1_MIPI_2LAN_480X854_30FPS;
            }
            else if (_width == 480 && _height == 854) 
            {
                display->_connector_type = ST7701_V1_MIPI_2LAN_480X854_30FPS;
            }
            else 
            {
                mp_raise_ValueError(MP_ERROR_TEXT("ST7701 unsupported resolution"));
            }
            _width = 0;
            _height = 0;
            _flag = 0;
        } 
        else if (type == DISPLAY_VIRT) 
        {
            display->_write_back_to_ide = true;
            display->_connector_type = VIRTUAL_DISPLAY_DEVICE;
        } 
        else 
        {
            mp_raise_ValueError(MP_ERROR_TEXT("Unsupported display type"));
        }
    } 
    else 
    {
        display->_connector_type = type;
    }

    memset(&display->_connector_info,0,sizeof(k_connector_info));
    kd_mpi_get_connector_info(display->_connector_type, &display->_connector_info);

    if (display->_connector_type == VIRTUAL_DISPLAY_DEVICE) 
    {
        _width = (width_obj != mp_const_none) ? mp_obj_get_int(width_obj) : 640;
        _height = (height_obj != mp_const_none) ? mp_obj_get_int(height_obj) : 480;
        _fps = (fps_obj != mp_const_none) ? mp_obj_get_int(fps_obj) : 90;

        if (_width & 7) 
        {
            mp_raise_ValueError(MP_ERROR_TEXT("width must be an integral multiple of 8 pixels"));
        }

        display->_connector_info.resolution.hdisplay = _width;
        display->_connector_info.resolution.vdisplay = _height;
        display->_connector_info.resolution.pclk = _fps;
    }

    display->_width = display->_connector_info.resolution.hdisplay;
    display->_height = display->_connector_info.resolution.vdisplay;

    int connector_fd = kd_mpi_connector_open(display->_connector_info.connector_name);
    kd_mpi_connector_power_set(connector_fd, 1);
    kd_mpi_connector_init(connector_fd, display->_connector_info);
    kd_mpi_connector_close(connector_fd);

    if (display->_write_back_to_ide) 
    {
        k_vb_config config = {0};
        config.max_pool_cnt = 1;
        config.comm_pool[0].blk_size = display->_width * display->_height * 2;
        config.comm_pool[0].blk_cnt = 4;
        config.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;

        int ret = media_manager_config(&media_class_method_obj,&config);
        if (!ret) 
        {
            mp_raise_ValueError(MP_ERROR_TEXT("Display configure buffer for ide failed."));
        }
        quality = quality < 10 ? 90:quality;
        ide_dbg_set_vo_wbc(quality, display->_width, display->_height);
    } 
    else 
    {
        ide_dbg_set_vo_wbc(false, 0, 0);
    }

    if (display->_osd_layer_num < 1) 
    {
        display->_osd_layer_num = 1;
    }

    k_vb_config config = {0};
    config.max_pool_cnt = 1;
    config.comm_pool[0].blk_size = display->_width * display->_height * 4;
    config.comm_pool[0].blk_cnt = display->_osd_layer_num + 2;
    config.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;
    int ret = media_manager_config(&media_class_method_obj,&config);
    if (!ret) 
    {
        mp_raise_ValueError(MP_ERROR_TEXT("Display configure buffer for ide failed."));
    }

    for (int i = 0; i < K_VO_MAX_CHN_NUMS; i++)
    {
        if (display->_layer_bind_cfg[i] == NULL) 
            continue;
        __config_layer(display,&display->_layer_bind_cfg[i]->layer_config);
    }

    display->_is_inited = true;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_init_obj, 1, display_init);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(display_init_obj_class_method, MP_ROM_PTR(&display_init_obj));

static mp_obj_t py_fun0_get_value(mp_obj_t base,qstr attr)
{
    mp_obj_t dest[2];
    mp_load_method(base, attr, dest);
    if (dest[0] != MP_OBJ_NULL && dest[1] != MP_OBJ_NULL) 
    {
        return mp_call_method_n_kw(0, 0, dest);
    }

    return mp_const_none;
}

STATIC mp_obj_t display_show_image(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) 
{
    display_t *display = NULL;

    if (mp_obj_is_type(args[0], &mp_type_type)) 
    {
        // 处理类方法的逻辑
        display_class_method_check();
        display = display_class_method;
    }
    else
    {
        display = &((mp_obj_display_t *)args[0])->display;
    }

    // Extract arguments and set defaults
    mp_obj_t img = args[1];
    int x = 0;
    int y = 0;
    int layer = DISPLAY_LAYER_OSD0;
    int alpha = 255;
    int flag = 0;

    // Check for kwargs and set them
    mp_map_elem_t *kw_x = mp_map_lookup(kwargs, MP_OBJ_NEW_QSTR(MP_QSTR_x), MP_MAP_LOOKUP);
    if (kw_x != NULL) x = mp_obj_get_int(kw_x->value);
    mp_map_elem_t *kw_y = mp_map_lookup(kwargs, MP_OBJ_NEW_QSTR(MP_QSTR_y), MP_MAP_LOOKUP);
    if (kw_y != NULL) y = mp_obj_get_int(kw_y->value);
    mp_map_elem_t *kw_layer = mp_map_lookup(kwargs, MP_OBJ_NEW_QSTR(MP_QSTR_layer), MP_MAP_LOOKUP);
    if (kw_layer != NULL) layer = mp_obj_get_int(kw_layer->value);
    mp_map_elem_t *kw_alpha = mp_map_lookup(kwargs, MP_OBJ_NEW_QSTR(MP_QSTR_alpha), MP_MAP_LOOKUP);
    if (kw_alpha != NULL) alpha = mp_obj_get_int(kw_alpha->value);
    mp_map_elem_t *kw_flag = mp_map_lookup(kwargs, MP_OBJ_NEW_QSTR(MP_QSTR_flag), MP_MAP_LOOKUP);
    if (kw_flag != NULL) flag = mp_obj_get_int(kw_flag->value);

    // Check layer range
    if ( !(DISPLAY_LAYER_OSD0 <= layer && layer <= DISPLAY_LAYER_OSD3) )
        mp_raise_ValueError(MP_ERROR_TEXT("layer is out of range."));

    // Extract width, height, and format from image
    int width = mp_obj_get_int(py_fun0_get_value(img, MP_QSTR_width));
    int height = mp_obj_get_int(py_fun0_get_value(img, MP_QSTR_height));
    int format = mp_obj_get_int(py_fun0_get_value(img, MP_QSTR_format));
    mp_int_t img_size = mp_obj_get_int(py_fun0_get_value(img, MP_QSTR_size));
    mp_int_t img_virtaddr = mp_obj_get_int(py_fun0_get_value(img, MP_QSTR_virtaddr));

    // Validate width
    if (width & 7) 
    {
        mp_raise_ValueError(MP_ERROR_TEXT("Image width must be an integral multiple of 8 pixels"));
    }

    // Map image format to pixel format
    int pixelformat;
    int stride = 1;
    switch (format) 
    {
        case PIXFORMAT_ARGB8888:
            pixelformat = PIXEL_FORMAT_ARGB_8888;
            stride = 4;
            break;
        case PIXFORMAT_RGB888:
            pixelformat = PIXEL_FORMAT_RGB_888;
            stride = 3;
            break;
        case PIXFORMAT_RGB565:
            pixelformat = PIXEL_FORMAT_RGB_565_LE;
            stride = 2;
            break;
        case PIXFORMAT_GRAYSCALE:
            pixelformat = PIXEL_FORMAT_RGB_MONOCHROME_8BPP;
            stride = 1;
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("Image format not support"));
    }

    // Buffer handling
    if (display->_layer_disp_buffers[layer] == NULL) 
    {
        int buf_cnt = 0;
        for (int i = 0; i < K_VO_MAX_CHN_NUMS; i++) 
        {
            if (display->_layer_disp_buffers[i] != NULL) 
                buf_cnt++;
        }

        if (buf_cnt > display->_osd_layer_num) 
        {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("please increase Display.config(osd_num=) or becareful the layer"));
        }
        printf("_layer_disp_buffers alloc layer %d\n", layer);
        display->_layer_disp_buffers[layer] = media_manager_buffer_get(&media_class_method_obj,4 * display->_width * display->_height,VB_INVALID_POOLID);
        if (display->_layer_disp_buffers[layer] == NULL) 
        {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("failed to get display buffer"));
        }
    }

    // Rotation handling
    if (display->_connector_is_st7701 && flag == 0 && display->_ide_vo_wbc_flag != 0) 
    {
        flag = display->_ide_vo_wbc_flag;
    }

    if (flag != 0) 
    {
        if (display->_layer_rotate_buffer == NULL) 
        {
            display->_layer_rotate_buffer = media_manager_buffer_get(&media_class_method_obj,4 * display->_width * display->_height,VB_INVALID_POOLID);
            if (display->_layer_rotate_buffer == NULL)
            {
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("please increase Display.config(osd_num=)"));
            }
        }

        int _x = x, _y = y, _w = width, _h = height;
        int _rotate = flag & 0x0F;

        if ((_rotate & DISPLAY_FLAG_ROTATION_90) == DISPLAY_FLAG_ROTATION_90 
            || (_rotate & DISPLAY_FLAG_ROTATION_270) == DISPLAY_FLAG_ROTATION_270) 
        {
            x = display->_width - _h - _y;
            y = _x;
            width = _h;
            height = _w;
        }

        k_video_frame_info input_frame = {0};
        input_frame.pool_id = display->_layer_rotate_buffer->info.pool_id;
        input_frame.v_frame.width = _w;
        input_frame.v_frame.height = _h;
        input_frame.v_frame.stride[0] = _w * stride;
        input_frame.v_frame.pixel_format = pixelformat;
        input_frame.v_frame.phys_addr[0] = display->_layer_rotate_buffer->info.phys_addr;
        input_frame.v_frame.virt_addr[0] = display->_layer_rotate_buffer->info.virt_addr;

        k_video_frame_info output_frame = {0};
        output_frame.v_frame.width = width;
        output_frame.v_frame.height = height;
        output_frame.v_frame.stride[0] = width * stride;
        output_frame.v_frame.virt_addr[0] = display->_layer_disp_buffers[layer]->info.virt_addr;
        
        memcpy_fast(display->_layer_rotate_buffer->info.virt_addr, (void *)img_virtaddr, img_size);
        kd_mpi_vo_osd_rotation(flag, &input_frame, &output_frame);
    } 
    else 
    {
        memcpy_fast(display->_layer_disp_buffers[layer]->info.virt_addr, (void *)img_virtaddr, img_size);
    }

    rectangle_t rect = {.x = x, .y = y, .w = width, .h = height};
    _config_layer(display,layer, rect, pixelformat, flag, alpha);

    if (!display->_layer_configured[layer]) 
    {
        k_video_frame_info frame_info = {0};
        frame_info.mod_id = K_ID_VO;
        frame_info.pool_id = display->_layer_disp_buffers[layer]->info.pool_id;
        frame_info.v_frame.width = width;
        frame_info.v_frame.height = height;
        frame_info.v_frame.pixel_format = pixelformat;
        frame_info.v_frame.stride[0] = width * stride;
        frame_info.v_frame.phys_addr[0] = display->_layer_disp_buffers[layer]->info.phys_addr;
        frame_info.v_frame.virt_addr[0] = display->_layer_disp_buffers[layer]->info.virt_addr;

        kd_mpi_vo_chn_insert_frame(layer, &frame_info);
        kd_mpi_vo_osd_enable(layer - DISPLAY_LAYER_OSD0);

        display->_layer_configured[layer] = true;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(display_show_image_obj, 1, display_show_image);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(display_show_image_obj_class_method, MP_ROM_PTR(&display_show_image_obj));

STATIC mp_obj_t display_deinit(mp_obj_t self_in) 
{
    display_t *display = NULL;

    if (mp_obj_is_type(self_in, &mp_type_type)) 
    {
        // 第一个参数是类类型，即类方法
        printf("[display_deinit] Called as class method\n");
        // 处理类方法的逻辑
        display_class_method_check();
        display = display_class_method;
    }
    else
    {
        display = &((mp_obj_display_t *)self_in)->display;
    }

    // 检查是否初始化
    if (!display->_is_inited) 
    {
        mp_printf(&mp_plat_print, "did't call Display.init()");
        return mp_const_none;
    }

    // 禁用所有图层
    for (int i = 0; i < K_VO_MAX_CHN_NUMS; i++) 
    {
        if (display->_layer_cfgs[i] != NULL) 
        {
            _disable_layer(display,i);
        }
    }

    // 关闭电源
    int connector_fd = kd_mpi_connector_open(display->_connector_info.connector_name);
    kd_mpi_connector_power_set(connector_fd, 0);
    kd_mpi_connector_close(connector_fd);

    ide_dbg_set_vo_wbc(false, 0, 0);
    ide_dbg_vo_wbc_deinit();
    kd_display_reset();

    // 取消绑定所有图层
    for (int i = 0; i < K_VO_MAX_CHN_NUMS; i++) 
    {
        if (display->_layer_bind_cfg[i] != NULL)
        {
            bind_config_del(display->_layer_bind_cfg[i]);
            display->_layer_bind_cfg[i] = NULL;
        }
    }

    if(display->_layer_rotate_buffer != NULL) 
    {
        media_manager_buffer_del(&media_class_method_obj, display->_layer_rotate_buffer);
        display->_layer_rotate_buffer = NULL;
    }

    for (int i = 0; i < K_VO_MAX_CHN_NUMS; i++) 
    {
        if(display->_layer_disp_buffers[i] != NULL) 
        {
            printf("_layer_disp_buffers del layer %d\n", i);
            media_manager_buffer_del(&media_class_method_obj, display->_layer_disp_buffers[i]);
            display->_layer_disp_buffers[i] = NULL;
        }
    }

    // 重置显示相关的属性
    display->_osd_layer_num = 1;
    display->_write_back_to_ide = false;
    display->_ide_vo_wbc_flag = 0;

    memset(&display->_connector_info,0, sizeof(k_connector_info));
    display->_connector_type = 0;
    display->_connector_is_st7701 = false;

    display->_width = 0;
    display->_height = 0;

    display->_is_inited = false;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(display_deinit_obj, display_deinit);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(display_deinit_obj_class_method, MP_ROM_PTR(&display_deinit_obj));

// 定义对象方法表
STATIC const mp_rom_map_elem_t display_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&display_init_obj_class_method) },
    { MP_ROM_QSTR(MP_QSTR_show_image), MP_ROM_PTR(&display_show_image_obj_class_method) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&display_deinit_obj_class_method) },
    { MP_ROM_QSTR(MP_QSTR_bind_layer), MP_ROM_PTR(&display_bind_layer_class_method) },
    { MP_ROM_QSTR(MP_QSTR_LT9611), MP_ROM_INT(LT9611) },
    { MP_ROM_QSTR(MP_QSTR_HX8377), MP_ROM_INT(HX8377) },
    { MP_ROM_QSTR(MP_QSTR_ST7701), MP_ROM_INT(ST7701) },
    { MP_ROM_QSTR(MP_QSTR_VIRT), MP_ROM_INT(VIRT) },
    { MP_ROM_QSTR(MP_QSTR_LAYER_VIDEO1), MP_ROM_INT(DISPLAY_LAYER_VIDEO1) },
    { MP_ROM_QSTR(MP_QSTR_LAYER_VIDEO2), MP_ROM_INT(DISPLAY_LAYER_VIDEO2) },
    { MP_ROM_QSTR(MP_QSTR_LAYER_OSD0), MP_ROM_INT(DISPLAY_LAYER_OSD0) },
    { MP_ROM_QSTR(MP_QSTR_LAYER_OSD1), MP_ROM_INT(DISPLAY_LAYER_OSD1) },
    { MP_ROM_QSTR(MP_QSTR_LAYER_OSD2), MP_ROM_INT(DISPLAY_LAYER_OSD2) },
    { MP_ROM_QSTR(MP_QSTR_LAYER_OSD3), MP_ROM_INT(DISPLAY_LAYER_OSD3) },
};
STATIC MP_DEFINE_CONST_DICT(display_locals_dict, display_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_display_type,
    MP_QSTR_display,
    MP_TYPE_FLAG_NONE,
    // make_new, media_manager_make_new,
    locals_dict, &display_locals_dict
    );
