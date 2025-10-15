cmake_minimum_required(VERSION 3.10)
project(easy-switcher)

set(CMAKE_CXX_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

include_directories(${LIBEVDEV_INCLUDE_DIRS})
link_directories(${LIBEVDEV_LIBRARY_DIRS})

add_executable(easy-switcher 
    src/main.cpp
    src/Config.cpp
    src/InputReader.cpp
    src/EventLoop.cpp
    src/DeviceManager.cpp
    src/VirtualKeyboard.cpp
    src/Converter.cpp)

target_link_libraries(${PROJECT_NAME} ${LIBEVDEV_LIBRARIES})

install(TARGETS easy-switcher RUNTIME DESTINATION /usr/bin)
install(FILES resources/easy-switcher.service DESTINATION /usr/lib/systemd/system)
install(FILES resources/easy-switcher.1 DESTINATION /usr/share/man/man1)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -E remove /usr/bin/easy-switcher
    COMMAND ${CMAKE_COMMAND} -E remove_directory /etc/easy-switcher
    COMMAND ${CMAKE_COMMAND} -E remove /usr/lib/systemd/system/easy-switcher.service
    COMMAND ${CMAKE_COMMAND} -E remove /usr/share/man/man1/easy-switcher.1
)
