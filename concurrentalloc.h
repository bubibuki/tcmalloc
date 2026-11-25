#pragma once
#include"common.h"
#include"threadcache.h"
#include"pagecache.h"

static void* concurrentalloc(size_t size){
    if(size > MAXBYTES){
        size_t actual_size = common::ClassSize::RoundUp(size);
        size_t page_num = actual_size >> PAGELOG;
        PageCache::GetPageCache()._mutex.lock();
        common::Span* span = PageCache::GetPageCache().NewSpan(page_num);
        span->_isUse = true;
        span->_objSize = actual_size;
        PageCache::GetPageCache()._mutex.unlock();
        return reinterpret_cast<void*>(span->_pageId << PAGELOG);
    }
    
    if (pTLSthreadcache == nullptr) {
       static ObjectPool<ThreadCache> ThreadCachePool;
			pTLSthreadcache = ThreadCachePool.New();
    }
		return pTLSthreadcache->Allocate(size);
	
}

static void concurrentfree(void* ptr) {
	common::Span* span = PageCache::GetPageCache().MapObjectToSpan(ptr);
    if(span == nullptr){
        assert(false);
        return;
    }
	size_t size = span->_objSize;
	if (size > MAXBYTES) {
		PageCache::GetPageCache()._mutex.lock();
		span->_isUse = false;
		PageCache::GetPageCache().ReleaseSpanToPageCache(span);
		PageCache::GetPageCache()._mutex.unlock();

	}
	else {
        if (pTLSthreadcache == nullptr) {
        static ObjectPool<ThreadCache> ThreadCachePool;
			pTLSthreadcache = ThreadCachePool.New();
    }
		assert(pTLSthreadcache);
		pTLSthreadcache->Deallocate(ptr, size);

	}
}