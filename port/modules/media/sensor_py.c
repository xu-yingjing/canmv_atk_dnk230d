#include "sensor_py.h"

static void __sensor_set_inbufs(sensor_t *sensor)
{
    if (!sensor->_dev_attr.dev_enable)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    
    if (sensor->_buf_in_init)
        return;
    
    sensor->_dev_attr.mode = VICAP_WORK_OFFLINE_MODE;
    sensor->_dev_attr.buffer_num = sensor->_dft_input_buff_num;
    sensor->_dev_attr.buffer_size = ALIGN_UP((sensor->_dev_attr.acq_win.width * sensor->_dev_attr.acq_win.height * 2), VICAP_ALIGN_4K);

    if (sensor->_dev_attr.mode == VICAP_WORK_OFFLINE_MODE)
    {
        k_vb_config config = {0};
        config.max_pool_cnt = 1;
        config.comm_pool[0].blk_size = sensor->_dev_attr.buffer_size;
        config.comm_pool[0].blk_cnt = sensor->_dev_attr.buffer_num;
        config.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;
        int ret = media_manager_config(&media_class_method_obj,&config);
        if (!ret)
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor({self._dev_id %d}) configure buffer failed({ret %d})"), sensor->_dev_id,ret);
    }
    sensor->_buf_in_init = true;

}

static bool __sensor_is_mcm_device(void)
{
    int cnt = 0;
    for (size_t i = 0; i < CAM_DEV_ID_MAX; i++)
    {
        if (Sensor._devs[i] != NULL)
            cnt++;
    }
    return cnt > 1? true : false;
}

static void __sensor_handle_mcm_device(void)
{
    if(!__sensor_is_mcm_device())
        return;
    for (size_t i = 0; i < CAM_DEV_ID_MAX; i++)
    {
        if (Sensor._devs[i] != NULL)
            __sensor_set_inbufs(Sensor._devs[i]);
    }
    
}

STATIC mp_obj_t sensor_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    mp_sensor_t *self =  m_new_obj(mp_sensor_t);
    if (self == NULL) 
    {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("obj new failed"));
        return mp_const_none;
    }
    self->base.type = type;

    self->sensor._dft_input_buff_num = 4;
    self->sensor._dft_output_buff_num = 6;

    int def_morror = 0;
    int dft_sensor_id = CAM_DEV_ID_0;

    if (strcmp(MICROPY_HW_BOARD_NAME,"k230d_canmv") == 0)
    {
        dft_sensor_id = CAM_DEV_ID_2;
        self->sensor._dft_output_buff_num = 4;
    }
    else if (strcmp(MICROPY_HW_BOARD_NAME,"k230_canmv_01studio") == 0)
    {
        dft_sensor_id = CAM_DEV_ID_2;
    }
    else if (strcmp(MICROPY_HW_BOARD_NAME,"k230d_canmv_bpi_zero") == 0)
    {
        dft_sensor_id = CAM_DEV_ID_2;
    }
    
    // 解析关键字参数
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_map_elem_t *id_elem = mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_id), MP_MAP_LOOKUP);
    self->sensor._dev_id = id_elem ? mp_obj_get_int(id_elem->value) : dft_sensor_id;
    if (self->sensor._dev_id > CAM_DEV_ID_MAX - 1) 
    {
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("invalid sensor id %d, should < %d"), self->sensor._dev_id, CAM_DEV_ID_MAX - 1);
    }

    mp_map_elem_t *force_elem = mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_force), MP_MAP_LOOKUP);
    bool force = force_elem ? mp_obj_is_true(force_elem->value) : false;
    if (!force && Sensor._devs[self->sensor._dev_id] != NULL) 
    {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("sensor(%d) is already inited."), self->sensor._dev_id);
    }

    mp_map_elem_t *type_elem = mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_type), MP_MAP_LOOKUP);
    if (type_elem != NULL) 
    {
        self->sensor._type = mp_obj_get_int(type_elem->value);
    }
    else
    {
        k_vicap_sensor_info info = {0};
        k_vicap_probe_config cfg = {0};
        cfg.csi_num = self->sensor._dev_id + 1;
        mp_map_elem_t *arg = mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_fps), MP_MAP_LOOKUP);
        cfg.fps = arg ? mp_obj_get_int(arg->value) : 60;
        
        arg = mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_width), MP_MAP_LOOKUP);
        cfg.width = arg ? mp_obj_get_int(arg->value) : 1920;

        arg = mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_height), MP_MAP_LOOKUP);
        cfg.height = arg ? mp_obj_get_int(arg->value) : 1080;

        int ret = kd_mpi_sensor_adapt_get(&cfg, &info);
        if (ret != 0) 
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Can not found sensor on {self._dev_id %d}"), self->sensor._dev_id);
        
        def_morror = cfg.mirror;
        self->sensor._type = info.sensor_type;
        mp_printf(&mp_plat_print, "use sensor {info.type %d}, output {info.width %d}x{info.height %d}@{info.fps %d}\n",
                    info.sensor_type, info.width, info.height, info.fps);
    }

    if(self->sensor._type > SENSOR_TYPE_MAX - 1)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("invalid sensor type %d, should < %d"), self->sensor._type, SENSOR_TYPE_MAX - 1);
    
    memset(&self->sensor._dev_attr, 0, sizeof(k_vicap_dev_attr));
    self->sensor._buf_in_init = false;

    self->sensor._dev_attr.buffer_num = self->sensor._dft_input_buff_num;
    self->sensor._dev_attr.mode = VICAP_WORK_ONLINE_MODE;
    self->sensor._dev_attr.input_type = VICAP_INPUT_TYPE_SENSOR;
    self->sensor._dev_attr.mirror = def_morror;

    self->sensor._is_started = false;
    for (size_t i = 0; i < VICAP_CHN_ID_MAX; i++)
    {
        memset(&self->sensor._chn_attr[i], 0, sizeof(k_vicap_chn_attr));
        self->sensor._buf_init[i] = false;
        self->sensor._chn_attr[i].buffer_num = self->sensor._dft_output_buff_num;
        self->sensor._imgs[i].is_init = false;
        self->sensor._is_rgb565[i] = false;
        self->sensor._is_grayscale[i] = false;
        self->sensor._framesize[i] = FRAME_SIZE_INVAILD;
    }

    Sensor._devs[self->sensor._dev_id] = &self->sensor;
    return self;
}

