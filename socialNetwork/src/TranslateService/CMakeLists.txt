add_executable(
    TranslateService
    TranslateService.cpp
    ${THRIFT_GEN_CPP_DIR}/TranslateService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

target_include_directories(
    TranslateService PRIVATE
    /usr/local/include/jaegertracing
    /opt/ctranslate2/include
)


target_link_libraries(
    TranslateService
    nlohmann_json::nlohmann_json
    ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    Boost::log
    Boost::log_setup
    jaegertracing
    /opt/ctranslate2/lib/libctranslate2.so
)


install(TARGETS TranslateService DESTINATION ./)

