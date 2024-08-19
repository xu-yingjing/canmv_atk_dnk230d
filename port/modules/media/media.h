#ifndef _MEDIA_H_
#define _MEDIA_H_

#include <rtthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "k_vb_comm.h"
#include "k_module.h"
#include "mpp_vb_mgmt.h"

#define MAX_MEDIA_BUFFER_POOLS              16 //FIX VALUE
#define MAX_MEDIA_LINK_COUNT                8

typedef struct media_linker
{
    k_mpp_chn src;
    k_mpp_chn dst;
}media_linker;

typedef struct media_buffer
{
    vb_block_info info;
    bool manged;
}media_buffer;

typedef struct media_manager
{
    int _vb_buffer_index;
    k_vb_config _vb_buffer;

    media_buffer *_buffers[MAX_MEDIA_BUFFER_POOLS];
    media_linker *_links[MAX_MEDIA_LINK_COUNT];

    bool _is_inited;
}media_manager;

bool media_manager_config(media_manager *obj,k_vb_config *config);
void media_manager_make_init(media_manager *media);
rt_err_t media_manager_init(media_manager *obj,bool foc_comress);
void media_manager_deinit(media_manager *obj,bool force);
media_buffer *media_manager_buffer_get(media_manager *obj,int size,int pool_id);
void media_manager_buffer_del(media_manager *obj,media_buffer *buffer);
media_linker *media_manager_link(media_manager *obj,k_mpp_chn *src,k_mpp_chn *dst);
void media_manager_del_link(media_manager *obj,media_linker *link);

#endif // !