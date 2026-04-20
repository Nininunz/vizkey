idf_component_register(
    SRCS
        "vizkey_profiles_nvs.c"
        "vizkey_profiles_fs.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash vizkey_matrix
)
