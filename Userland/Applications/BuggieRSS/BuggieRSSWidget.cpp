/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BuggieRSSWidget.h"
#include "RSSModel.h"
#include <AK/Vector.h>
#include <Applications/BuggieRSS/BuggieRSSWindowGML.h>
#include <LibCore/DateTime.h>
#include <LibGUI/TableView.h>
#include <LibWeb/OutOfProcessWebView.h>

BuggieRSSWidget::BuggieRSSWidget()
{
    load_from_gml(buggierss_window_gml);
    Vector<ArticleEntry> vec;
    vec.append({ false, "How to eat steak", Core::DateTime::now() });
    m_rss_model = RSSModel::create(vec);

    m_web_view = *find_descendant_of_type_named<Web::OutOfProcessWebView>("web_view");
    m_table_view = *find_descendant_of_type_named<GUI::TableView>("table_view");

    auto file = Core::File::construct("/res/html/misc/blink.html");
    if (!file->open(Core::OpenMode::ReadOnly))
        return;

    auto html = file->read_all();
    m_web_view->load_html(html, URL::create_with_file_protocol("/res/html/misc/blink.html"));

    m_table_view->set_model(m_rss_model);
    m_table_view->set_column_headers_visible(true);
}

BuggieRSSWidget::~BuggieRSSWidget()
{
}
