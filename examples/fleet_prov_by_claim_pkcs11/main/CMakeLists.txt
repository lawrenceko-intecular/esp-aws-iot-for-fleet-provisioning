set(COMPONENT_SRCS 
	"app_main.c"
	"fleet_prov_by_claim_demo.c"
	"fleet_prov_demo_helpers.c"
	"fleet_provisioning_serializer.c"
	"pkcs11_operations.c"
	)

set(COMPONENT_ADD_INCLUDEDIRS
	"."
	"${PROJECT_DIR}/../../libraries/common/logging/"
	"${PROJECT_DIR}/../../libraries/corePKCS11/corePKCS11/source/dependency/3rdparty/mbedtls_utils/"
	)

idf_component_register(SRCS "${COMPONENT_SRCS}"
					   INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
					  )