/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "RSSModel.h"
#include <AK/Forward.h>
#include <LibCore/DateTime.h>

RSSModel::RSSModel(Vector<ArticleEntry> entries)
    : m_entries(move(entries))
{
}

RSSModel::~RSSModel()
{
}

int RSSModel::row_count(GUI::ModelIndex const&) const
{
    return m_entries.size();
}

String RSSModel::column_name(int column_index) const
{
    switch (column_index) {
    case Column::ReadIcon:
        return "Read";
    case Column::Title:
        return "Title";
    case Column::PublishDate:
        return "Publish date";
    default:
        VERIFY_NOT_REACHED();
    }
}

GUI::Variant RSSModel::data(GUI::ModelIndex const& index, GUI::ModelRole role) const
{
    auto& value = m_entries[index.row()];
    if (role == GUI::ModelRole::Display) {
        if (index.column() == Column::ReadIcon)
            return value.read;
        if (index.column() == Column::Title)
            return value.title;
        if (index.column() == Column::PublishDate)
            return value.time.to_string();
    }
    return {};
}
