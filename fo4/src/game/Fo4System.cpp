// Fallout 4 port of FSMP's src/hdtSkyrimSystem.cpp (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".

#include "Fo4System.h"
#include "hdtSkinnedMesh/hdtSkinnedMeshShape.h"
#include "hdtSkinnedMesh/hdtSkinnedMeshWorld.h"  // RESET_PHYSICS

#include "Fo4Skin.h"
#include "Fo4Settings.h"
#include "Patterns/hdtPatternLibrary.h"
#include "Patterns/hdtXmlPatternExpander.h"
#include "XmlReader.h"

// F16C isn't supported on super old processors. AVX2+ (AVX processors can have it, but not guaranteed)
#if defined(__AVX2__) || defined(__AVX512F__)
#	include <immintrin.h>
#else
// float32
// Martin Kallman
//
// Fast half-precision to single-precision floating point conversion
//  - Supports signed zero and denormals-as-zero (DAZ)
//  - Does not support infinities or NaN
//  - Few, partially pipelinable, non-branching instructions,
//  - Core operations ~6 clock cycles on modern x86-64
static void __float32(float* __restrict out, const uint16_t in)
{
	uint32_t t1;
	uint32_t t2;
	uint32_t t3;

	t1 = in & 0x7fff;  // Non-sign bits
	t2 = in & 0x8000;  // Sign bit
	t3 = in & 0x7c00;  // Exponent

	t1 <<= 13;  // Align mantissa on MSB
	t2 <<= 16;  // Shift sign bit into position

	t1 += 0x38000000;  // Adjust bias

	t1 = (t3 == 0 ? 0 : t1);  // Denormals-as-zero

	t1 |= t2;  // Re-insert sign bit

	*((uint32_t*)out) = t1;
};
#endif

namespace hdt
{
	static constexpr float PI = 3.1415926535897932384626433832795f;

	btEmptyShape Fo4SystemCreator::BoneTemplate::emptyShape[1];

	SkinnedMeshBone* Fo4System::findBone(const RE::BSFixedString& name)
	{
		for (auto i : m_bones) {
			if (i->m_name == name) {
				return i.get();
			}
		}

		return nullptr;
	}

	SkinnedMeshBody* Fo4System::findBody(const RE::BSFixedString& name)
	{
		for (auto i : m_meshes) {
			if (i->m_name == name) {
				return i.get();
			}
		}

		return nullptr;
	}

	int Fo4System::findBoneIdx(const RE::BSFixedString& name)
	{
		for (int i = 0; i < m_bones.size(); ++i) {
			if (m_bones[i]->m_name == name) {
				return i;
			}
		}

		return -1;
	}

	Fo4System::Fo4System(RE::NiNode* skeleton) :
		m_skeleton(skeleton), m_oldRoot(nullptr)
	{
		m_oldRoot = m_skeleton;
	}

	float Fo4System::prepareForRead(float timeStep)
	{
		auto newRoot = m_skeleton.get();
		while (newRoot->parent) {
			newRoot = newRoot->parent;
		}

		if (m_oldRoot.get() != newRoot) {
			timeStep = RESET_PHYSICS;
		}

		if (!m_initialized) {
			timeStep = RESET_PHYSICS;
			m_initialized = true;
		}

		if (timeStep <= RESET_PHYSICS) {
			if (!this->block_resetting) {
				updateTransformUpDown(m_skeleton.get(), true);
			}

			m_lastRootRotation = convertNi(m_skeleton->world.rotate);
		} else {
			// Fallout 4: FSMP's player-only exceptions (weapon drawn, first-person camera, its reset counter) aren't
			// ported yet; the rotation clamp / reset applies to every skeleton.
			const auto& settings = fo4::GetSettings();
			btQuaternion newRot = convertNi(m_skeleton->world.rotate);
			btVector3 rotAxis;
			float rotAngle;
			btTransformUtil::calculateDiffAxisAngleQuaternion(m_lastRootRotation, newRot, rotAxis, rotAngle);

			if (settings.clampRotations) {
				float limit = settings.rotationSpeedLimit * timeStep;
				if (rotAngle < -limit || rotAngle > limit) {
					rotAngle = btClamped(rotAngle, -limit, limit);
					btQuaternion clampedRot(rotAxis, rotAngle);
					m_lastRootRotation = clampedRot * m_lastRootRotation;
				} else {
					m_lastRootRotation = newRot;
				}
			} else if (settings.unclampedResets) {
				float limit = settings.unclampedResetAngle * timeStep;
				if (rotAngle < -limit || rotAngle > limit) {
					timeStep = RESET_PHYSICS;
					updateTransformUpDown(m_skeleton.get(), true);
				}
				m_lastRootRotation = newRot;
			} else {
				m_lastRootRotation = newRot;
			}
		}

		m_oldRoot.reset(newRoot);
		return timeStep;
	}

	Fo4SystemCreator::Fo4SystemCreator()
	{
	}

	void Fo4SystemCreator::indexBone(Fo4Bone* bone)
	{
		m_boneIndex.emplace(bone->m_name.data(), bone);
	}

	Fo4Bone* Fo4SystemCreator::findBoneFromIndex(const RE::BSFixedString& name) const
	{
		auto it = m_boneIndex.find(name.data());
		return it != m_boneIndex.end() ? it->second : nullptr;
	}

	RE::NiNode* Fo4SystemCreator::findObjectByName(const RE::BSFixedString& name)
	{
		// TODO check it's not a lurker skeleton
		return findNode(m_skeleton, name);
	}

	Fo4Bone* Fo4SystemCreator::getOrCreateBone(const RE::BSFixedString& name)
	{
		auto bone = findBoneFromIndex(getRenamedBone(name));
		if (bone) {
			return bone;
		}

		logger::warn("Bone {} used before being created, trying to create it with current default values", name.c_str());
		// Create from the renamed name too: the lookup above used it, and under a rename
		// map (skeleton merge / DynamicHDT) the raw name's node typically no longer
		// exists — creating from the raw name would drop the reference or split the
		// bone into two identities. Constraint readers already create from renamed names.
		return createBoneFromNodeName(getRenamedBone(name));
	}

	RE::BSFixedString Fo4SystemCreator::getRenamedBone(const RE::BSFixedString& name)
	{
		auto iter = m_renameMap.find(name);
		if (iter != m_renameMap.end())
			return iter->second;
		return name;
	}

