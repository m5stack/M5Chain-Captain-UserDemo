/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "detail_view.h"

namespace chain_iot::view {

void DetailView::reset()
{
    _phase = Phase::Hidden;
}

void DetailView::open()
{
    if (_phase != Phase::Hidden) return;
    _phase = Phase::Opening;
}

void DetailView::close()
{
    if (_phase != Phase::Visible) return;
    _phase = Phase::Closing;
}

bool DetailView::completeTransition()
{
    const Phase previous_phase = _phase;
    if (_phase == Phase::Opening) {
        _phase = Phase::Visible;
    } else if (_phase == Phase::Closing) {
        _phase = Phase::Hidden;
    }
    return previous_phase != _phase;
}

bool DetailView::active() const
{
    return _phase != Phase::Hidden;
}

bool DetailView::transitioning() const
{
    return _phase == Phase::Opening || _phase == Phase::Closing;
}

bool DetailView::visible() const
{
    return _phase == Phase::Visible;
}

DetailView::Phase DetailView::phase() const
{
    return _phase;
}

}  // namespace chain_iot::view
