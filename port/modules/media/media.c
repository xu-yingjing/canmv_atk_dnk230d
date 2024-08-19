#include "media.h"

bool media_manager_config(media_manager *obj,k_vb_config *config)
{
    if (obj->_is_inited)
    {
        printf("please run MediaManager._config() before MediaManager.init()\r\n");
        return false;
    }

    for (int i = 0; i < MAX_MEDIA_BUFFER_POOLS; i++)
    {
        if(config->comm_pool[i].blk_size && config->comm_pool[i].blk_cnt)
        {
            if (obj->_vb_buffer_index > (MAX_MEDIA_BUFFER_POOLS - 1))
            {
                printf("MediaManager buffer configure failed, pool exceeds the max\r\n");
                return false;
            }
            printf("vb_buffer.comm_pool[%d] config\r\n",obj->_vb_buffer_index);
            obj->_vb_buffer.comm_pool[obj->_vb_buffer_index].blk_size = config->comm_pool[i].blk_size;
            obj->_vb_buffer.comm_pool[obj->_vb_buffer_index].blk_cnt = config->comm_pool[i].blk_cnt;
            obj->_vb_buffer.comm_pool[obj->_vb_buffer_index].mode = config->comm_pool[i].mode;
            obj->_vb_buffer_index++;
        }
    }
    return true;
}

void media_linker_init(media_linker *obj,k_mpp_chn *src,k_mpp_chn *dst)
{
    memcpy(&obj->src,src,sizeof(k_mpp_chn));
    memcpy(&obj->dst,dst,sizeof(k_mpp_chn));
}

media_linker *media_manager_add_link(media_manager *obj,media_linker *link)
{
    for (int i = 0; i < MAX_MEDIA_LINK_COUNT; i++)
    {
        if(obj->_links[i] == NULL)
        {
            media_linker *l = malloc(sizeof(media_linker));
            if (l != NULL)
            {
                memcpy(l, link, sizeof(media_linker));
                obj->_links[i] = l;
                return l;
            }
            else
            {
                printf("link malloc err\r\n");
                return NULL;
            }
        }
    }
    return NULL;
}

void media_manager_del_link(media_manager *obj,media_linker *link)
{
    for (int i = 0; i < MAX_MEDIA_LINK_COUNT; i++)
    {
        if (obj->_links[i] != NULL && memcmp(obj->_links[i], link, sizeof(media_linker)) == 0)
        {
            int ret = vb_mgmt_pop_link_info(&link->src,&link->dst);
            if (ret)
                printf("_del_link failed ret({ret})\r\n");
                
            free(obj->_links[i]);
            obj->_links[i] = NULL;
            return;
        }
    }
    printf("del link failed {link}\r\n");
}

void media_manager_destory_all_link(media_manager *obj)
{
    for (int i = 0; i < MAX_MEDIA_LINK_COUNT; i++)
    {
        if(obj->_links[i] != NULL)
            media_manager_del_link(obj,obj->_links[i]);
    }
}

media_buffer *media_manager_add_buffer(media_manager *obj,media_buffer *buffer)
{    
    for (int i = 0; i < MAX_MEDIA_BUFFER_POOLS; i++)
    {
        if(obj->_buffers[i] == NULL)
        {
            media_buffer *b = malloc(sizeof(media_buffer));
            if (b != NULL)
            {
                memcpy(b, buffer, sizeof(media_buffer));
                obj->_buffers[i] = b;
                return b;
            }
            else
            {
                printf("buffer malloc err\r\n");
                return NULL;
            }
        }
    }
    return NULL;
}

void media_manager_buffer_del(media_manager *obj,media_buffer *buffer)
{
    media_buffer * find_buffer = NULL;

    if(buffer == NULL)
        return;

    if (buffer->manged)
    {
        for (int i = 0; i < MAX_MEDIA_BUFFER_POOLS; i++)
        {               
            if(obj->_buffers[i] != NULL && memcmp(obj->_buffers[i],buffer,sizeof(vb_block_info)) == 0)
            {
                find_buffer = obj->_buffers[i];
                obj->_buffers[i] = NULL;
                break;
            }
        }  
    }
    
    vb_block_info info = {0};
    info.size = buffer->info.size;
    info.handle = buffer->info.handle;
    info.virt_addr = buffer->info.virt_addr;

    int ret = vb_mgmt_put_block(&info);
    if (ret != 0)
    {
        printf("MediaManager.Buffer, release buf block failed({ret})\r\n");
    }

    if (find_buffer != NULL)
        free(find_buffer);
    else
        printf("del buffer failed {buffer}\r\n");

}

void media_manager_destory_all_buffer(media_manager *obj)
{
    for (int i = 0; i < MAX_MEDIA_BUFFER_POOLS; i++)
    {
        media_manager_buffer_del(obj,obj->_buffers[i]);
    }
}

