serenity_component(
    XMLEditor
    TARGETS XMLEditor
)

compile_gml(XMLEditor.gml XMLEditorGML.h XML_editor_gml)

set(SOURCES
    XMLEditorGML.h
    main.cpp
)

serenity_app(XMLEditor ICON app-calendar)
target_link_libraries(XMLEditor LibGUI)
