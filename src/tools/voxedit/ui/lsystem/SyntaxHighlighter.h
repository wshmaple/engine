#pragma once

#include "ui/TurboBadger.h"

namespace voxedit {

class Syntaxighlighter : public tb::TBSyntaxHighlighter {
public:
	void OnBeforePaintFragment(const tb::TBPaintProps *props, tb::TBTextFragment *fragment) override {
	}

	void OnAfterPaintFragment(const tb::TBPaintProps *props, tb::TBTextFragment *fragment) override {
	}
};

}
