#include "media.h"
#include "py/obj.h"
#include "py/runtime.h"

// 定义 MediaManager 对象结构
typedef struct _media_manager_obj_t {
    mp_obj_base_t base;
    media_manager manager;
} media_manager_obj_t;

typedef struct media_buffer_obj_t {
    mp_obj_base_t base;
    media_buffer *buffer;
} mp_media_buffer_t;
 
extern const mp_obj_type_t media_manager_type;
extern const mp_obj_type_t media_manager_buffer_type;
media_manager media_class_method_obj = {._vb_buffer_index = 0,._is_inited = false,._vb_buffer.max_pool_cnt = MAX_MEDIA_BUFFER_POOLS,
                                            ._buffers = {0},._links = {0}};

// 定义类方法
STATIC mp_obj_t media_manager_init_wrapper(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) 
{
    media_manager *manager = NULL;

    if (mp_obj_is_type(pos_args[0], &mp_type_type)) 
    {
        // 第一个参数是类类型，即类方法
        printf("Called as class method\n");
        // 处理类方法的逻辑
        manager = &media_class_method_obj;
    }
    else
    {
        manager = &((media_manager_obj_t *)pos_args[0])->manager;
    }

    // 定义参数
    enum { ARG_for_comress };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_for_comress, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_true} },
    };

    // 解析参数
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // 获取参数值
    bool for_comress = mp_obj_is_true(args[ARG_for_comress].u_obj);
    printf("for_comress %d\r\n",for_comress);
    
    rt_err_t res = media_manager_init(manager, for_comress);
    return mp_obj_new_int(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(media_manager_init_obj, 1, media_manager_init_wrapper);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(media_manager_init_class_method, MP_ROM_PTR(&media_manager_init_obj));

// 包装 media_manager_deinit 函数
STATIC mp_obj_t media_manager_deinit_wrapper(size_t n_args, const mp_obj_t *args) 
{
    media_manager *manager = NULL;

    if (mp_obj_is_type(args[0], &mp_type_type)) 
    {
        // 第一个参数是类类型，即类方法
        printf("Called as class method\n");
        // 处理类方法的逻辑
        manager = &media_class_method_obj;
        if (manager == NULL)    
            return mp_const_none;
    }
    else
    {
        manager = &((media_manager_obj_t *)args[0])->manager;
    }

    bool force = false;

    if (n_args > 1)
    {
        force = mp_obj_is_true(args[1]);
    }

    printf("force %d\r\n",force);
    media_manager_deinit(manager, force);

    if(manager == &media_class_method_obj)
    {
         media_manager_make_init(manager);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(media_manager_deinit_obj,1,2, media_manager_deinit_wrapper);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(media_manager_deinit_class_method, MP_ROM_PTR(&media_manager_deinit_obj));


STATIC mp_obj_t media_manager_config_wrapper(mp_obj_t self_in,mp_obj_t config_obj) 
{
    media_manager *manager = NULL;

    if (mp_obj_is_type(self_in, &mp_type_type)) 
    {
        // 第一个参数是类类型，即类方法
        printf("Called as class method\n");
        // 处理类方法的逻辑
        manager = &media_class_method_obj;
        if (manager == NULL)    
            return mp_const_none;
    }
    else
    {
        manager = &((media_manager_obj_t *)self_in)->manager;
    }

    mp_buffer_info_t config_buf;
    mp_get_buffer_raise(config_obj, &config_buf, MP_BUFFER_RW);

    k_vb_config *config = (k_vb_config *)config_buf.buf;
    
    media_manager_config(manager, config);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(media_manager_config_obj, media_manager_config_wrapper);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(media_manager_config_class_method, MP_ROM_PTR(&media_manager_config_obj));


STATIC mp_obj_t media_manager_link_wrapper(mp_obj_t self_in,mp_obj_t src_obj,mp_obj_t dst_obj) 
{
    media_manager *manager = NULL;

    if (mp_obj_is_type(self_in, &mp_type_type)) 
    {
        // 第一个参数是类类型，即类方法
        printf("Called as class method\n");
        // 处理类方法的逻辑
        manager = &media_class_method_obj;
        if (manager == NULL)    
            return mp_const_none;
    }
    else
    {
        manager = &((media_manager_obj_t *)self_in)->manager;
    }

    if (!mp_obj_is_type(src_obj, &mp_type_tuple) || !mp_obj_is_type(dst_obj, &mp_type_tuple)) {
        mp_raise_TypeError(MP_ERROR_TEXT("Expected two tuples"));
    }

    mp_obj_tuple_t *src_tuple = MP_OBJ_TO_PTR(src_obj);
    mp_obj_tuple_t *dst_tuple = MP_OBJ_TO_PTR(dst_obj);

    if (src_tuple->len != 3 || dst_tuple->len != 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("Tuple length mismatch"));
    }

    k_mpp_chn src_chn = {
        .mod_id = mp_obj_get_int(src_tuple->items[0]),
        .dev_id = mp_obj_get_int(src_tuple->items[1]),
        .chn_id = mp_obj_get_int(src_tuple->items[2])};

    k_mpp_chn dst_chn = {
        .mod_id = mp_obj_get_int(dst_tuple->items[0]),
        .dev_id = mp_obj_get_int(dst_tuple->items[1]),
        .chn_id = mp_obj_get_int(dst_tuple->items[2])};
    
    media_manager_link(manager, &src_chn, &dst_chn);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(media_manager_link_obj, media_manager_link_wrapper);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(media_manager_link_class_method, MP_ROM_PTR(&media_manager_link_obj));

STATIC mp_obj_t media_manager_buffer_get_wrapper(size_t n_args, const mp_obj_t *args) 
{
    printf("media_manager_buffer_get_wrapper\r\n");
    int size = mp_obj_get_int(args[0]);
    int pool_id = 0xFFFFFFFF;
    if (n_args > 1)
        pool_id = mp_obj_get_int(args[1]);
    
    media_buffer *buffer = media_manager_buffer_get(&media_class_method_obj, size, pool_id);
    if (buffer == NULL)
        return mp_const_none;

    mp_media_buffer_t *mp_buffer =  m_new_obj(mp_media_buffer_t);
    printf("mp_buffer %pr\n", mp_buffer);
    if (mp_buffer == NULL) 
    {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("obj new failed"));
        return mp_const_none;
    }

    mp_buffer->buffer = buffer;
    mp_buffer->base.type = &media_manager_buffer_type;
    
    return mp_buffer;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(media_manager_buffer_get_obj,1,2, media_manager_buffer_get_wrapper);
STATIC MP_DEFINE_CONST_STATICMETHOD_OBJ(media_manager_buffer_get_static_method, MP_ROM_PTR(&media_manager_buffer_get_obj));

STATIC mp_obj_t media_manager_buffer_destructor(mp_obj_t self_in) 
{
    printf("media_manager_buffer_destructor:%p\r\n", self_in);
    mp_media_buffer_t *buffer = self_in;
    if(!mp_obj_is_type(self_in, &media_manager_buffer_type)) 
        mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("Invalid object type"));

    media_manager_buffer_del(&media_class_method_obj, buffer->buffer);
    m_del_obj(mp_media_buffer_t, self_in);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(media_manager_buffer_destructor_obj, media_manager_buffer_destructor);

STATIC const mp_rom_map_elem_t media_manager_buffer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&media_manager_buffer_get_static_method) },
};
STATIC MP_DEFINE_CONST_DICT(media_manager_buffer_locals_dict, media_manager_buffer_locals_dict_table);

STATIC void media_manager_buffer_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) 
{
    mp_media_buffer_t *self = MP_OBJ_TO_PTR(self_in);

    if (attr == MP_QSTR_phys_addr) 
    {
        dest[0] = mp_obj_new_int_from_uint(self->buffer->info.phys_addr);
    } 
    else if(attr == MP_QSTR_virt_addr) 
    {
        dest[0] = mp_obj_new_int_from_uint(self->buffer->info.virt_addr);
    }
    else if (attr == MP_QSTR___del__)
    {
        dest[0] = (mp_obj_t)(&media_manager_buffer_destructor_obj);  // Fallback to default behavior
        dest[1] = self_in;
    }
    else 
    {
        dest[0] = MP_OBJ_NULL;  // Fallback to default behavior
    }
}

MP_DEFINE_CONST_OBJ_TYPE(
    media_manager_buffer_type,
    MP_QSTR_Buffer,
    MP_TYPE_FLAG_NONE,
    // make_new, media_manager_make_new,
    attr, media_manager_buffer_attr,
    locals_dict, &media_manager_buffer_locals_dict
    );

// 定义对象方法表
STATIC const mp_rom_map_elem_t media_manager_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&media_manager_init_class_method) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&media_manager_deinit_class_method) },
    { MP_ROM_QSTR(MP_QSTR__config), MP_ROM_PTR(&media_manager_config_class_method) },
    { MP_ROM_QSTR(MP_QSTR_link), MP_ROM_PTR(&media_manager_link_class_method) },
    { MP_ROM_QSTR(MP_QSTR_Buffer), MP_ROM_PTR(&media_manager_buffer_type) },
};
STATIC MP_DEFINE_CONST_DICT(media_manager_locals_dict, media_manager_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    media_manager_type,
    MP_QSTR_mediaManager,
    MP_TYPE_FLAG_NONE,
    // make_new, media_manager_make_new,
    locals_dict, &media_manager_locals_dict
    );

