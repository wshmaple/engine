/**
 * @file
 */

#include "Mesh.h"
#include "Renderer.h"
#include "core/Common.h"
#include "core/Color.h"
#include "core/Array.h"
#include "core/Log.h"
#include "core/GLM.h"
#include "video/ScopedLineWidth.h"
#include "video/Types.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace video {

namespace {
const aiVector3D VECZERO(0.0f, 0.0f, 0.0f);
const aiColor4D COLOR_BLACK(0.0f, 0.0f, 0.0f, 0.0f);
static inline core::Vertex convertVertex(const aiVector3D& p, const aiVector3D& n, const aiVector3D& t, const aiColor4D& c) {
	return core::Vertex(glm::vec3(p.x, p.y, p.z), glm::vec3(n.x, n.y, n.z), glm::vec2(t.x, t.y), glm::vec4(c.r, c.g, c.b, c.a));
}

static inline glm::vec3 toVec3(const aiVector3D& vector) {
	return glm::vec3(vector.x, vector.y, vector.z);
}

static inline glm::quat toQuat(const aiQuaternion& quat) {
	return glm::quat(quat.w, quat.x, quat.y, quat.z);
}

static inline glm::mat4 toMat4(const aiMatrix4x4& m) {
	// assimp matrices are row major, but glm wants them to be column major
	return glm::transpose(glm::mat4(m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2, m.c3, m.c4, m.d1, m.d2, m.d3, m.d4));
}

}

struct MeshNormals {
	struct AttributeData {
		glm::vec4 vertex;
		glm::vec3 color;
	};
	std::vector<AttributeData> data;

	inline void reserve(size_t amount) {
		data.reserve(amount);
	}
};

Mesh::Mesh() :
		io::IOResource(), _importer(new Assimp::Importer()) {
}

Mesh::~Mesh() {
	shutdown();
	delete _importer;
}

void Mesh::shutdown() {
	_importer->FreeScene();
	_textures.clear();
	_images.clear();
	_meshData.clear();
	_vertexBuffer.shutdown();
	_vertexBufferNormals.shutdown();
	_vertexBufferNormalsIndex = -1;
	_vertexBufferIndex = -1;

	_vertices.clear();
	_indices.clear();
	_boneInfo.clear();
	_boneMapping.clear();
	_globalInverseTransform = glm::mat4(1.0f);
	_numBones = 0;

	_readyToInit = false;
}

