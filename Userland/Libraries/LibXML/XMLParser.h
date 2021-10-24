/*
 * Copyright (c) 2020, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Vector.h>

namespace XML {

enum ReadType {
    FILE,
    STRING
};

class XMLAttribute {
public:
    String get_key() { return m_key; }
    void set_key(String key) { m_key = key; }

    String get_value() { return m_key; }
    void set_value(String value) { m_value = value; }

private:
    String m_key;
    String m_value;
};

class XMLElement {
public:
    XMLElement(XMLElement* parent);

    String get_inner_text() { return m_inner_text; }
    void set_inner_text(String inner_text) { m_inner_text = inner_text; }

    String get_tag_name() { return m_tag_name; }
    void set_tag_name(String tag_name) { m_tag_name = tag_name; }

    XMLElement* get_parent() { return m_parent; }

    void add_attribute(XMLAttribute attribute);

    XMLElement* get_child(int index);
    XMLElement* get_child(String tag_name);
    XMLElement* get_child_from_path(String path);

    XMLAttribute* get_attribute(String attribute_name);
    XMLAttribute* get_attribute_or_default(String attribute_name, String default_value);

private:
    String m_tag_name;
    String m_inner_text;

    XMLElement* m_parent = nullptr;

    Vector<XMLAttribute> m_attributes;
    Vector<XMLElement> m_children;
};

class XMLDocument {
public:
    XMLDocument(String data);

    XMLElement* get_root_element() { return m_root_element; }

    bool parse();

private:
    XMLElement* m_root_element;

    String m_data;
};

}
