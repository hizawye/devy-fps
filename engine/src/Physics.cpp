#include "engine/Physics.h"

#include <btBulletDynamicsCommon.h>

namespace devy::engine {

PhysicsWorld::PhysicsWorld() {
  broadphase_ = std::make_unique<btDbvtBroadphase>();
  collision_config_ = std::make_unique<btDefaultCollisionConfiguration>();
  dispatcher_ = std::make_unique<btCollisionDispatcher>(collision_config_.get());
  solver_ = std::make_unique<btSequentialImpulseConstraintSolver>();
  world_ = std::make_unique<btDiscreteDynamicsWorld>(dispatcher_.get(), broadphase_.get(), solver_.get(), collision_config_.get());
  world_->setGravity(btVector3(0.0f, -9.8f, 0.0f));
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::step(float delta_seconds) {
  if (world_) {
    world_->stepSimulation(delta_seconds, 1, delta_seconds);
  }
}

btDiscreteDynamicsWorld* PhysicsWorld::native() {
  return world_.get();
}

} // namespace devy::engine
