/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BuggieRSSWidget.h"
#include <LibConfig/Client.h>
#include <LibGUI/Application.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Window.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    if (pledge("stdio recvfd sendfd rpath unix proc exec", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    auto app = GUI::Application::construct(argc, argv);

    if (unveil("/res", "r") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil("/tmp/portal/webcontent", "rw") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil(nullptr, nullptr) < 0) {
        perror("unveil");
        return 1;
    }

    auto app_icon = GUI::Icon::default_icon("app-buggierss");

    auto window = GUI::Window::construct();
    window->resize(700, 400);
    window->center_on_screen();

    window->set_title("BuggieRSS");
    window->set_minimum_size(480, 250);
    window->set_icon(app_icon.bitmap_for_size(16));
    window->set_main_widget<BuggieRSSWidget>();

    window->show();

    return app->exec();
}
