#pragma once

#include "hdtSkinnedMesh/hdtSkinnedMeshWorld.h"

namespace hdt
{
	struct GlobalConfig;
}

namespace FO4SMP
{
	// The Fallout 4 physics world: FSMP's SkinnedMeshWorld plus the settings the game layer owns.
	// Milestone 1 creates it, applies configs.json to it and steps it once while empty; nothing is
	// added to it and nothing steps it per frame yet.
	class PhysicsWorld : public hdt::SkinnedMeshWorld
	{
	public:
		// Created on first call (Bullet's task scheduler starts then). Never destroyed: the world
		// outlives the game's own shutdown, so it is left to the process exit.
		static PhysicsWorld* Get();

		// Copies the solver and wind settings into the world (the part of FSMP's applyConfig that
		// touches SkyrimPhysicsWorld).
		void Apply(const hdt::GlobalConfig& a_config);

		// Steps the empty world through one 1/60 s frame. Returns the time it took in milliseconds.
		double SmokeStep();

		// Steps the world through a_seconds of game time, in fixed ticks (FSMP's min-fps), at most
		// maxSubSteps of them. Returns the time it took in milliseconds.
		double Step(float a_seconds);

		[[nodiscard]] float TimeTick() const noexcept { return _timeTick; }
		[[nodiscard]] int MaxSubSteps() const noexcept { return _maxSubSteps; }
		[[nodiscard]] int NumIterations() { return getSolverInfo().m_numIterations; }
		[[nodiscard]] float Erp() { return getSolverInfo().m_erp; }

	private:
		PhysicsWorld() = default;

		float _timeTick = 1.0f / 60.0f;
		int _maxSubSteps = 4;
	};
}
