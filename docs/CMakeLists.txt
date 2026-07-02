find_package(
    trailbook
    0.1.0
    REQUIRED
    PATHS "${CMAKE_SOURCE_DIR}/cmake"
)
find_package(
    trailbook-ext-everest
    0.1.0
    REQUIRED
    PATHS "${CMAKE_SOURCE_DIR}/cmake"
)

set(EVEREST_DOCS_REPO_URL "git@github.com:everest/everest.github.io" CACHE STRING "The git URL of the repository where the deployed docs are stored")
add_trailbook(
    NAME "everest"
    STEM_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/source
    DEPLOYED_DOCS_REPO_URL ${EVEREST_DOCS_REPO_URL}
    DEPLOYED_DOCS_REPO_BRANCH "main"
)

get_target_property(
    TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
    trailbook_everest
    TRAILBOOK_INSTANCE_SOURCE_DIRECTORY
)
# Sphinx reads conf.py from TRAILBOOK_INSTANCE_SOURCE_DIRECTORY (binary tree).
# The stem-copy custom command fills this directory at build time, but
# configure_file runs at configure time — so ensure the directory exists now.
file(MAKE_DIRECTORY ${TRAILBOOK_INSTANCE_SOURCE_DIRECTORY})

# Compute EVEREST_GIT_VERSION_STRING for conf.py.in substitution.
# Mirrors everest-cmake's evc_generate_version_information(); we run it
# here because this subdir is configured before ev_add_project() (which is
# what eventually triggers the everest-cmake version macro).
find_program(GIT_EXECUTABLE NAMES git REQUIRED)
execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --dirty --always --tags
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE _everest_git_describe
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND ${GIT_EXECUTABLE} symbolic-ref --quiet --short HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE _everest_git_branch
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if("${_everest_git_branch}" STREQUAL "")
    # Detached HEAD (e.g. tag-triggered build) — branch is empty,
    # so omit the leading "@".
    set(EVEREST_GIT_VERSION_STRING "${_everest_git_describe}")
else()
    set(EVEREST_GIT_VERSION_STRING "${_everest_git_branch}@${_everest_git_describe}")
endif()

# Render conf.py directly into the trailbook source directory (binary tree).
# conf.py.in lives outside the stem dir so the stem-copy doesn't drag the
# template alongside the rendered output.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/conf.py.in
    ${TRAILBOOK_INSTANCE_SOURCE_DIRECTORY}/conf.py
    @ONLY
)

set(EVEREST_WORKSPACE_DIRECTORY ${CMAKE_SOURCE_DIR})
trailbook_ev_create_snapshot(
    EVEREST_WORKSPACE_DIRECTORY "${EVEREST_WORKSPACE_DIRECTORY}"
    TRAILBOOK_NAME "everest"
    OUTPUT_FILE "${TRAILBOOK_INSTANCE_SOURCE_DIRECTORY}/reference/assets/snapshot.yaml"
)
