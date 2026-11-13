#ifndef LEAFTL_GC_H_
#define LEAFTL_GC_H_
#include "leaftl_writeback.h"
typedef struct {
    void *context;
    unsigned int first_ppn,page_count;
    unsigned int (*reverse)(void *,unsigned int);
    void (*drain)(void *);
    unsigned int (*reserve)(void *,unsigned int,unsigned int *);
    int (*read)(void *,unsigned int);
    int (*program)(void *,unsigned int,unsigned int);
    void (*publish)(void *,unsigned int,unsigned int,unsigned int);
    void (*abandon)(void *);
    /* Must return only after erase; free-list insertion only on success. */
    int (*erase)(void *);
    LEAFTL_MAPPING_STATUS (*lookup)(void *,unsigned int lpn,unsigned int *actual_ppn);
} LEAFTL_GC_IO;
/* Separate scratch from host flush: GC may run inside host reserve().
 * Single exclusive owner; source block remains allocated on any failure. */
LEAFTL_MAPPING_STATUS LeaFtlGcRelocate(LEAFTL_MAPPING *,LEAFTL_WRITEBACK_WORK *,
    const LEAFTL_GC_IO *,unsigned int *moved);
#endif