	RE::BSTSmartPointer<Fo4System> Fo4SystemCreator::createOrUpdateSystem(RE::NiNode* skeleton, RE::NiAVObject* model, DefaultBBP::PhysicsFile_t* file, std::unordered_map<RE::BSFixedString, RE::BSFixedString>&& renameMap, Fo4System* old_system)
	{
		auto path = file->first;
		if (path.empty()) {
			return nullptr;
		}

		auto loaded = readAllFile(path.c_str());
		if (loaded.empty()) {
			return nullptr;
		}

		m_renameMap = std::move(renameMap);
		m_skeleton = skeleton;
		m_model = model;
		m_filePath = path;

		// Expand any <pattern> macros before parsing, so the loader sees only ordinary SMP elements.
		// Shared definitions from the global patterns/ folder are visible alongside the file's own.
		// Fail closed on a malformed pattern: no physics for this item (the asset validator reports why).
		PatternOptions patternOpts;
		patternOpts.libraries = &fo4::GetPatternLibraries();
		PatternExpansion expanded = expandPatterns(loaded, patternOpts);
		if (!expanded.ok) {
			// Fail-closed log is the only feedback a mod author gets, so include the diagnostic's line
			// (many carry one; 0 means the failure has no single source line, e.g. a whole-file parse error).
			if (expanded.diags.empty())
				logger::error("[SMP] pattern expansion failed for '{}': unknown error", path.c_str());
			else if (const PatternDiag& d = expanded.diags.front(); d.line > 0)
				logger::error("[SMP] pattern expansion failed for '{}' (line {}): {}", path.c_str(), d.line,
					d.message.c_str());
			else
				logger::error("[SMP] pattern expansion failed for '{}': {}", path.c_str(), d.message.c_str());
			if (!old_system)
				updateTransformUpDown(m_skeleton, true);
			return nullptr;
		}
		loaded = std::move(expanded.xml);

		XMLReader reader((uint8_t*)loaded.data(), loaded.size());
		m_reader = &reader;

		m_reader->nextStartElement();
		if (m_reader->GetName() != "system") {
			if (!old_system) {
				updateTransformUpDown(m_skeleton, true);
			}

			return nullptr;
		}

		auto meshNameMap = file->second;

		m_mesh = RE::make_smart<Fo4System>(skeleton);
		m_boneIndex.clear();

		// FSMP puts the skeleton in Havok's reference pose here so shapes are built from a neutral pose.
		// Not ported yet (needs Fallout 4's Havok skeleton access): shapes are built from the current pose.

		if (!old_system) {
			updateTransformUpDown(m_skeleton, true);
		}

		try {
			while (m_reader->Inspect()) {
				if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
					const auto name = m_reader->GetName();
					if (name == "bone") {
						readOrUpdateBone();
					} else if (name == "bone-default") {
						auto clsname = m_reader->getAttribute("name", "");
						auto extends = m_reader->getAttribute("extends", "");
						auto defaultBoneInfo = getBoneTemplate(extends);
						readBoneTemplate(defaultBoneInfo);
						m_boneTemplates[clsname] = defaultBoneInfo;
					} else if (name == "per-vertex-shape") {
						auto shape = readPerVertexShape(meshNameMap);
						if (shape && shape->m_vertices.size()) {
							m_mesh->m_meshes.push_back(shape);
							shape->m_mesh = m_mesh.get();
						}
					} else if (name == "per-triangle-shape") {
						auto shape = readPerTriangleShape(&meshNameMap);
						if (shape && shape->m_vertices.size()) {
							m_mesh->m_meshes.push_back(shape);
							shape->m_mesh = m_mesh.get();
						}
					} else if (name == "constraint-group") {
						auto constraint = readConstraintGroup();
						if (constraint)
							m_mesh->m_constraintGroups.push_back(constraint);
					} else if (name == "generic-constraint") {
						auto constraint = readGenericConstraint();
						if (constraint)
							m_mesh->m_constraints.push_back(constraint);
					} else if (name == "stiffspring-constraint") {
						auto constraint = readStiffSpringConstraint();
						if (constraint)
							m_mesh->m_constraints.push_back(constraint);
					} else if (name == "conetwist-constraint") {
						auto constraint = readConeTwistConstraint();
						if (constraint)
							m_mesh->m_constraints.push_back(constraint);
					} else if (name == "generic-constraint-default") {
						auto clsname = m_reader->getAttribute("name", "");
						auto extends = m_reader->getAttribute("extends", "");
						auto defaultGenericConstraintTemplate = getGenericConstraintTemplate(extends);
						readGenericConstraintTemplate(defaultGenericConstraintTemplate);
						m_genericConstraintTemplates[clsname] = defaultGenericConstraintTemplate;
					} else if (name == "stiffspring-constraint-default") {
						auto clsname = m_reader->getAttribute("name", "");
						auto extends = m_reader->getAttribute("extends", "");
						auto defaultStiffSpringConstraintTemplate = getStiffSpringConstraintTemplate(extends);
						readStiffSpringConstraintTemplate(defaultStiffSpringConstraintTemplate);
						m_stiffSpringConstraintTemplates[clsname] = defaultStiffSpringConstraintTemplate;
					} else if (name == "conetwist-constraint-default") {
						auto clsname = m_reader->getAttribute("name", "");
						auto extends = m_reader->getAttribute("extends", "");
						auto defaultConeTwistConstraintTemplate = getConeTwistConstraintTemplate(extends);
						readConeTwistConstraintTemplate(defaultConeTwistConstraintTemplate);
						m_coneTwistConstraintTemplates[clsname] = defaultConeTwistConstraintTemplate;
					} else if (name == "shape") {
						auto attrName = m_reader->getAttribute("name");
						auto shape = readShape();
						if (shape) {
							m_shapeRefs.push_back(shape);
							m_shapes.insert(std::make_pair(attrName, shape));
						}
					} else {
						logger::warn("unknown element - {}", name.c_str());
						m_reader->skipCurrentElement();
					}
				} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
					break;
			}
		} catch (const std::string& err) {
			logger::error("xml parse error - {}", err.c_str());
			return nullptr;
		}

		if (m_deferredBuilds.size() > 2) {
			tbb::parallel_for_each(
				m_deferredBuilds.begin(), m_deferredBuilds.end(),
				[](const DeferredBuild& db) {
					if (db.vertexShape)
						db.vertexShape->autoGen();
					db.body->finishBuild();
				});
		} else if (!m_deferredBuilds.empty()) {
			for (const auto& db : m_deferredBuilds) {
				if (db.vertexShape)
					db.vertexShape->autoGen();
				db.body->finishBuild();
			}
		}

		m_deferredBuilds.clear();

		if (m_reader->GetErrorCode() != Xml::ErrorCode::None) {
			logger::error("xml parse error - {}", m_reader->GetErrorMessage());
			return nullptr;
		}

		m_mesh->m_skeleton.reset(m_skeleton);
		m_mesh->m_shapeRefs.swap(m_shapeRefs);
		std::sort(m_mesh->m_bones.begin(), m_mesh->m_bones.end(), [](const auto& a, const auto& b) {
			return static_cast<Fo4Bone*>(a.get())->m_depth < static_cast<Fo4Bone*>(b.get())->m_depth;
		});

