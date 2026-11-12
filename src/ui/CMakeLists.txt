add_library(rippl_ui STATIC
    choice_screen.c
    cover_flow.c
    fade.c
    slide.c
    header.c
    icons.c
    label.c
    list_navigation.c
    list_view.c
    marquee.c
    menu_screen.c
    message_screen.c
    navigation.c
    rippl_logo.c
    rippl_logo_data.c
    screen.c
    screen_stack.c
    status_bar.c
    theme.c
    toast.c
)
target_include_directories(rippl_ui PUBLIC include)
target_link_libraries(rippl_ui
    PUBLIC rippl_core rippl_gfx
    PRIVATE rippl_options rippl_compact rippl_lto
)
