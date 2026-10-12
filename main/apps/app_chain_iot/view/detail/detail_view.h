/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

namespace chain_iot::view {

class DetailView final {
public:
    enum class Phase : std::uint8_t {
        Hidden,
        Opening,
        Visible,
        Closing,
    };

    void reset();
    void open();
    void close();
    bool completeTransition();

    bool active() const;
    bool transitioning() const;
    bool visible() const;
    Phase phase() const;

private:
    Phase _phase = Phase::Hidden;
};

}  // namespace chain_iot::view
