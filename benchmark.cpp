#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include "concurrentalloc.h"

using namespace std;

// 1. 原生 malloc / free 压测
void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
    vector<thread> vthread(nworks);
    atomic<size_t> malloc_costtime = 0;
    atomic<size_t> free_costtime = 0;

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = thread([&, k]() {
            vector<void*> v;
            v.reserve(ntimes);

            for (size_t j = 0; j < rounds; ++j)
            {
                auto begin1 = chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++)
                {
                    // 申请 1~8192 字节的随机大小，覆盖各个桶
                    v.push_back(malloc((16 + i) % 8192 + 1));
                }
                auto end1 = chrono::high_resolution_clock::now();

                auto begin2 = chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++)
                {
                    free(v[i]);
                }
                auto end2 = chrono::high_resolution_clock::now();
                v.clear();

                // 换算成毫秒
                malloc_costtime += chrono::duration_cast<chrono::milliseconds>(end1 - begin1).count();
                free_costtime += chrono::duration_cast<chrono::milliseconds>(end2 - begin2).count();
            }
            });
    }

    for (auto& t : vthread) t.join();

    printf("%zu 个线程并发执行 %zu 轮次，每轮次 malloc %zu 次: 花费：%zu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());
    printf("%zu 个线程并发执行 %zu 轮次，每轮次 free %zu 次: 花费：%zu ms\n",
        nworks, rounds, ntimes, free_costtime.load());
    printf("%zu 个线程并发 malloc&free %zu 次，总计花费：%zu ms\n",
        nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());
}

// 2. 自研内存池压测
void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
    vector<thread> vthread(nworks);
    atomic<size_t> malloc_costtime = 0;
    atomic<size_t> free_costtime = 0;

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = thread([&]() {
            vector<void*> v;
            v.reserve(ntimes);

            for (size_t j = 0; j < rounds; ++j)
            {
                auto begin1 = chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++)
                {
                    // 调用你的并发分配接口
                    v.push_back(concurrentalloc((16 + i) % 8192 + 1));
                }
                auto end1 = chrono::high_resolution_clock::now();

                auto begin2 = chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++)
                {
                    // 调用你的并发释放接口
                    concurrentfree(v[i]);
                }
                auto end2 = chrono::high_resolution_clock::now();
                v.clear();

                malloc_costtime += chrono::duration_cast<chrono::milliseconds>(end1 - begin1).count();
                free_costtime += chrono::duration_cast<chrono::milliseconds>(end2 - begin2).count();
            }
            });
    }

    for (auto& t : vthread) t.join();

    printf("%zu 个线程并发执行 %zu 轮次，每轮次 concurrent alloc %zu 次: 花费：%zu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());
    printf("%zu 个线程并发执行 %zu 轮次，每轮次 concurrent dealloc %zu 次: 花费：%zu ms\n",
        nworks, rounds, ntimes, free_costtime.load());
    printf("%zu 个线程并发 concurrent alloc&dealloc %zu 次，总计花费：%zu ms\n",
        nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());
}

int main()
{
    size_t ntimes = 10000;   // 每轮申请/释放次数
    size_t nworks = 4;       // 线程数
    size_t rounds = 10;      // 轮次

    cout << "==========================================================" << endl;
    cout << "测试: " << nworks << " 个线程, " << rounds << " 轮, 每轮 " << ntimes << " 次" << endl;
    
    cout << "\n【自研内存池 ConcurrentAlloc】" << endl;
    BenchmarkConcurrentMalloc(ntimes, nworks, rounds);
    
    cout << "\n【系统原生 malloc/free】" << endl;
    BenchmarkMalloc(ntimes, nworks, rounds);
    cout << "==========================================================" << endl;

    return 0;
}