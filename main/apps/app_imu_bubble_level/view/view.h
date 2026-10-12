/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "mode_view.h"

#include <memory>

namespace view {

enum class LevelMode {
    Horizontal,
    Vertical,
};

class ImuBubbleLevelView {
public:
    ~ImuBubbleLevelView();

    void init(const ImuData& initialData);
    void destroy();
    void update(std::uint32_t currentTime);
    void setImuData(const ImuData& data);
    void toggleDataLabels();
    bool setMode(LevelMode mode);
    LevelMode getMode() const
    {
        return _mode;
    }

private:
    std::unique_ptr<LevelModeView> createModeView(LevelMode mode);
    void renderIfRequested();

    std::unique_ptr<LevelModeView> _mode_view;
    ImuData _imu_data;
    LevelMode _mode        = LevelMode::Horizontal;
    bool _has_imu_data     = false;
    bool _show_data_labels = true;
};

}  // namespace view
