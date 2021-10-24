/*
 * Copyright (c) 2019-2020, Ryan Grieb <ryan.m.grieb@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Applications/XMLEditor/XMLEditorGML.h>
#include <LibGUI/TextEditor.h>
#include <LibGUI/Label.h>
#include <LibGUI/Action.h>
#include <LibGUI/ActionGroup.h>
#include <LibGUI/Application.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/Calendar.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Menubar.h>
#include <LibGUI/Toolbar.h>
#include <LibGUI/Window.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    if (pledge("stdio recvfd sendfd rpath unix", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    auto app = GUI::Application::construct(argc, argv);

    if (pledge("stdio recvfd sendfd rpath", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    if (unveil("/res", "r") < 0) {
        perror("unveil");
        return 1;
    }

    unveil(nullptr, nullptr);

    auto window = GUI::Window::construct();
    window->set_title("XMLEditor");
    window->resize(600, 480);
    window->set_minimum_size(171, 141);

    auto& main_widget = window->set_main_widget<GUI::Widget>();
    main_widget.load_from_gml(XML_editor_gml);

    auto input = main_widget.find_descendant_of_type_named<GUI::TextEditor>("input_data");
    auto parse_button = main_widget.find_descendant_of_type_named<GUI::Button>("parse_data");
    auto output = main_widget.find_descendant_of_type_named<GUI::Label>("parsed_data");

    input->set_text(
"<a>\n\
    hello\n\
    <b>\n\
        more text\n\
    </b>\n\
    even more text\n\
</a>\n\
<c>\n\
    more text still\n\
</c>");

    parse_button->on_click = [&](auto) {
        output->set_text(input->text());
        dbgln(input->text());
    };

    /*
    input->on_cursor_change = [&] {
        output->set_text(input->text());
        dbgln(input->text());
    };
    */

    window->show();
    app->exec();
}
