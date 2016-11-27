#include "PaletteWidget.h"
#include "voxel/MaterialColor.h"

PaletteWidget::PaletteWidget() :
		ui::Widget() {
	SetIsFocusable(true);
}

PaletteWidget::~PaletteWidget() {
}

void PaletteWidget::OnPaint(const PaintProps &paint_props) {
	Super::OnPaint(paint_props);
	const tb::TBRect rect = GetRect();
	const int xAmount = (rect.w + 2 * _padding) / (_width + _padding);
	const int yAmount = (rect.h + 2 * _padding) / (_height + _padding);
	const tb::TBRect renderRect(0, 0, _width, _height);
	const voxel::MaterialColorArray& colors = voxel::getMaterialColors();
	const glm::vec4& borderColor = core::Color::Black;
	const tb::TBColor tbBorderColor(borderColor.r, borderColor.g, borderColor.b);
	const int min = std::enum_value(voxel::VoxelType::Min);
	const int max = std::enum_value(voxel::VoxelType::Max);
	int i = min;
	core_assert(max <= (int)colors.size());
	for (int y = 0; y < yAmount; ++y) {
		for (int x = 0; x < xAmount; ++x) {
			if (i >= max) {
				break;
			}
			const glm::ivec4 color(colors[i] * 255.0f);
			const int transX = x * _padding + x * _width;
			const int transY = y * _padding + y * _height;
			const tb::TBColor tbColor(color.r, color.g, color.b, color.a);
			tb::g_renderer->Translate(transX, transY);
			tb::g_tb_skin->PaintRectFill(renderRect, tbColor);
			tb::g_tb_skin->PaintRect(renderRect, tbBorderColor, 1);
			tb::g_renderer->Translate(-transX, -transY);
			++i;
		}
	}
}

bool PaletteWidget::OnEvent(const tb::TBWidgetEvent &ev) {
	if (ev.type == tb::EVENT_TYPE_POINTER_DOWN) {
		const tb::TBRect rect = GetRect();
		const int max = std::enum_value(voxel::VoxelType::Max);
		const int xAmount = rect.w / (_width + _padding);
		const int col = ev.target_x / (_width + _padding);
		const int row = ev.target_y / (_height + _padding);
		const int index = row * xAmount + col;
		if (index >= max) {
			return false;
		}
		const voxel::MaterialColorArray& colors = voxel::getMaterialColors();
		const glm::ivec4& color = colors[index];
		SetValue(core::Color::GetRGBA(color));
		_voxelType = (voxel::VoxelType)index;
		_dirty = true;
		return true;
	}
	return Super::OnEvent(ev);
}

tb::PreferredSize PaletteWidget::OnCalculatePreferredContentSize(const tb::SizeConstraints &constraints) {
	const voxel::MaterialColorArray& colors = voxel::getMaterialColors();
	const int size = colors.size();
	int maxAmountY = size / _amountX;
	if (size % _amountX) {
		++maxAmountY;
	}
	return tb::PreferredSize(_amountX * _width + (_amountX - 2) * _padding, maxAmountY * _height + std::max(0, maxAmountY - 2) * _padding);
}

void PaletteWidget::OnInflate(const tb::INFLATE_INFO &info) {
	_width = info.node->GetValueInt("width", 20);
	_height = info.node->GetValueInt("height", 20);
	_padding = info.node->GetValueInt("padding", 2);
	_amountX = info.node->GetValueInt("amount-x", 8);
	Super::OnInflate(info);
}

namespace tb {
TB_WIDGET_FACTORY(PaletteWidget, TBValue::TYPE_NULL, WIDGET_Z_TOP) {}
}