bool Mesh::loadMesh(const std::string& filename) {
	if (filename.empty()) {
		Log::error("Failed to load mesh: No filename given");
		return false;
	}
#if 0
	// TODO: implement custom io handler to support meshes that are split over several files (like obj)
	class MeshIOSystem : public Assimp::IOSystem {
	};
	MeshIOSystem iosystem;
	_importer->SetIOHandler(&iosystem);
#endif
	_filename = filename;
	_scene = _importer->ReadFile(filename.c_str(), aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_FindDegenerates);
	if (_scene == nullptr) {
		_state = io::IOSTATE_FAILED;
		Log::error("Error parsing '%s': '%s'", filename.c_str(), _importer->GetErrorString());
		return false;
	}
	if (_scene->mRootNode == nullptr) {
		Log::error("Scene doesn't have a root node'%s': '%s'", filename.c_str(), _importer->GetErrorString());
		return false;
	}
	for (uint32_t i = 0; i < _scene->mNumAnimations; ++i) {
		const aiAnimation* animation = _scene->mAnimations[i];
		Log::debug("Animation %i: %s", i, animation->mName.C_Str());
	}
	if (_scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE) {
		Log::warn("Scene incomplete '%s': '%s'", filename.c_str(), _importer->GetErrorString());
	}

	_globalInverseTransform = glm::inverse(toMat4(_scene->mRootNode->mTransformation));

	_meshData.resize(_scene->mNumMeshes);

	uint32_t numIndices = 0u;
	uint32_t numVertices = 0u;

	for (uint32_t i = 0; i < _meshData.size(); ++i) {
		const aiMesh* mesh = _scene->mMeshes[i];
		RenderMeshData& meshData = _meshData[i];
		meshData.materialIndex = mesh->mMaterialIndex;
		meshData.noOfIndices = mesh->mNumFaces * 3;
		meshData.baseVertex = numVertices;
		meshData.baseIndex = numIndices;

		numVertices += mesh->mNumVertices;
		numIndices += meshData.noOfIndices;
	}

	_vertices.reserve(numVertices);
	_indices.reserve(numIndices);
	_boneInfo.clear();

	_aabbMins = glm::vec3(std::numeric_limits<float>::max());
	_aabbMaxs = glm::vec3(std::numeric_limits<float>::min());

	for (uint32_t i = 0u; i < _meshData.size(); ++i) {
		const aiMesh* mesh = _scene->mMeshes[i];

		if (mesh->HasVertexColors(0)) {
			Log::debug("Mesh has vertex color");
		}

		for (uint32_t fi = 0; fi < mesh->mNumFaces; ++fi) {
			const aiFace& face = mesh->mFaces[fi];
			core_assert(face.mNumIndices == 3);
			_indices.push_back(face.mIndices[0]);
			_indices.push_back(face.mIndices[1]);
			_indices.push_back(face.mIndices[2]);
		}

		for (uint32_t vi = 0; vi < mesh->mNumVertices; ++vi) {
			const aiVector3D& pos = mesh->mVertices[vi];
			const aiVector3D& normal = mesh->mNormals[vi];
			const aiVector3D& texCoord = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][vi] : VECZERO;
			const aiColor4D& color = mesh->HasVertexColors(0) ? mesh->mColors[0][vi] : COLOR_BLACK;

			if (pos.x <= _aabbMins.x) {
				_aabbMins.x = pos.x;
			}
			if (pos.x > _aabbMaxs.x) {
				_aabbMaxs.x = pos.x;
			}
			if (pos.y <= _aabbMins.y) {
				_aabbMins.y = pos.y;
			}
			if (pos.y > _aabbMaxs.y) {
				_aabbMaxs.y = pos.y;
			}
			if (pos.z <= _aabbMins.z) {
				_aabbMins.z = pos.z;
			}
			if (pos.z > _aabbMaxs.z) {
				_aabbMaxs.z = pos.z;
			}

			_vertices.emplace_back(convertVertex(pos, normal, texCoord, color));
		}
		loadBones(i, mesh);
	}

	loadTextureImages(_scene, filename);
	_readyToInit = true;
	Log::info("Loaded mesh %s with %i vertices and %i indices", filename.c_str(), (int)_vertices.size(), (int)_indices.size());
	return true;
}

