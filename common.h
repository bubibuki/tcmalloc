#pragma once
#include<iostream>
#include<mutex>
#include<assert.h>
#include<unordered_map>
#include<vector>
#include <sys/mman.h>

constexpr size_t PAGELOG = 13;//一个页LOG2后的值
constexpr size_t MAXBYTES = 256*1024;//最大申请字节数
constexpr size_t NFREELIST = 16 + 56 + 56 + 56 + 24;//threadcache，centralcache的桶数量
constexpr size_t NPAGES = 129;//pagecache

#ifdef _WIN32
#include<Windows.h>
#else
#endif
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
       void* ptr = VirtualAlloc(0,kpage * (1 << 13),MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE);
#else
       void* ptr = mmap(
        NULL,
        kpage * (1ULL << PAGELOG),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
       );
#endif
       if(ptr == nullptr){
        throw std::bad_alloc();
       }
       return ptr;
}
inline static void SystemFree(void* ptr,size_t kpage)
{
#ifdef _WIN32
       VirtualFree(ptr,0,MEM_RELEASE);
#else
       munmap(ptr,kpage * (1ULL << PAGELOG));
#endif
}

namespace common{
    inline void*& NexPtr(void* ptr){
        return *static_cast<void**>(ptr);
    }
}

#include"objectpool.h"
#include"PageMap.h"

namespace common{

       class FreeList{
       public:

              FreeList() : _head(nullptr), _size(0), _max_size(1) {}       

              void push(void* obj){
                     assert(obj);
                     NexPtr(obj) = _head;
                     _head = obj;
                     ++_size;
              }

              void push_range(void* start,void* end,size_t size){
                     assert(start);
                     assert(end);
                     NexPtr(end) = _head;
                     _head = start;
                     _size += size;
              }

              void pop_range(void*& start,void*& end,size_t size){
                     assert(_size >= size);
                     start =_head;
                     end = start;
                     for(int x = 0; x < size-1; ++x ){
                            end = NexPtr(end);
                     }
                     _head = NexPtr(end);
                     NexPtr(end) = nullptr;
                     _size -= size;

              }

              void* pop(){
                     assert(_head);
                     void* ret = _head;
                     _head = NexPtr(_head);
                     common::NexPtr(ret) = nullptr;
                     --_size;
                     return ret;
              }

              bool empty(){
                     return _head == nullptr;
              }

              size_t size()const{
                     return _size;
              }

              size_t& max_size(){
                     return _max_size;
              }
       private:
              void* _head = nullptr;
              size_t _size = 0;
              size_t _max_size = 1;//从1改为32，可以减少频繁回收
       };
      
       class ClassSize{
       public:
             static inline size_t _RoundUp(size_t bytes, size_t align){
                    return (((bytes) + align - 1) & ~(align - 1));
             }
             
             static inline size_t RoundUp(size_t bytes){
                    if(bytes <= 128){
                       return _RoundUp(bytes, 8);
                    }
                    else if(bytes <= 1024){
                       return _RoundUp(bytes, 16);
                    }
                    else if(bytes <= 8*1024){
                       return _RoundUp(bytes, 128);
                    }
                    else if(bytes <= 64*1024){
                       return _RoundUp(bytes, 1024);
                    }
                    else if(bytes <= 256*1024){
                      return _RoundUp(bytes, 8*1024);
                    }
                    else{
                       return _RoundUp(bytes, 1UL << PAGELOG);
                    }
                    return -1;
             }

             static inline size_t _Index(size_t bytes, size_t align_shift)
             {
                  return ((bytes + (1UL << align_shift) - 1) >> align_shift) - 1;
             }
             
             static inline size_t Index(size_t bytes){

                  assert(bytes <= MAXBYTES);
                  static int group_array[4] = {16, 56, 56, 56};
                  if(bytes <= 128){
                     return _Index(bytes, 3);
                  }
                  else if(bytes <= 1024){
                     return _Index(bytes - 128, 4) + group_array[0];
                  }
                  else if(bytes <= 8 * 1024){
                     return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
                  }
                  else if(bytes <= 64 * 1024){
                     return _Index(bytes - 8 * 1024, 10) + group_array[2] + 
                            group_array[1] + group_array[0];
                  }
                  else if(bytes <= 256 * 1024){
                     return _Index(bytes - 64 * 1024, 13) + group_array[3] + 
                            group_array[2] + group_array[1] + group_array[0];
                  }
                  else{
                     assert(false);
                  }
                  return -1;
             }

             static size_t MaxSize(size_t size)
             {
                  if(size == 0){
                     return 0;
                  }
                  size_t num = MAXBYTES / size;
                  if(num < 2){
                     num = 2;
                  }
                  if(num > 512){
                     num = 512;
                  }
                  return num;
             }

             static size_t NumMovePage(size_t size)
             {
                  size_t num = MaxSize(size);
                  size_t npage = num * size;
                  npage >>= PAGELOG;
                  if(npage == 0)
                       npage =1;
                  return npage; 
             }
       private:
       };

       struct Span
       {
              size_t _pageId = 0;//大块内存起始页的页号
              size_t _n = 0;//
              Span* _next = nullptr;
              Span* _prev = nullptr;
              size_t _useCount = 0;
              void* _freeList = nullptr;
              bool _isUse = false;
              size_t _objSize = 0;
       };

       static ObjectPool<common::Span>SpanPool;

       class SpanList{
       public:
             SpanList()
             {
              _head = common::SpanPool.New();
              _head->_next = _head->_prev = _head;
             }
             Span* begin(){
                   return _head->_next;
             }
             Span* end(){
                   return _head;
             }
             bool empty(){
                   return _head->_next == _head;
             }
             void insert(Span* pos, Span* span)
             {
                   assert(pos);
                   assert(span);
                   Span* prev = pos->_prev;
                   prev->_next = span;
                   span->_prev = prev;
                   span->_next = pos;
                   pos->_prev =span;
             }
             void pushfront(Span* span)
             {
                   assert(span);
                   insert(begin(), span);
             }
             Span* erase(Span* pos)
             {
                   assert(pos->_next != pos);
                   Span* prev = pos->_prev;
                   prev->_next = pos->_next;
                   pos->_next->_prev = prev;
                   return pos;
             }
             Span* popfront(){
                   return erase(begin());
             }
       private:
             Span* _head;
       public:
             std::mutex _mutex;//桶锁
       };
}

