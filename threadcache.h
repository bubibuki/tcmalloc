#pragma once
#include "common.h"

class ThreadCache {
public:
    void* Allocate(size_t size);
    void Deallocate(void* obj, size_t size);
    void* FetchFromCentralCache(size_t index, size_t size);
    void ListTooLong(common::FreeList& list, size_t size);


private:
    common::FreeList _freelist[NFREELIST];

    // 【新增】桶号 -> 该桶的字节数。不用 ClassSize 反查，省得依赖它的内部表
    size_t _classSize[NFREELIST] = { 0 };
};

extern thread_local ThreadCache* pTLSthreadcache;