void Mesh::setupBufferAttributes(Shader& shader) {
	_vertexBuffer.clearAttributes();

	const int posLocation = shader.checkAttributeLocation("a_pos");
	if (posLocation != -1) {
		video::Attribute attribPos;
		attribPos.bufferIndex = _vertexBufferIndex;
		attribPos.index = posLocation;
		attribPos.stride = sizeof(core::Vertex);
		attribPos.size = shader.getAttributeComponents(attribPos.index);
		attribPos.type = video::mapType<decltype(core::Vertex::_pos)::value_type>();
		attribPos.offset = offsetof(core::Vertex, _pos);
		core_assert_always(_vertexBuffer.addAttribute(attribPos));
	}

	const int texcoordsLocation = shader.checkAttributeLocation("a_texcoords");
	if (texcoordsLocation != -1) {
		video::Attribute attribTexCoord;
		attribTexCoord.bufferIndex = _vertexBufferIndex;
		attribTexCoord.index = texcoordsLocation;
		attribTexCoord.stride = sizeof(core::Vertex);
		attribTexCoord.size = shader.getAttributeComponents(attribTexCoord.index);
		attribTexCoord.type = video::mapType<decltype(core::Vertex::_texcoords)::value_type>();
		attribTexCoord.offset = offsetof(core::Vertex, _texcoords);
		core_assert_always(_vertexBuffer.addAttribute(attribTexCoord));
	}

	const int colorLocation = shader.checkAttributeLocation("a_color");
	if (colorLocation != -1) {
		video::Attribute attribColor;
		attribColor.bufferIndex = _vertexBufferIndex;
		attribColor.index = colorLocation;
		attribColor.stride = sizeof(core::Vertex);
		attribColor.size = shader.getAttributeComponents(attribColor.index);
		attribColor.type = video::mapType<decltype(core::Vertex::_color)::value_type>();
		attribColor.offset = offsetof(core::Vertex, _color);
		core_assert_always(_vertexBuffer.addAttribute(attribColor));
	}

	const int normLocation = shader.checkAttributeLocation("a_norm");
	if (normLocation != -1) {
		video::Attribute attribNorm;
		attribNorm.bufferIndex = _vertexBufferIndex;
		attribNorm.index = normLocation;
		attribNorm.stride = sizeof(core::Vertex);
		attribNorm.size = shader.getAttributeComponents(attribNorm.index);
		attribNorm.type = video::mapType<decltype(core::Vertex::_norm)::value_type>();
		attribNorm.offset = offsetof(core::Vertex, _norm);
		core_assert_always(_vertexBuffer.addAttribute(attribNorm));
	}

	const int boneIdsLocation = shader.checkAttributeLocation("a_boneids");
	if (boneIdsLocation != -1) {
		video::Attribute attribBoneIds;
		attribBoneIds.bufferIndex = _vertexBufferIndex;
		attribBoneIds.index = boneIdsLocation;
		attribBoneIds.stride = sizeof(core::Vertex);
		attribBoneIds.size = shader.getAttributeComponents(attribBoneIds.index);
		attribBoneIds.type = video::mapType<decltype(core::Vertex::_boneIds[0])>();
		attribBoneIds.offset = offsetof(core::Vertex, _boneIds);
		attribBoneIds.typeIsInt = true;
		core_assert_always(_vertexBuffer.addAttribute(attribBoneIds));
	}

	const int boneweightsLocation = shader.checkAttributeLocation("a_boneweights");
	if (boneweightsLocation != -1) {
		video::Attribute attribBoneWeights;
		attribBoneWeights.bufferIndex = _vertexBufferIndex;
		attribBoneWeights.index = boneweightsLocation;
		attribBoneWeights.stride = sizeof(core::Vertex);
		attribBoneWeights.size = shader.getAttributeComponents(attribBoneWeights.index);
		attribBoneWeights.type = video::mapType<decltype(core::Vertex::_boneWeights[0])>();
		attribBoneWeights.offset = offsetof(core::Vertex, _boneWeights);
		core_assert_always(_vertexBuffer.addAttribute(attribBoneWeights));
	}
}

void Mesh::setupNormalBufferAttributes(Shader& shader) {
	_vertexBufferNormals.clearAttributes();

	video::Attribute attribPos;
	attribPos.bufferIndex = _vertexBufferNormalsIndex;
	attribPos.index = shader.enableVertexAttributeArray("a_pos");
	attribPos.stride = sizeof(MeshNormals::AttributeData);
	attribPos.size = shader.getAttributeComponents(attribPos.index);
	attribPos.type = video::mapType<decltype(MeshNormals::AttributeData::vertex)::value_type>();
	attribPos.offset = offsetof(MeshNormals::AttributeData, vertex);
	_vertexBufferNormals.addAttribute(attribPos);

	video::Attribute attribColor;
	attribColor.bufferIndex = _vertexBufferNormalsIndex;
	attribColor.index = shader.enableVertexAttributeArray("a_color");
	attribColor.stride = sizeof(MeshNormals::AttributeData);
	attribColor.size = shader.getAttributeComponents(attribColor.index);
	attribColor.type = video::mapType<decltype(MeshNormals::AttributeData::color)::value_type>();
	attribColor.offset = offsetof(MeshNormals::AttributeData, color);
	_vertexBufferNormals.addAttribute(attribColor);
}

