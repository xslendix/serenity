/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "RSSModel.h"
#include <LibGUI/TableView.h>
#include <LibGUI/Widget.h>
#include <LibWeb/OutOfProcessWebView.h>

class BuggieRSSWidget final : public GUI::Widget {
    C_OBJECT(BuggieRSSWidget);

public:
    virtual ~BuggieRSSWidget() override;

private:
    BuggieRSSWidget();

    RefPtr<RSSModel> m_rss_model;
    RefPtr<GUI::TableView> m_table_view;
    RefPtr<Web::OutOfProcessWebView> m_web_view;
};