STATIC mp_obj_t sensor_reset(mp_obj_t self_in) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(self_in);

    int ret = kd_mpi_vicap_get_sensor_info(self->sensor._type, &self->sensor._dev_attr.sensor_info);
    if (ret) 
    {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) get info failed(%d)"), self->sensor._dev_id, ret);
    }

    self->sensor._dev_attr.acq_win.h_start = 0;
    self->sensor._dev_attr.acq_win.v_start = 0;
    self->sensor._dev_attr.acq_win.width = self->sensor._dev_attr.sensor_info.width;
    self->sensor._dev_attr.acq_win.height = self->sensor._dev_attr.sensor_info.height;

    self->sensor._dev_attr.mode = VICAP_WORK_ONLINE_MODE;
    self->sensor._dev_attr.input_type = VICAP_INPUT_TYPE_SENSOR;
    self->sensor._dev_attr.dev_enable = true;
    self->sensor._dev_attr.pipe_ctrl.data = 0xffffffff;
    self->sensor._dev_attr.pipe_ctrl.bits.af_enable = 0;
    self->sensor._dev_attr.pipe_ctrl.bits.ahdr_enable = 0;
    self->sensor._dev_attr.pipe_ctrl.bits.dnr3_enable = 0;
    self->sensor._dev_attr.dw_enable = 0;
    self->sensor._dev_attr.cpature_frame = 0;

    // 假设存在一个外部函数来处理 MCM 设备
    __sensor_handle_mcm_device();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sensor_reset_obj, sensor_reset);

