#ifndef SAP_LIST_LOCKFREE
#define SAP_LIST_LOCKFREE

#include <limits>
#include <iostream>
#include <atomic>
#include <array>
#include <thread>

/*
 * SapRef is a bitfield that stores all pointers, counter, flag data in a
 * single bitfield
 *
 * - 20 bit prev
 * - 20 bit next
 * - 23 bit reference counter
 * - 1 bit marked
 */
typedef uint64_t SapRef;

/*
 * Node which store the ID, position, and width of an object.
 */
class SapNodeLF {
	public:
	SapNodeLF() {
		eid = 0;
		position = 0.0f;
		width = 0.0f;
		ref = 0;
	};

	// reference to the object that this node represents
	int eid;

	// position along the axis
	float position;

	// size of the object
	float width;

	// reference field which contains both prev and next pointers
	std::atomic<SapRef> ref;
};


/*
 * Bitfield manipulation functions
 *
 * the following is used to read fields stored in the SapRef.
 */

/*
 * get index of previous node
 */
uint32_t getPrev(SapRef ref) {
	return (ref & 0xFFFFF00000000000) >> 44;
};

/*
 * get index of successor node
 */
uint32_t getNext(SapRef ref) {
	return (ref & 0x00000FFFFF000000) >> 24;
};

/*
 * get aba counter of current
 */
uint32_t getCounter(SapRef ref) {
	return (ref & 0x0000000000FFFFFE) >> 1;
};

/*
 * get marked flag of current node
 */
bool getMarked(SapRef ref) {
	return ref & 0x0000000000000001;
};

/*
 * combine all fields into one bitfield
 */
SapRef buildRef(uint32_t prev, uint32_t next, uint32_t counter, bool marked) {
	SapRef ref = 0;
	ref |= (uint64_t) prev << 44;
	ref |= (uint64_t) next << 24;
	ref |= (uint64_t) counter << 1;
	ref |= (uint64_t) marked;
	return ref;
};

/*
 * build a bitfield specifically for referencing a different next node
 */
SapRef buildRefToNext(SapRef ref, uint32_t next, bool marked) {
	uint32_t prev = getPrev(ref);
	uint32_t counter = getCounter(ref);

	return buildRef(prev, next, counter + 1, marked);
};
/*
 * build a bitfield specifically for referencing a different prev node
 */
SapRef buildRefToPrev(SapRef ref, uint32_t prev, bool marked) {
	uint32_t next = getNext(ref);
	uint32_t counter = getCounter(ref);

	return buildRef(prev, next, counter + 1, marked);
};

/*
 * build a bitfield specifically referencing prev and next in the current node
 */
SapRef buildRefMiddle(SapRef ref, uint32_t prev, uint32_t next) {
	uint32_t counter = getCounter(ref);

	return buildRef(prev, next, counter + 1, false);
};

/*
 * build a bitfield with a marked flag, used for logical deletion
 */
SapRef buildRefMarked(SapRef ref) {
	uint32_t prev = getPrev(ref);
	uint32_t next = getNext(ref);
	uint32_t counter = getCounter(ref);

	return buildRef(prev, next, counter + 1, true);
};

/*
 * Keep track of the positions of objects in a linked list data structure.
 */
class SapListLF {
	std::atomic<SapRef> head;
	std::atomic<SapRef> free;

	/*
     * Pool of nodes.
     * Warning - Make sure there are enough nodes. Compile Time Constant.
     * Threads will deadlock waiting for nodes which will never be allocated.
     */
	std::array<SapNodeLF, 102400> nodepool;


	uint32_t allocateNode(void) {
		while (true) {
			auto ref = free.load();
			uint32_t index = getNext(ref);

			// free list is empty
			if (index == 0) continue;

			index -= 1;
			auto &node = nodepool[index];
			uint32_t next_index = getNext(node.ref);

			auto new_ref = buildRefToNext(ref, next_index, false);

			bool succ = free.compare_exchange_strong(ref, new_ref);
			if (!succ) continue;

			return index;
		}
		return 0;
	};

	/*
	 * store old nodes in free list for later use
	 */
	void recycleNode(uint32_t index) {
		auto &node = nodepool[index];

		while (true) {
			auto ref = free.load();

			// point node to old index
			auto old_index = getNext(ref);
			node.ref = buildRefToNext(node.ref, old_index, false);

			// attempt insertion into free list
			auto new_ref = buildRefToNext(ref, index + 1, false);
			bool succ = free.compare_exchange_strong(ref, new_ref);
			if (succ) break;
		}
	};

