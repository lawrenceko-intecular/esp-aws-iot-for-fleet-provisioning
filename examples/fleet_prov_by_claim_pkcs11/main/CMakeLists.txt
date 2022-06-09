set(COMPONENT_SRCS 
	"app_main.c"
	"fleet_prov_by_claim_demo.c"
	)

set(COMPONENT_ADD_INCLUDEDIRS
	"."
	"${PROJECT_DIR}/../../libraries/common/logging/"
	)

idf_component_register(SRCS "${COMPONENT_SRCS}"
					   INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
					  )