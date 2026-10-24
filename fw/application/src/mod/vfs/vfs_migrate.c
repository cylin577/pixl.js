#include "vfs.h"
#include "vfs_driver_fs.h"

#include "nrf_log.h"

#include <string.h>

#include "cwalk2.h"
#include "mui_mem.h"

// One-shot SPIFFS -> LFS carry-over of user files.
// Both filesystems claim the same flash region, so the LFS format erases the
// legacy data: every file must be staged in RAM before the format runs.
// shortcut: 8 KiB staging budget, files over 4 KiB are skipped (animations,
// videos) and must be re-copied by the user; raise the budget when RAM allows.
#define VFS_MIGRATE_MAX_FILES 64
#define VFS_MIGRATE_MAX_FILE (4 * 1024)
#define VFS_MIGRATE_MAX_TOTAL (8 * 1024)

typedef struct {
    uint32_t data_off;
    uint32_t meta_off; // 0 when the file carries no meta blob
    uint32_t size;
    char path[VFS_MAX_FULL_PATH_LEN];
} vfs_migrate_item_t;

static bool _meta_empty(const uint8_t *p_meta) {
    for (uint32_t i = 0; i < VFS_MAX_META_LEN; i++) {
        if (p_meta[i] != 0) {
            return false;
        }
    }
    return true;
}

static int32_t _migrate_walk(vfs_driver_t *p_driver, const char *dir, vfs_migrate_item_t *items, uint32_t *p_count,
                             uint8_t *buff, uint32_t *p_size) {
    vfs_dir_t fd;
    vfs_obj_t obj;

    int32_t err = p_driver->open_dir(dir, &fd);
    if (err != VFS_OK) {
        return err;
    }

    while (p_driver->read_dir(&fd, &obj) == VFS_OK) {
        char path[VFS_MAX_FULL_PATH_LEN];
        cwalk_append_segment(path, dir, obj.name);

        if (obj.type == VFS_TYPE_DIR) {
            _migrate_walk(p_driver, path, items, p_count, buff, p_size);
            continue;
        }

        bool keep_meta = !_meta_empty(obj.meta);
        uint32_t need = obj.size + (keep_meta ? VFS_MAX_META_LEN : 0);
        if (*p_count >= VFS_MIGRATE_MAX_FILES || obj.size == 0 || obj.size > VFS_MIGRATE_MAX_FILE ||
            *p_size + need > VFS_MIGRATE_MAX_TOTAL) {
            NRF_LOG_WARNING("migration: skip %s (%d bytes)", nrf_log_push(path), obj.size);
            continue;
        }

        int32_t read = p_driver->read_file_data(path, buff + *p_size, obj.size);
        if (read != (int32_t)obj.size) {
            NRF_LOG_WARNING("migration: read %s failed (%d)", nrf_log_push(path), read);
            continue;
        }

        vfs_migrate_item_t *p_item = &items[(*p_count)++];
        p_item->data_off = *p_size;
        p_item->meta_off = 0;
        p_item->size = obj.size;
        strncpy(p_item->path, path, sizeof(p_item->path) - 1);
        p_item->path[sizeof(p_item->path) - 1] = '\0';

        if (keep_meta) {
            p_item->meta_off = *p_size + obj.size;
            memcpy(buff + p_item->meta_off, obj.meta, VFS_MAX_META_LEN);
        }
        *p_size += need;
    }

    p_driver->close_dir(&fd);
    return VFS_OK;
}

// lfs_file_open does not create missing parent directories
static void _migrate_mkdirs(vfs_driver_t *p_driver, const char *path) {
    char dir[VFS_MAX_PATH_LEN];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    for (char *p_slash = strchr(dir + 1, '/'); p_slash != NULL; p_slash = strchr(p_slash + 1, '/')) {
        *p_slash = '\0';
        p_driver->create_dir(dir); // fails when the dir already exists, fine
        *p_slash = '/';
    }
}

int32_t vfs_migrate_spiffs_to_lfs(void) {
    vfs_migrate_item_t *p_items = mui_mem_malloc(sizeof(vfs_migrate_item_t) * VFS_MIGRATE_MAX_FILES);
    uint8_t *p_buff = mui_mem_malloc(VFS_MIGRATE_MAX_TOTAL);
    if (p_items == NULL || p_buff == NULL) {
        NRF_LOG_WARNING("migration: out of memory");
        mui_mem_free(p_items);
        mui_mem_free(p_buff);
        return VFS_ERR_NOMEM;
    }

    uint32_t count = 0;
    uint32_t size = 0;

    // legacy data present? mount does not touch flash when the fs is intact
    if (vfs_driver_spiffs.mount() == VFS_OK) {
        _migrate_walk(&vfs_driver_spiffs, "/", p_items, &count, p_buff, &size);
        vfs_driver_spiffs.umount();
    }
    NRF_LOG_INFO("migration: staged %d files, %d bytes", count, size);

    // fresh LFS, then restore what we staged
    if (vfs_driver_lfs.format() != VFS_OK || vfs_driver_lfs.mount() != VFS_OK) {
        NRF_LOG_WARNING("migration: lfs format/mount failed");
        mui_mem_free(p_items);
        mui_mem_free(p_buff);
        return VFS_ERR_FAIL;
    }

    for (uint32_t i = 0; i < count; i++) {
        vfs_migrate_item_t *p_item = &p_items[i];
        _migrate_mkdirs(&vfs_driver_lfs, p_item->path);
        int32_t written = vfs_driver_lfs.write_file_data(p_item->path, p_buff + p_item->data_off, p_item->size);
        if (written != (int32_t)p_item->size) {
            NRF_LOG_WARNING("migration: write %s failed (%d)", nrf_log_push(p_item->path), written);
            continue;
        }
        if (p_item->meta_off != 0) {
            vfs_driver_lfs.update_file_meta(p_item->path, p_buff + p_item->meta_off, VFS_MAX_META_LEN);
        }
    }

    mui_mem_free(p_items);
    mui_mem_free(p_buff);
    return VFS_OK;
}