STATIC mp_obj_t sensor_set_framesize(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }
    
    enum { framesize_index, chn_index, alignment_index, width_index, height_index};
    static const mp_arg_t allowed_args[] = 
    {
        { MP_QSTR_framesize, MP_ARG_INT, {.u_int = FRAME_SIZE_INVAILD} },
        { MP_QSTR_chn, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = CAM_CHN_ID_0} },
        { MP_QSTR_alignment, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_width, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_height, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    };
    // 解析参数
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int framesize = args[framesize_index].u_int;
    int chn = args[chn_index].u_int;
    int alignment = args[alignment_index].u_int;
    int width = args[width_index].u_int;
    int height = args[height_index].u_int;

    printf("set framesize %d, chn %d, alignment %d, width %d, height %d\n", framesize, chn, alignment, width, height);
    
    if (!self->sensor._dev_attr.dev_enable)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    
    if (chn > VICAP_CHN_ID_MAX - 1)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("invaild chn id {%d}, should < {%d}"), chn, VICAP_CHN_ID_MAX - 1);

    self->sensor._framesize[chn] = framesize;

    if (width == 0 && height == 0)
    {
        if (framesize >=0 && framesize < (sizeof(parse_framesize) / sizeof(parse_framesize[0])))
        {
            width = parse_framesize[framesize].width;
            height = parse_framesize[framesize].height;
        }
    }

    if ((width % 16) != 0)
    {
        width = ALIGN_UP(width, 16);
        mp_printf(&mp_plat_print, "Warning: sensor({%d}) chn({%d}) set_framesize align up width to {%d}\n", 
                                    self->sensor._dev_id, chn, width);
    }
    
    if (width > self->sensor._dev_attr.acq_win.width || width < CAM_OUT_WIDTH_MIN)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("sensor({%d}) chn({%d}) set_framesize invaild width({%d}), should be {%d} - {%d}"),
                                                    self->sensor._dev_id, chn,width, CAM_OUT_WIDTH_MIN, self->sensor._dev_attr.acq_win.width);

    if (height > self->sensor._dev_attr.acq_win.height || height < CAM_OUT_WIDTH_MIN)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("sensor({%d}) chn({%d}) set_framesize invaild height({%d}), should be {%d} - {%d}"),
                                                    self->sensor._dev_id, chn, height, CAM_OUT_WIDTH_MIN, self->sensor._dev_attr.acq_win.height);
    
    self->sensor._chn_attr[chn].chn_enable = true;
    self->sensor._chn_attr[chn].out_win.h_start = 0;
    self->sensor._chn_attr[chn].out_win.v_start = 0;
    self->sensor._chn_attr[chn].out_win.width = width;
    self->sensor._chn_attr[chn].out_win.height = height;
    self->sensor._chn_attr[chn].alignment = alignment;

    if (self->sensor._chn_attr[chn].pix_format)
    {
        int buf_size = 0;
        int pix_format = self->sensor._chn_attr[chn].pix_format;
        int out_width = self->sensor._chn_attr[chn].out_win.width;
        int out_height = self->sensor._chn_attr[chn].out_win.height;
        int in_width = self->sensor._dev_attr.acq_win.width;
        int in_height = self->sensor._dev_attr.acq_win.height;

        if (pix_format == PIXEL_FORMAT_YUV_SEMIPLANAR_420) 
        {
            buf_size = ALIGN_UP((out_width * out_height * 3 / 2), VICAP_ALIGN_4K);
        } 
        else if (pix_format == PIXEL_FORMAT_RGB_888 || pix_format == PIXEL_FORMAT_RGB_888_PLANAR) 
        {
            buf_size = ALIGN_UP((out_width * out_height * 3), VICAP_ALIGN_4K);
        } 
        else if (pix_format == PIXEL_FORMAT_RGB_BAYER_10BPP || pix_format == PIXEL_FORMAT_RGB_BAYER_12BPP ||
                   pix_format == PIXEL_FORMAT_RGB_BAYER_14BPP || pix_format == PIXEL_FORMAT_RGB_BAYER_16BPP) 
        {
            self->sensor._chn_attr[chn].out_win.width = in_width;
            self->sensor._chn_attr[chn].out_win.height = in_height;
            buf_size = ALIGN_UP((in_width * in_height * 2), VICAP_ALIGN_4K);
        } 
        else 
        {
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) chn(%d) set_framesize not support pix_format(%d)"),
                              self->sensor._dev_id, chn, pix_format);
        }

        self->sensor._chn_attr[chn].buffer_size = buf_size;
    }

    if (self->sensor._buf_init[chn] == false && self->sensor._chn_attr[chn].buffer_num && self->sensor._chn_attr[chn].buffer_size) 
    {
        self->sensor._buf_init[chn] = true;
        k_vb_config config = {0};
        config.max_pool_cnt = 1;
        config.comm_pool[0].blk_size = self->sensor._chn_attr[chn].buffer_size;
        config.comm_pool[0].blk_cnt = self->sensor._chn_attr[chn].buffer_num;
        config.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;
        bool ret = media_manager_config(&media_class_method_obj,&config);
        if (ret != 0) 
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) chn(%d) set_framesize configure buffer failed(%d)"),
                              self->sensor._dev_id, chn, ret);
    }
    
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(sensor_set_framesize_obj, 1, sensor_set_framesize);

