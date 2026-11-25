#pragma once
#include<iostream>
#include<vector>
#include<mutex>
#include <algorithm>

template<class T>
class ObjectPool{
public:
      T* New()
      {
        _mutex.lock();
        
        T* obj = nullptr;
        if(_freelist)
        {
            obj = static_cast<T*>(_freelist);
            _freelist = common::NexPtr(_freelist);
        }
        else
        {
            size_t size = sizeof(T);
            if(_leftsize < size)
            {
                _leftsize = (std::max)((size_t)128 * 1024, size);
                _memory = static_cast<char*>(SystemAlloc(_leftsize >> PAGELOG));
            }
            obj = reinterpret_cast<T*>(_memory);
            size_t objsize = (std::max)(sizeof(T), sizeof(void*)); // 注意加括号，防止 Windows 宏冲突
            _memory += objsize;
            _leftsize -= objsize;
        }

        new(obj)T;//定位new
        _mutex.unlock();
        return obj;
      }

      void Delete(T* obj)
      {
        assert(obj);
        _mutex.lock();
        if(obj)
        {
            obj->~T();
            common::NexPtr(obj) = _freelist;
            _freelist = obj;
        }
        _mutex.unlock();
      }
private:
    std::mutex _mutex;
    char* _memory;
    size_t _leftsize = 0;
    void* _freelist = nullptr;
};

struct TreeNode
{
    int _val;
    TreeNode* _left;
    TreeNode* _right;

    TreeNode():_val(0), _left(nullptr), _right(nullptr){}
};
//性能对比测试
static void TestObjectPool()
{
	// 申请释放的轮次
	const size_t Rounds = 5;

	// 每轮申请释放多少次
	const size_t N = 10000000;

	std::vector<TreeNode*> v1;
	v1.reserve(N);

	size_t begin1 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v1.push_back(new TreeNode);
		}
		for (int i = 0; i < N; ++i)
		{
			delete v1[i];
		}
		v1.clear();
	}

	size_t end1 = clock();

	std::vector<TreeNode*> v2;
	v2.reserve(N);

	ObjectPool<TreeNode> TNPool;
	size_t begin2 = clock();
	for (size_t j = 0; j < Rounds; ++j)
	{
		for (int i = 0; i < N; ++i)
		{
			v2.push_back(TNPool.New());
		}
		for (int i = 0; i < N; ++i)
		{
			TNPool.Delete(v2[i]);
		}
		v2.clear();
	}
	size_t end2 = clock();

	std::cout << "new cost time:" << end1 - begin1 << std::endl;
	std::cout << "object pool cost time:" << end2 - begin2 << std::endl;
}