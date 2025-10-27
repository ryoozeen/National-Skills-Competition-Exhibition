# CMake 최소 요구 버전
cmake_minimum_required(VERSION 3.16)

# 프로젝트 이름 및 사용 언어(C++)
project(safety_admin_client LANGUAGES CXX)

# C++17 표준 사용
set(CMAKE_CXX_STANDARD 17)

# Qt 자동 코드 생성 기능 활성화 (moc, uic, rcc)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6 라이브러리 의존성 (Core/Widgets/Network/Multimedia/Sql)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Multimedia MultimediaWidgets Sql)

# 실행 파일 대상 및 소스 파일 목록
add_executable(safety_admin_ui
    main.cpp
    login_window.cpp login_window.h
    admin_window.cpp admin_window.h
    monitoring_page.cpp monitoring_page.h
    alerts_page.cpp alerts_page.h
    attendance_page.cpp attendance_page.h
    robot_page.cpp robot_page.h
    manual_control_page.cpp manual_control_page.h
    camera_viewer_page.cpp camera_viewer_page.h
    settings_page.cpp settings_page.h
    user_editor_dialog.cpp user_editor_dialog.h
    notification.cpp notification.h
    networkclient.cpp networkclient.h
    backgr.qrc
    mjpegview.h mjpegview.cpp
)

# 타겟에 Qt 라이브러리 연결
target_link_libraries(safety_admin_ui PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Sql
)

# ======================= 설정 파일 자동 복사 =======================

# INI 원본 경로 (우선 고정 경로, 없으면 소스 폴더 경로)
set(INI_SOURCE "/home/jin/project/admin_client.ini")
if(NOT EXISTS "${INI_SOURCE}")
  set(INI_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/admin_client.ini")
endif()

# 빌드 완료 후 실행 파일 위치로 INI 파일 복사
add_custom_command(TARGET safety_admin_ui POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${INI_SOURCE}"
          "$<TARGET_FILE_DIR:safety_admin_ui>/admin_client.ini"
  COMMENT "Copying admin_client.ini to run directory"
)
