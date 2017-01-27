#ifndef SAP_LIST_OPTIMISTIC
#define SAP_LIST_OPTIMISTIC

#include <limits>
#include <iostream>
#include <mutex>

/*
 * Node which store the ID, position, and width of an object.
 */
class SapNodeO {
	public:
	SapNodeO(int e, float p, float w) {
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

	SapNodeO *prev;
	SapNodeO *next;

	std::mutex mtx;
};


/*
 * Keep track of the positions of objects in a linked list data structure.
 */
class SapListO {
	struct SapNodeO *head;

	public:

	/*
     * initialize list with two sentinels at both ends
     */
	SapListO() {
		auto min = new SapNodeO(0, -std::numeric_limits<float>::infinity(), 0.0f);
		auto max = new SapNodeO(0, std::numeric_limits<float>::infinity(), 0.0f);

		min->next = max;
		max->prev = min;
		head = min;
	};

	/*
     * add node into doubly linked list
     */
	SapNodeO* add(int e, float p, float w) {
		auto node = new SapNodeO(e, p, w);

		while (true) {
			auto prev = head;
			auto curr = head->next;
			while (node->position > curr->position) {
				prev = curr;
				curr = curr->next;
			}

			prev->mtx.lock();
			node->mtx.lock();
			curr->mtx.lock();

			// validation
			if (prev->next != curr
				|| curr->prev != prev) {

					/*
					 * invalid, something happened between finding
					 * the nodes and locking the nodes.
					 * retry
					 */
					prev->mtx.unlock();
					node->mtx.unlock();
					curr->mtx.unlock();
					continue;
			}

			node->prev = prev;
			node->next = curr;
			prev->next = node;
			curr->prev = node;

			prev->mtx.unlock();
			node->mtx.unlock();
			curr->mtx.unlock();
			return node;
		}
	};

	/*
     * remove node from doubly linked list
     */
	void remove(SapNodeO *node) {

		while (true) {
			auto prev = node->prev;
			auto succ = node->next;

			prev->mtx.lock();
			node->mtx.lock();
			succ->mtx.lock();

			// validation
			if (prev->next != node
				|| succ->prev != node
				|| node->next != succ
				|| node-> prev != prev) {

				/*
				 * invalid, something happened between finding
				 * the nodes and locking the nodes.
				 * retry
				 */
				prev->mtx.unlock();
				node->mtx.unlock();
				succ->mtx.unlock();
				continue;
			}

			prev->next = succ;
			succ->prev = prev;

			prev->mtx.unlock();
			node->mtx.unlock();
			succ->mtx.unlock();

			delete node;
			return;
		}
	};

	/*
	 * move node into correct position on linked list
     */
	SapNodeO* update(SapNodeO *n, float p, float w) {
		auto a = add(n->eid, p, w);
		remove(n);
		return a;
	};

	/*
     * find all nodes that intersect the object
     */
	void query(SapNodeO *node) {
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
