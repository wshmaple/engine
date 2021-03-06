#include "TestMeshApp.h"
#include "core/command/Command.h"
#include "core/GameConfig.h"
#include "video/ScopedPolygonMode.h"
#include "video/ScopedViewPort.h"
#include "io/Filesystem.h"

#define MaxDepthBufferUniformName "u_cascades"

TestMeshApp::TestMeshApp(const io::FilesystemPtr& filesystem, const core::EventBusPtr& eventBus, const core::TimeProviderPtr& timeProvider) :
		Super(filesystem, eventBus, timeProvider), _colorShader(shader::ColorShader::getInstance()) {
	setCameraMotion(true);
	setRenderPlane(false);
	_fogColor = core::Color::LightBlue;
}

core::AppState TestMeshApp::onConstruct() {
	core::AppState state = Super::onConstruct();

	core::Command::registerCommand("loadmesh", [this] (const core::CmdArgs& args) {
		if (args.empty()) {
			Log::error("Usage: loadmesh <meshname>");
			return;
		}
		const std::string& mesh = args[0];
		Log::info("Trying to load mesh %s", mesh.c_str());
		const video::MeshPtr& meshPtr = _meshPool.getMesh(mesh);
		if (meshPtr->isLoading()) {
			_mesh->shutdown();
			_mesh = meshPtr;
		} else {
			Log::warn("Failed to load mesh: %s", mesh.c_str());
		}
	}).setHelp("Load a mesh from the pool. The name is without extension and the file must be in the mesh/ dir.");

	_meshName = core::Var::get("mesh", "chr_skelett2_bake");
	_animationIndex = core::Var::get("animation", "0");
	_shadowMap = core::Var::getSafe(cfg::ClientShadowMap);
	_shadowMapShow = core::Var::get(cfg::ClientShadowMapShow, "false");
	_debugShadow = core::Var::getSafe(cfg::ClientDebugShadow);
	_debugShadowCascade = core::Var::getSafe(cfg::ClientDebugShadowMapCascade);

	return state;
}

core::AppState TestMeshApp::onInit() {
	core::AppState state = Super::onInit();
	if (state != core::AppState::Running) {
		return state;
	}

	_shadowRangeZ = _camera.farPlane() * 3.0f;

	if (!_shadow.init()) {
		Log::error("Failed to init shadow object");
		return core::AppState::InitFailure;
	}

	_camera.setPosition(glm::vec3(0.0f, 10.0f, 150.0f));
	_camera.setOmega(glm::vec3(0.0f, 0.1f, 0.0f));
	_camera.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
	_camera.setTargetDistance(50.0f);
	_camera.setRotationType(video::CameraRotationType::Target);

	if (!_shadowMapShader.setup()) {
		Log::error("Failed to init shadowmap shader");
		return core::AppState::InitFailure;
	}
	if (!_shadowMapRenderShader.setup()) {
		Log::error("Failed to init shadowmap debug shader");
		return core::AppState::InitFailure;
	}
	if (!_meshShader.setup()) {
		Log::error("Failed to init mesh shader");
		return core::AppState::InitFailure;
	}
	if (!_colorShader.setup()) {
		Log::error("Failed to init color shader");
		return core::AppState::InitFailure;
	}

	_meshPool.init();

	const std::string& mesh = _meshName->strVal();
	_mesh = _meshPool.getMesh(mesh);
	if (!_mesh->isLoading()) {
		Log::error("Failed to load the mesh %s", mesh.c_str());
		return core::AppState::InitFailure;
	}
	const int maxDepthBuffers = _meshShader.getUniformArraySize(MaxDepthBufferUniformName);
	const glm::ivec2 smSize(core::Var::getSafe(cfg::ClientShadowMapSize)->intVal());
	if (!_depthBuffer.init(smSize, video::DepthBufferMode::DEPTH_CMP, maxDepthBuffers)) {
		Log::error("Failed to init the depthbuffer");
		return core::AppState::InitFailure;
	}

	const glm::ivec2& fullscreenQuadIndices = _shadowMapDebugBuffer.createFullscreenTexturedQuad(true);
	video::Attribute attributePos;
	attributePos.bufferIndex = fullscreenQuadIndices.x;
	attributePos.index = _shadowMapRenderShader.getLocationPos();
	attributePos.size = _shadowMapRenderShader.getComponentsPos();
	_shadowMapDebugBuffer.addAttribute(attributePos);

	video::Attribute attributeTexcoord;
	attributeTexcoord.bufferIndex = fullscreenQuadIndices.y;
	attributeTexcoord.index = _shadowMapRenderShader.getLocationTexcoord();
	attributeTexcoord.size = _shadowMapRenderShader.getComponentsTexcoord();
	_shadowMapDebugBuffer.addAttribute(attributeTexcoord);

	return state;
}