STATIC mp_obj_t sensor_set_pixformat(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    int pix_format = mp_obj_get_int(args[1]);

    int chn = CAM_CHN_ID_0;
    mp_map_elem_t *kw_chn = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_chn), MP_MAP_LOOKUP);
    if (kw_chn != NULL) chn = mp_obj_get_int(kw_chn->value);

    printf("pix_format %d chn %d\r\n",pix_format,chn);
    
    if (!self->sensor._dev_attr.dev_enable) {
        mp_raise_msg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    }

    if (chn > CAM_CHN_ID_MAX - 1) {
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("invaild chn id %d, should < %d"), chn, CAM_CHN_ID_MAX - 1);
    }

    self->sensor._is_rgb565[chn] = false;

    if (pix_format == SENSOR_RGB565) 
    {
        self->sensor._is_rgb565[chn] = true;
        pix_format = PIXEL_FORMAT_RGB_888;
    } 
    else if (pix_format == SENSOR_GRAYSCALE) 
    {
        self->sensor._is_grayscale[chn] = true;
        pix_format = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    }

    self->sensor._chn_attr[chn].pix_format = pix_format;
    self->sensor._chn_attr[chn].chn_enable = true;

    if (self->sensor._chn_attr[chn].out_win.width && self->sensor._chn_attr[chn].out_win.height) 
    {
        int buf_size = 0;
        int out_width = self->sensor._chn_attr[chn].out_win.width;
        int out_height = self->sensor._chn_attr[chn].out_win.height;
        int in_width = self->sensor._dev_attr.acq_win.width;
        int in_height = self->sensor._dev_attr.acq_win.height;

        if (pix_format == PIXEL_FORMAT_YUV_SEMIPLANAR_420) 
        {
            buf_size = ALIGN_UP((out_width * out_height * 3 / 2), VICAP_ALIGN_4K);
        } 
        else if (pix_format == PIXEL_FORMAT_RGB_888 || pix_format == PIXEL_FORMAT_RGB_888_PLANAR) 
        {
            buf_size = ALIGN_UP((out_width * out_height * 3), VICAP_ALIGN_4K);
        } 
        else if (pix_format == PIXEL_FORMAT_RGB_BAYER_10BPP || pix_format == PIXEL_FORMAT_RGB_BAYER_12BPP ||
                   pix_format == PIXEL_FORMAT_RGB_BAYER_14BPP || pix_format == PIXEL_FORMAT_RGB_BAYER_16BPP) 
        {
            self->sensor._chn_attr[chn].out_win.width = in_width;
            self->sensor._chn_attr[chn].out_win.height = in_height;
            buf_size = ALIGN_UP((in_width * in_height * 2), VICAP_ALIGN_4K);
        } 
        else 
        {
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) chn(%d) set_pixformat not support pix_format(%d)"),
                              self->sensor._dev_id, chn, pix_format);
        }

        self->sensor._chn_attr[chn].buffer_size = buf_size;
    }

    if (!self->sensor._buf_init[chn] && self->sensor._chn_attr[chn].buffer_num && self->sensor._chn_attr[chn].buffer_size) 
    {
        k_vb_config config = {0};
        config.max_pool_cnt = 1;
        config.comm_pool[0].blk_size = self->sensor._chn_attr[chn].buffer_size;
        config.comm_pool[0].blk_cnt = self->sensor._chn_attr[chn].buffer_num;
        config.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;
        bool ret = media_manager_config(&media_class_method_obj,&config);
        if (!ret) {
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) chn(%d) set_pixformat configure buffer failed(%d)"),
                              self->sensor._dev_id, chn, ret);
        }
        self->sensor._buf_init[chn] = true;
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(sensor_set_pixformat_obj, 2, sensor_set_pixformat);

