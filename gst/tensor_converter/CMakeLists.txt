ADD_LIBRARY(tensor_converter SHARED tensor_converter.c)
ADD_LIBRARY(tensor_converterStatic STATIC tensor_converter.c)

TARGET_LINK_LIBRARIES(tensor_converter ${pkgs_LIBRARIES})
TARGET_INCLUDE_DIRECTORIES(tensor_converter PUBLIC ${pkgs_INCLUDE_DIRS})
TARGET_COMPILE_OPTIONS(tensor_converter PUBLIC ${pkgs_CFLAGS_OTHER})
TARGET_LINK_LIBRARIES(tensor_converterStatic ${pkgs_LIBRARIES})
TARGET_INCLUDE_DIRECTORIES(tensor_converterStatic PUBLIC ${pkgs_INCLUDE_DIRS})
TARGET_COMPILE_OPTIONS(tensor_converterStatic PUBLIC ${pkgs_CFLAGS_OTHER})

INSTALL(TARGETS tensor_converter tensor_converterStatic
	RUNTIME DESTINATION ${EXEC_PREFIX}
	LIBRARY DESTINATION ${GST_INSTALL_DIR}
	ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
	)
