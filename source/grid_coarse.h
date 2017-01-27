#ifndef GRID_COARSE
#define GRID_COARSE

#include <mutex>
#include <array>
#include <iostream>

/*
 * linked list of all items within a bucket
 */
struct GridNode {
	int eid;
	GridNode *next;
};

/*
 * linked list of all buckets within an item
 */
struct GridReference {
	int bucket;
	GridReference *next;
};

class GridC {
	// size of a cell
	float cell_size;

	// buckets that coordinates are mapped unto
	std::array<GridNode*, 100> buckets;

	std::mutex mtx;

	/*
	 * Hash function which maps x, y coordinates into a bucket.
	 * Currently is a 10 x 10 grid which wraps around at the edges.
	 */
	void hash_func(int &row, int &col, float x, float y) {
		col = std::rint((x / cell_size) - 0.5f);
		row = std::rint((y / cell_size) - 0.5f);
	};

	public:

	GridC(int cs) {
		cell_size = cs;

		// initialize buckets
		for (auto &bucket : buckets) {
			bucket = nullptr;
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
	GridReference* add(int eid, float x1, float y1, float x2, float y2) {
		// lock coarse mutex
		std::lock_guard<std::mutex> lock(mtx);

		int row1, col1, row2, col2;

		// hash floating point coordinate into integer coordinates
		hash_func(row1, col1, x1, y1);
		hash_func(row2, col2, x2, y2);

		GridReference *ref = nullptr;

		// insert into every bucket the object touches
		for (auto i = row1; i <= row2; i++) {
			for (auto j = col1; j <= col2; j++) {
				// hash integer coordinates to bucket
				auto hash = (j % 10) + 10 * (i % 10);
				auto &bucket = buckets[hash];

				// insertion
				auto node = new GridNode();
				node->eid = eid;
				node->next = bucket;
				bucket = node;

				// create reference
				auto r = new GridReference();
				r->bucket = hash;
				r->next = ref;
				ref = r;
			}
		}
		return ref;
	};

	/*
     * Clears the grid after each iteration
     */
	void clear(void) {
		for (auto &bucket : buckets) {
			while (bucket) {
				auto &node = bucket;
				bucket = node->next;
				delete node;
			}
		}
	};

	/*
     * query possible collisions from a given eid
     */
	void query(GridReference *node, int eid) {
		// for every bucket in reference
		for (auto *i = node; i; i = i->next) {

			// for every node in bucket
			for (auto *j = buckets[i->bucket]; j; j = j->next) {

				// ignore collisons with self
				if (j->eid == eid) continue;

				std::cout << eid << " intersects " << j->eid << std::endl;
			}
		}
	};

	/*
     * print the contents of the buckets
     */
	void print(void) {
		for (int i = 0; i < buckets.size(); i++) {
			for (auto *node = buckets[i]; node; node = node->next) {
				std::cout << "bucket " << i;
				std::cout << " item " << node->eid << std::endl;
			}
		}
	};
};

#endif
