/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/Forward.h"
#include <AK/Vector.h>
#include <AK/Weakable.h>
#include <LibCore/DateTime.h>
#include <LibGUI/Model.h>
#include <LibGUI/Variant.h>

struct ArticleEntry {
    bool read = false;
    String title;
    Core::DateTime time;
};

class RSSModel
    : public GUI::Model
    , public Weakable<RSSModel> {

public:
    enum Column {
        ReadIcon = 0,
        Title,
        PublishDate,
        __Count
    };

    static NonnullRefPtr<RSSModel> create(Vector<ArticleEntry> entries)
    {
        return adopt_ref(*new RSSModel(entries));
    }
    virtual ~RSSModel() override;

    virtual String column_name(int column_index) const override;
    virtual int row_count(GUI::ModelIndex const& = GUI::ModelIndex()) const override;
    virtual int column_count(const GUI::ModelIndex& = GUI::ModelIndex()) const override { return Column::__Count; }
    virtual GUI::Variant data(GUI::ModelIndex const&, GUI::ModelRole = GUI::ModelRole::Display) const override;

private:
    RSSModel(Vector<ArticleEntry> entries);

    Vector<ArticleEntry> m_entries;
};
