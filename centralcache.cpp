#include"centralcache.h"
#include"common.h"
#include"pagecache.h"

common::Span* CentralCache::GetOneSpan(common::SpanList& list, size_t byte_size){
    //遍历对应链表的节点，看是否又span对象，有直接返回
    for(common::Span* it = list.begin(); it != list.end(); it = it->_next){
        if(it->_freeList){
            return it;
        }
    }
    //走到这里说明没有。这里解锁central的桶锁，因为下面的操作是向pagecache获取一个Span
    //这个Span只会这个线程获得，不是临界资源，所以下面的一些对Span的操作不需要对centralcache加锁
    list._mutex.unlock();
    //对pagecache加锁，进行申请Span操作
    PageCache::GetPageCache()._mutex.lock();
    common::Span* span = PageCache::GetPageCache().NewSpan(common::ClassSize::NumMovePage(byte_size));
    span->_objSize = byte_size;
    span->_isUse = true;
    PageCache::GetPageCache()._mutex.unlock();

    //这部分对span的操作不用加锁，因为这个新的span还没有挂在桶上，不会竞争
    char* start = reinterpret_cast<char*>(span->_pageId << PAGELOG);

    //通过Span存储页数计算有多少字节总量
    size_t bytes = span->_n << PAGELOG;
    char* end = start + bytes;
    span->_freeList = static_cast<void*>(start);

    //下面是对整块内存进行切分，变成定长的自由链表
    size_t j = 1;
    while(end - start >= 2 * byte_size){
        common::NexPtr(start) = static_cast<void*>(byte_size + start);
        start = static_cast<char*>(common::NexPtr(start));
        ++j;
    }
    //j应该和这里的整除是相等的，不让就是划分错了
    assert((j == (bytes / byte_size)));
    common::NexPtr(start) = nullptr;
    //对list操作就涉及到桶的操作了，所以要上锁
    list._mutex.lock();
    list.pushfront(span);
    //这里不能提前解锁，要是提前解锁了，执行完这个函数，回到FetchRangObj中对span操作的时候就没有cc锁来保护
    //list._mutex.unlock();

    return span;
}

//从中心缓存获取一定数量的对象给threadcache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t byte_size){
    //参数列表分别是：空闲链表的开始和结尾指针，要获取的个数，每个内存的大小，返回值是实际获取的个数
    size_t index = common::ClassSize::Index(byte_size);//获取桶的下标

    //对cc中的_spanlist操作时候加锁
    _spanlist[index]._mutex.lock();

    common::Span* span = GetOneSpan(_spanlist[index], byte_size);
    assert(span);
    assert(span->_freeList);

    start = end = span->_freeList;
    size_t actual_num = 1;//实际获取个数
    for(size_t x = 0; x < batchNum -1; ++x){
        if(common::NexPtr(end) == nullptr){
            break;
        }

        end = common::NexPtr(end);
        ++actual_num;
    }

    span->_freeList = common::NexPtr(end);
    common::NexPtr(end) = nullptr;//获取下来的链式内存的尾部记得置空

    span->_useCount += actual_num;//span中内存块被使用的个数，方便后面回收判断，当_useCount为0代表可以回收
    _spanlist[index]._mutex.unlock();
    
    return actual_num;
}

void CentralCache::ReleaseListToSpans(void* start, size_t byte_size){
    size_t index = common::ClassSize::Index(byte_size);
    //上锁操作临界资源
    _spanlist[index]._mutex.lock();
    while(start != nullptr){
        common::Span* span = PageCache::GetPageCache().MapObjectToSpan(start);

        //将span链接到_freeList中
        void* next = common::NexPtr(start);
        common::NexPtr(start) = span->_freeList;
        span->_freeList = start;
        start = next;

        span->_useCount--;
        //说明没有使用这个span的内存的变量
        if(span->_useCount == 0){
            _spanlist[index].erase(span);//从centralcache剥去
            PageCache::GetPageCache()._mutex.lock();//提前锁防止span设置空闲，被其他page线程拿去合并了
            span->_isUse = false;
            span->_freeList = nullptr;
            span->_next = span->_prev = nullptr;

            _spanlist[index]._mutex.unlock();
            PageCache::GetPageCache().ReleaseSpanToPageCache(span);
            PageCache::GetPageCache()._mutex.unlock();
            _spanlist[index]._mutex.lock();
        }
    }
    _spanlist[index]._mutex.unlock();
}

CentralCache CentralCache::_centralcache;