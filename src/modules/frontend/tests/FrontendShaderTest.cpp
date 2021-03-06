/**
 * @file
 */

#include "video/tests/AbstractGLTest.h"
#include "FrontendShaders.h"

namespace frontend {

class FrontendShaderTest : public video::AbstractGLTest {
};

TEST_F(FrontendShaderTest, testWorldShader) {
	if (!_supported) {
		return;
	}
	shader::WorldShader shader;
	ASSERT_TRUE(shader.setup());
	shader.shutdown();
}

TEST_F(FrontendShaderTest, testMeshShader) {
	if (!_supported) {
		return;
	}
	shader::MeshShader shader;
	ASSERT_TRUE(shader.setup());
	shader.shutdown();
}

TEST_F(FrontendShaderTest, testColorShader) {
	if (!_supported) {
		return;
	}
	shader::ColorShader shader;
	ASSERT_TRUE(shader.setup());
	shader.shutdown();
}

}
