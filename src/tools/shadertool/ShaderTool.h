/**
 * @file
 */

#pragma once

#include "core/ConsoleApp.h"
#include "Types.h"

/**
 * @brief This tool validates the shaders and generated c++ code for them.
 */
class ShaderTool: public core::ConsoleApp {
private:
	using Super = core::ConsoleApp;
protected:
	ShaderStruct _shaderStruct;
	std::string _namespaceSrc;
	std::string _sourceDirectory;
	std::string _shaderDirectory;
	std::string _shaderTemplateFile;
	std::string _uniformBufferTemplateFile;
	std::string _shaderfile;

	bool parse(const std::string& src, bool vertex);
public:
	ShaderTool(const io::FilesystemPtr& filesystem, const core::EventBusPtr& eventBus, const core::TimeProviderPtr& timeProvider);

	core::AppState onConstruct() override;
	core::AppState onRunning() override;
};
