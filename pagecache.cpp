#include"common.h"
#include"pagecache.h"

#ifdef TREE

//获取从对象到span的映射
common::Span* PageCache::MapObjectToSpan(void* obj){
    size_t id = reinterpret_cast<size_t>(obj) >> PAGELOG;
    void* ret = _idspan_map.get(id);
    assert(ret);
    return static_cast<common::Span*>(ret);
}
//释放空闲span回到PageCache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(common::Span* span){
    assert(span);
    if(span->_n > NPAGES-1){
		for(size_t i = 0; i < span->_n; ++i){
            _idspan_map.set(span->_pageId + i, nullptr);
        }

        void* ptr = reinterpret_cast<void*>(span->_pageId << PAGELOG);
        SystemFree(ptr, span->_n);
        common::SpanPool.Delete(span);
        return;
    }

    for(size_t x = 0; x < span->_n; ++x){
        _idspan_map.set(span->_pageId + x, nullptr);
    }
    //向左合并
    size_t lid = span->_pageId - 1;
    common::Span* lit = static_cast<common::Span*>(_idspan_map.get(lid));
    while(lit && (!lit->_isUse) && (lit->_n + span->_n  <= NPAGES - 1)){
        common::Span* lspan = lit;
        //删除lspan的映射
        for(size_t i = 0; i < lspan->_n; ++i){
            _idspan_map.set(lspan->_pageId + i, nullptr);
        }

        //向左合并
        span->_n += lspan->_n;
        span->_pageId = lspan->_pageId;

        //lspan在page中删除掉
        _freelist[lspan->_n].erase(lspan);
        
        //删除lspan
        common::SpanPool.Delete(lspan);

        lid = span->_pageId - 1;
        lit = static_cast<common::Span*>(_idspan_map.get(lid));
    }

    //向右合并
    size_t rid = span->_pageId + span->_n;
    common::Span* rit = static_cast<common::Span*>(_idspan_map.get(rid));
    while(rit && (!rit->_isUse) && (rit->_n + span->_n <= NPAGES - 1)){
        common::Span* rspan = rit;
        //删除rspan的映射
        for(size_t i = 0; i < rspan->_n; ++i){
            _idspan_map.set(rspan->_pageId + i, nullptr);
        }
    
        //向右合并
        span->_n += rspan->_n;

        //rspan在page中删除掉
        _freelist[rspan->_n].erase(rspan);

        //删除rspan
        common::SpanPool.Delete(rspan);

        rid = span->_pageId + span->_n;
        rit = static_cast<common::Span*>(_idspan_map.get(rid));
    }
    for(size_t i = 0; i < span->_n; ++i){
        _idspan_map.set(span->_pageId + i, span);
	}
    //设置为未使用
    span->_isUse = false;
    //span加入page中
    _freelist[span->_n].pushfront(span);
}

//获取一个K页的span
common::Span* PageCache::NewSpan(size_t k){
    assert(k > 0 && k < NPAGES);
    if(k > NPAGES - 1){
        void* ptr = SystemAlloc(k);
        common::Span* span = common::SpanPool.New();
        span->_n = k;
        span->_pageId = reinterpret_cast<size_t>(ptr) >> PAGELOG;
        
        _idspan_map.set(span->_pageId, span);
        return span;
    }
    //如果还有对应的span页，就直接弹出一个
    if(!_freelist[k].empty()){
        common::Span* span = _freelist[k].popfront();
        //把每一页建立映射
        for(size_t y = 0; y < span->_n; ++y){
            _idspan_map.set(span->_pageId + y, span);
        }
        return span;
    }
    //没有就往后面遍历，看是否有大的页进行分离
    for(int x = k + 1;x < NPAGES; ++x){
        if(!_freelist[x].empty()){
            common::Span* xspan = _freelist[x].popfront();
            common::Span* kspan = common::SpanPool.New();//分裂出的目标span
            
            _idspan_map.set(xspan->_pageId, nullptr);
            _idspan_map.set(xspan->_pageId + xspan->_n - 1, nullptr);
            
            kspan->_n = k;//设置有几页
            kspan->_pageId = xspan->_pageId;//页号就是xspan的

            xspan->_n -= k;//xspan包含的页减少了
            xspan->_pageId += k;//页号对应往后移动
            _freelist[xspan->_n].pushfront(xspan);//更新xspan的位置

            //重建映射、中间那部分就没有被删除掉还是原来的映射，所有只用建立头尾部的
            _idspan_map.set(xspan->_pageId, xspan);
            _idspan_map.set(xspan->_pageId + xspan->_n - 1, xspan);

            //全部重建映射
            for(size_t y = 0; y < kspan->_n; ++y){
                _idspan_map.set(kspan->_pageId + y, kspan);
            }
            
            return kspan;
        }
    }
    //如果没有更大的span了，就直接申请128页的内存
    common::Span* bigspan = common::SpanPool.New();
    void* ptr = SystemAlloc(NPAGES - 1);
    bigspan->_n = NPAGES - 1;
    bigspan->_pageId = reinterpret_cast<size_t>(ptr) >> PAGELOG;

    _idspan_map.Ensure(bigspan->_pageId, bigspan->_n);
    _idspan_map.set(bigspan->_pageId, bigspan);
    _idspan_map.set(bigspan->_pageId + bigspan->_n - 1, bigspan);
    _freelist[bigspan->_n].pushfront(bigspan);
    return NewSpan(k);//进行递归再调一次
}

