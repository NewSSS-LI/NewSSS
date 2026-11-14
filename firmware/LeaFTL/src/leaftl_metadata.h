#ifndef LEAFTL_METADATA_H_
#define LEAFTL_METADATA_H_
#include "leaftl_flash.h"
/* Small fixed pool descriptors, never a per-LPN map. Base is VSA page 0. */
typedef struct { unsigned int base,used,bad; } LEAFTL_METADATA_BLOCK;
typedef struct {
    void *context;
    int (*program)(void *,unsigned int,const unsigned char *);
    int (*read)(void *,unsigned int,unsigned char *);
    int (*erase)(void *,unsigned int base);
} LEAFTL_METADATA_IO;
typedef struct {
    LEAFTL_METADATA_BLOCK *blocks;
    LEAFTL_FLASH_DIRECTORY_ENTRY *directory;
    unsigned char *gc_page;
    unsigned int count,pages,stride,groups,current,busy;
    LEAFTL_METADATA_IO io;
} LEAFTL_METADATA;
/* Bases must be unique, disjoint block ranges. Pool is already erased at
 * fresh-namespace init, never shares data free lists. Keep one erase block
 * for GC. Borrowed GC DMA page is disjoint from pending append/read buffers. */
int LeaFtlMetadataInit(LEAFTL_METADATA *,LEAFTL_METADATA_BLOCK *,unsigned int count,
    unsigned int pages,unsigned int stride,LEAFTL_FLASH_DIRECTORY_ENTRY *,unsigned int groups,
    unsigned char *gc_page,const LEAFTL_METADATA_IO *);
int LeaFtlMetadataAppend(void *,const unsigned char *,unsigned int,unsigned int *vsa);
int LeaFtlMetadataRead(void *,unsigned int,unsigned char *,unsigned int);
int LeaFtlMetadataCollect(LEAFTL_METADATA *);
#endif
