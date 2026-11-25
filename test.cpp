#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>
#include "concurrentalloc.h"

using namespace std;

// ==========================================
// 1. 基础功能测试：单线程申请与释放
// ==========================================
void TestBasic() {
    cout << "========== 开始基础功能测试 ==========" << endl;
    
    // 测试小对象 (走 ThreadCache + CentralCache)
    vector<void*> v1;
    for (int i = 0; i < 1000; ++i) {
        void* ptr = concurrentalloc(16); // 申请 16 字节
        assert(ptr != nullptr);
        v1.push_back(ptr);
    }
    
    for (void* ptr : v1) {
        concurrentfree(ptr); // 释放 16 字节
    }

    // 测试中对象 (走 CentralCache 批量分配)
    vector<void*> v2;
    for (int i = 0; i < 1000; ++i) {
        void* ptr = concurrentalloc(1024); // 申请 1KB
        assert(ptr != nullptr);
        v2.push_back(ptr);
    }
    for (void* ptr : v2) {
        concurrentfree(ptr);
    }

    // 测试大对象 (直接走 PageCache / SystemAlloc)
    vector<void*> v3;
    for (int i = 0; i < 10; ++i) {
        void* ptr = concurrentalloc(512 * 1024); // 申请 512KB
        assert(ptr != nullptr);
        v3.push_back(ptr);
    }
    for (void* ptr : v3) {
        concurrentfree(ptr);
    }

    cout << "基础功能测试通过！" << endl;
}

// ==========================================
// 2. 多线程并发压测：同线程分配释放
// ==========================================
void ThreadWorker_SameThread(int id, int loop_count) {
    for (int i = 0; i < loop_count; ++i) {
        // 随机申请不同大小的内存
        size_t size = (rand() % 1024) + 1; 
        void* ptr = concurrentalloc(size);
        assert(ptr != nullptr);
        
        // 释放
        concurrentfree(ptr);
    }
    cout << "线程 " << id << " 循环 " << loop_count << " 次完成" << endl;
}

// ==========================================
// 3. 多线程并发压测：跨线程释放 (最容易暴露问题)
// ==========================================
void ThreadWorker_CrossThread(int id, vector<void*>& shared_ptr, std::mutex& mtx) {
    for (int i = 0; i < 10000; ++i) {
        if (id % 2 == 0) {
            // 偶数线程：负责申请内存，丢进共享区
            void* ptr = concurrentalloc(16);
            std::lock_guard<std::mutex> lock(mtx);
            shared_ptr.push_back(ptr);
        } else {
            // 奇数线程：负责从共享区拿内存去释放
            void* ptr = nullptr;
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (!shared_ptr.empty()) {
                    ptr = shared_ptr.back();
                    shared_ptr.pop_back();
                }
            }
            if (ptr != nullptr) {
                concurrentfree(ptr); // 跨线程释放
            }
        }
    }
}

int main() {
    srand(time(0));

    // 1. 单线程基础测试
    TestBasic();

    // 2. 同线程高并发压测
    cout << "\n========== 开始多线程同线程分配释放压测 ==========" << endl;
    auto start = chrono::high_resolution_clock::now();
    
    vector<thread> threads;
    int thread_num = 4;
    int loop_count = 100000;
    
    for (int i = 0; i < thread_num; ++i) {
        threads.emplace_back(ThreadWorker_SameThread, i, loop_count);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();
    cout << "同线程压测完成！总耗时: " << duration << " ms" << endl;

    // 3. 跨线程释放压测（高风险区，如果你的代码没做归属判断，这里大概率会崩）
    cout << "\n========== 开始跨线程释放压测 (高风险) ==========" << endl;
    vector<void*> shared_ptr;
    std::mutex mtx;
    vector<thread> cross_threads;
    
    for (int i = 0; i < 4; ++i) {
        cross_threads.emplace_back(ThreadWorker_CrossThread, i, std::ref(shared_ptr), std::ref(mtx));
    }
    
    for (auto& t : cross_threads) {
        t.join();
    }
    cout << "跨线程压测完成！" << endl;

    return 0;
}