# 查找目录下的所有源文件
# 并将名称保存到 DIR_SRCS 变量
# aux_source_directory(. DIR_SRCS)
set (CLIENT_SRCS client.c)
set (SERVER_SRCS server.c)
set (CLIENT_UINF client_unif.c)

#set (CMAKE_CXX_FLAGS "-Wall -lxml2 -lrdmacm -libverbs -lpthread -lnuma -lrt -lm")

 set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-msse2") 

set (EXECUTABLE_OUTPUT_PATH ${DHMP_SOURCE_DIR}/bin)

# 指定生成目标
add_executable(client ${CLIENT_SRCS})
add_executable(server ${SERVER_SRCS})
add_executable(client_unif ${CLIENT_UINF})


# 添加链接库
target_link_libraries(client_unif dhmp)
target_link_libraries(client_unif xml2)
target_link_libraries(client_unif pthread)
target_link_libraries(client_unif rdmacm)
target_link_libraries(client_unif ibverbs)
target_link_libraries(client_unif numa)
target_link_libraries(client_unif rt)
target_link_libraries(client_unif m)
target_link_libraries(client_unif json-c)


target_link_libraries(client dhmp)
target_link_libraries(client xml2)
target_link_libraries(client pthread)
target_link_libraries(client rdmacm)
target_link_libraries(client ibverbs)
target_link_libraries(client numa)
target_link_libraries(client rt)
target_link_libraries(client m)
target_link_libraries(client json-c)

target_link_libraries(server dhmp)
target_link_libraries(server xml2)
target_link_libraries(server pthread)
target_link_libraries(server rdmacm)
target_link_libraries(server ibverbs)
target_link_libraries(server numa)
target_link_libraries(server rt)
target_link_libraries(server m)
target_link_libraries(server json-c)