void TestMeshApp::onRenderUI() {
	Super::onRenderUI();
	ImGui::Separator();
	ImGui::Text("Mesh %s", _mesh->filename().c_str());
	ImGui::Text("%i vertices", (int)_mesh->vertices().size());
	ImGui::Text("%i indices", (int)_mesh->indices().size());
	ImGui::Text("%i bones", (int)_mesh->bones());
	ImGui::Text("%i animations", (int)_mesh->animations());
	ImGui::Separator();
	if (ImGui::Checkbox("Render axis", &_renderAxis)) {
		setRenderAxis(_renderAxis);
	}
	ImGui::Checkbox("Render normals", &_renderNormals);
	if (ImGui::Checkbox("Render plane", &_renderPlane)) {
		setRenderPlane(_renderPlane);
	}
	if (ImGui::Checkbox("Camera motion", &_cameraMotion)) {
		setCameraMotion(_cameraMotion);
	}
	if (ImGui::InputFloat("Camera speed", &_cameraSpeed, 0.02f, 0.1f)) {
		setCameraSpeed(_cameraSpeed);
	}
	ImGui::InputFloat("Shadow bias", &_shadowBias, 0.001f, 0.01f);
	ImGui::InputFloat("Shadow bias slope", &_shadowBiasSlope, 0.01f, 0.1f);
	ImGui::InputFloat("Shadow range", &_shadowRangeZ, 0.01f, 0.1f);
	ImGui::InputFloat("Fog range", &_fogRange, 0.01f, 0.1f);
	ImGui::InputVarFloat("Rotation speed", _rotationSpeed, 0.01f, 0.1f);
	if (_mesh->animations() > 1 && ImGui::InputVarInt("Animation index", _animationIndex, 1, 1)) {
		_animationIndex->setVal(_mesh->currentAnimation());
	}
	ImGui::CheckboxVar("Shadow map", _shadowMap);
	ImGui::CheckboxVar("Show shadow map", _shadowMapShow);
	ImGui::CheckboxVar("Shadow map debug", _debugShadow);
	ImGui::CheckboxVar("Show shadow cascades", _debugShadowCascade);
	ImGui::InputVarString("Mesh", _meshName);
	if (_meshName->isDirty()) {
		const video::MeshPtr& meshPtr = _meshPool.getMesh(_meshName->strVal());
		if (meshPtr->isLoading()) {
			_mesh->shutdown();
			_mesh = meshPtr;
		} else {
			Log::warn("Failed to load mesh: %s", _meshName->strVal().c_str());
		}
		_meshName->markClean();
	}
	ImGui::InputFloat3("Position", glm::value_ptr(_position));
	ImGui::ColorEdit3("Diffuse color", glm::value_ptr(_diffuseColor));
	ImGui::ColorEdit3("Ambient color", glm::value_ptr(_ambientColor));
	ImGui::ColorEdit4("Fog color", glm::value_ptr(_fogColor));
	ImGui::ColorEdit4("Clear color", glm::value_ptr(_clearColor));
}