STATIC mp_obj_t sensor_bind_info(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    enum { x_index, y_index, chn_index};
    static const mp_arg_t allowed_args[] = 
    {
        { MP_QSTR_x, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_y, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_chn, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = CAM_CHN_ID_0} },
    };
    // 解析参数
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int x = args[x_index].u_int;
    int y = args[y_index].u_int;
    int chn = args[chn_index].u_int;

    printf("x %d y %d chn %d\r\n",x,y,chn);

    if (!self->sensor._dev_attr.dev_enable) 
        mp_raise_msg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));

    if (chn > CAM_CHN_ID_MAX - 1) 
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("invaild chn id %d, should < %d"), chn, CAM_CHN_ID_MAX - 1);
    
    int width = self->sensor._chn_attr[chn].out_win.width;
    int height = self->sensor._chn_attr[chn].out_win.height;
    int pix_format = self->sensor._chn_attr[chn].pix_format;


    mp_obj_t src_tuple = mp_obj_new_tuple(3, (mp_obj_t[]){MP_OBJ_NEW_SMALL_INT(CAMERA_MOD_ID), MP_OBJ_NEW_SMALL_INT(self->sensor._dev_id), MP_OBJ_NEW_SMALL_INT(chn)});
    mp_obj_t rect_tuple = mp_obj_new_tuple(4, (mp_obj_t[]){MP_OBJ_NEW_SMALL_INT(x), MP_OBJ_NEW_SMALL_INT(y), MP_OBJ_NEW_SMALL_INT(width), MP_OBJ_NEW_SMALL_INT(height)});

    mp_obj_t kwargs_dict = mp_obj_new_dict(0);
    mp_obj_dict_store(kwargs_dict, MP_OBJ_NEW_QSTR(MP_QSTR_src), src_tuple);
    mp_obj_dict_store(kwargs_dict, MP_OBJ_NEW_QSTR(MP_QSTR_rect), rect_tuple);
    mp_obj_dict_store(kwargs_dict, MP_OBJ_NEW_QSTR(MP_QSTR_pix_format), MP_OBJ_NEW_SMALL_INT(pix_format));

    return kwargs_dict;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(sensor_bind_info_obj, 1, sensor_bind_info);

static mp_obj_t __sensor_run_mcm_device(void)
{
    if (!__sensor_is_mcm_device())
        return mp_const_none;
    
    sensor_each
    (
        int ret = kd_mpi_vicap_set_dev_attr(i, sensor->_dev_attr);
        if (ret)
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, set dev attr failed(%d)"), i, ret);

        for (size_t chn_num = 0; chn_num < VICAP_CHN_ID_MAX; chn_num++)
        {
            if (!sensor->_chn_attr[chn_num].chn_enable) 
                continue;
            ret = kd_mpi_vicap_set_chn_attr(i, chn_num, sensor->_chn_attr[chn_num]);
            if (ret)
                mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, set chn(%d) attr failed(%d)"), i, chn_num, ret);
        }
    )

    sensor_each
    (
        int ret = kd_mpi_vicap_init(i);
        if (ret)
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, vicap init failed(%d)"), i, ret);
    )

    sensor_each
    (
        mp_printf(&mp_plat_print, "sensor(%d), mode %d, buffer_num %d, buffer_size %d\n", i, sensor->_dev_attr.mode, sensor->_dev_attr.buffer_num, sensor->_dev_attr.buffer_size);
        int ret = kd_mpi_vicap_start_stream(i);
        if (ret)
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, vicap start stream failed(%d)"), i, ret);
        sensor->_is_started = true;
    )

    return mp_const_none;
}

STATIC mp_obj_t sensor_run(mp_obj_t self_in) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(self_in);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }
    
    if (!self->sensor._dev_attr.dev_enable) {
        mp_raise_msg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    }

    if (__sensor_is_mcm_device()) {
        return __sensor_run_mcm_device();
    }

    if (self->sensor._is_started)
        return mp_const_none;

    int ret = kd_mpi_vicap_set_dev_attr(self->sensor._dev_id, self->sensor._dev_attr);
    if (ret) 
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, set dev attr failed(%d)"), self->sensor._dev_id, ret);

    for (int chn_num = 0; chn_num < VICAP_CHN_ID_MAX; chn_num++) 
    {
        if (!self->sensor._chn_attr[chn_num].chn_enable) 
            continue;
        ret = kd_mpi_vicap_set_chn_attr(self->sensor._dev_id, chn_num, self->sensor._chn_attr[chn_num]);
        if (ret) 
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, set chn(%d) attr failed(%d)"), self->sensor._dev_id, chn_num, ret);
    }

    ret = kd_mpi_vicap_init(self->sensor._dev_id);
    if (ret) 
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, vicap init failed(%d)"), self->sensor._dev_id, ret);

    mp_printf(&mp_plat_print, "sensor(%d), mode %d, buffer_num %d, buffer_size %d\n", self->sensor._dev_id, self->sensor._dev_attr.mode , self->sensor._dev_attr.buffer_num, self->sensor._dev_attr.buffer_size);

    ret = kd_mpi_vicap_start_stream(self->sensor._dev_id);
    if (ret) 
    {
        kd_mpi_vicap_deinit(self->sensor._dev_id);
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) run error, vicap start stream failed(%d)"), self->sensor._dev_id, ret);
    }

    vb_mgmt_vicap_dev_inited(self->sensor._dev_id);
    self->sensor._is_started = true;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sensor_run_obj, sensor_run);