		return m_mesh->valid() ? m_mesh : nullptr;
	}

	RE::BSTSmartPointer<ConstraintGroup> Fo4SystemCreator::readConstraintGroup()
	{
		RE::BSTSmartPointer<ConstraintGroup> ret = RE::make_smart<ConstraintGroup>();

		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto name = m_reader->GetName();

				if (name == "generic-constraint") {
					auto constraint = readGenericConstraint();
					if (constraint)
						ret->m_constraints.push_back(constraint);
				} else if (name == "stiffspring-constraint") {
					auto constraint = readStiffSpringConstraint();
					if (constraint)
						ret->m_constraints.push_back(constraint);
				} else if (name == "conetwist-constraint") {
					auto constraint = readConeTwistConstraint();
					if (constraint)
						ret->m_constraints.push_back(constraint);
				} else if (name == "generic-constraint-default") {
					auto clsname = m_reader->getAttribute("name", "");
					auto extends = m_reader->getAttribute("extends", "");
					auto defaultGenericConstraintTemplate = getGenericConstraintTemplate(extends);
					readGenericConstraintTemplate(defaultGenericConstraintTemplate);
					m_genericConstraintTemplates[clsname] = defaultGenericConstraintTemplate;
				} else if (name == "stiffspring-constraint-default") {
					auto clsname = m_reader->getAttribute("name", "");
					auto extends = m_reader->getAttribute("extends", "");
					auto defaultStiffSpringConstraintTemplate = getStiffSpringConstraintTemplate(extends);
					readStiffSpringConstraintTemplate(defaultStiffSpringConstraintTemplate);
					m_stiffSpringConstraintTemplates[clsname] = defaultStiffSpringConstraintTemplate;
				} else if (name == "conetwist-constraint-default") {
					auto clsname = m_reader->getAttribute("name", "");
					auto extends = m_reader->getAttribute("extends", "");
					auto defaultConeTwistConstraintTemplate = getConeTwistConstraintTemplate(extends);
					readConeTwistConstraintTemplate(defaultConeTwistConstraintTemplate);
					m_coneTwistConstraintTemplates[clsname] = defaultConeTwistConstraintTemplate;
				} else {
					logger::warn("unknown element - {}", name.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
				break;
		}
		return ret;
	}

	void Fo4SystemCreator::readBoneTemplate(BoneTemplate& cinfo)
	{
		bool clearCollide = true;
		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto name = m_reader->GetName();
				if (name == "mass")
					cinfo.m_mass = m_reader->readFloat();
				else if (name == "inertia")
					cinfo.m_localInertia = m_reader->readVector3();
				else if (name == "centerOfMassTransform")
					cinfo.m_centerOfMassTransform = m_reader->readTransform();
				else if (name == "linearDamping")
					cinfo.m_linearDamping = m_reader->readFloat();
				else if (name == "angularDamping")
					cinfo.m_angularDamping = m_reader->readFloat();
				else if (name == "friction")
					cinfo.m_friction = m_reader->readFloat();
				else if (name == "rollingFriction")
					cinfo.m_rollingFriction = m_reader->readFloat();
				else if (name == "restitution")
					cinfo.m_restitution = m_reader->readFloat();
				else if (name == "margin-multiplier")
					cinfo.m_marginMultipler = m_reader->readFloat();
				else if (name == "shape") {
					auto shape = readShape();
					if (shape) {
						m_shapeRefs.push_back(shape);
						cinfo.m_collisionShape = shape.get();
					} else
						cinfo.m_collisionShape = BoneTemplate::emptyShape;
				} else if (name == "collision-filter")
					cinfo.m_collisionFilter = m_reader->readInt();
				else if (name == "can-collide-with-bone") {
					if (clearCollide) {
						cinfo.m_canCollideWithBone.clear();
						cinfo.m_noCollideWithBone.clear();
						clearCollide = false;
					}
					cinfo.m_canCollideWithBone.push_back(m_reader->readText());
				} else if (name == "no-collide-with-bone") {
					if (clearCollide) {
						cinfo.m_canCollideWithBone.clear();
						cinfo.m_noCollideWithBone.clear();
						clearCollide = false;
					}
					cinfo.m_noCollideWithBone.push_back(m_reader->readText());
				} else if (name == "gravity-factor") {
					cinfo.m_gravityFactor = btClamped(m_reader->readFloat(), 0.0f, 1.0f);
				} else if (name == "wind-factor") {
					cinfo.m_windFactor = std::max(m_reader->readFloat(), 0.0f);
				} else {
					logger::warn("unknown element - {}", name.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
				break;
		}
	}

	std::shared_ptr<btCollisionShape> Fo4SystemCreator::readShape()
	{
		auto typeStr = m_reader->getAttribute("type");
		if (typeStr == "ref") {
			auto shapeName = m_reader->getAttribute("name");
			m_reader->skipCurrentElement();
			auto iter = m_shapes.find(shapeName);
			if (iter != m_shapes.end())
				return iter->second;
			logger::warn("unknown shape - {}", shapeName.c_str());
			return nullptr;
		}
		if (typeStr == "box") {
			btVector3 halfExtend(0, 0, 0);
			float margin = 0;
			while (m_reader->Inspect()) {
				if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
					auto name = m_reader->GetName();
					if (name == "halfExtend")
						halfExtend = m_reader->readVector3();
					else if (name == "margin")
						margin = m_reader->readFloat();
					else {
						logger::warn("unknown element - {}", name.c_str());
						m_reader->skipCurrentElement();
					}
				} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
					break;
			}
			auto ret = std::make_shared<btBoxShape>(halfExtend);
			ret->setMargin(margin);
			return ret;
		}
		if (typeStr == "sphere") {
			float radius = 0;
			while (m_reader->Inspect()) {
				if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
					auto name = m_reader->GetName();
					if (name == "radius")
						radius = m_reader->readFloat();
					else {
						logger::warn("unknown element - {}", name.c_str());
						m_reader->skipCurrentElement();
					}
				} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
					break;
			}
			return std::make_shared<btSphereShape>(radius);
		}
		if (typeStr == "capsule") {
			float radius = 0;
			float height = 0;
			while (m_reader->Inspect()) {
				if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
					auto name = m_reader->GetName();
					if (name == "radius")
						radius = m_reader->readFloat();
					else if (name == "height")
						height = m_reader->readFloat();
					else {
						logger::warn("unknown element - {}", name.c_str());
						m_reader->skipCurrentElement();
					}
				} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
					break;
			}
			return std::make_shared<btCapsuleShape>(radius, height);
		}
		if (typeStr == "hull") {
			float margin = 0;
			auto ret = std::make_shared<btConvexHullShape>();
			while (m_reader->Inspect()) {
				if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
					auto name = m_reader->GetName();
					if (name == "point")
						ret->addPoint(m_reader->readVector3(), false);
					else if (name == "margin")
						margin = m_reader->readFloat();
					else {
						logger::warn("unknown element - {}", name.c_str());
						m_reader->skipCurrentElement();
					}
				} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
					break;
			}
			ret->recalcLocalAabb();
			return ret->getNumPoints() ? ret : nullptr;
		}
		if (typeStr == "cylinder") {
			float height = 0;
			float radius = 0;
			float margin = 0;
			while (m_reader->Inspect()) {
				if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
					auto name = m_reader->GetName();
					if (name == "height")
						height = m_reader->readFloat();
					else if (name == "radius")
						radius = m_reader->readFloat();
					else if (name == "margin")
						margin = m_reader->readFloat();
					else {
						logger::warn("unknown element - {}", name.c_str());
						m_reader->skipCurrentElement();
					}
				} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
					break;
			}

			if (radius >= 0 && height >= 0) {
				auto ret = std::make_shared<btCylinderShape>(btVector3(radius, height, radius));
				ret->setMargin(margin);
				return ret;
			}
			return nullptr;
		}
		if (typeStr == "compound") {
			auto ret = std::make_shared<btCompoundShape>();
			while (m_reader->Inspect()) {
				if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
					if (m_reader->GetName() == "child") {
						btTransform tr;
						std::shared_ptr<btCollisionShape> shape;

						while (m_reader->Inspect()) {
							if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
								if (m_reader->GetName() == "transform") {
									tr = m_reader->readTransform();
								} else if (m_reader->GetName() == "shape") {
									shape = readShape();
								} else {
									logger::warn("unknown element - {}", m_reader->GetName().c_str());
									m_reader->skipCurrentElement();
								}
							} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
								break;
						}

						if (shape) {
							ret->addChildShape(tr, shape.get());
							m_shapeRefs.push_back(shape);
						}
					}
				} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
					break;
			}
			return ret->getNumChildShapes() ? ret : nullptr;
		}
		logger::warn("Unknown shape type {}", typeStr.c_str());
		return nullptr;
	}

	void Fo4SystemCreator::readOrUpdateBone()
	{
		RE::BSFixedString name = getRenamedBone(m_reader->getAttribute("name"));
		if (findBoneFromIndex(name)) {
			logger::warn("Bone {} already exists, skipped", name.c_str());
			m_reader->skipCurrentElement();
			return;
		}

		RE::BSFixedString cls = m_reader->getAttribute("template", "");
		if (!createBoneFromNodeName(name, cls, true))
			m_reader->skipCurrentElement();
	}

	Fo4Bone* Fo4SystemCreator::createBoneFromNodeName(const RE::BSFixedString& bodyName, const RE::BSFixedString& templateName, const bool readTemplate)
	{
		auto node = findObjectByName(bodyName);
		if (node) {
			logger::info("Found node named {}, creating bone", bodyName.c_str());
			auto boneTemplate = getBoneTemplate(templateName);
			if (readTemplate)
				readBoneTemplate(boneTemplate);
			auto bone = new Fo4Bone(node->name.c_str(), node, this->m_skeleton, boneTemplate);
			bone->m_localToRig = boneTemplate.m_centerOfMassTransform;
			bone->m_rigToLocal = boneTemplate.m_centerOfMassTransform.inverse();
			bone->m_marginMultipler = boneTemplate.m_marginMultipler;
			bone->m_gravityFactor = boneTemplate.m_gravityFactor;
			bone->m_windFactor = boneTemplate.m_windFactor;

			bone->readTransform(RESET_PHYSICS);

			m_mesh->m_bones.push_back(hdt::make_smart(bone));
			indexBone(bone);
			return bone;
		}
		logger::warn("Node named {} doesn't exist, skipped, no bone created", bodyName.c_str());
		return nullptr;
	}

	std::pair<RE::BSTSmartPointer<Fo4Body>, Fo4SystemCreator::VertexOffsetMap> Fo4SystemCreator::generateMeshBody(const std::string name, DefaultBBP::NameSet_t* names)
	{
		// Fallout 4: bones, bind data and vertices come from BSSkin::Instance / BSSkin::BoneData and the
		// renderer's vertex buffer (Fo4Skin.cpp) instead of Skyrim's NiSkinInstance / NiSkinData / NiSkinPartition.
		RE::BSTSmartPointer<Fo4Body> body = RE::make_smart<Fo4Body>();
		body->m_name = name;

		int vertexStart = 0;
		int boneStart = 0;

		VertexOffsetMap vertexOffsetMap;

		for (auto& meshName : *names) {
			auto* triShape = castBSTriShape(findObject(m_model, meshName.c_str()));
			if (!triShape) {
				logger::warn("Shape {} for {} not found in the model", meshName, name);
				continue;
			}

			fo4::SkinnedShape skinned;
			if (const auto error = fo4::ReadSkinnedShape(triShape, skinned, false); !error.empty()) {
				logger::warn("Shape {} for {} skipped: {}", meshName, name, error);
				continue;
			}

			// Fallout 4: each bone's bounding sphere, in bone space, from the vertices it weighs (skinToBone *
			// position). FSMP uses the skin data's sphere, which in Skyrim is in bone space; BSSkin::BoneData's
			// sphere is compared below and logged, and the sphere computed here is used either way, so the
			// collision mid-phase (SkinnedMeshBody::updateBoundingSphereAabb) culls with spheres that hold the
			// bone's vertices.
			std::vector<btVector3> sphereCenter(skinned.bones.size(), btVector3(0, 0, 0));
			std::vector<float> sphereRadius(skinned.bones.size(), 0.0f);
			{
				std::vector<int> count(skinned.bones.size(), 0);
				std::vector<btVector3> boneSpace(skinned.vertices.size() * 4);
				for (std::size_t j = 0; j < skinned.vertices.size(); ++j) {
					const auto& sv = skinned.vertices[j];
					for (int k = 0; k < 4; ++k) {
						if (sv.weights[k] > 0.0f) {
							const auto b = sv.bones[k];
							const auto p = convertNi(skinned.bones[b].skinToBone) * convertNi(sv.position);
							sphereCenter[b] += p;
							++count[b];
						}
					}
				}
				float fileOffset = 0.0f, fileOffsetSkin = 0.0f;
				int compared = 0;
				for (std::size_t b = 0; b < skinned.bones.size(); ++b) {
					if (count[b]) {
						sphereCenter[b] /= static_cast<float>(count[b]);
					}
				}
				for (std::size_t j = 0; j < skinned.vertices.size(); ++j) {
					const auto& sv = skinned.vertices[j];
					for (int k = 0; k < 4; ++k) {
						if (sv.weights[k] > 0.0f) {
							const auto b = sv.bones[k];
							const auto p = convertNi(skinned.bones[b].skinToBone) * convertNi(sv.position);
							sphereRadius[b] = std::max(sphereRadius[b], (p - sphereCenter[b]).length());
						}
					}
				}
				for (std::size_t b = 0; b < skinned.bones.size(); ++b) {
					if (!count[b]) {
						continue;
					}
					const auto file = convertNi(skinned.bones[b].boundCenter);
					fileOffset += (file - sphereCenter[b]).length();
					// The same center if the file's sphere were in skin (mesh) space.
					fileOffsetSkin += (convertNi(skinned.bones[b].skinToBone) * file - sphereCenter[b]).length();
					++compared;
				}
				if (compared) {
					logger::info("Shape {}: bone spheres from vertices; the skin data's centers are {:.1f} units off on average as bone space, {:.1f} as mesh space",
						meshName, fileOffset / compared, fileOffsetSkin / compared);
				}
			}

			// Bones: each skin bone becomes (or reuses) a physics bone.
			std::vector<int> localToBody(skinned.bones.size(), -1);
			for (std::size_t boneIdx = 0; boneIdx < skinned.bones.size(); ++boneIdx) {
				const auto& sb = skinned.bones[boneIdx];
				if (!sb.node) {
					continue;
				}
				const RE::BSFixedString& boneName = sb.node->name;
				auto bone = static_cast<SkinnedMeshBone*>(findBoneFromIndex(boneName));
				if (!bone) {
					auto defaultBoneInfo = getBoneTemplate("");
					auto newBone = new Fo4Bone(boneName, sb.node, this->m_skeleton, defaultBoneInfo);
					m_mesh->m_bones.push_back(hdt::make_smart(newBone));
					indexBone(newBone);
					bone = newBone;
					logger::info("Created bone {} added to body {}, created without default values", boneName.c_str(), name);
				}

				localToBody[boneIdx] = static_cast<int>(body->m_skinnedBones.size());
				body->addBone(bone, convertNi(sb.skinToBone), BoundingSphere(sphereCenter[boneIdx], sphereRadius[boneIdx]));
			}

			body->m_vertices.resize(vertexStart + skinned.vertices.size());
			for (std::size_t j = 0; j < skinned.vertices.size(); ++j) {
				const auto& sv = skinned.vertices[j];
				auto& v = body->m_vertices[j + vertexStart];
				v.m_skinPos = convertNi(sv.position);
				for (int k = 0; k < 4; ++k) {
					const int mapped = localToBody[sv.bones[k]];
					if (mapped < 0 || sv.weights[k] <= 0.0f) {
						v.m_weight[k] = 0.0f;
						v.m_boneIdx[k] = static_cast<U32>(boneStart);
					} else {
						v.m_weight[k] = sv.weights[k];
						v.m_boneIdx[k] = static_cast<U32>(mapped);
					}
				}
			}

			// Fallout 4: weights that don't sum to 1 pull a vertex toward the origin of whatever frame it is
			// skinned in. Count them, and normalize (FSMP's skinning is a plain weighted sum).
			{
				int off = 0, empty = 0;
				float worst = 0.0f;
				for (std::size_t j = 0; j < skinned.vertices.size(); ++j) {
					auto& v = body->m_vertices[j + vertexStart];
					const float sum = v.m_weight[0] + v.m_weight[1] + v.m_weight[2] + v.m_weight[3];
					if (sum <= 1e-6f) {
						++empty;
						continue;
					}
					if (std::abs(sum - 1.0f) > 0.01f) {
						++off;
						worst = std::max(worst, std::abs(sum - 1.0f));
					}
					for (float& w : v.m_weight) {
						w /= sum;
					}
				}
				if (off || empty) {
					logger::info("Shape {}: {} vertices' weights didn't sum to 1 (worst off by {:.3f}; normalized), {} with no weight on a known bone",
						meshName, off, worst, empty);
				}
			}

			logger::info("Shape {} for {}: {} bones, {} vertices ({} precision)", meshName, name, skinned.bones.size(), skinned.vertices.size(), skinned.fullPrecision ? "full" : "half");

			vertexOffsetMap.insert({ meshName, vertexStart });
			boneStart = static_cast<int>(body->m_skinnedBones.size());
			vertexStart = static_cast<int>(body->m_vertices.size());
		}

		if (0 == vertexStart) {
			m_reader->skipCurrentElement();
			return { nullptr, {} };
		}

		for (auto& i : body->m_vertices)
			i.sortWeight();

		return { body, vertexOffsetMap };
	}

	RE::BSTSmartPointer<Fo4Body> Fo4SystemCreator::readPerVertexShape(DefaultBBP::NameMap_t meshNameMap)
	{
		auto name = m_reader->getAttribute("name");
		auto it = meshNameMap.find(name);
		auto names = (it == meshNameMap.end()) ? DefaultBBP::NameSet_t({ name }) : it->second;

		auto body = generateMeshBody(name, &names).first;
		if (!body) {
			return nullptr;
		}

		auto shape = RE::make_smart<PerVertexShape>(body.get());

		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto nodeName = m_reader->GetName();
				if (nodeName == "priority") {
					logger::warn("priority is deprecated and no longer used");
					m_reader->skipCurrentElement();
				} else if (nodeName == "margin") {
					shape->m_shapeProp.margin = m_reader->readFloat();
				} else if (nodeName == "shared") {
					auto str = m_reader->readText();
					if (str == "public") {
						body->m_shared = Fo4Body::SharedType::SHARED_PUBLIC;
					} else if (str == "internal") {
						body->m_shared = Fo4Body::SharedType::SHARED_INTERNAL;
					} else if (str == "external") {
						body->m_shared = Fo4Body::SharedType::SHARED_EXTERNAL;
					} else if (str == "private") {
						body->m_shared = Fo4Body::SharedType::SHARED_PRIVATE;
					} else {
						logger::warn("unknown shared value, use default value \"public\"");
						body->m_shared = Fo4Body::SharedType::SHARED_PUBLIC;
					}
				} else if (nodeName == "tag") {
					body->m_tags.push_back(m_reader->readText());
				} else if (nodeName == "can-collide-with-tag") {
					body->m_canCollideWithTags.insert(m_reader->readText());
				} else if (nodeName == "no-collide-with-tag") {
					body->m_noCollideWithTags.insert(m_reader->readText());
				} else if (nodeName == "can-collide-with-bone") {
					auto bone = getOrCreateBone(m_reader->readText());
					if (bone)
						body->m_canCollideWithBones.insert(bone);
				} else if (nodeName == "no-collide-with-bone") {
					auto bone = getOrCreateBone(m_reader->readText());
					if (bone)
						body->m_noCollideWithBones.insert(bone);
				} else if (nodeName == "weight-threshold") {
					auto boneName = m_reader->getAttribute("bone");
					float wt = m_reader->readFloat();
					for (int i = 0; i < body->m_skinnedBones.size(); ++i) {
						if (body->m_skinnedBones[i].ptr->m_name == getRenamedBone(boneName)) {
							body->m_skinnedBones[i].weightThreshold = wt;
							break;
						}
					}
				} else if (nodeName == "disable-tag") {
					body->m_disableTag = m_reader->readText();
				} else if (nodeName == "disable-priority") {
					body->m_disablePriority = m_reader->readInt();
				} else {
					logger::warn("unknown element - {}", name.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag) {
				break;
			}
		}

		m_deferredBuilds.push_back({ body.get(), shape.get() });

		return body;
	}

	RE::BSTSmartPointer<Fo4Body> Fo4SystemCreator::readPerTriangleShape(DefaultBBP::NameMap_t* meshNameMap)
	{
		auto name = m_reader->getAttribute("name");
		auto it = meshNameMap->find(name);
		auto names = (it == meshNameMap->end()) ? DefaultBBP::NameSet_t({ name }) : it->second;

		auto bodyData = generateMeshBody(name, &names);
		auto body = bodyData.first;
		auto vertexOffsetMap = bodyData.second;
		if (!body)
			return nullptr;

		auto shape = RE::make_smart<PerTriangleShape>(body.get());

		for (auto entry : vertexOffsetMap) {
			auto* g = castBSTriShape(findObject(m_model, entry.first.c_str()));
			if (!g) {
				continue;
			}
			fo4::SkinnedShape skinned;
			if (const auto error = fo4::ReadSkinnedShape(g, skinned, true); !error.empty()) {
				logger::warn("Shape {} has no usable skin data ({}), skipped", entry.first.c_str(), error);
				return nullptr;
			}
			const int offset = entry.second;
			for (std::size_t j = 0; j + 2 < skinned.triangles.size(); j += 3) {
				shape->addTriangle(skinned.triangles[j] + offset, skinned.triangles[j + 1] + offset, skinned.triangles[j + 2] + offset);
			}
		}

		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto nodeName = m_reader->GetName();
				if (nodeName == "priority") {
					logger::warn("priority is deprecated and no longer used");
					m_reader->skipCurrentElement();
				} else if (nodeName == "margin") {
					shape->m_shapeProp.margin = m_reader->readFloat();
				} else if (nodeName == "shared") {
					auto str = m_reader->readText();
					if (str == "public") {
						body->m_shared = Fo4Body::SharedType::SHARED_PUBLIC;
					} else if (str == "internal") {
						body->m_shared = Fo4Body::SharedType::SHARED_INTERNAL;
					} else if (str == "external") {
						body->m_shared = Fo4Body::SharedType::SHARED_EXTERNAL;
					} else if (str == "private") {
						body->m_shared = Fo4Body::SharedType::SHARED_PRIVATE;
					} else {
						logger::warn("unknown shared value, use default value \"public\"");
						body->m_shared = Fo4Body::SharedType::SHARED_PUBLIC;
					}
				} else if (nodeName == "prenetration" || nodeName == "penetration") {
					shape->m_shapeProp.penetration = m_reader->readFloat();
				} else if (nodeName == "tag") {
					body->m_tags.push_back(m_reader->readText());
				} else if (nodeName == "no-collide-with-tag") {
					body->m_noCollideWithTags.insert(m_reader->readText());
				} else if (nodeName == "can-collide-with-tag") {
					body->m_canCollideWithTags.insert(m_reader->readText());
				} else if (nodeName == "can-collide-with-bone") {
					auto bone = getOrCreateBone(m_reader->readText());
					if (bone)
						body->m_canCollideWithBones.insert(bone);
				} else if (nodeName == "no-collide-with-bone") {
					auto bone = getOrCreateBone(m_reader->readText());
					if (bone)
						body->m_noCollideWithBones.insert(bone);
				} else if (nodeName == "weight-threshold") {
					auto boneName = m_reader->getAttribute("bone");
					float wt = m_reader->readFloat();
					for (int i = 0; i < body->m_skinnedBones.size(); ++i) {
						if (body->m_skinnedBones[i].ptr->m_name == getRenamedBone(boneName)) {
							body->m_skinnedBones[i].weightThreshold = wt;
						}
					}
				} else if (nodeName == "disable-tag") {
					body->m_disableTag = m_reader->readText();
				} else if (nodeName == "disable-priority") {
					body->m_disablePriority = m_reader->readInt();
				} else {
					logger::warn("unknown element - {}", nodeName.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag) {
				break;
			}
		}

		m_deferredBuilds.push_back({ body.get(), nullptr });

		return body;
	}

	void Fo4SystemCreator::readFrameLerp(btTransform& tr)
	{
		tr.setIdentity();
		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto name = m_reader->GetName();
				if (name == "translationLerp")
					tr.getOrigin().setX(m_reader->readFloat());
				else if (name == "rotationLerp")
					tr.getOrigin().setY(m_reader->readFloat());
				else {
					logger::warn("unknown element - {}", name.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
				break;
		}
	}

	bool Fo4SystemCreator::parseFrameType(const std::string& name, FrameType& frameType, btTransform& frame)
	{
		if (name == "frameInA") {
			frameType = FrameInA;
			frame = m_reader->readTransform();
		} else if (name == "frameInB") {
			frameType = FrameInB;
			frame = m_reader->readTransform();
		} else if (name == "frameInLerp") {
			frameType = FrameInLerp;
			readFrameLerp(frame);
		} else
			return false;
		return true;
	}

	void Fo4SystemCreator::readGenericConstraintTemplate(GenericConstraintTemplate& dest)
	{
		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto name = m_reader->GetName();
				if (parseFrameType(name, dest.frameType, dest.frame))
					;
				else if (name == "enableLinearSprings")
					dest.enableLinearSprings = m_reader->readBool();
				else if (name == "enableAngularSprings")
					dest.enableAngularSprings = m_reader->readBool();
				else if (name == "linearStiffnessLimited")
					dest.linearStiffnessLimited = m_reader->readBool();
				else if (name == "angularStiffnessLimited")
					dest.angularStiffnessLimited = m_reader->readBool();

				else if (name == "springDampingLimited")
					dest.springDampingLimited = m_reader->readBool();
				else if (name == "linearNonHookeanDamping")
					dest.linearNonHookeanDamping = m_reader->readVector3();
				else if (name == "angularNonHookeanDamping")
					dest.angularNonHookeanDamping = m_reader->readVector3();
				else if (name == "linearNonHookeanStiffness")
					dest.linearNonHookeanStiffness = m_reader->readVector3();
				else if (name == "angularNonHookeanStiffness")
					dest.angularNonHookeanStiffness = m_reader->readVector3();

				else if (name == "linearMotors")
					dest.linearMotors = m_reader->readBool();
				else if (name == "angularMotors")
					dest.angularMotors = m_reader->readBool();
				else if (name == "linearServoMotors")
					dest.linearServoMotors = m_reader->readBool();
				else if (name == "angularServoMotors")
					dest.angularServoMotors = m_reader->readBool();
				else if (name == "linearTargetVelocity")
					dest.linearTargetVelocity = m_reader->readVector3();
				else if (name == "angularTargetVelocity")
					dest.angularTargetVelocity = m_reader->readVector3();
				else if (name == "linearMaxMotorForce")
					dest.linearMaxMotorForce = m_reader->readVector3();
				else if (name == "angularMaxMotorForce")
					dest.angularMaxMotorForce = m_reader->readVector3();

				else if (name == "stopERP")
					dest.stopERP = m_reader->readFloat();
				else if (name == "stopCFM")
					dest.stopCFM = m_reader->readFloat();
				else if (name == "motorERP")
					dest.motorERP = m_reader->readFloat();
				else if (name == "motorCFM")
					dest.motorCFM = m_reader->readFloat();

				else if (name == "useLinearReferenceFrameA")
					dest.useLinearReferenceFrameA = m_reader->readBool();
				else if (name == "linearLowerLimit")
					dest.linearLowerLimit = m_reader->readVector3();
				else if (name == "linearUpperLimit")
					dest.linearUpperLimit = m_reader->readVector3();
				else if (name == "angularLowerLimit")
					dest.angularLowerLimit = m_reader->readVector3();
				else if (name == "angularUpperLimit")
					dest.angularUpperLimit = m_reader->readVector3();
				else if (name == "linearStiffness")
					dest.linearStiffness = m_reader->readVector3();
				else if (name == "angularStiffness")
					dest.angularStiffness = m_reader->readVector3();
				else if (name == "linearDamping")
					dest.linearDamping = m_reader->readVector3();
				else if (name == "angularDamping")
					dest.angularDamping = m_reader->readVector3();
				else if (name == "linearEquilibrium")
					dest.linearEquilibrium = m_reader->readVector3();
				else if (name == "angularEquilibrium")
					dest.angularEquilibrium = m_reader->readVector3();
				else if (name == "linearBounce")
					dest.linearBounce = m_reader->readVector3();
				else if (name == "angularBounce")
					dest.angularBounce = m_reader->readVector3();
				else {
					logger::warn("unknown element - {}", name.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
				break;
		}
	}

	bool Fo4SystemCreator::findBones(const RE::BSFixedString& bodyAName, const RE::BSFixedString& bodyBName, Fo4Bone*& bodyA, Fo4Bone*& bodyB)
	{
		bodyA = findBoneFromIndex(bodyAName);
		bodyB = findBoneFromIndex(bodyBName);

		if (!bodyA) {
			logger::warn("constraint {} <-> {} : bone for bodyA doesn't exist, will try to create it", bodyAName.c_str(), bodyBName.c_str());
			bodyA = createBoneFromNodeName(bodyAName);
			if (!bodyA) {
				m_reader->skipCurrentElement();
				return false;
			}
		}
		if (!bodyB) {
			logger::warn("constraint {} <-> {} : bone for bodyB doesn't exist, will try to create it", bodyAName.c_str(), bodyBName.c_str());
			bodyB = createBoneFromNodeName(bodyBName);
			if (!bodyB) {
				m_reader->skipCurrentElement();
				return false;
			}
		}
		if (bodyA == bodyB) {
			logger::warn("constraint between same object {} <-> {}, skipped", bodyAName.c_str(), bodyBName.c_str());
			m_reader->skipCurrentElement();
			return false;
		}

		if (bodyA->m_rig.isKinematicObject() && bodyB->m_rig.isKinematicObject()) {
			logger::warn("constraint between two kinematic object {} <-> {}, skipped", bodyAName.c_str(), bodyBName.c_str());
			m_reader->skipCurrentElement();
			return false;
		}

		logger::info("OK: constraint between object {} <-> {}", bodyAName.c_str(), bodyBName.c_str());
		return true;
	}

	btQuaternion rotFromAtoB(const btVector3& a, const btVector3& b)
	{
		auto axis = a.cross(b);
		if (axis.fuzzyZero())
			return btQuaternion::getIdentity();
		float sinA = axis.length();
		float cosA = a.dot(b);
		float angle = btAtan2(cosA, sinA);
		return btQuaternion(axis, angle);
	}

	void Fo4SystemCreator::calcFrame(FrameType type, const btTransform& frame, const btQsTransform& trA, const btQsTransform& trB, btTransform& frameA, btTransform& frameB)
	{
		btQsTransform frameInWorld;
		switch (type) {
		case FrameInA:
			frameA = frame;
			frameInWorld = trA * frame;
			frameB = (trB.inverse() * frameInWorld).asTransform();
			break;
		case FrameInB:
			frameB = frame;
			frameInWorld = trB * frameB;
			frameA = (trA.inverse() * frameInWorld).asTransform();
			break;
		case FrameInLerp:
			{
				auto trans = trA.getOrigin().lerp(trB.getOrigin(), frame.getOrigin().x());
				auto rot = trA.getBasis().slerp(trB.getBasis(), frame.getOrigin().y());
				frameInWorld = btQsTransform(rot, trans);
				frameA = (trA.inverse() * frameInWorld).asTransform();
				frameB = (trB.inverse() * frameInWorld).asTransform();
				break;
			}
		case AWithXPointToB:
			{
				btMatrix3x3 matr(trA.getBasis());
				frameInWorld = trA;
				auto old = matr.getColumn(0).normalized();
				auto a2b = (trB.getOrigin() - trA.getOrigin()).normalized();
				auto q = rotFromAtoB(old, a2b);
				frameInWorld.getBasis() *= q;
				frameA = (trA.inverse() * frameInWorld).asTransform();
				frameB = (trB.inverse() * frameInWorld).asTransform();
				break;
			}
		case AWithYPointToB:
			{
				btMatrix3x3 matr(trA.getBasis());
				frameInWorld = trA;
				auto old = matr.getColumn(1).normalized();
				auto a2b = (trB.getOrigin() - trA.getOrigin()).normalized();
				auto q = rotFromAtoB(old, a2b);
				frameInWorld.getBasis() *= q;
				frameA = (trA.inverse() * frameInWorld).asTransform();
				frameB = (trB.inverse() * frameInWorld).asTransform();
				break;
			}
		case AWithZPointToB:
			{
				btMatrix3x3 matr(trA.getBasis());
				frameInWorld = trA;
				auto old = matr.getColumn(2).normalized();
				auto a2b = (trB.getOrigin() - trA.getOrigin()).normalized();
				auto q = rotFromAtoB(old, a2b);
				frameInWorld.getBasis() *= q;
				frameA = (trA.inverse() * frameInWorld).asTransform();
				frameB = (trB.inverse() * frameInWorld).asTransform();
				break;
			}
		}
	}

	RE::BSTSmartPointer<Generic6DofConstraint> Fo4SystemCreator::readGenericConstraint()
	{
		auto bodyAName = getRenamedBone(m_reader->getAttribute("bodyA"));
		auto bodyBName = getRenamedBone(m_reader->getAttribute("bodyB"));
		auto clsname = m_reader->getAttribute("template", "");

		Fo4Bone *bodyA, *bodyB;
		if (!findBones(bodyAName, bodyBName, bodyA, bodyB))
			return nullptr;

		auto trA = bodyA->m_currentTransform;
		auto trB = bodyB->m_currentTransform;

		auto cinfo = getGenericConstraintTemplate(clsname);
		readGenericConstraintTemplate(cinfo);
		btTransform frameA, frameB;
		calcFrame(cinfo.frameType, cinfo.frame, trA, trB, frameA, frameB);

		RE::BSTSmartPointer<Generic6DofConstraint> constraint;
		if (cinfo.useLinearReferenceFrameA) {
			constraint = RE::make_smart<Generic6DofConstraint>(bodyB, bodyA, frameB, frameA);
		} else {
			constraint = RE::make_smart<Generic6DofConstraint>(bodyA, bodyB, frameA, frameB);
		}

		constraint->setLinearLowerLimit(cinfo.linearLowerLimit);
		constraint->setLinearUpperLimit(cinfo.linearUpperLimit);
		constraint->setAngularLowerLimit(cinfo.angularLowerLimit);
		constraint->setAngularUpperLimit(cinfo.angularUpperLimit);
		for (int i = 0; i < 3; ++i) {
			constraint->setStiffness(i, cinfo.linearStiffness[i], cinfo.linearStiffnessLimited);
			constraint->setStiffness(i + 3, cinfo.angularStiffness[i], cinfo.angularStiffnessLimited);
			constraint->setDamping(i, cinfo.linearDamping[i], cinfo.springDampingLimited);
			constraint->setDamping(i + 3, cinfo.angularDamping[i], cinfo.springDampingLimited);

			constraint->setEquilibriumPoint(i, cinfo.linearEquilibrium[i]);
			constraint->setEquilibriumPoint(i + 3, cinfo.angularEquilibrium[i]);

			constraint->setNonHookeanDamping(i, cinfo.linearNonHookeanDamping[i]);
			constraint->setNonHookeanDamping(i + 3, cinfo.angularNonHookeanDamping[i]);
			constraint->setNonHookeanStiffness(i, cinfo.linearNonHookeanStiffness[i]);
			constraint->setNonHookeanStiffness(i + 3, cinfo.angularNonHookeanStiffness[i]);

			constraint->enableSpring(i, cinfo.enableLinearSprings);
			constraint->enableSpring(i + 3, cinfo.enableAngularSprings);

			constraint->enableMotor(i, cinfo.linearMotors);
			constraint->enableMotor(i + 3, cinfo.angularMotors);
			constraint->setServo(i, cinfo.linearServoMotors);
			constraint->setServo(i + 3, cinfo.angularServoMotors);
			// TODO: Test if servo motors go to [0, 0, 0], or whatever equilibrium is.  Provide option to set server motor target.  Hard coded to equilibrium right now.
			constraint->setServoTarget(i, cinfo.linearEquilibrium[i]);
			constraint->setServoTarget(i + 3, cinfo.angularEquilibrium[i]);
			constraint->setTargetVelocity(i, cinfo.linearTargetVelocity[i]);
			constraint->setTargetVelocity(i + 3, cinfo.angularTargetVelocity[i]);
			constraint->setMaxMotorForce(i, cinfo.linearMaxMotorForce[i]);
			constraint->setMaxMotorForce(i + 3, cinfo.angularMaxMotorForce[i]);

			constraint->setParam(BT_CONSTRAINT_ERP, cinfo.motorERP, i);
			constraint->setParam(BT_CONSTRAINT_CFM, cinfo.motorCFM, i);
			constraint->setParam(BT_CONSTRAINT_STOP_ERP, cinfo.stopERP, i);
			constraint->setParam(BT_CONSTRAINT_STOP_CFM, cinfo.stopCFM, i);

			auto rotMotor = constraint->getRotationalLimitMotor(i);
			if (rotMotor) {
				rotMotor->m_motorERP = cinfo.motorERP;
				rotMotor->m_motorCFM = cinfo.motorCFM;
				rotMotor->m_stopERP = cinfo.stopERP;
				rotMotor->m_stopCFM = cinfo.stopCFM;
			}
		}
		constraint->getTranslationalLimitMotor()->m_bounce = cinfo.linearBounce;
		constraint->getRotationalLimitMotor(0)->m_bounce = cinfo.angularBounce[0];
		constraint->getRotationalLimitMotor(1)->m_bounce = cinfo.angularBounce[1];
		constraint->getRotationalLimitMotor(2)->m_bounce = cinfo.angularBounce[2];
		/*constraint->getTranslationalLimitMotor()->m_limitSoftness = 1;
		constraint->getRotationalLimitMotor(0)->m_limitSoftness = 1;
		constraint->getRotationalLimitMotor(1)->m_limitSoftness = 1;
		constraint->getRotationalLimitMotor(2)->m_limitSoftness = 1;*/

		return constraint;
	}

	void Fo4SystemCreator::readStiffSpringConstraintTemplate(StiffSpringConstraintTemplate& dest)
	{
		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto name = m_reader->GetName();
				if (name == "minDistanceFactor")
					dest.minDistanceFactor = std::max(m_reader->readFloat(), 0.0f);
				else if (name == "maxDistanceFactor")
					dest.maxDistanceFactor = std::max(m_reader->readFloat(), 0.0f);
				else if (name == "stiffness")
					dest.stiffness = std::max(m_reader->readFloat(), 0.0f);
				else if (name == "damping")
					dest.damping = std::max(m_reader->readFloat(), 0.0f);
				else if (name == "equilibrium")
					dest.equilibriumFactor = btClamped(m_reader->readFloat(), 0.0f, 1.0f);
				else {
					logger::warn("unknown element - {}", name.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
				break;
		}
	}

	void Fo4SystemCreator::readConeTwistConstraintTemplate(ConeTwistConstraintTemplate& dest)
	{
		while (m_reader->Inspect()) {
			if (m_reader->GetInspected() == XMLReader::Inspected::StartTag) {
				auto name = m_reader->GetName();
				if (parseFrameType(name, dest.frameType, dest.frame))
					;
				else if (name == "swingSpan1" || name == "coneLimit" || name == "limitZ")
					dest.swingSpan1 = std::max(m_reader->readFloat(), 0.f);
				else if (name == "swingSpan2" || name == "planeLimit" || name == "limitY")
					dest.swingSpan2 = std::max(m_reader->readFloat(), 0.f);
				else if (name == "twistSpan" || name == "twistLimit" || name == "limitX")
					dest.twistSpan = std::max(m_reader->readFloat(), 0.f);
				else if (name == "limitSoftness")
					dest.limitSoftness = btClamped(m_reader->readFloat(), 0.f, 1.f);
				else if (name == "biasFactor")
					dest.biasFactor = btClamped(m_reader->readFloat(), 0.f, 1.f);
				else if (name == "relaxationFactor")
					dest.relaxationFactor = btClamped(m_reader->readFloat(), 0.f, 1.f);
				else {
					logger::warn("unknown element - {}", name.c_str());
					m_reader->skipCurrentElement();
				}
			} else if (m_reader->GetInspected() == XMLReader::Inspected::EndTag)
				break;
		}
	}

	const Fo4SystemCreator::BoneTemplate& Fo4SystemCreator::getBoneTemplate(const RE::BSFixedString& name)
	{
		auto iter = m_boneTemplates.find(name);
		if (iter == m_boneTemplates.end())
			return m_boneTemplates[RE::BSFixedString()];
		return iter->second;
	}

	const Fo4SystemCreator::GenericConstraintTemplate& Fo4SystemCreator::getGenericConstraintTemplate(const RE::BSFixedString& name)
	{
		auto iter = m_genericConstraintTemplates.find(name);
		if (iter == m_genericConstraintTemplates.end())
			return m_genericConstraintTemplates[RE::BSFixedString()];
		return iter->second;
	}

	const Fo4SystemCreator::StiffSpringConstraintTemplate& Fo4SystemCreator::getStiffSpringConstraintTemplate(const RE::BSFixedString& name)
	{
		auto iter = m_stiffSpringConstraintTemplates.find(name);
		if (iter == m_stiffSpringConstraintTemplates.end())
			return m_stiffSpringConstraintTemplates[RE::BSFixedString()];
		return iter->second;
	}

	const Fo4SystemCreator::ConeTwistConstraintTemplate& Fo4SystemCreator::getConeTwistConstraintTemplate(const RE::BSFixedString& name)
	{
		auto iter = m_coneTwistConstraintTemplates.find(name);
		if (iter == m_coneTwistConstraintTemplates.end())
			return m_coneTwistConstraintTemplates[RE::BSFixedString()];
		return iter->second;
	}

	RE::BSTSmartPointer<StiffSpringConstraint> Fo4SystemCreator::readStiffSpringConstraint()
	{
		auto bodyAName = getRenamedBone(m_reader->getAttribute("bodyA"));
		auto bodyBName = getRenamedBone(m_reader->getAttribute("bodyB"));
		auto clsname = m_reader->getAttribute("template", "");

		Fo4Bone *bodyA, *bodyB;
		if (!findBones(bodyAName, bodyBName, bodyA, bodyB))
			return nullptr;

		StiffSpringConstraintTemplate cinfo = getStiffSpringConstraintTemplate(clsname);
		readStiffSpringConstraintTemplate(cinfo);

		RE::BSTSmartPointer<StiffSpringConstraint> constraint = RE::make_smart<StiffSpringConstraint>(bodyA, bodyB);
		constraint->m_minDistance *= cinfo.minDistanceFactor;
		constraint->m_maxDistance *= cinfo.maxDistanceFactor;
		constraint->m_stiffness = cinfo.stiffness;
		constraint->m_damping = cinfo.damping;
		constraint->m_equilibriumPoint = constraint->m_minDistance * cinfo.equilibriumFactor + constraint->m_maxDistance * (1 - cinfo.equilibriumFactor);
		return constraint;
	}

	RE::BSTSmartPointer<ConeTwistConstraint> Fo4SystemCreator::readConeTwistConstraint()
	{
		auto bodyAName = getRenamedBone(m_reader->getAttribute("bodyA"));
		auto bodyBName = getRenamedBone(m_reader->getAttribute("bodyB"));
		auto clsname = m_reader->getAttribute("template", "");

		Fo4Bone *bodyA = nullptr, *bodyB = nullptr;
		if (!findBones(bodyAName, bodyBName, bodyA, bodyB)) {
			return nullptr;
		}

		auto trA = bodyA->m_currentTransform;
		auto trB = bodyB->m_currentTransform;

		auto cinfo = getConeTwistConstraintTemplate(clsname);
		readConeTwistConstraintTemplate(cinfo);
		btTransform frameA, frameB;
		calcFrame(cinfo.frameType, cinfo.frame, trA, trB, frameA, frameB);

		RE::BSTSmartPointer<ConeTwistConstraint> constraint = RE::make_smart<ConeTwistConstraint>(bodyA, bodyB, frameA, frameB);
		constraint->setLimit(cinfo.swingSpan1, cinfo.swingSpan2, cinfo.twistSpan, cinfo.limitSoftness, cinfo.biasFactor, cinfo.relaxationFactor);

		return constraint;
	}
}
