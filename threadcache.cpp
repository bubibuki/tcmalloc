#include "common.h"
#include "threadcache.h"
#include "centralcache.h"

thread_local ThreadCache* pTLSthreadcache = nullptr;


void* ThreadCache::Allocate(size_t size) {
    assert(size <= MAXBYTES);
    size_t index   = common::ClassSize::Index(size);
    size_t roundup = common::ClassSize::RoundUp(size);

    _classSize[index] = roundup;              // 【新增】记下这个桶的字节数

    if (!_freelist[index].empty()) {
        return _freelist[index].pop();
    }
    return FetchFromCentralCache(index, roundup);
}

void ThreadCache::Deallocate(void* obj, size_t size) {
    assert(obj);
    assert(size <= MAXBYTES);
    size_t index   = common::ClassSize::Index(size);
    size_t roundup = common::ClassSize::RoundUp(size);

    _classSize[index] = roundup;              // 【新增】

    _freelist[index].push(obj);

    if (_freelist[index].size() >= _freelist[index].max_size()) {
        ListTooLong(_freelist[index], roundup);
    }
}

void* ThreadCache::FetchFromCentralCache(size_t index, size_t bytesize) {
    // 慢启动：这次想拿多少个
    size_t batchnum = (std::min)(common::ClassSize::MaxSize(bytesize),
                                 _freelist[index].max_size());

    void* start = nullptr;
    void* end   = nullptr;
    size_t actual_num = CentralCache::GetCentralCache()
                            .FetchRangeObj(start, end, batchnum, bytesize);
    assert(actual_num >= 1);

    // 只有"要多少给多少 且 拿满了当前阈值"才翻倍
    if (actual_num == batchnum && batchnum == _freelist[index].max_size()) {
        _freelist[index].max_size() *= 2;
    }

    if (actual_num != 1) {
        _freelist[index].push_range(common::NexPtr(start), end, actual_num - 1);
    }
    return start;
}

void ThreadCache::ListTooLong(common::FreeList& list, size_t roundup) {
    void* start = nullptr;
    void* end   = nullptr;
    size_t release_num = list.max_size() / 2;
    if (release_num == 0) {
        release_num = 1;
    }
    list.pop_range(start, end, release_num);
    CentralCache::GetCentralCache().ReleaseListToSpans(start, roundup);
}