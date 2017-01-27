#ifndef THREAD_POOL
#define THREAD_POOL

#include <thread>
#include <vector>
#include <atomic>
#include <iostream>

/*
 * Task Reference combines a counter and an index together. The counter is
 * used to solve the ABA problem.
 * - 32 bit counter
 * - 32 bit index
 *
 * note that indices start from 1 to N
 * (0 is reserved for null)
 */
typedef uint64_t TaskRef;

/*
 * Store tasks in a stack data structure
 */
class TaskNode {
	public:
	std::function<void()> task;
	TaskRef next;

	TaskNode() {
		task = nullptr;
		next = 0;
	}
};

/*
 * Simple thread pool class which recylces old threads. It's expensive
 * to recreate threads over and over. Threads will compete for task nodes
 * which stores functions to execute in a stack. Stack is fine because
 * order of execution does not matter.
 */
class ThreadPool {
	// vector of thread references
	std::vector<std::thread> pool;
	// pool of preallocated tasks
	std::vector<TaskNode> taskpool;

	// pause flag which causes workers to keep looping
	std::atomic<bool> pause;
	// quit flag which causes workers to terminate
	std::atomic<bool> quit;

	// number of issued tasks
	std::atomic<int> issued;
	// number of completed tasks
	std::atomic<int> completed;

	// index stack queue, FILO, order does not matter
	std::atomic<TaskRef> head;
	// free list which stores old nodes
	std::atomic<TaskRef> free;


	/*
     * Extract node from a reference and return a new reference
     */
	TaskRef read_reference_remove(TaskRef ref, uint32_t &index) {
		auto curr_counter = ref >> 32;
		index = ref & 0x00000000FFFFFFFF;

		// reference is null
		if (index == 0) return 0;

		// subtract one because references start from 1 to N.
		// 0 is reserved for null
		//index -= 1;

		auto &node = taskpool[index];
		auto next_index = node.next & 0x00000000FFFFFFFF;

		return ((curr_counter + 1) << 32) | next_index;
	}

	/*
     * Create a new reference for insertion
     */
	TaskRef read_reference_insert(TaskRef ref, uint32_t index) {
		auto curr_counter = ref >> 32;
		return ((curr_counter + 1) << 32) | index;
	}


	/*
     * Main loop for every worker thread.
     */
	void worker_func(void) {
		while (true) {
			if (quit) break;
			if (pause) continue;

			// get current head
			auto ref = head.load();

			uint32_t index;
			TaskRef new_ref = read_reference_remove(ref, index);

			// reference is null, list is empty
			if (new_ref == 0) continue;

			// try CAS removal
			bool succ = head.compare_exchange_strong(ref, new_ref);
			if (!succ) continue;

			auto &node = taskpool[index];

			// execute task
			node.task();

			recycleNode(index);

			// successfully completed a task
			completed.fetch_add(1);
		}
	};

	/*
	 * recycle one from the free list
	 */
	uint32_t allocateNode() {
		while (true) {
			// attempt to fetch node from free list
			auto ref = free.load();

			uint32_t index;
			TaskRef new_ref = read_reference_remove(ref, index);

			// reference is null, list is empty
			if (new_ref == 0) continue;

			// attempt CAS removal
			bool succ = free.compare_exchange_strong(ref, new_ref);
			if (!succ) continue;

			// return recyclced node
			return index;
		}
	};

	/*
	 * store old nodes in free list for later use
	 */
	void recycleNode(uint32_t index) {
		auto &node = taskpool[index];

		// wipe node
		node.task = nullptr;
		while (true) {
			auto ref = free.load();
			node.next = ref;
			
			auto new_ref = read_reference_insert(ref, index);

			// attempt insertion into free list
			bool succ = free.compare_exchange_strong(ref, new_ref);
			if (succ) break;
		}
	};

	public:
	ThreadPool(int size) {
		pool.resize(size);
		taskpool.resize(10000);

		quit.store(false);
		head.store(0);
		free.store(0);
		issued.store(0);
		completed.store(0);

		// initialize free list
		for (uint32_t i = 1; i < taskpool.size(); i++) {
			recycleNode(i);
		}
	}

	/*
     * create worker threads
     * pause is used to start threads simultaneously
     */
	void start(void) {
		pause.store(true);
		for (auto &thread : pool) {
			thread = std::thread([this] { worker_func(); });
		}
		pause.store(false);
	};

	/*
     * Tell workers to stop
     */
	void stop(void) {
		quit.store(true);
		for (auto &thread : pool) {
			thread.join();
		}
	};

	/*
     * Insert task into task pool
     */
	void add(std::function<void()> func) {
		auto index = allocateNode();
		auto &node = taskpool[index];

		node.task = func;

		while (true) {
			auto ref = head.load();
			node.next = ref;
			
			auto new_ref = read_reference_insert(ref, index);

			// attempt CAS insertion
			bool succ = head.compare_exchange_strong(ref, new_ref);
			if (succ) break;
		}

		// successfully issued a new task
		issued.fetch_add(1);
	};

	/*
     * Main thread waits until tasks are completed
     */
	void wait(void) {
		while (completed < issued);
	};
};

#endif
