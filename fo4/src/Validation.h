#pragma once

// Milestone 5: FSMP's physics XML validator (its XSD check, src/Validator/Validators/hdtXSDValidator.cpp, against
// FSMP-Validator's hdtSMP64.xsd, MIT, shipped as F4SE\Plugins\FO4SMP\hdtSMP64.xsd). Each physics XML FO4SMP builds
// from is checked once per session and its problems logged, so a broken XML says why in FO4SMP.log. FSMP's NIF
// checks (Skyrim NIF layouts) and its console report aren't ported.

#include <string>

namespace FO4SMP::Validation
{
	// a_path: the file read (relative to Data); a_label: how it's named in the log (the NIF's path).
	void CheckOnce(const std::string& a_path, const std::string& a_label);
}