bool Mesh::initMesh(Shader& shader, float timeInSeconds, uint8_t animationIndex) {
	if (_state != io::IOSTATE_LOADED) {
		if (!_readyToInit) {
			return false;
		}

		for (const image::ImagePtr& i : _images) {
			if (i && i->isLoading()) {
				return false;
			}
		}

		_textures.clear();
		_textures.resize(_images.size());
		int materialIndex = 0;
		for (const image::ImagePtr& i : _images) {
			if (i && i->isLoaded()) {
				_textures[materialIndex++] = createTextureFromImage(i);
			} else {
				++materialIndex;
			}
		}
		if (materialIndex == 0) {
			_textures.push_back(createWhiteTexture("***empty***"));
		}
		_images.clear();

		_state = io::IOSTATE_LOADED;

		_vertexBufferNormalsIndex = _vertexBufferNormals.create();
		_vertexBufferNormals.setMode(_vertexBufferNormalsIndex, VertexBufferMode::Dynamic);

		_vertexBufferIndex = _vertexBuffer.create(_vertices);
		_vertexBuffer.create(_indices, VertexBufferType::IndexBuffer);
	}

	_timeInSeconds = timeInSeconds;
	_animationIndex = animationIndex;
	if (_animationIndex >= _scene->mNumAnimations) {
		_animationIndex = 0l;
	}

	if (&shader != _lastShader) {
		core_assert(shader.isActive());
		_lastShader = &shader;
		setupBufferAttributes(shader);
	}

	const int size = shader.getUniformArraySize("u_bonetransforms");
	if (size > 0) {
		core_assert_always(size == 100);
		glm::mat4 transforms[100];
		// TODO: use uniform block
		boneTransform(_timeInSeconds, transforms, size, _animationIndex);
		shader.setUniformMatrixv("u_bonetransforms", transforms, size);
	}

	return true;
}

void Mesh::loadBones(uint32_t meshIndex, const aiMesh* mesh) {
	Log::debug("Load %i bones", mesh->mNumBones);
	for (uint32_t i = 0u; i < mesh->mNumBones; ++i) {
		uint32_t boneIndex = 0u;
		const aiBone* aiBone = mesh->mBones[i];
		const std::string boneName(aiBone->mName.data);

		auto iter = _boneMapping.find(boneName);
		if (iter == _boneMapping.end()) {
			// Allocate an index for a new bone
			boneIndex = _numBones;
			++_numBones;
			const BoneInfo bi = { toMat4(aiBone->mOffsetMatrix), glm::mat4(1.0f) };
			_boneInfo.push_back(bi);
			_boneMapping[boneName] = boneIndex;
		} else {
			boneIndex = iter->second;
		}

		Log::debug("Load bone %s with %i weights defined", boneName.c_str(), aiBone->mNumWeights);
		for (uint32_t j = 0u; j < aiBone->mNumWeights; ++j) {
			const aiVertexWeight& weights = aiBone->mWeights[j];
			const uint32_t vertexID = _meshData[meshIndex].baseVertex + weights.mVertexId;
			const float weight = weights.mWeight;
			_vertices[vertexID].addBoneData(boneIndex, weight);
		}
	}
}

uint32_t Mesh::findPosition(float animationTime, const aiNodeAnim* nodeAnim) {
	core_assert(nodeAnim->mNumPositionKeys > 0);
	for (uint32_t i = 0u; i < nodeAnim->mNumPositionKeys - 1; ++i) {
		if (animationTime < (float) nodeAnim->mPositionKeys[i + 1].mTime) {
			return i;
		}
	}

	core_assert_msg(false, "could not find a suitable position for animationTime %f", animationTime);

	return 0u;
}

