set(SOURCES
    XMLParser.cpp
)

serenity_lib(LibXML xml)
target_link_libraries(LibXML LibC LibCore)
