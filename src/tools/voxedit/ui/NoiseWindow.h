/**
 * @file
 */

#pragma once

#include "ui/turbobadger/Window.h"

class EditorScene;

namespace voxedit {

class NoiseWindow : public ui::turbobadger::Window {
private:
	using Super = ui::turbobadger::Window;
	EditorScene* _scene;

	tb::TBEditField* _octaves;
	tb::TBEditField* _frequency;
	tb::TBEditField* _lacunarity;
	tb::TBEditField* _gain;
public:
	NoiseWindow(ui::turbobadger::Window* window, EditorScene* scene);

	bool OnEvent(const tb::TBWidgetEvent &ev) override;
};

}