STATIC mp_obj_t sensor_stop(mp_uint_t n_args, const mp_obj_t *args) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    bool is_del = (n_args == 2) ? mp_obj_is_true(args[1]) : false;

    printf("is_del=%d\n", is_del);

    if (!is_del && !self->sensor._dev_attr.dev_enable)
        mp_raise_msg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));

    if (!is_del && !self->sensor._is_started)
        mp_raise_msg(&mp_type_AssertionError, MP_ERROR_TEXT("should call run() first"));

    int ret = kd_mpi_vicap_stop_stream(self->sensor._dev_id);
    if (!is_del && ret) 
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) stop error, stop stream failed(%d)"), self->sensor._dev_id, ret);

    ret = kd_mpi_vicap_deinit(self->sensor._dev_id);
    if (!is_del && ret) 
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) stop error, vicap deinit failed(%d)"), self->sensor._dev_id, ret);

    vb_mgmt_vicap_dev_deinited(self->sensor._dev_id);

    self->sensor._dev_attr.dev_enable = false;
    for (size_t chn = 0; chn < VICAP_CHN_ID_MAX; chn++)
    {
        if (self->sensor._imgs[chn].is_init == true)
            dumped_image_release(&self->sensor._imgs[chn]);
    }
    self->sensor._is_started = false;

    Sensor._devs[self->sensor._dev_id] = NULL;

    return mp_const_none;

}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sensor_stop_obj,1,2, sensor_stop);

STATIC mp_obj_t sensor_snapshot(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) 
{
    mp_obj_t img = mp_const_none;

    mp_sensor_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    int chn = CAM_CHN_ID_0;
    mp_map_elem_t *kw_chn = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_chn), MP_MAP_LOOKUP);
    if (kw_chn != NULL) chn = mp_obj_get_int(kw_chn->value);

    if (!self->sensor._dev_attr.dev_enable) 
        mp_raise_msg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));

    if (chn > CAM_CHN_ID_MAX - 1)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("invalid chn id %d, should < %d"), chn, CAM_CHN_ID_MAX - 1);

    if (self->sensor._imgs[chn].is_init == false)
        dumped_image_init(&self->sensor._imgs[chn],self->sensor._dev_id, chn);
    else
        dumped_image_release(&self->sensor._imgs[chn]);
    
    k_video_frame_info frame_info = {0};
    int ret = kd_mpi_vicap_dump_frame(self->sensor._dev_id, chn, VICAP_DUMP_YUV, &frame_info, 1000);
    dumped_image_push_phys(&self->sensor._imgs[chn],frame_info.v_frame.phys_addr[0]);
    if (ret)
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) snapshot chn(%d) failed(%d)"), self->sensor._dev_id, chn, ret);
    
    k_u64 phys_addr = frame_info.v_frame.phys_addr[0];
    int img_width = frame_info.v_frame.width;
    int img_height = frame_info.v_frame.height;
    int fmt = frame_info.v_frame.pixel_format;
    int img_size;
    int img_fmt;
    if (fmt == PIXEL_FORMAT_YUV_SEMIPLANAR_420)
    {  
        img_size = img_width * img_height * 3 / 2;
        img_fmt = PIXFORMAT_YUV420;  // image.YUV420
    } 
    else if (fmt == PIXEL_FORMAT_RGB_888) 
    {  
        img_size = img_width * img_height * 3;
        img_fmt = PIXFORMAT_RGB888;  // image.RGB888
    } 
    else if (fmt == PIXEL_FORMAT_RGB_888_PLANAR) 
    {
        img_size = img_width * img_height * 3;
        img_fmt = PIXFORMAT_RGBP888;  // image.RGBP888
    } 
    else 
    {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor(%d) snapshot chn(%d) not support pixelformat(%d)"), self->sensor._dev_id, chn, fmt);
    }

    void *pvirt_addr = kd_mpi_sys_mmap_cached(phys_addr, img_size);
    long virt_addr = (long)pvirt_addr;
    dumped_image_push_virt(&self->sensor._imgs[chn], virt_addr,img_size);
    if (virt_addr)
    {
        // 创建位置参数列表
        mp_obj_t pos_args[3];
        pos_args[0] = mp_obj_new_int(img_width);
        pos_args[1] = mp_obj_new_int(img_height);
        pos_args[2] = mp_obj_new_int(img_fmt);
        mp_map_t kw_args;
        mp_map_init(&kw_args,5);
        mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_alloc), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = mp_obj_new_int(ALLOC_VB);
        mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_phyaddr), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = mp_obj_new_int(phys_addr);
        mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_virtaddr), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = mp_obj_new_int((long)virt_addr);
        mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_poolid), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = mp_obj_new_int(frame_info.pool_id);
        extern mp_obj_t py_image_load_image(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args);
        if (self->sensor._is_rgb565[chn] && img_fmt == PIXFORMAT_RGB888)
            mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_cvt_565), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = mp_obj_new_bool(true);
        else if (self->sensor._is_grayscale[chn] && img_fmt == PIXFORMAT_YUV420)
            mp_map_lookup(&kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_cvt_565), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = mp_obj_new_bool(false);
        img = py_image_load_image(3,pos_args,&kw_args);    
    }
    else
    {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("sensor({%d}) snapshot chn({%d}) mmap failed"), self->sensor._dev_id, chn);
    }
    
    return img;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(sensor_snapshot_obj,1, sensor_snapshot);