media_buffer *media_manager_buffer_get(media_manager *obj,int size,int pool_id)
{
    if(!obj->_is_inited)
    {
        printf("please run MediaManager.Buffer() after MediaManager.init()\r\n");
        return NULL;
    }

    vb_block_info info = {0};

    info.size = size;
    info.pool_id = pool_id;

    int ret = vb_mgmt_get_block(&info);
    if (ret != 0)
    {
        printf("MediaManager.Buffer, get buf block failed, size {size}, ret({ret})\r\n");
        return NULL;
    }

    media_buffer buffer;
    buffer.info.handle = info.handle;
    buffer.info.pool_id = info.pool_id;
    buffer.info.phys_addr = info.phys_addr;
    buffer.info.virt_addr = info.virt_addr;
    buffer.info.size = info.size;
    buffer.manged = true;

    media_buffer *ret_buff = media_manager_add_buffer(obj,&buffer);
    if (ret_buff == NULL)
    {
        vb_mgmt_put_block(&info);
        media_manager_buffer_del(obj,&buffer);
        printf("MediaManager.Buffer, add buffer to manager failed\r\n");
    }
    return ret_buff;
}

void media_manager_make_init(media_manager *media)
{
    // 初始化对象的数据
    memset(media, 0, sizeof(media_manager));

    media->_vb_buffer_index = 0;
    media->_is_inited = false;
    media->_vb_buffer.max_pool_cnt = MAX_MEDIA_BUFFER_POOLS;

    for (int i = 0; i < MAX_MEDIA_BUFFER_POOLS; i++) 
    {
        media->_buffers[i] = NULL;
    }

    for (int i = 0; i < MAX_MEDIA_LINK_COUNT; i++) 
    {
        media->_links[i] = NULL;
    }
}


rt_err_t media_manager_init(media_manager *obj,bool foc_comress)
{
    if (obj->_is_inited)
    {
        printf("MediaManager, already initialized.\r\n");
        return RT_ERROR;
    }
    
    if(foc_comress)
    {
        k_vb_config config = {0};
        config.max_pool_cnt = 1;
        config.comm_pool[0].blk_size = (1920 * 1080 + 0xfff) & ~0xfff;
        config.comm_pool[0].blk_cnt = 2;
        config.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;
        bool res = media_manager_config(obj, &config);
        if(!res)
        {
            printf("MediaManager configure buffer for image compress failed.\r\n");
            return RT_ERROR;
        }
    }

    printf("buffer pool : %d\r\n", obj->_vb_buffer_index);
    int res = kd_mpi_vb_set_config(&obj->_vb_buffer);
    if (res)
    {
        printf("MediaManager, vb config failed({ret})\r\n");
        return RT_ERROR;
    }

    k_vb_supplement_config supplement_config = {0};
    supplement_config.supplement_config |= VB_SUPPLEMENT_JPEG_MASK;
    res = kd_mpi_vb_set_supplement_config(&supplement_config);
    if (res)
    {
        printf("MediaManager, vb supplement config failed({ret})\r\n");
        return RT_ERROR;
    }

    res = kd_mpi_vb_init();
    if(res)
    {
        printf("MediaManager, vb init failed({ret})\r\n");
        return RT_ERROR;
    }
    
    extern int ide_dbg_vo_wbc_init(void);
    ide_dbg_vo_wbc_init();
    obj->_is_inited = true;
    return RT_EOK;
}

void media_manager_deinit(media_manager *obj,bool force)
{
    usleep(100000);

    obj->_vb_buffer.max_pool_cnt = MAX_MEDIA_BUFFER_POOLS;
    obj->_vb_buffer_index = 0;
    obj->_is_inited = false;

    media_manager_destory_all_link(obj);
    media_manager_destory_all_buffer(obj);

    if(!force)
        return;
    
    printf("kd_mpi_vb_exit\r\n");

    int ret = kd_mpi_vb_exit();
    if (ret)
    {
        printf("MediaManager, vb deinit failed({ret})\r\n");
    }
}

media_linker *media_manager_link(media_manager *obj,k_mpp_chn *src,k_mpp_chn *dst)
{
    int ret = kd_mpi_sys_bind(src,dst);
    if(ret)
    {
        printf("MediaManager link {src} to {dst} failed({ret})\r\n");
        return NULL;
    }

    media_linker l;
    media_linker_init(&l,src,dst);

    media_linker *add = media_manager_add_link(obj,&l);
    if (add == NULL)
    {
        kd_mpi_sys_unbind(src, dst);
        printf("MediaManager link {src} to {dst} failed(add to links)\r\n");
        return NULL;
    }
    
    vb_mgmt_push_link_info(src, dst);

    return add;

}


void media_example(void)
{
    printf("media_example begin\r\n");
    k_vb_config cfg = {0};
    cfg.max_pool_cnt = 1;
    cfg.comm_pool[0].blk_size = (1920 * 1080 + 0xfff) & ~0xfff;
    cfg.comm_pool[0].blk_cnt = 2;
    cfg.comm_pool[0].mode = VB_REMAP_MODE_NOCACHE;

    media_manager media = {0};
    media_manager_config(&media,&cfg);

    media_manager_init(&media,false);


    media_manager_buffer_get(&media,4*800*480,0xFFFFFFFF);


    k_mpp_chn dst = {0};
    dst.mod_id = 11;//DISPLAY_MOD_ID
    dst.dev_id = 0;//DISPLAY_DEV_ID
    dst.chn_id = 1;//LAYER_VIDEO1

    k_mpp_chn src = {0};
    src.mod_id = 6;//CAMERA_MOD_ID
    src.dev_id = 0;   //CAM_DEV_ID_0
    src.chn_id = 0;     //CAM_CHN_ID_0
    media_manager_link(&media,&src,&dst);

    media_manager_deinit(&media,false);
    printf("media_example end\r\n");
}

