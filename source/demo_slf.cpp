#include <SFML/System.hpp>
#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>
#include <random>
#include <vector>
#include <iostream>
#include <chrono>

#include "sap_lockfree.h"
#include "threadpool.h"


// note that it will crash lots of times with more than one thread.
// some kind of error in prev reference. Should have listend to the
// swedish research paper that said to use reference counting. It's
// possible for nodes to point to nodes outside of the list though the
// prev reference. No way to validate if an object was deleted though a
// pointer.
#define NUM_OBJECTS 500
#define NUM_THREADS 1

	
// Collision system in detail
// integrate positions
//		store in p1
// check collisions between p0 p1
//		store collision pair in list
// find most recent collision
// set p1 as toi
//
// why position0 and position1?
//		allows interpolation between two positions within one timestep
//		gravity will curve the positions
//		collision detection is linear
//		more accurate to check between old and new positions than with
//		old position and velocity

struct Entity {
	sf::Vector2f position0;
	sf::Vector2f position1;
	sf::Vector2f velocity;
	sf::Color color;
	uint32_t sapID;
};

struct Collision {
	Entity &a;
	Entity &b;
	float t;
};

int radius = 6;

class World {
	public:
	void step(float dt);
	std::vector<Entity> entities;
	private:
	void kinematics(float dt);
	void collisions(float dt);
};




// initialize threads
ThreadPool pool(NUM_THREADS);

// collision detection - broadphase
SapListLF list;

// total test time
std::chrono::duration<double> elapsed_seconds;



// update entity position
void updateEntityPosition(Entity &entity, float dt) {
	entity.position0 = entity.position1;
	entity.position1 += entity.velocity * dt;
}

// perform collision with wall
void updateEntityWall(Entity &entity) {
	auto &p0 = entity.position0;
	auto &p1 = entity.position1;
	auto &velocity = entity.velocity;

	// check for collisions with the screen
	if (p1.x > 800 - radius) {
		velocity.x *= -1.f;
		p1 = p0;
	}
	if (p1.x < 0 + radius) {
		velocity.x *= -1.f;
		p1 = p0;
	}
	if (p1.y > 600 - radius) {
		velocity.y *= -1.f;
		p1 = p0;
	}
	if (p1.y < 0 + radius) {
		velocity.y *= -1.f;
		p1 = p0;
	}
}

// update entity on the list
void updateSapList(Entity &entity) {
	entity.sapID = list.update(entity.sapID, entity.position1.x - radius, radius * 2.0f);
}


void collisionCallback(std::vector<Entity> &entities, float dt, int i, int j);

// query entity on the list
void querySapList(Entity &entity, std::vector<Entity> &entities, float dt) {
	list.query_callback(entity.sapID, std::bind(collisionCallback, std::ref(entities), dt, std::placeholders::_1, std::placeholders::_2));
}





void World::step(float dt) {
	std::chrono::time_point<std::chrono::system_clock> start, end;

	start = std::chrono::system_clock::now();
	kinematics(dt);
	collisions(dt);
	end = std::chrono::system_clock::now();

	std::chrono::duration<double> interval_seconds = end - start;

	elapsed_seconds += interval_seconds;
}

void World::kinematics(float dt) {
	for (auto &entity : entities) {
		pool.add(std::bind(updateEntityPosition, std::ref(entity), dt));
	}
	pool.wait();
}

float perp(sf::Vector2f &a, sf::Vector2f &b) {
	auto top = a.x * -b.y + a.y * b.x;
	auto bottom = std::sqrt(b.x * b.x + b.y * b.y);
	return top / bottom;
}

float proj(sf::Vector2f &a, sf::Vector2f &b) {
	auto top = a.x * b.x + a.y * b.y;
	auto bottom = std::sqrt(b.x * b.x + b.y * b.y);
	return top / bottom;
}

float dot(sf::Vector2f &a, sf::Vector2f &b) {
	return a.x * b.x + a.y * b.y;
}

float rd(float r, float c) {
	return std::sqrt(r * r - c * c);
}

void reflect(sf::Vector2f &a, sf::Vector2f n) {
	// max is used to prevent internal collisions
	// It checks if the object is moving away from the collision
	// when dot product of momentum and normal > 0, moving towards collision
	// when dot product < 0, moving away, don't reflect momentum again
	//
	// max prevents objects from bouncing off each other rapidly.
	a += -2.f * std::max(dot(a, n), 0.f) * n;
}

bool circle_circle_ccd_check(Entity &a, Entity &b, float dt) {
	// a is moving, b is still, distance from b to a
	auto d = a.position0 - b.position0;
	// relative velocity, a is moving, b is still
	auto av = a.position1 - a.position0;
	auto bv = b.position1 - b.position0;
	auto v = av - bv;

	// point at closest contact between a's velocity and b
	auto closest = perp(d, v);
	// time of closest contact
	auto t = proj(d, v);
	if (closest <= radius) {
		// two points of contact
		auto t1 = t - rd(radius * 2, closest);
		auto t2 = t + rd(radius * 2, closest);
		// scale by speed
		t1 /= std::sqrt(v.x * v.x + v.y * v.y);
		t2 /= std::sqrt(v.x * v.x + v.y * v.y);
		if (t1 > 0.f && t1 <= dt) return true;
		if (t2 > 0.f && t2 <= dt) return true;
	}
	return false;
}

