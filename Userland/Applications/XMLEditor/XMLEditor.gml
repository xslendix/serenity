@GUI::Widget {
    fill_with_background_color: true
    layout: @GUI::VerticalBoxLayout {
    }

    @GUI::TextEditor {
        name: "input_data"
    }

    @GUI::Button {
        name: "parse_data"
        text: "parse input"
    }

    @GUI::Label {
        name: "parsed_data"
    }

}
