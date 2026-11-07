#include "PhysicsWorld.h"

#include "GlobalConfig.h"

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
		_maxSubSteps = a_config.maxSubSteps;
		getSolverInfo().m_numIterations = a_config.numIterations;
		getSolverInfo().m_erp = a_config.erp;
		m_enableWind = a_config.windEnabled;
	}

	double PhysicsWorld::SmokeStep()
	{
		const auto start = std::chrono::steady_clock::now();
		stepSimulation(1.0f / 60.0f, _maxSubSteps, _timeTick);
		const auto end = std::chrono::steady_clock::now();
		return std::chrono::duration<double, std::milli>(end - start).count();
	}
}