STATIC mp_obj_t sensor_set_hmirror(mp_obj_t self_in,mp_obj_t enable_in) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(self_in);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    if (!self->sensor._dev_attr.dev_enable)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    

    if (mp_obj_is_true(enable_in))
        self->sensor._dev_attr.mirror |= 1;
    else
        self->sensor._dev_attr.mirror &= ~1;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sensor_set_hmirror_obj, sensor_set_hmirror);

STATIC mp_obj_t sensor_get_hmirror(mp_obj_t self_in) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(self_in);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    if (!self->sensor._dev_attr.dev_enable)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    

    if (self->sensor._dev_attr.mirror & 1)
        return mp_const_true;
    else
        return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sensor_get_hmirror_obj, sensor_get_hmirror);

STATIC mp_obj_t sensor_set_vflip(mp_obj_t self_in,mp_obj_t enable_in) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(self_in);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    if (!self->sensor._dev_attr.dev_enable)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    

    if (mp_obj_is_true(enable_in))
        self->sensor._dev_attr.mirror |= 2;
    else
        self->sensor._dev_attr.mirror &= ~2;
    
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sensor_set_vflip_obj, sensor_set_vflip);

STATIC mp_obj_t sensor_get_vflip(mp_obj_t self_in) 
{
    mp_sensor_t *self = MP_OBJ_TO_PTR(self_in);
    if (!mp_obj_is_type(self, &mp_sensor_type))
    {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("self is not sensor object"));
    }

    if (!self->sensor._dev_attr.dev_enable)
        mp_raise_msg_varg(&mp_type_AssertionError, MP_ERROR_TEXT("should call reset() first"));
    

    if (self->sensor._dev_attr.mirror & 2)
        return mp_const_true;
    else
        return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sensor_get_vflip_obj, sensor_get_vflip);


