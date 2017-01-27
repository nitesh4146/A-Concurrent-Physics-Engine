/*
 * Unit test for SAP list
 */

#include <thread>
#include <atomic>
#include <random>
#include <vector>
#include <iostream>
#include <algorithm>

#include "sap_lockfree.h"
#include "threadpool.h"

struct entity {
	float position;
	float velocity;
	uint32_t sap;
};


void eloop(SapListLF &list, struct entity &entity) {
	if (entity.position > 100.0f) entity.velocity *= -0.95f;
	if (entity.position < 0.0f) entity.velocity *= -0.95f;
	entity.position += entity.velocity;
	entity.sap = list.update2(entity.sap, entity.position, 3.0f);
}


int main(int argc, char **argv) {
	SapListLF list;

	// random number generator, 0 seeded
	std::mt19937 mt(0);
	std::uniform_real_distribution<float> dist_p(0.0, 100.0);
	std::uniform_real_distribution<float> dist_v(-5.0, 5.0);

	std::vector<struct entity> entities;
	entities.resize(100);

	int id = 1;
	for (auto &entity : entities) {
		entity.position = dist_p(mt);
		entity.velocity = dist_v(mt);
		entity.sap = list.add(id++, entity.position, 3.0f);
	}

	list.print();

	ThreadPool pool(4);
	pool.start();

	for (int i = 0; i < 1000; i++) {
		std::shuffle(entities.begin(), entities.end(), mt);
		for (auto &entity : entities) {
			pool.add(std::bind(eloop, std::ref(list), std::ref(entity)));
		}

		pool.wait();
		list.print();
		list.query(entities[0].sap);
		std::cout << "-----------" << std::endl;
	}

	pool.stop();

	return 0;
}
