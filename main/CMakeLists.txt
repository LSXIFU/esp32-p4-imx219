idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES common imx219 cam_pipeline comm
                                esp_driver_isp esp_driver_i2c esp_driver_cam
                                esp_driver_gpio esp_mm
                                esp_hw_support esp_timer
                       )
