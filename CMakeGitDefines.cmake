# Get the current working branch
execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get the latest abbreviated commit hash of the working branch
execute_process(
        COMMAND git log -1 --format=%h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
        COMMAND git describe --tags
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        RESULT_VARIABLE GIT_TAG_ERR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (${GIT_TAG_ERR} EQUAL 0)
    set(GIT_VERSION "${GIT_TAG}")
    else()
    set(GIT_VERSION "${GIT_BRANCH} ${GIT_COMMIT_HASH}")
endif ()