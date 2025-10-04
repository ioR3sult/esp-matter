#pragma once
#include <stddef.h>
#include "host/ble_gatt.h"

static inline size_t gatt_count_terminated(const struct ble_gatt_svc_def *svcs){
    size_t n=0; if(!svcs) return 0;
    while (svcs[n].type!=0 || svcs[n].uuid!=NULL || svcs[n].characteristics!=NULL) n++;
    return n;
}

static inline size_t gatt_total_services(const struct ble_gatt_svc_def *const *lists,size_t cnt){
    size_t t=0; for(size_t i=0;i<cnt;i++) t+=gatt_count_terminated(lists[i]); return t;
}

static inline size_t gatt_flatten(const struct ble_gatt_svc_def *const *lists,size_t cnt,
                                  struct ble_gatt_svc_def *merged,size_t cap){
    const size_t total=gatt_total_services(lists,cnt);
    if (cap < total + 1) return 0;
    size_t out=0;
    for(size_t i=0;i<cnt;i++){
        const struct ble_gatt_svc_def *src=lists[i];
        for(size_t j=0;j<gatt_count_terminated(src);j++) merged[out++]=src[j];
    }
    merged[out]=(struct ble_gatt_svc_def){0};
    return out;
}
