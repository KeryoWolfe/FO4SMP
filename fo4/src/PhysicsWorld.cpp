#include "PhysicsWorld.h"

#include "GlobalConfig.h"
#include "game/Fo4Settings.h"
#include "game/Fo4System.h"

namespace FO4SMP
{
	PhysicsWorld* PhysicsWorld::Get()
	{
		static PhysicsWorld* world = new PhysicsWorld();
		return world;
	}

	void PhysicsWorld::Apply(const hdt::GlobalConfig& a_config)
	{
		// As FSMP's applyConfig (config.cpp) does for SkyrimPhysicsWorld.
		_timeTick = 1.0f / static_cast<float>(std::max(1, a_config.minFps));
		_maxSubSteps = std::max(1, a_config.maxSubSteps);
		getSolverInfo().m_numIterations = a_config.numIterations;
		getSolverInfo().m_erp = a_config.erp;
		m_enableWind = a_config.windEnabled;
		hdt::fo4::ApplySettings(a_config);
	}

	double PhysicsWorld::SmokeStep()
	{
		const auto start = std::chrono::steady_clock::now();
		stepSimulation(1.0f / 60.0f, _maxSubSteps, _timeTick);
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	double PhysicsWorld::Step(float a_seconds)
	{
		// FSMP's SkyrimPhysicsWorld::doUpdate / doUpdate2ndStep, on this thread (FSMP runs the second
		// half on a task; here the whole frame is done before returning so the writes land this frame).
		_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);

		_accumulatedInterval += a_seconds;
		_averageInterval += (a_seconds - _averageInterval) * 0.125f;

		if (m_systems.empty()) {
			_accumulatedInterval = 0.0f;
			return 0.0;
		}

		const float tick = std::min(_averageInterval, _timeTick);
		if (_accumulatedInterval * 2.0f <= tick) {
			return 0.0;
		}
		const float remainingTimeStep = std::min(_accumulatedInterval, tick * static_cast<float>(_maxSubSteps));

		const auto start = std::chrono::steady_clock::now();

		readTransform(remainingTimeStep);
		UpdateActiveState();
		const auto offset = ApplyTranslationOffset();
		stepSimulation(remainingTimeStep, 0, tick);
		RestoreTranslationOffset(offset);
		_accumulatedInterval = 0.0f;
		writeTransform();

		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	void PhysicsWorld::AddSystem(hdt::Fo4System* a_system)
	{
		a_system->m_initialized = false;
		addSkinnedMeshSystem(a_system);
	}

	void PhysicsWorld::RemoveSystem(hdt::Fo4System* a_system)
	{
		removeSkinnedMeshSystem(a_system);
	}

	// FSMP: moves the simulation near the origin before stepping, for float precision far from it.
	btVector3 PhysicsWorld::ApplyTranslationOffset()
	{
		btVector3 center;
		center.setZero();
		int count = 0;
		for (int i = 0; i < m_collisionObjects.size(); ++i) {
			if (auto* rig = btRigidBody::upcast(m_collisionObjects[i])) {
				center += rig->getWorldTransform().getOrigin();
				++count;
			}
		}
		if (count > 0) {
			center /= static_cast<btScalar>(count);
			for (int i = 0; i < m_collisionObjects.size(); ++i) {
				if (auto* rig = btRigidBody::upcast(m_collisionObjects[i])) {
					rig->getWorldTransform().getOrigin() -= center;
				}
			}
		}
		return center;
	}

	void PhysicsWorld::RestoreTranslationOffset(const btVector3& a_offset)
	{
		for (int i = 0; i < m_collisionObjects.size(); ++i) {
			if (auto* rig = btRigidBody::upcast(m_collisionObjects[i])) {
				rig->getWorldTransform().getOrigin() += a_offset;
			}
		}
	}

	// FSMP: per skeleton, a body with a disable tag is off when another body carries that tag; among bodies
	// sharing a disable tag, only the highest priority stays on.
	void PhysicsWorld::UpdateActiveState()
	{
		struct Group
		{
			std::unordered_set<RE::BSFixedString> tags;
			std::unordered_map<RE::BSFixedString, std::vector<hdt::Fo4Body*>> list;
		};

		std::unordered_map<RE::NiNode*, Group> maps;

		RE::BSFixedString invalidString;
		for (auto& i : m_systems) {
			auto* system = static_cast<hdt::Fo4System*>(i.get());
			auto& map = maps[system->m_skeleton.get()];
			for (auto& j : system->meshes()) {
				auto* shape = static_cast<hdt::Fo4Body*>(j.get());
				if (!shape) {
					continue;
				}
				if (shape->m_disableTag == invalidString) {
					for (auto& k : shape->m_tags) {
						map.tags.insert(k);
					}
				} else {
					map.list[shape->m_disableTag].push_back(shape);
				}
			}
		}

		for (auto& i : maps) {
			for (auto& j : i.second.list) {
				if (i.second.tags.contains(j.first)) {
					for (auto* k : j.second) {
						k->m_disabled = true;
					}
				} else if (!j.second.empty()) {
					std::sort(j.second.begin(), j.second.end(), [](hdt::Fo4Body* a, hdt::Fo4Body* b) {
						if (a->m_disablePriority != b->m_disablePriority) {
							return a->m_disablePriority > b->m_disablePriority;
						}
						return a < b;
					});
					for (auto* k : j.second) {
						k->m_disabled = true;
					}
					j.second[0]->m_disabled = false;
				}
			}
		}
	}
}
