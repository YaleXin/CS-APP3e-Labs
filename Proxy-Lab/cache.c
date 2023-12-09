#include <stddef.h>
#include "cache.h"
#include "csapp.h"
// 读优先的方式修改全局cache
sem_t all_cache_mutex, all_cache_w;
int readcnt;
void cache_init(caches *mycaches){
    readcnt = 0;
    mycaches->cnt = 0;
    mycaches->global_clock = 0;
    Sem_init(&all_cache_mutex, 0, 1);
    Sem_init(&all_cache_w, 0, 1);
}
void get_cache(caches *mycaches, char *uri, unsigned char *content, int *status){
    *status = 0;
    P(&all_cache_mutex);
    readcnt++;
    if (readcnt == 1){
        P(&all_cache_w);
    }
    V(&all_cache_mutex);


    for(int i = 0; i < mycaches->cnt; i++){
        if (!strcmp(uri, mycaches->cache_lines[i].uri)){
            memcpy((void*)content, (void*)mycaches->cache_lines[i].object_content, sizeof(mycaches->cache_lines[i].object_content));
            *status = 1;
            break;
        }
    }

    P(&all_cache_mutex);
    readcnt--;
    if (readcnt == 0){
        V(&all_cache_w);
    }
    V(&all_cache_mutex);
}
void put_cache(caches *mycaches, char *uri, unsigned char *content, size_t content_size, int *status){
    *status = 0;
    if(content_size > MAX_OBJECT_SIZE){
        return;
    }
    P(&all_cache_w);
    // 如果已经满了
    if(mycaches->cnt == CACHE_NUM){
        // 寻找时间戳最小的
        int min_clock = mycaches->cache_lines[0].clock, min_idx = 0;
        for(int i = 1; i < CACHE_NUM; i++){
            if(min_clock > mycaches->cache_lines[i].clock){
                min_idx = i;
                min_clock = mycaches->cache_lines[i].clock;
            }
        }
        // printf("cache full, so replace %s!\n", mycaches->cache_lines[min_idx].uri);
        // 替换
        strcpy(mycaches->cache_lines[min_idx].uri, uri);
        memcpy((void*)mycaches->cache_lines[min_idx].object_content, (void*)content, content_size);
        mycaches->cache_lines[min_idx].clock = mycaches->global_clock;
        *status = 1;
    } else{
        strcpy(mycaches->cache_lines[mycaches->cnt].uri, uri);
        memcpy((void*)mycaches->cache_lines[mycaches->cnt].object_content, (void*)content, content_size);
        mycaches->cache_lines[mycaches->cnt].clock = mycaches->global_clock;
        *status = 1;
        mycaches->cnt++;
    }
    mycaches->global_clock++;
    V(&all_cache_w);
}