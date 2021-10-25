serenity_component(
    BuggieRSS
    TARGETS BuggieRSS
)

compile_gml(BuggieRSSWindow.gml BuggieRSSWindowGML.h buggierss_window_gml)

set(SOURCES
    BuggieRSSWindowGML.h
    RSSModel.cpp
    RSSModel.h
    BuggieRSSWidget.cpp
    BuggieRSSWidget.h
    main.cpp
)

serenity_app(BuggieRSS ICON app-buggierss)
target_link_libraries(BuggieRSS LibGUI LibWeb)