uint32_t Mesh::findRotation(float animationTime, const aiNodeAnim* nodeAnim) {
	core_assert(nodeAnim->mNumRotationKeys > 0);
	for (uint32_t i = 0u; i < nodeAnim->mNumRotationKeys - 1; ++i) {
		if (animationTime < (float) nodeAnim->mRotationKeys[i + 1].mTime) {
			return i;
		}
	}

	core_assert_msg(false, "could not find a suitable rotation for animationTime %f", animationTime);

	return 0u;
}

uint32_t Mesh::findScaling(float animationTime, const aiNodeAnim* nodeAnim) {
	core_assert(nodeAnim->mNumScalingKeys > 0);
	for (uint32_t i = 0u; i < nodeAnim->mNumScalingKeys - 1; ++i) {
		if (animationTime < (float) nodeAnim->mScalingKeys[i + 1].mTime) {
			return i;
		}
	}

	core_assert_msg(false, "could not find a suitable scaling for animationTime %f", animationTime);

	return 0u;
}

glm::vec3 Mesh::calcInterpolatedPosition(float animationTime, const aiNodeAnim* nodeAnim) {
	if (nodeAnim->mNumPositionKeys == 1) {
		return toVec3(nodeAnim->mPositionKeys[0].mValue);
	}

	const uint32_t positionIndex = findPosition(animationTime, nodeAnim);
	const uint32_t nextPositionIndex = (positionIndex + 1);
	core_assert(nextPositionIndex < nodeAnim->mNumPositionKeys);
	const float deltaTime = (float) (nodeAnim->mPositionKeys[nextPositionIndex].mTime - nodeAnim->mPositionKeys[positionIndex].mTime);
	const float factor = (animationTime - (float) nodeAnim->mPositionKeys[positionIndex].mTime) / deltaTime;
	core_assert(factor >= 0.0f && factor <= 1.0f);
	const glm::vec3& start = toVec3(nodeAnim->mPositionKeys[positionIndex].mValue);
	const glm::vec3& end = toVec3(nodeAnim->mPositionKeys[nextPositionIndex].mValue);
	const glm::vec3& delta = end - start;
	return start + factor * delta;
}

glm::mat4 Mesh::calcInterpolatedRotation(float animationTime, const aiNodeAnim* nodeAnim) {
	// we need at least two values to interpolate...
	if (nodeAnim->mNumRotationKeys == 1) {
		return glm::mat4_cast(toQuat(nodeAnim->mRotationKeys[0].mValue));
	}

	const uint32_t rotationIndex = findRotation(animationTime, nodeAnim);
	const uint32_t nextRotationIndex = (rotationIndex + 1);
	core_assert(nextRotationIndex < nodeAnim->mNumRotationKeys);
	const float deltaTime = (float) (nodeAnim->mRotationKeys[nextRotationIndex].mTime - nodeAnim->mRotationKeys[rotationIndex].mTime);
	const float factor = (animationTime - (float) nodeAnim->mRotationKeys[rotationIndex].mTime) / deltaTime;
	core_assert(factor >= 0.0f && factor <= 1.0f);
	const glm::quat& startRotationQ = toQuat(nodeAnim->mRotationKeys[rotationIndex].mValue);
	const glm::quat& endRotationQ = toQuat(nodeAnim->mRotationKeys[nextRotationIndex].mValue);
	const glm::quat& out = glm::normalize(glm::slerp(startRotationQ, endRotationQ, factor));
	return glm::mat4_cast(out);
}