	public:

	/*
     * initialize list with two sentinels at both ends
     */
	SapListLF() {
		head.store(0);
		free.store(0);

		for (int i = 0; i < nodepool.size(); i++) {
			recycleNode(i);
		}

		auto min_index = allocateNode();
		auto max_index = allocateNode();

		auto &min = nodepool[min_index];
		auto &max = nodepool[max_index];

		// initialize min
		min.eid = 0;
		min.position = -std::numeric_limits<float>::infinity();
		min.width = 0.0f;
		min.ref = 0;

		// initialize max
		max.eid = 0;
		max.position = std::numeric_limits<float>::infinity();
		max.width = 0.0f;
		max.ref = 0;

		// link min to max, max to min
		min.ref = buildRefToNext(min.ref, max_index, false);
		max.ref = buildRefToPrev(max.ref, min_index, false);

		// link head to min
		head = buildRefToNext(0, min_index, false);
	};

	/*
     * add node into doubly linked list
     */
	uint32_t add(int e, float p, float w) {
		auto node_index = allocateNode();
		auto &node = nodepool[node_index];

		// initialize node
		node.eid = e;
		node.position = p;
		node.width = w;
		node.ref = 0;

		while (true) {
			// dereference head to get first prev node
			auto head_ref = head.load();

			// index of prev node in node pool
			auto prev_index = getNext(head_ref);
			// reference to prev node
			auto *prev = &nodepool[prev_index];
			// reference stored in prev node
			auto prev_ref = prev->ref.load();

			// index of curr node in node pool
			auto curr_index = getNext(prev_ref);
			// reference to curr node
			auto *curr = &nodepool[curr_index];
			// reference stored in curr node
			auto curr_ref = curr->ref.load();

			// find insertion position in list
			while (node.position > curr->position) {
				prev_index = curr_index;
				prev = &nodepool[prev_index];
				prev_ref = prev->ref.load();

				curr_index = getNext(prev_ref);
				curr = &nodepool[curr_index];
				curr_ref = curr->ref.load();
			}

			// point node to both prev and curr
			node.ref = buildRefMiddle(node.ref, prev_index, curr_index);

			// point prev to node
			auto new_prev_ref = buildRefToNext(prev_ref, node_index, false);
			// attempt insertion
			bool succ = prev->ref.compare_exchange_strong(prev_ref, new_prev_ref);
			if (!succ) continue;

			// attempt backward insertion
			while (true) {
				auto ref = node.ref.load();
				curr_index = getNext(ref);
				curr = &nodepool[curr_index];
				curr_ref = curr->ref.load();
				auto new_curr_ref = buildRefToPrev(curr_ref, node_index, false);
				bool s = curr->ref.compare_exchange_strong(curr_ref, new_curr_ref);
				if (s) break;
			}

			return node_index;
		}
	};

	/*
     * remove node from doubly linked list
     */
	void remove(uint32_t index) {
		auto &node = nodepool[index];

		// mark node
		while (true) {
			auto ref = node.ref.load();
			auto new_ref = buildRefMarked(ref);
			bool s = node.ref.compare_exchange_strong(ref, new_ref);
			if (s) break;
		}

		// remove prev reference
		while (true) {
			auto ref = node.ref.load();
			auto prev_index = getPrev(ref);
			auto succ_index = getNext(ref);
			auto &prev = nodepool[prev_index];
			auto prev_ref = prev.ref.load();
			auto new_prev_ref = buildRefToNext(prev_ref, succ_index, false);
			bool s = prev.ref.compare_exchange_strong(prev_ref, new_prev_ref);
			if (s) break;
		}
		
		// remove succ reference
		while (true) {
			auto ref = node.ref.load();
			auto prev_index = getPrev(ref);
			auto succ_index = getNext(ref);
			auto &succ = nodepool[succ_index];
			auto succ_ref = succ.ref.load();
			auto new_succ_ref = buildRefToPrev(succ_ref, prev_index, false);
			bool s = succ.ref.compare_exchange_strong(succ_ref, new_succ_ref);
			if (s) break;
		}

		recycleNode(index);
	};

