/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"

#include "horizontal_level_view.h"
#include "vertical_level_view.h"

#include <cmath>

namespace {

constexpr float MinimumAccelMagnitude     = 0.25F;
constexpr float VerticalModeEnterZRatio   = 0.55F;
constexpr float HorizontalModeEnterZRatio = 0.70F;

}  // namespace

namespace view {

ImuBubbleLevelView::~ImuBubbleLevelView()
{
    destroy();
}

void ImuBubbleLevelView::init(const ImuData& initialData)
{
    destroy();
    _show_data_labels = true;
    setMode(LevelMode::Horizontal);
    setImuData(initialData);
    if (_mode == LevelMode::Horizontal) {
        static_cast<HorizontalLevelView*>(_mode_view.get())->startEntryFade();
    }
    renderIfRequested();
}

void ImuBubbleLevelView::destroy()
{
    _mode_view.reset();
    _mode         = LevelMode::Horizontal;
    _has_imu_data = false;
}

void ImuBubbleLevelView::update(std::uint32_t currentTime)
{
    if (_mode_view) {
        _mode_view->update(currentTime);
    }
    renderIfRequested();
}

void ImuBubbleLevelView::setImuData(const ImuData& data)
{
    _imu_data         = data;
    _has_imu_data     = true;
    bool mode_changed = false;

    const float magnitude =
        std::sqrt(data.accelX * data.accelX + data.accelY * data.accelY + data.accelZ * data.accelZ);
    if (magnitude >= MinimumAccelMagnitude) {
        const float z_ratio = std::abs(data.accelZ) / magnitude;
        if (_mode == LevelMode::Horizontal && z_ratio <= VerticalModeEnterZRatio) {
            mode_changed = setMode(LevelMode::Vertical);
        } else if (_mode == LevelMode::Vertical && z_ratio >= HorizontalModeEnterZRatio) {
            mode_changed = setMode(LevelMode::Horizontal);
        }
    }

    if (_mode_view && !mode_changed) {
        _mode_view->setImuData(data);
    }
}

void ImuBubbleLevelView::toggleDataLabels()
{
    _show_data_labels = !_show_data_labels;
    if (_mode_view) {
        _mode_view->setDataLabelsVisible(_show_data_labels);
    }
}

bool ImuBubbleLevelView::setMode(LevelMode mode)
{
    if (_mode_view && _mode == mode) {
        return true;
    }

    LevelSurfaceTransition surface_transition;
    const bool animate_horizontal = _mode_view && _mode == LevelMode::Vertical && mode == LevelMode::Horizontal;
    if (animate_horizontal) {
        surface_transition = static_cast<VerticalLevelView*>(_mode_view.get())->surfaceTransition();
    }

    auto next_view = createModeView(mode);
    if (next_view == nullptr) {
        return false;
    }

    _mode_view = std::move(next_view);
    _mode      = mode;
    _mode_view->init();
    _mode_view->setDataLabelsVisible(_show_data_labels);
    if (_has_imu_data) {
        _mode_view->setImuData(_imu_data);
    }
    if (mode == LevelMode::Vertical) {
        static_cast<VerticalLevelView*>(_mode_view.get())->startEnterTransition();
    } else if (animate_horizontal) {
        static_cast<HorizontalLevelView*>(_mode_view.get())->startVerticalExitTransition(surface_transition);
    }
    return true;
}

void ImuBubbleLevelView::renderIfRequested()
{
    if (_mode_view == nullptr || !_mode_view->renderRequested()) {
        return;
    }

    auto& canvas = GetHAL().gfxCanvas();
    _mode_view->draw(canvas);
    _mode_view->clearRenderRequest();
    GetHAL().presentGfx();
}

std::unique_ptr<LevelModeView> ImuBubbleLevelView::createModeView(LevelMode mode)
{
    switch (mode) {
        case LevelMode::Horizontal:
            return std::make_unique<HorizontalLevelView>();
        case LevelMode::Vertical:
            return std::make_unique<VerticalLevelView>();
    }
    return nullptr;
}

}  // namespace view
