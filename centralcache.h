#pragma once
#include"common.h"

class CentralCache{
public:
    static CentralCache& GetCentralCache(){
        return _centralcache;
    }
    //获取一个非空的span
    common::Span* GetOneSpan(common::SpanList& list, size_t byte_size);
    //从中心缓存获取一定数量的对象给threadcache
    size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);
    //将一定数量的对象释放到span跨度
    void ReleaseListToSpans(void* start, size_t byte_size);
private:
    common::SpanList _spanlist[NFREELIST];
    static CentralCache _centralcache;
    CentralCache(const CentralCache&) = delete;
    CentralCache(){}
};