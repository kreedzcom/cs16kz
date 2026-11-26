#ifndef KZ_STORAGE_H
#define KZ_STORAGE_H

#include <filesystem>
#include <functional>

enum class StorageTable : int
{
    outgoing_queue,
    upload_queue,
};

struct retry_msg
{
    int32_t retry_count;
    int64_t msg_type;
    int64_t msg_id;
    int64_t timestamp;
    StorageTable table;
    std::shared_ptr<std::string> message;
};

extern std::filesystem::path g_data_dir;

extern std::mutex g_retry_mtx;
extern std::vector<retry_msg> g_retry_queue;

extern void kz_storage_init(void);
extern void kz_storage_uninit(void);
extern void kz_storage_load();
extern void kz_storage_clear();
extern void kz_storage_requeue_all_pending(void);
extern retry_msg kz_storage_save(int64_t msg_type, StorageTable table, const std::function<std::string(int64_t)>& build);
extern void kz_storage_delete(int64_t msg_id, StorageTable table);
extern void kz_storage_delete_by_value(const std::string& value, StorageTable table);
extern void kz_storage_batch_delete(const std::vector<int64_t>& ids, StorageTable table);
/** Returns false if [msg_id] is not in outgoing_queue or storage is unavailable. */
extern bool kz_storage_try_get_outgoing(int64_t msg_id, int64_t* msg_type_out, std::string* msg_out);
extern int64_t kz_storage_count(StorageTable table);
extern void kz_storage_print(StorageTable table, int limit, bool from_end);
extern void kz_storage_print_row(StorageTable table, int64_t id);
extern void kz_storage_delete_all(StorageTable table);
extern bool kz_storage_checkpoint(void);
#endif
