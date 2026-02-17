#ifndef KZ_STORAGE_H
#define KZ_STORAGE_H

#include <filesystem>

enum class StorageTable : int
{
    outgoing_queue,
    upload_queue,
};

typedef struct
{
    int32_t retry_count;
    int64_t msg_type;
    int64_t msg_id;
    int64_t timestamp;
    StorageTable table;
    std::shared_ptr<std::string> message;
} retry_msg;

extern std::filesystem::path g_data_dir;

extern std::mutex g_retry_mtx;
extern std::vector<retry_msg> g_retry_queue;

extern void kz_storage_init(void);
extern void kz_storage_uninit(void);
extern void kz_storage_load();
extern void kz_storage_clear();
extern int64_t kz_storage_get_next_id(StorageTable table);
extern void kz_storage_save(std::shared_ptr<std::string> text, int64_t msg_type, int64_t msg_id, StorageTable table);
extern void kz_storage_delete(int64_t msg_id, StorageTable table);
extern void kz_storage_batch_delete(const std::vector<int64_t>& ids, StorageTable table);
#endif
