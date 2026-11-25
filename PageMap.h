#include<cstdlib>
#include<assert.h>
#include <cstring> 

template <int BITS>
class TCMalloc_PageMap1 {
private:
	//求出最大长度
	static const int LENGTH = 1 << BITS;
	void** array_;//给一个二级指针，将数组指针交给他存储
public:  
	typedef uintptr_t Number;  //uintptr_t = u long long
	explicit TCMalloc_PageMap1(void* (allocator)(size_t)) {//这里是传一个内存申请的函数
		//这里我们后面改成用定长内存池来申请
			array_ = reinterpret_cast<void**>((*allocator)(sizeof(void*) << BITS));
		memset(array_, 0, sizeof(void*) << BITS);
	}

	void* get(Number k) const {
		if ((k >> BITS) > 0) {
			return NULL;
		}
		return array_[k];
	}	
	void set(Number k, void* v) {
		array_[k] = v;
	}
};

template <int BITS>
class TCMalloc_PageMap2 {
private:
	static const int ROOT_BITS = 5;
	static const int ROOT_LENGTH = 1 << ROOT_BITS;
	//第二层的长度
	static const int LEAF_BITS = BITS - ROOT_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;
	// Leaf node 最底层的数组类型
	struct Leaf {
		void* values[LEAF_LENGTH];
	};
	Leaf* root_[ROOT_LENGTH]; // 第一层的数组类型
	/*void* (*allocator_)(size_t); */// Memory allocator
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap2(/*void* (*allocator)(size_t) = SystemAlloc*/) {
		//allocator_ = allocator;
		memset(root_, 0, sizeof(root_));
	}
	void* get(Number k) const {
		const Number i1 = k >> LEAF_BITS;//可以根据我给打图看
		const Number i2 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 || root_[i1] == NULL) {
			return NULL;
		}
		return root_[i1]->values[i2];
	}
	void set(Number k, void* v) {
		const Number i1 = k >> LEAF_BITS;
		const Number i2 = k & (LEAF_LENGTH - 1);
		assert(i1 < ROOT_LENGTH);
		root_[i1]->values[i2] = v;
	}
	//确保访问的内存是开辟的，因为多层的原因
	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> LEAF_BITS;
			// Check for overflow
			if (i1 >= ROOT_LENGTH)
				return false;
			//第二层为空就创建
			if (root_[i1] == NULL) {
				static ObjectPool<Leaf> LeafPool;
				Leaf* leaf = LeafPool.New();
				if (leaf == NULL) return false;
				memset(leaf, 0, sizeof(*leaf));
				root_[i1] = leaf;
			}
			//第二层申请试一下申请第二层一整块，因此申请完了可以跳掉第二个第二层了
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}
	void PreallocateMoreMemory() {
		//提前开辟多少个内存
		Ensure(0, 1 << BITS);
	}
};
// Three-level radix tree
template <int BITS>
class TCMalloc_PageMap3 {
private:
	static const int INTERIOR_BITS = (BITS + 2) / 3; // 向上取整的三等分
	//上面几层都是等分的且往上取整保证能存储所有的数据
	static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS;//获取长度

	//获取最后一层的长度，因为三等分可能不均分
	static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;
	// Leaf node
	struct Leaf {
		void* values[LEAF_LENGTH];
	};
	// Interior node
	struct Node {
		Node* ptrs[INTERIOR_LENGTH];
	};
	Node* root_; // Root of radix tree
	//void* (*allocator_)(size_t); // Memory allocator
	Node* NewNode() {
		static ObjectPool<Node> NodePool;//定义一个定长内存池
		Node* result = NodePool.New();
		if (result != NULL) {
  			memset(result, 0, sizeof(*result));//将所有数据清0，防止指针越界
		}
		return result;
	}
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap3(/*void* (*allocator)(size_t) = SystemAlloc*/) {
		//allocator_ = allocator;
		root_ = NewNode();
	}
	void* get(Number k) const {
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);//获取三层下标，可以通过二层对比
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 ||
			root_->ptrs[i1] == NULL || root_->ptrs[i1]->ptrs[i2]
			== NULL) {
			return NULL;
		}
		return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3];
	}
	void set(Number k, void* v) {
		assert(k >> BITS == 0);
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
	}
	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);
			const Number i2 = (key >> LEAF_BITS) &(INTERIOR_LENGTH - 1);
			// Check for overflow
			if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH)
				return false;
			// Make 2nd level node if necessary
			if (root_->ptrs[i1] == NULL) {
				Node* n = NewNode();
				if (n == NULL) return false;
				root_->ptrs[i1] = n;
			}
			// Make leaf node if necessary
			if (root_->ptrs[i1]->ptrs[i2] == NULL) {
				static ObjectPool<Leaf> LeafPool;
				Leaf* leaf = LeafPool.New();
				if (leaf == NULL) return false;
				memset(leaf, 0, sizeof(*leaf));
				root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);
			}
			// Advance key past whatever is covered by this leafnode
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}
	void PreallocateMoreMemory() {
	}
};