
#ifndef VFS_DRIVER_FS_H
#define VFS_DRIVER_FS_H

#include "vfs.h"

extern const vfs_driver_t vfs_driver_spiffs;
extern const vfs_driver_t vfs_driver_lfs;

// one-shot SPIFFS -> LFS carry-over run on the first boot after the driver switch
int32_t vfs_migrate_spiffs_to_lfs(void);
#endif