// 定义对象方法表
STATIC const mp_rom_map_elem_t sensor_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&sensor_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_framesize), MP_ROM_PTR(&sensor_set_framesize_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pixformat), MP_ROM_PTR(&sensor_set_pixformat_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind_info), MP_ROM_PTR(&sensor_bind_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&sensor_run_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&sensor_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_snapshot), MP_ROM_PTR(&sensor_snapshot_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_hmirror), MP_ROM_PTR(&sensor_set_hmirror_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_hmirror), MP_ROM_PTR(&sensor_get_hmirror_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_vflip), MP_ROM_PTR(&sensor_set_vflip_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_vflip), MP_ROM_PTR(&sensor_get_vflip_obj) },
    { MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(SENSOR_RGB565) },
    { MP_ROM_QSTR(MP_QSTR_RGB888), MP_ROM_INT(SENSOR_RGB888) },
    { MP_ROM_QSTR(MP_QSTR_RGBP888), MP_ROM_INT(SENSOR_RGBP888) },
    { MP_ROM_QSTR(MP_QSTR_YUV420SP), MP_ROM_INT(SENSOR_YUV420SP) },
    { MP_ROM_QSTR(MP_QSTR_GRAYSCALE), MP_ROM_INT(SENSOR_GRAYSCALE) },
    { MP_ROM_QSTR(MP_QSTR_QQCIF   ),  MP_ROM_INT(0 ) },
    { MP_ROM_QSTR(MP_QSTR_QCIF    ),  MP_ROM_INT(1 ) },
    { MP_ROM_QSTR(MP_QSTR_CIF     ),  MP_ROM_INT(2 ) },
    { MP_ROM_QSTR(MP_QSTR_QSIF    ),  MP_ROM_INT(3 ) },
    { MP_ROM_QSTR(MP_QSTR_SIF     ),  MP_ROM_INT(4 ) },
    { MP_ROM_QSTR(MP_QSTR_QQVGA   ),  MP_ROM_INT(5 ) },
    { MP_ROM_QSTR(MP_QSTR_QVGA    ),  MP_ROM_INT(6 ) },
    { MP_ROM_QSTR(MP_QSTR_VGA     ),  MP_ROM_INT(7 ) },
    { MP_ROM_QSTR(MP_QSTR_HQQVGA  ),  MP_ROM_INT(8 ) },
    { MP_ROM_QSTR(MP_QSTR_HQVGA   ),  MP_ROM_INT(9 ) },
    { MP_ROM_QSTR(MP_QSTR_HVGA    ),  MP_ROM_INT(10) },
    { MP_ROM_QSTR(MP_QSTR_B64X64  ),  MP_ROM_INT(11) },
    { MP_ROM_QSTR(MP_QSTR_B128X64 ),  MP_ROM_INT(12) },
    { MP_ROM_QSTR(MP_QSTR_B128X128),  MP_ROM_INT(13) },
    { MP_ROM_QSTR(MP_QSTR_B160X160),  MP_ROM_INT(14) },
    { MP_ROM_QSTR(MP_QSTR_B320X320),  MP_ROM_INT(15) },
    { MP_ROM_QSTR(MP_QSTR_QQVGA2  ),  MP_ROM_INT(16) },
    { MP_ROM_QSTR(MP_QSTR_WVGA    ),  MP_ROM_INT(17) },
    { MP_ROM_QSTR(MP_QSTR_WVGA2   ),  MP_ROM_INT(18) },
    { MP_ROM_QSTR(MP_QSTR_SVGA    ),  MP_ROM_INT(19) },
    { MP_ROM_QSTR(MP_QSTR_XGA     ),  MP_ROM_INT(20) },
    { MP_ROM_QSTR(MP_QSTR_WXGA    ),  MP_ROM_INT(21) },
    { MP_ROM_QSTR(MP_QSTR_SXGA    ),  MP_ROM_INT(22) },
    { MP_ROM_QSTR(MP_QSTR_SXGAM   ),  MP_ROM_INT(23) },
    { MP_ROM_QSTR(MP_QSTR_UXGA    ),  MP_ROM_INT(24) },
    { MP_ROM_QSTR(MP_QSTR_HD      ),  MP_ROM_INT(25) },
    { MP_ROM_QSTR(MP_QSTR_FHD     ),  MP_ROM_INT(26) },
    { MP_ROM_QSTR(MP_QSTR_QHD     ),  MP_ROM_INT(27) },
    { MP_ROM_QSTR(MP_QSTR_QXGA    ),  MP_ROM_INT(28) },
    { MP_ROM_QSTR(MP_QSTR_WQXGA   ),  MP_ROM_INT(29) },
    { MP_ROM_QSTR(MP_QSTR_WQXGA2  ),  MP_ROM_INT(30) },
};
STATIC MP_DEFINE_CONST_DICT(sensor_locals_dict, sensor_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_sensor_type,
    MP_QSTR_sensor,
    MP_TYPE_FLAG_NONE,
    make_new, sensor_make_new,
    locals_dict, &sensor_locals_dict
    );