float circle_circle_ccd_time(Entity &a, Entity &b, float dt) {
	// a is moving, b is still, distance from b to a
	auto d = a.position0 - b.position0;
	// relative velocity, a is moving, b is still
	auto av = a.position1 - a.position0;
	auto bv = b.position1 - b.position0;
	auto v = av - bv;

	// point at closest contact between a's velocity and b
	auto closest = perp(d, v);
	// time of closest contact
	auto t = proj(d, v);

	// two points of contact
	auto t1 = t - rd(radius * 2, closest);
	auto t2 = t + rd(radius * 2, closest);
	// scale by speed into time
	t1 /= std::sqrt(v.x * v.x + v.y * v.y);
	t2 /= std::sqrt(v.x * v.x + v.y * v.y);

	// return lowest time of impact
	auto ft = dt;
	if (t1 > 0.f && t1 <= dt && t1 < ft) ft = t1;
	if (t2 > 0.f && t2 <= dt && t2 < ft) ft = t2;
	return ft;
}

bool circle_circle2(Entity &a, Entity &b, float dt) {
	return circle_circle_ccd_check(a, b, dt);
}

/*
 * This is the basic collsion detection function.  It uses pythagoras to
 * check the distance between to circles. It does not account for time. 
 * If two circles are moving fast enough, they will jump through each other.
 * This is known as the tunneling problem.
 */
bool circle_circle(Entity &a, Entity &b, float dt) {
	auto d = a.position1 - b.position1;
	return ((d.x * d.x) + (d.y * d.y)) < (4.0f * radius * radius);
}




/*
 * Used by threadpool to handle multithreaded collisions
 */
void collisionCallback(std::vector<Entity> &entities, float dt, int i, int j) {
	auto &a = entities.at(i);
	auto &b = entities.at(j);
	if (circle_circle(a, b, dt)) {
		auto t = circle_circle_ccd_time(a, b, dt);
		a.position1 = a.position0 + t * (a.position1 - a.position0);
		b.position1 = b.position0 + t * (b.position1 - b.position0);

		// simple reflection, does not perform transfer of momentum
		// normal vector from a to b
		auto n = b.position1 - a.position1;
		n /= std::sqrt(n.x * n.x + n.y * n.y);
		reflect(a.velocity, n);

		// normal vector from b to a
		n = a.position1 - b.position1;
		n /= std::sqrt(n.x * n.x + n.y * n.y);
		reflect(b.velocity, n);
	}
}







void World::collisions(float dt) {
	// collision detection will walls
	for (auto &entity : entities) {
		pool.add(std::bind(updateEntityWall, std::ref(entity)));
	}
	pool.wait();

	// update saplist
	for (auto &entity : entities) {
		pool.add(std::bind(updateSapList, std::ref(entity)));
	}
	pool.wait();

	// perform collision detection between balls
	for (int i = 0; i < entities.size(); i++) {
		pool.add(std::bind(querySapList, std::ref(entities[i]), std::ref(entities), dt));
	}
	pool.wait();
}

int main() {
	// random number generator, 0 seeded
	std::mt19937 mt(0);
	std::uniform_real_distribution<float> dist(1.0, 5.0);
	std::uniform_int_distribution<int> dist2(0, 255);
	std::uniform_int_distribution<int> dist3(100, 500);

	// initialize entities
	World world;
	world.entities.resize(NUM_OBJECTS);

	int sapID = 0;
	for (auto &entity : world.entities) {
		entity.position1.x = dist3(mt);
		entity.position1.y = dist3(mt);
		entity.velocity.x = dist(mt);
		entity.velocity.y = dist(mt);
		entity.color = sf::Color(dist2(mt), dist2(mt), dist2(mt));

		// add entity to Sap List
		entity.sapID = list.add(sapID++, entity.position1.x - radius, radius * 2.0f);
	}

	// create window
	sf::RenderWindow window(sf::VideoMode(800, 600), "Collision Test");
	window.setFramerateLimit(60);

	// initialize threadpool
	pool.start();

	// main loop
	for (int i = 0; i < 300; i++) {
		sf::Event event;

		// check for window exit
		while (window.pollEvent(event))
			if (event.type == sf::Event::Closed)
				window.close();

		// step physics engine
		world.step(1.f);

		// render
		window.clear(sf::Color::Black);
		for (auto &entity : world.entities) {
			// check for collisions on the screen
			auto &position = entity.position1;
			auto rshift = sf::Vector2f(radius, radius);
			auto &color = entity.color;

			sf::CircleShape shape(radius);
			shape.setPosition(position - rshift);
			shape.setFillColor(color);
			window.draw(shape);
		}
		window.display();
	}

	// stop threadpool
	pool.stop();

	std::cout << "TIME: " << elapsed_seconds.count() << std::endl;
	return 0;
}