PageCache PageCache::_pagecache;
#else
// 获取从对象到span的映射 
common::Span* PageCache::MapObjectToSpan(void* obj) {
	size_t id = reinterpret_cast<size_t>(obj) >> PAGELOG;

	std::lock_guard<std::mutex>guard(_mutex);
	auto it = _idspan_map.find(id);
	if (it != _idspan_map.end()) {
		return it->second;
	}
	else {
		//cout << "     " << obj << "   " << (((size_t)obj) >> 13) << endl;
		assert(false);
		return nullptr;
	}
}
// 释放空闲span回到Pagecache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(common::Span* span) {
	assert(span);
	if (span->_n > NPAGES - 1) {
		void* ptr = reinterpret_cast<void*>(span->_pageId << PAGELOG);
		SystemFree(ptr);
		_idspan_map.erase(span->_pageId);
		common::SpanPool.Delete(span);
		return;
	}

	for (size_t x = 0; x < span->_n; ++x) {
		_idspan_map.erase(span->_pageId + x);
	}
	//向左合并
	size_t lid = span->_pageId - 1;
	auto lit = _idspan_map.find(lid);
	while ((lit != _idspan_map.end()) && (!lit->second->_isUse) && (lit->second->_n + span->_n <= NPAGES - 1))
	{
		common::Span* lspan = lit->second;
		//删除lspan的映射
		_idspan_map.erase(lspan->_pageId);
		_idspan_map.erase(lspan->_pageId + lspan->_n - 1);

		//span向左合并
		span->_n += lspan->_n;
		span->_pageId = lspan->_pageId;

		//lspan在page里面删除
		_freelist[lspan->_n].erase(lspan);


		//删除lspan
		common::SpanPool.Delete(lspan);

		//为下次循环做准备
		lid = span->_pageId - 1;
		lit = _idspan_map.find(lid);
	}

	//向右合并
	size_t rid = span->_pageId + span->_n;
	auto rit = _idspan_map.find(rid);
	while ((rit != _idspan_map.end()) && (!rit->second->_isUse) && (rit->second->_n + span->_n <= NPAGES - 1)) {
		common::Span* rspan = rit->second;
		//删除rspan的映射
		_idspan_map.erase(rspan->_pageId);
		_idspan_map.erase(rspan->_pageId + rspan->_n - 1);

		//span向右合并
		span->_n += rspan->_n;

		//rspan在page里面删除
		_freelist[rspan->_n].erase(rspan);

		//删除rspan
		common::SpanPool.Delete(rspan);

		//为下次循环做准备
		rid = span->_pageId + span->_n;
		rit = _idspan_map.find(rid);
	}
	//添加左边映射
	_idspan_map[span->_pageId] = span;
	//添加右边映射
	_idspan_map[span->_pageId + span->_n - 1] = span;
	//设为未使用
	span->_isUse = false;
	//span加入page中
	_freelist[span->_n].pushfront(span);
}
// 获取⼀个K⻚的span
common::Span* PageCache::NewSpan(size_t k) {
	//assert(k > 0 && k < NPAGES);
	if (k > NPAGES - 1) {
		void* ptr = SystemAlloc(k);
		common::Span* span = common::SpanPool.New();
		span->_n = k;
		span->_pageId = reinterpret_cast<size_t>(ptr) >> PAGELOG;
		_idspan_map[span->_pageId] = span;
		return span;
	}
	//如果还有对应的span页，就直接弹出一个
	if (!_freelist[k].empty()) {
		common::Span* span = _freelist[k].popfront();

		for (size_t y = 0; y < span->_n; ++y) {
			_idspan_map[span->_pageId + y] = span;
		}
		return span;
	}

	//没有就往后面遍历，看是否有大的页进行分离
	for (int x = k + 1; x < NPAGES; ++x) {
		if (!_freelist[x].empty()) {
			common::Span* xspan = _freelist[x].popfront();

			common::Span* kspan = common::SpanPool.New();//分裂出的目标span

			_idspan_map.erase(xspan->_pageId);
			_idspan_map.erase(xspan->_pageId + xspan->_n - 1);

			kspan->_n = k;//设置有几页
			kspan->_pageId = xspan->_pageId;//页号就是xspan的

			xspan->_n -= k;//含的页减少了
			xspan->_pageId += k;//页号对应往后移动
			_freelist[xspan->_n].pushfront(xspan);//更新xspan位置 

			//xspan两侧页映射，方便进行span的合并
			_idspan_map[xspan->_pageId] = xspan;
			_idspan_map[xspan->_pageId + xspan->_n - 1] = xspan;

			//增加映射
			for (size_t y = 0; y < kspan->_n; ++y) {
				_idspan_map[kspan->_pageId + y] = kspan;
			}

			return kspan;
		}

	}
	//如果没有大的span了，就直接申请128页的内存
	common::Span* bigspan = common::SpanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigspan->_n = NPAGES - 1;
	bigspan->_pageId = reinterpret_cast<size_t>(ptr) >> PAGELOG;
	_idspan_map[bigspan->_pageId] = bigspan;
	_idspan_map[bigspan->_pageId + bigspan->_n - 1] = bigspan;
	_freelist[bigspan->_n].pushfront(bigspan);
	return NewSpan(k);//进行递归再调一次就行了//或者直接在这切分也行
}

PageCache PageCache::_pagecache;

#endif