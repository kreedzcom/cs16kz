#ifndef KZ_STORAGE_H
#define KZ_STORAGE_H

#include <filesystem>

enum class StorageTable : int
{
    outgoing_queue,
    replay_up_queue,
};

typedef struct
{
    int64_t msg_id;
    std::string message;
    int64_t timestamp;
    StorageTable table;
} retry_msg;

extern std::filesystem::path g_data_dir;
extern std::vector<retry_msg> g_storage_queue;

extern void kz_storage_init(void);
extern void kz_storage_uninit(void);
extern int64_t kz_storage_get_next_id(StorageTable table);
extern void kz_storage_save(const std::string& text, int64_t msg_id, StorageTable table);
extern void kz_storage_delete(int64_t msg_id, StorageTable table);

#endif
