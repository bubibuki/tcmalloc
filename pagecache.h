#pragma once
#include"common.h"

#define TREE

class PageCache{
public:
    static PageCache& GetPageCache(){
        return _pagecache;
    }
    //获取从对象到span的映射
    common::Span* MapObjectToSpan(void* obj);
    //释放空间span回到Pagecache，并合并相邻的span
    void ReleaseSpanToPageCache(common::Span* span);
    //获取一个k页的span
    common::Span* NewSpan(size_t k);
private:
    common::SpanList _freelist[NPAGES];
    PageCache(const PageCache&) = delete;
    PageCache(){}
    static PageCache _pagecache;

#ifdef TREE
    #ifdef _WIN64
        //64位系统，地址空间很大
        typedef TCMalloc_PageMap3<51> PageMap;//支持2^51个页号
    #else
        //32位系统，地址空间较小
        typedef TCMalloc_PageMap3<51> PageMap;
    #endif
#else
    typedef std::unordered_map<size_t, common::Span*> PageMap;
#endif
    PageMap _idspan_map;

public:
    std::mutex _mutex;//page锁
};