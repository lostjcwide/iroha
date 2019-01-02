add_library(spdlog INTERFACE IMPORTED)

set(spdlog_external_project_name gabime_spdlog)

unset(spdlog_INCLUDE_DIR CACHE)
find_path(spdlog_INCLUDE_DIR spdlog/spdlog.h
  PATHS ${EP_PREFIX}/src/${spdlog_external_project_name}/include
  )
mark_as_advanced(spdlog_INCLUDE_DIR)

unset(spdlog_VERSION)
set(spdlog_version_file_path ${spdlog_INCLUDE_DIR}/spdlog/version.h)
if (EXISTS ${spdlog_version_file_path})
  file(READ ${spdlog_version_file_path} spdlog_version_file)
  string(REGEX MATCH "SPDLOG_VER_MAJOR ([0-9]*)" _ ${spdlog_version_file})
  set(ver_major ${CMAKE_MATCH_1})
  string(REGEX MATCH "SPDLOG_VER_MINOR ([0-9]*)" _ ${spdlog_version_file})
  set(ver_minor ${CMAKE_MATCH_1})
  string(REGEX MATCH "SPDLOG_VER_PATCH ([0-9]*)" _ ${spdlog_version_file})
  set(ver_patch ${CMAKE_MATCH_1})
  set(spdlog_VERSION "${ver_major}.${ver_minor}.${ver_patch}")
endif()

if (NOT DEFINED spdlog_VERSION OR spdlog_VERSION VERSION_LESS spdlog_FIND_VERSION)
  message(STATUS "Package 'spdlog' of version ${spdlog_FIND_VERSION} not found. "
          "Will download it from git repo.")

  set(git_url https://github.com/gabime/spdlog.git)
  set(spdlog_VERSION ${spdlog_FIND_VERSION})
  set(git_tag "v${spdlog_VERSION}")
  set_package_properties(spdlog
      PROPERTIES
      URL ${git_url}
      DESCRIPTION "Logging library"
      )

  externalproject_add(${spdlog_external_project_name}
      GIT_REPOSITORY  ${git_url}
      GIT_TAG         ${git_tag}
      GIT_SHALLOW     1
      CONFIGURE_COMMAND "" # remove configure step
      BUILD_COMMAND "" # remove build step
      INSTALL_COMMAND "" # remove install step
      TEST_COMMAND "" # remove test step
      UPDATE_COMMAND "" # remove update step
      )
  externalproject_get_property(${spdlog_external_project_name} source_dir)
  set(spdlog_INCLUDE_DIR ${source_dir}/include)
  file(MAKE_DIRECTORY ${spdlog_INCLUDE_DIR})

  add_dependencies(spdlog ${spdlog_external_project_name})
endif ()

set_target_properties(spdlog PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${spdlog_INCLUDE_DIR}
    )

find_package_handle_standard_args(spdlog
    REQUIRED_VARS
      spdlog_INCLUDE_DIR
    VERSION_VAR
      spdlog_VERSION
    )
