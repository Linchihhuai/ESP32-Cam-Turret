idf_component_register(SRCS "app.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cam nvs_flash servo web_ui)