	/*
	 * move node into correct position on linked list.
     *
     * this version uses add and remove which does not use prev for traversal
     */
	uint32_t update(uint32_t n, float p, float w) {
		auto &node = nodepool[n];
		auto a = add(node.eid, p, w);
		remove(n);
		return a;
	};

	/*
	 * move node into correct position on linked list.
     *
     * this version uses prev for traversal. should be more efficent.
     */
	uint32_t update2(uint32_t old_index, float p, float w) {
		auto &old_node = nodepool[old_index];

		auto index = allocateNode();
		auto &node = nodepool[index];

		// initialize node
		node.eid = old_node.eid;
		node.position = p;
		node.width = w;

		int iters = 0;

		while (true) {
			auto ref = old_node.ref.load();

			auto prev_index = getPrev(ref);
			auto *prev = &nodepool[prev_index];
			auto prev_ref = prev->ref.load();

			auto succ_index = getNext(prev_ref);
			auto *succ = &nodepool[succ_index];
			auto succ_ref = succ->ref.load();

			// traverse prev and next references
			while (true) {
				if (node.position > succ->position) {
					// too high, jump down through prev reference

					prev_index = getNext(prev_ref);
					prev = &nodepool[prev_index];
					prev_ref = prev->ref.load();

					succ_index = getNext(prev_ref);
					succ = &nodepool[succ_index];
					succ_ref = succ->ref.load();

				} else if (node.position < prev->position) {
					// too low, jump up though next reference

					prev_index = getPrev(prev_ref);
					prev = &nodepool[prev_index];
					prev_ref = prev->ref.load();

					succ_index = getNext(prev_ref);
					succ = &nodepool[succ_index];
					succ_ref = succ->ref.load();

				} else {
					// just right
					break;
				}

				// fall back onto old update version if prev pointers
				// lead to a cycle
				iters++;
				if (iters > 1000) return update(old_index, p, w);
			}

			// point node to both prev and succ
			node.ref = buildRefMiddle(node.ref, prev_index, succ_index);

			// point prev to node
			auto new_prev_ref = buildRefToNext(prev_ref, index, false);
			// attempt insertion
			bool s = prev->ref.compare_exchange_strong(prev_ref, new_prev_ref);
			if (!s) continue;

			// attempt backward insertion, we don't care if it succeeds
			auto new_succ_ref = buildRefToPrev(succ_ref, index, false);
			succ->ref.compare_exchange_strong(succ_ref, new_succ_ref);

			remove(old_index);
			return index;
		}
	};

	/*
     * find all nodes that intersect the object
     */
	void query(uint32_t index) {
		auto &node = nodepool[index];

		// dereference node
		auto ref = node.ref.load();

		while (true) {
			// dereference curr
			auto curr_index = getNext(ref);
			auto &curr = nodepool[curr_index];

			// end of intersections
			if (curr.position > node.position + node.width) break;

			std::cout << node.eid << " intersect " << curr.eid;
			std::cout << std::endl << std::flush;

			ref = curr.ref.load();
		}
	};

	/*
     * find all nodes that intersect the object
     */
	void query_callback(uint32_t index, std::function<void(int,int)> func) {
		auto &node = nodepool[index];

		// dereference node
		auto ref = node.ref.load();

		while (true) {
			// dereference curr
			auto curr_index = getNext(ref);
			auto &curr = nodepool[curr_index];

			// end of intersections
			if (curr.position > node.position + node.width) break;

			// callback
			func(node.eid, curr.eid);

			ref = curr.ref.load();
		}
	};

	/*
     * print current state of list
     */
	void print(void) {
		// dereference head to get first curr node
		auto head_ref = head.load();

		// index of curr node in node pool
		auto curr_index = getNext(head_ref);
		// reference to prev node
		auto *curr = &nodepool[curr_index];
		// reference stored in prev node
		auto curr_ref = curr->ref.load();

		while (true) {
			std::cout << curr->eid << " @ " << curr->position;
			std::cout << " to " << curr->position + curr->width;
			std::cout << std::endl << std::flush;

			curr_index = getNext(curr_ref);

			// null found, end of linked list
			if (curr_index == 0) break;

			curr = &nodepool[curr_index];
			curr_ref = curr->ref.load();
		}
	};
};


#endif
