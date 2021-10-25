@GUI::Widget {
    fill_with_background_color: true
    layout: @GUI::VerticalBoxLayout {
        spacing: 2
    }

    @GUI::HorizontalSplitter {
        name: "splitter"
        first_resizee_minimum_size: 80

        @GUI::TableView {
            name: "table_view"
        }

        @Web::OutOfProcessWebView {
            name: "web_view"
        }
    }

    @GUI::Statusbar {
        name: "statusbar"

        @GUI::Progressbar {
            name: "progressbar"
            text: "Downloading feed data: "
            visible: false
        }
    }
}
