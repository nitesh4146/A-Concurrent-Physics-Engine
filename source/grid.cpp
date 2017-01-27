/*
 * Unit test for grid
 */

#include <thread>
#include <mutex>
#include <random>
#include <array>
#include <iostream>
#include <algorithm>

#include "grid_lockfree.h"
#include "threadpool.h"

int main(int argc, char **argv) {
	GridLF grid(100.0f);

	auto r1 = grid.add(1, 80.0f, 88.0f, 100.0f, 200.0f);
	auto r2 = grid.add(2, 320.0f, 604.0f, 500.0f, 660.0f);
	auto r3 = grid.add(3, 1050.0f, 4.0f, 1400.0f, 8.0f);
	auto r4 = grid.add(4, 0.0f, 0.0f, 150.0f, 140.0f);

	grid.print();

	grid.query(r1);

	return 0;
}
