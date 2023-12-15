add_subdirectory(killer)

if (KWIN_BUILD_X11)
    add_subdirectory(wayland_wrapper)
endif()