glm::vec3 Mesh::calcInterpolatedScaling(float animationTime, const aiNodeAnim* nodeAnim) {
	if (nodeAnim->mNumScalingKeys == 1) {
		return toVec3(nodeAnim->mScalingKeys[0].mValue);
	}

	const uint32_t scalingIndex = findScaling(animationTime, nodeAnim);
	const uint32_t nextScalingIndex = (scalingIndex + 1);
	core_assert(nextScalingIndex < nodeAnim->mNumScalingKeys);
	const float deltaTime = (float) (nodeAnim->mScalingKeys[nextScalingIndex].mTime - nodeAnim->mScalingKeys[scalingIndex].mTime);
	const float factor = (animationTime - (float) nodeAnim->mScalingKeys[scalingIndex].mTime) / deltaTime;
	core_assert(factor >= 0.0f && factor <= 1.0f);
	const glm::vec3& start = toVec3(nodeAnim->mScalingKeys[scalingIndex].mValue);
	const glm::vec3& end = toVec3(nodeAnim->mScalingKeys[nextScalingIndex].mValue);
	const glm::vec3 delta = end - start;
	return start + factor * delta;
}

void Mesh::readNodeHierarchy(const aiAnimation* animation, float animationTime, const aiNode* node, const glm::mat4& parentTransform) {
	const std::string nodeName(node->mName.data);
	glm::mat4 nodeTransformation;
	const aiNodeAnim* nodeAnim = findNodeAnim(animation, nodeName);

	if (nodeAnim != nullptr) {
		// Interpolate scaling and generate scaling transformation matrix
		const glm::vec3& scaling = calcInterpolatedScaling(animationTime, nodeAnim);
		const glm::mat4& scalingM = glm::scale(scaling);

		// Interpolate rotation and generate rotation transformation matrix
		const glm::mat4& rotationM = calcInterpolatedRotation(animationTime, nodeAnim);

		// Interpolate translation and generate translation transformation matrix
		const glm::vec3& translation = calcInterpolatedPosition(animationTime, nodeAnim);
		const glm::mat4& translationM = glm::translate(translation);

		// Combine the above transformations
		nodeTransformation = translationM * rotationM * scalingM;
	} else {
		nodeTransformation = toMat4(node->mTransformation);
	}

	const glm::mat4 globalTransformation = parentTransform * nodeTransformation;

	auto iter = _boneMapping.find(nodeName);
	if (iter != _boneMapping.end()) {
		const uint32_t boneIndex = iter->second;
		_boneInfo[boneIndex].finalTransformation = _globalInverseTransform * globalTransformation * _boneInfo[boneIndex].boneOffset;
		Log::trace("update bone transform for node name %s (index: %u)", nodeName.c_str(), boneIndex);
	} else {
		Log::trace("Could not find bone mapping for node name %s", nodeName.c_str());
	}

	for (uint32_t i = 0u; i < node->mNumChildren; ++i) {
		readNodeHierarchy(animation, animationTime, node->mChildren[i], globalTransformation);
	}
}

void Mesh::boneTransform(float timeInSeconds, glm::mat4* transforms, size_t size, uint8_t animationIndex) {
	if (_numBones == 0 || _scene->mNumAnimations == 0) {
		core_assert_always(size >= 1);
		transforms[0] = glm::mat4(1.0f);
		return;
	}
	core_assert_always(animationIndex < _scene->mNumAnimations);
	core_assert_always(_numBones <= size);

	const aiAnimation* animation = _scene->mAnimations[animationIndex];
	const float ticksPerSecond = (float) (animation->mTicksPerSecond != 0 ? animation->mTicksPerSecond : 25.0f);
	const float timeInTicks = timeInSeconds * ticksPerSecond;
	const float animationTime = fmod(timeInTicks, (float) animation->mDuration);

	readNodeHierarchy(animation, animationTime, _scene->mRootNode, glm::mat4(1.0f));

	for (uint32_t i = 0u; i < _numBones; ++i) {
		transforms[i] = _boneInfo[i].finalTransformation;
	}
}

