#include "typesdef.h"
#include "osal/irq.h"
#include "osal/string.h"
#include "lib/dsleepdata.h"

struct system_sleepdata {
    uint32 magic1;
    uint32 cur_addr;
    uint32 regions[SYSTEM_SLEEPDATA_ID_MAX];
    uint32 magic2;
};

extern size_t __sleep_data;
extern size_t __text_start;

void *system_sleepdata_request(uint8 id, uint32 size)
{
    void *ptr = NULL;
    uint32 flags;
    uint32 start = (uint32)&__sleep_data;
    uint32 end   = (uint32)&__text_start;
    struct system_sleepdata *sdata = (struct system_sleepdata *)start;

    size  = ALIGN(size, 4);
    flags = disable_irq();
    if (sdata->magic1 != 0x1A2B3C4D || sdata->magic2 != 0xD4C3B2A1) {
        os_memset(sdata, 0, sizeof(struct system_sleepdata));
        sdata->magic1 = 0x1A2B3C4D;
        sdata->magic2 = 0xD4C3B2A1;
        sdata->cur_addr = start + sizeof(struct system_sleepdata);
    }
    if (id < SYSTEM_SLEEPDATA_ID_MAX) {
        if (sdata->regions[id] == 0) {
            if (sdata->cur_addr + size < end) {
                os_memset(sdata->cur_addr, 0, size);
                sdata->regions[id] = sdata->cur_addr;
                sdata->cur_addr   += size;
            }
        }
        ptr = (void *)sdata->regions[id];
    }
    enable_irq(flags);
    return ptr;
}

void system_sleepdata_reset(void)
{
    uint32 flags;
    uint32 *start = (uint32 *)&__sleep_data;
    flags = disable_irq();
    *start = 0;
    enable_irq(flags);
}

uint32 system_sleepdata_freesize(void)
{
    uint32 flags;
    uint32 fsize = 0;
    uint32 start = (uint32)&__sleep_data;
    uint32 end   = (uint32)&__text_start;
    struct system_sleepdata *sdata = (struct system_sleepdata *)start;

    flags = disable_irq();
    if (sdata->magic1 != 0x1A2B3C4D || sdata->magic2 != 0xD4C3B2A1) {
        os_memset(sdata, 0, sizeof(struct system_sleepdata));
        sdata->magic1 = 0x1A2B3C4D;
        sdata->magic2 = 0xD4C3B2A1;
        sdata->cur_addr = start + sizeof(struct system_sleepdata);
    }
    fsize = end - sdata->cur_addr;
    enable_irq(flags);
    return fsize;
}

