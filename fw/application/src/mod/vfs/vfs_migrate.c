#include "vfs.h"
#include "vfs_driver_fs.h"

/* SPIFFS -> LFS boot migration marker: "/.lfs_migrated" */
void vfs_migrate_spiffs_to_lfs(void) {
    /* no-op placeholder: migration performed by driver init */
}
