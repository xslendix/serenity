/*
 * Copyright (c) 2020, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "XMLParser.h"

#include <AK/Format.h>
#include <AK/String.h>
#include <AK/Vector.h>

namespace XML {

XMLElement::XMLElement(XMLElement* parent)
{
    m_parent = parent;
}

XMLDocument::XMLDocument(String data)
{
    m_data = data;
}

bool XMLDocument::parse()
{
    if (m_data.length() == 0)
        return false;

    m_root_element = new XMLElement(nullptr);

    auto current_element = m_root_element;

    String lex;

    bool in_tag = false;

    // walk through the entire XML string
    for (int i = 0; i < m_data.length(); i++) {
        // If this character is a newline or return do nothing
        if (m_data[i] == '\n' || m_data[i] == '\r')
            continue;

        // If this character is a tag opener, attempt to open a new tag
        if (m_data[i] == '<') {
            // If this character is not in a tag, indicate that we are now in a tag
            if (!in_tag)
                in_tag = true;
            // If this character is within a tag already, fail to open a new tag
            else if (in_tag) {
                dbgln("Cannot open a tag inside a tag.");
                return false;
            }

            // If this character expresses an end tag, epress that we are no longer in a tag, and continue the parse
            if (m_data[i + 1] == '/') {
                i += 2;
                in_tag = false;

                continue;
            }

            current_element = new XMLElement(current_element);
        }
    }

    return true;
}

void XMLElement::add_attribute(XMLAttribute attribute)
{
    m_attributes.push_back(attribute);
}

XMLElement* XMLElement::get_child(int index)
{
    return &m_children[index];
}

XMLElement* XMLElement::get_child(String tag_name)
{
    for (int i = 0; i < m_children.size(); i++)
        if (m_children[i].get_tag_name() == tag_name)
            return &m_children[i];

    dbgln("Could not get child {} for element {}", tag_name, get_tag_name());

    return nullptr;
}

XMLElement* XMLElement::get_child_from_path(String path)
{
    auto element = this;
    for (auto i : path.split('/')) {
        auto child = element->get_child(i);
        if (!child) {
            dbgln("Could not get child {} for element {}", i, element->get_tag_name());
            return nullptr;
        }

        element = child;
    }

    return element;
}

XMLAttribute* XMLElement::get_attribute(String attribute_name)
{
    for (int i = 0; i < m_attributes.size(); i++)
        if (m_attributes[i].get_key() == attribute_name)
            return m_attributes[i];

    return nullptr;
}

XMLAttribute* XMLElement::get_attribute_or_default(String attribute_name, String default_value)
{
    for (int i = 0; i < m_attributes.size(); i++)
        if (m_attributes[i].get_key() == attribute_name)
            return m_attributes[i];

    XMLAttribute* attr = new XMLAttribute();
    attr->set_key(attribute_name);
    attr->set_value(default_value);

    return attr;
}

}
