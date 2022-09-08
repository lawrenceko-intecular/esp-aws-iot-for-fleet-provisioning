set(COMPONENT_SRCS 
	"app_main.c"
	"fleet_prov_demo_core_mqtt.c"
	)

set(COMPONENT_ADD_INCLUDEDIRS
	"."
	"${PROJECT_DIR}/../../libraries/common/logging/"
	"${PROJECT_DIR}/../../platform/posix/"
	"${PROJECT_DIR}/../../platform/posix/posix/transport/include/"
	"${PROJECT_DIR}/../../libraries/corePKCS11/corePKCS11/source/dependency/3rdparty/mbedtls_utils/"
	"${PROJECT_DIR}/../../libraries/corePKCS11/corePKCS11/source/dependency/3rdparty/mbedtls/programs/util/"
	)

idf_component_register(SRCS "${COMPONENT_SRCS}"
					   INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
					  )