idf_component_register(SRCS "web_ui.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES cam esp_event esp_http_server esp_netif esp_wifi servo
                       EMBED_FILES "index.html.gz")
