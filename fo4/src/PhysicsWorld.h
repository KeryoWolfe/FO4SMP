#pragma once

#include "hdtSkinnedMesh/hdtSkinnedMeshWorld.h"

namespace hdt
{
	struct GlobalConfig;
	class Fo4System;
}

namespace FO4SMP
{
	// The Fallout 4 physics world: FSMP's SkinnedMeshWorld plus the per-frame logic of FSMP's
	// SkyrimPhysicsWorld (tick averaging, substep limit, translation offset, disable tags), run on the
	// game's main thread from the frame hook.
	class PhysicsWorld : public hdt::SkinnedMeshWorld
	{
	public:
		// Created on first call (Bullet's task scheduler starts then). Never destroyed: the world
		// outlives the game's own shutdown, so it is left to the process exit.
		static PhysicsWorld* Get();

		// Copies the solver and wind settings into the world (the part of FSMP's applyConfig that
		// touches SkyrimPhysicsWorld).
		void Apply(const hdt::GlobalConfig& a_config);

		// Steps the world once through 1/60 s (milestone 1's smoke test). Returns milliseconds.
		double SmokeStep();

		// One frame, as FSMP's doUpdate + writeTransform: read bone poses, step, write the simulated bones
		// back. a_seconds is the frame's game time. Returns milliseconds spent (0 when nothing was stepped).
		double Step(float a_seconds);

		void AddSystem(hdt::Fo4System* a_system);
		void RemoveSystem(hdt::Fo4System* a_system);
		[[nodiscard]] std::size_t SystemCount() const noexcept { return m_systems.size(); }

		[[nodiscard]] float TimeTick() const noexcept { return _timeTick; }
		[[nodiscard]] int MaxSubSteps() const noexcept { return _maxSubSteps; }
		[[nodiscard]] int NumIterations() { return getSolverInfo().m_numIterations; }
		[[nodiscard]] float Erp() { return getSolverInfo().m_erp; }

	private:
		PhysicsWorld();

		btVector3 ApplyTranslationOffset();
		void RestoreTranslationOffset(const btVector3& a_offset);
		void UpdateActiveState();

		float _timeTick = 1.0f / 60.0f;
		int _maxSubSteps = 4;
		float _accumulatedInterval = 0.0f;
		float _averageInterval = 1.0f / 60.0f;
	};
}
