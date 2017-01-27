#ifndef SAP_LIST_COARSE
#define SAP_LIST_COARSE

#include <limits>
#include <iostream>
#include <mutex>

/*
 * Node which store the ID, position, and width of an object.
 */
class SapNodeC {
	public:
	SapNodeC(int e, float p, float w) {
		eid = e;
		position = p;
		width = w;
		prev = nullptr;
		next = nullptr;
	};

	// reference to the object that this node represents
	int eid;

	// position along the axis
	float position;

	// size of the object
	float width;

	SapNodeC *prev;
	SapNodeC *next;
};


/*
 * Keep track of the positions of objects in a linked list data structure.
 */
class SapListC {
	struct SapNodeC *head;
	std::mutex mtx;

	public:

	/*
     * initialize list with two sentinels at both ends
     */
	SapListC() {
		mtx.lock();

		auto min = new SapNodeC(0, -std::numeric_limits<float>::infinity(), 0.0f);
		auto max = new SapNodeC(0, std::numeric_limits<float>::infinity(), 0.0f);

		min->next = max;
		max->prev = min;
		head = min;

		mtx.unlock();
	};

	/*
     * add node into doubly linked list
     */
	SapNodeC* add(int e, float p, float w) {
		auto node = new SapNodeC(e, p, w);

		mtx.lock();

		auto prev = head;
		auto curr = head->next;
		while (node->position > curr->position) {
			prev = curr;
			curr = curr->next;
		}

		node->prev = prev;
		node->next = curr;
		prev->next = node;
		curr->prev = node;

		mtx.unlock();

		return node;
	};

	/*
     * remove node from doubly linked list
     */
	void remove(SapNodeC *node) {

		mtx.lock();

		auto prev = node->prev;
		auto succ = node->next;

		prev->next = succ;
		succ->prev = prev;

		mtx.unlock();

		delete node;
	};

	/*
	 * move node into correct position on linked list
     */
	SapNodeC* update(SapNodeC *n, float p, float w) {
		auto a = add(n->eid, p, w);
		remove(n);
		return a;
	};

	/*
     * find all nodes that intersect the object
     */
	void query(SapNodeC *node) {
		auto curr = node->next;
		while (node->position + node->width >= curr->position) {
			std::cout << node->eid << " intersect " << curr->eid;
			std::cout << std::endl << std::flush;
			curr = curr->next;
		}
	};

	/*
     * print current state of list
     */
	void print(void) {
		for (auto curr = head; curr != nullptr; curr = curr->next) {
			std::cout << curr->eid << " @ " << curr->position;
			std::cout << " to " << curr->position + curr->width;
			std::cout << std::endl << std::flush;
		}
	};
};

#endif
