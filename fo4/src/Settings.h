#pragma once

// Milestone 5: the MCM page (Data\MCM\Config\4SMP). MCM keeps the defaults in MCM\Config\4SMP\settings.ini and
// writes what the player changes to MCM\Settings\4SMP.ini; only that file is read, so values never changed in the
// menu leave configs.json / userConfigs.json in charge.

namespace FO4SMP::Settings
{
	// Each frame (paused too): re-reads MCM\Settings\4SMP.ini when it changed (checked every 2 s).
	void Poll();
}