void TestMeshApp::doRender() {
	const uint8_t animationIndex = _animationIndex->intVal();
	const uint64_t timeInSeconds = lifetimeInSeconds();

	video::enable(video::State::DepthTest);
	video::depthFunc(video::CompareFunc::LessEqual);
	video::enable(video::State::CullFace);
	video::enable(video::State::DepthMask);

	_model = glm::translate(glm::mat4(1.0f), _position);
	const int maxDepthBuffers = _meshShader.getUniformArraySize(MaxDepthBufferUniformName);
	_shadow.calculateShadowData(_camera, true, maxDepthBuffers, _depthBuffer.dimension());
	const std::vector<glm::mat4>& cascades = _shadow.cascades();
	const std::vector<float>& distances = _shadow.distances();

	{
		video::disable(video::State::Blend);
		// put shadow acne into the dark
		video::cullFace(video::Face::Front);
		const glm::vec2 offset(_shadowBiasSlope, (_shadowBias / _shadowRangeZ) * (1 << 24));
		const video::ScopedPolygonMode scopedPolygonMode(video::PolygonMode::Solid, offset);

		_depthBuffer.bind();
		video::ScopedShader scoped(_shadowMapShader);
		if (_mesh->initMesh(_shadowMapShader, timeInSeconds, animationIndex)) {
			_shadowMapShader.recordUsedUniforms(true);
			_shadowMapShader.clearUsedUniforms();
			_shadowMapShader.setModel(_model);
			for (int i = 0; i < maxDepthBuffers; ++i) {
				_depthBuffer.bindTexture(i);
				_shadowMapShader.setLightviewprojection(cascades[i]);
				if (_renderPlane) {
					renderPlane();
				}
				_mesh->render();
			}
		} else {
			_shadowMapShader.recordUsedUniforms(false);
		}
		_depthBuffer.unbind();
		video::cullFace(video::Face::Back);
		video::enable(video::State::Blend);
	}

	bool meshInitialized = false;
	{
		video::clearColor(_clearColor);
		video::clear(video::ClearFlag::Color | video::ClearFlag::Depth);

		if (_renderPlane) {
			renderPlane();
		}

		video::ScopedShader scoped(_meshShader);
		_meshShader.clearUsedUniforms();
		_meshShader.recordUsedUniforms(true);
		meshInitialized = _mesh->initMesh(_meshShader, timeInSeconds, animationIndex);
		if (meshInitialized) {
			_meshShader.setViewprojection(_camera.viewProjectionMatrix());
			_meshShader.setFogrange(_fogRange);
			_meshShader.setViewdistance(_camera.farPlane());
			_meshShader.setModel(_model);
			_meshShader.setTexture(video::TextureUnit::Zero);
			_meshShader.setDiffuseColor(_diffuseColor);
			_meshShader.setAmbientColor(_ambientColor);
			_meshShader.setShadowmap(video::TextureUnit::One);
			_meshShader.setDepthsize(glm::vec2(_depthBuffer.dimension()));
			_meshShader.setFogcolor(_fogColor);
			_meshShader.setCascades(cascades);
			_meshShader.setDistances(distances);
			_meshShader.setLightdir(_shadow.sunDirection());
			video::bindTexture(video::TextureUnit::One, _depthBuffer);
			const video::ScopedPolygonMode scopedPolygonMode(_camera.polygonMode());
			_mesh->render();
		} else {
			_meshShader.recordUsedUniforms(false);
		}
	}
	if (meshInitialized && _renderNormals) {
		video::ScopedShader scoped(_colorShader);
		_colorShader.recordUsedUniforms(true);
		_colorShader.clearUsedUniforms();
		_colorShader.setViewprojection(_camera.viewProjectionMatrix());
		_colorShader.setModel(_model);
		_mesh->renderNormals(_colorShader);
	}

	if (_shadowMapShow->boolVal()) {
		const int width = _camera.width();
		const int height = _camera.height();

		// activate shader
		video::ScopedShader scopedShader(_shadowMapRenderShader);
		_shadowMapRenderShader.recordUsedUniforms(true);
		_shadowMapRenderShader.clearUsedUniforms();
		_shadowMapRenderShader.setShadowmap(video::TextureUnit::Zero);
		_shadowMapRenderShader.setFar(_camera.farPlane());
		_shadowMapRenderShader.setNear(_camera.nearPlane());

		// bind buffers
		core_assert_always(_shadowMapDebugBuffer.bind());

		// configure shadow map texture
		video::bindTexture(video::TextureUnit::Zero, _depthBuffer);
		if (_depthBuffer.depthCompare()) {
			video::disableDepthCompareTexture(video::TextureUnit::Zero, _depthBuffer.textureType(), _depthBuffer.texture());
		}

		// render shadow maps
		for (int i = 0; i < maxDepthBuffers; ++i) {
			const int halfWidth = (int) (width / 4.0f);
			const int halfHeight = (int) (height / 4.0f);
			video::ScopedViewPort scopedViewport(i * halfWidth, 0, halfWidth, halfHeight);
			_shadowMapRenderShader.setCascade(i);
			video::drawArrays(video::Primitive::Triangles, _shadowMapDebugBuffer.elements(0));
		}

		// restore texture
		if (_depthBuffer.depthCompare()) {
			video::setupDepthCompareTexture(video::TextureUnit::Zero, _depthBuffer.textureType(), _depthBuffer.texture());
		}

		// unbind buffer
		_shadowMapDebugBuffer.unbind();
	}
}

void TestMeshApp::renderPlane() {
	_plane.render(_camera);
}

core::AppState TestMeshApp::onCleanup() {
	_shadowMapDebugBuffer.shutdown();
	_shadowMapRenderShader.shutdown();
	_depthBuffer.shutdown();
	_meshShader.shutdown();
	_colorShader.shutdown();
	_shadowMapShader.shutdown();
	_mesh->shutdown();
	_meshPool.shutdown();
	return Super::onCleanup();
}