const aiNodeAnim* Mesh::findNodeAnim(const aiAnimation* animation, const std::string& nodeName) {
	for (uint32_t i = 0u; i < animation->mNumChannels; ++i) {
		const aiNodeAnim* nodeAnim = animation->mChannels[i];
		if (!strcmp(nodeAnim->mNodeName.data, nodeName.c_str())) {
			return nodeAnim;
		}
	}

	Log::trace("Could not find animation node for %s", nodeName.c_str());
	return nullptr;
}

void Mesh::loadTextureImages(const aiScene* scene, const std::string& filename) {
	std::string::size_type slashIndex = filename.find_last_of('/');
	std::string dir;

	if (slashIndex == std::string::npos) {
		dir = ".";
	} else if (slashIndex == 0) {
		dir = "/";
	} else {
		dir = filename.substr(0, slashIndex);
	}

	_images.resize(scene->mNumMaterials);
	for (uint32_t i = 0; i < scene->mNumMaterials; i++) {
		const aiMaterial* material = scene->mMaterials[i];
		const aiTextureType texType = aiTextureType_DIFFUSE;
		if (material->GetTextureCount(texType) <= 0) {
			Log::debug("No textures for texture type %i at index %i", texType, i);
			continue;
		}

		aiString path;
		if (material->GetTexture(texType, 0, &path) != AI_SUCCESS) {
			Log::warn("Could not get texture path for material index %i", i);
			continue;
		}
		Log::debug("Texture for texture type %i at index %i: %s", texType, i, path.data);

		std::string p(path.data);

		if (p.substr(0, 2) == ".\\") {
			p = p.substr(2, p.size() - 2);
		}

		const std::string fullPath = dir + "/" + p;
		_images[i] = image::loadImage(fullPath, false);
	}
}

int Mesh::render() {
	if (_state != io::IOSTATE_LOADED) {
		return 0;
	}
	_vertexBuffer.bind();
	int drawCalls = 0;
	for (const RenderMeshData& mesh : _meshData) {
		const uint32_t matIdx = mesh.materialIndex;
		if (matIdx < _textures.size() && _textures[matIdx]) {
			_textures[matIdx]->bind(video::TextureUnit::Zero);
		}
		video::drawElementsBaseVertex<Indices::value_type>(video::Primitive::Triangles, mesh.noOfIndices, mesh.baseIndex, mesh.baseVertex);
		++drawCalls;
	}
	_vertexBuffer.unbind();

	return drawCalls;
}

int Mesh::renderNormals(video::Shader& shader) {
	core_assert(shader.isActive());

	if (_state != io::IOSTATE_LOADED) {
		return 0;
	}
	setupNormalBufferAttributes(shader);

	MeshNormals normalData;
	normalData.reserve(_vertices.size() * 2);
	for (const core::Vertex& v : _vertices) {
		glm::mat4 bonetrans = glm::mat4(1.0f);
		for (int i = 0; i < lengthof(v._boneIds); ++i) {
			const glm::mat4& bmat = _boneInfo[v._boneIds[i]].finalTransformation * v._boneWeights[i];
			bonetrans += bmat;
		}
		const glm::vec4& pos  = bonetrans * glm::vec4(v._pos,  1.0f);
		const glm::vec4& norm = bonetrans * glm::vec4(v._norm, 0.0f);
		const glm::vec4& extended = pos + 2.0f * norm;
		normalData.data.emplace_back(MeshNormals::AttributeData{pos,      glm::vec3(core::Color::Red)});
		normalData.data.emplace_back(MeshNormals::AttributeData{extended, glm::vec3(core::Color::Yellow)});
	}

	_vertexBufferNormals.update(_vertexBufferNormalsIndex, normalData.data);
	_vertexBufferNormals.bind();
	ScopedLineWidth lineWidth(2.0f);
	video::drawArrays(video::Primitive::Lines, normalData.data.size());
	_vertexBufferNormals.unbind();

	return 1;
}

int Mesh::animations() const {
	if (_scene == nullptr) {
		return -1;
	}
	return _scene->mNumAnimations;
}

}
