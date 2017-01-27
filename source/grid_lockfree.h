#ifndef GRID_LOCKFREE
#define GRID_LOCKFREE

#include <atomic>
#include <array>
#include <iostream>
#include <limits>
#include <thread>

/*
 * linked list of all items within a bucket
 * or
 * linked list of all buckets within an item
 *
 * GridReference has been merged with GridNode
 */
struct GridNode {
	int data;
	GridNode *next;
};

class GridLF {
	// size of a cell
	float cell_size;


	std::atomic<int> alloc;
	std::atomic<int> freed;

	// buckets that coordinates are mapped unto
	std::array<GridNode*, 10000> buckets;


	std::array<GridNode, 204800> nodepool;
	std::atomic<GridNode*> free;

	/*
	 * Hash function which maps x, y coordinates into a bucket.
	 * Currently is a 10 x 10 grid which wraps around at the edges.
	 */
	void hash_func(int &row, int &col, float x, float y) {
		col = std::rint((x / cell_size) - 0.5f);
		row = std::rint((y / cell_size) - 0.5f);
	};

	/*
     * return a node to the freelist
     */
	void freeNode(GridNode *node) {
		while (true) {
			auto old_free = free.load();
			node->next = old_free;
			bool s = free.compare_exchange_strong(old_free, node);
			if (s) break;
		}
		freed.fetch_add(1);
	};

	/*
     * returns and entire list to the freelist
	 */
	void freeNodeList(GridNode *list) {
		while (list) {
			auto *n = list;
			list = list->next;
			freeNode(n);
		}
	};

	/*
     * allocate a node from the free list
     */
	GridNode* allocateNode(void) {
		while (true) {
			auto old_free = free.load();

			// list is empty, try again later
			if (old_free == nullptr) continue;

			auto new_free = old_free->next;
			bool s = free.compare_exchange_strong(old_free, new_free);
			if (s) {
				alloc.fetch_add(1);
				old_free->data = 0;
				old_free->next = nullptr;
				return old_free;
			}
		}
	};

	/*
	 * create sentinel nodes for linked lists
	 */
	GridNode* build_sentinels(void) {
		auto *min = allocateNode();
		auto *max = allocateNode();

		min->data = std::numeric_limits<int>::lowest();
		max->data = std::numeric_limits<int>::max();

		min->next = max;
		max->next = nullptr;
		return min;
	}

	public:

	GridLF(int cs) {
		cell_size = cs;

		free.store(0);

		alloc.store(0);
		freed.store(0);

		// initialize buckets
		for (auto &bucket : buckets) {
			bucket = nullptr;
		}

		// initialize freelist
		for (auto &node : nodepool) {
			freeNode(&node);
		}
	};

	/*
     * Insert references of objects into the grid
	 *
	 * Input is an EntityID and an AABB bounding box representing the object.
	 * the function assumes x1, y1 is less than x2, y2.
	 *
 	 * returns a linked list to buckets
     */
	GridNode* add(int eid, float x1, float y1, float x2, float y2) {
		int row1, col1, row2, col2;

		// hash floating point coordinate into integer coordinates
		hash_func(row1, col1, x1, y1);
		hash_func(row2, col2, x2, y2);

		// reference list of buckets
		// first node stores eid
		GridNode *id = allocateNode();
		id->data = eid;

		// reference list
		GridNode *ref = nullptr;

		// insert into every bucket the object touches
		for (auto i = row1; i <= row2; i++) {
			for (auto j = col1; j <= col2; j++) {
				// hash integer coordinates to bucket
				auto hash = (j % 100) + 100 * (i % 100);
				auto &bucket = buckets[hash];

				// insert object into bucket
				auto node = allocateNode();
				node->data = eid;
				node->next = bucket;
				bucket = node;

				// add bucket to reference list
				auto r = allocateNode();
				r->data = hash;
				r->next = ref;
				ref = r;
			}
		}

		// make id node first
		id->next = ref;
		return id;
	};

	/*
     * Clears the grid after each iteration
     */
	void clear(void) {
		for (auto &bucket : buckets) {
			while (bucket) {
				auto *node = bucket;
				bucket = node->next;
				freeNode(node);
			}
		}
	};

	/*
     * return reference list to memory, all entities must do this or
     * it will leak the grid's nodepool memory.
     */
	void returnRefNodes(GridNode *nodes) {
		freeNodeList(nodes);
	};

	/*
     * query possible collisions from a given eid
     */
	void query(GridNode *node) {
		// first node stores eid
		int eid = node->data;

		// list of collisions
		// used to remove duplicate collisions between
		// same objects in different cells
		GridNode *collisions = build_sentinels();

		// for every bucket in reference
		for (auto *i = node->next; i; i = i->next) {

			// for every node in bucket
			for (auto *j = buckets[i->data]; j; j = j->next) {

				// ignore symmetric collisons and collisions with self
				if (j->data <= eid) continue;

				// find position on collision list
				auto *prev = collisions;
				auto *curr = prev->next;
				while (j->data > curr->data) curr = curr->next;

				// position already exists, skip
				if (curr->data == j->data) continue;

				auto *node = allocateNode();
				node->data = j->data;
				node->next = curr;
				prev->next = node;
			}
		}

		// cycle collisions
		for (auto *curr = collisions->next; curr->next; curr = curr->next) {
			std::cout << eid << " intersects " << curr->data << std::endl;
		}

		freeNodeList(collisions);
	};

	void query_callback(GridNode *node, std::function<void(int,int)> func) {
		// first node stores eid
		int eid = node->data;

		// list of collisions
		// used to remove duplicate collisions between
		// same objects in different cells
		GridNode *collisions = build_sentinels();

		// for every bucket in reference
		for (auto *i = node->next; i; i = i->next) {

			// for every node in bucket
			for (auto *j = buckets[i->data]; j; j = j->next) {

				// ignore symmetric collisons and collisions with self
				if (j->data <= eid) continue;

				// find position on collision list
				auto *prev = collisions;
				auto *curr = prev->next;
				while (j->data > curr->data) curr = curr->next;

				// position already exists, skip
				if (curr->data == j->data) continue;

				auto *node = allocateNode();
				node->data = j->data;
				node->next = curr;
				prev->next = node;
			}
		}

		// cycle collisions
		for (auto *curr = collisions->next; curr->next; curr = curr->next) {
			func(eid, curr->data);
		}

		freeNodeList(collisions);
	};

	/*
     * print the contents of the buckets
     */
	void print(void) {
		for (int i = 0; i < buckets.size(); i++) {
			for (auto *node = buckets[i]; node; node = node->next) {
				std::cout << "bucket " << i;
				std::cout << " item " << node->data << std::endl;
			}
		}
	};
};

#endif
