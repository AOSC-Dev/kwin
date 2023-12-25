set(kwinbincompatmarker_SOURCES
    main.cpp
    elfparser.cpp
    elfparser.h
    markerinterface.cpp
    markerinterface.h
)

add_library(kwinbincompatmarker MODULE ${kwinbincompatmarker_SOURCES})
target_link_libraries(kwinbincompatmarker kwin)

install(TARGETS kwinbincompatmarker DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/plugins/)
