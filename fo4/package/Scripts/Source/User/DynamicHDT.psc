Scriptname DynamicHDT Native Hidden
{FSMP's DynamicHDT functions, provided by FO4SMP (F4SE\Plugins\FO4SMP.dll). Same signatures as FSMP's, plus
HasPhysicsSystem and IsSoftSuspended from FO4 Faster HDT-SMP. Paths are as the NIF names them (Meshes\...\x.xml).}

; Use new_physics_file_path instead of the physics XML the ArmorAddon's model names, on this actor.
; persist: also keep it in the save. Returns true on success.
Bool Function ReloadPhysicsFile(Actor on_actor, ArmorAddon on_ARMA_item, String new_physics_file_path, Bool persist, Bool verbose_log = false) Native Global

; Use new_physics_file_path wherever this actor's physics uses old_physics_file_path.
Bool Function SwapPhysicsFile(Actor on_actor, String old_physics_file_path, String new_physics_file_path, Bool persist, Bool verbose_log = false) Native Global

; The physics XML the ArmorAddon's model uses on this actor (after any override), or "" if none.
String Function QueryCurrentPhysicsFile(Actor on_actor, ArmorAddon on_ARMA_item, Bool verbose_log = false) Native Global

; True when the actor has at least one physics system built.
Bool Function HasPhysicsSystem(Actor actor) Native Global

; True when the actor is kept in its rest shape without physics (beyond the simulated NPCs).
Bool Function IsSoftSuspended(Actor actor) Native Global

; Make the named bones dynamic (on = true) or kinematic/frozen (on = false), in every physics system of the actor.
; Each return entry is that bone's previous state (true = was dynamic; false = kinematic or not found).
Bool[] Function TogglePhysics(Actor actor, String[] boneNames, Bool on) Native Global

; full = true: rebuild the actor's physics from its rest pose. full = false: stop the bones' motion, keep their pose.
Function ResetPhysics(Actor actor, Bool full) Native Global
