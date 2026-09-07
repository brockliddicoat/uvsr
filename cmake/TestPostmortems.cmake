cmake_minimum_required(VERSION 3.24)

set(root "${UVSR_BUILD_DIRECTORY}/postmortem-contract-fixture")
file(MAKE_DIRECTORY "${root}/docs/postmortem")
set(report "docs/postmortem/screen-space-diffuse.md")
set(manifest "${root}/docs/postmortem/publication-manifest.tsv")
set(contents "# fixture\n\nretained evidence.\n")
file(WRITE "${root}/${report}" "${contents}")
file(SHA256 "${root}/${report}" hash)
set(row "${hash}\t${report}\tfixture\n")
file(WRITE "${manifest}" "${row}")

function(check name succeeds)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DUVSR_SOURCE_DIRECTORY=${root}"
        -DUVSR_SKIP_WORKTREE_DISCOVERY=ON
        -P "${UVSR_SOURCE_DIRECTORY}/cmake/VerifyPostmortems.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if ((succeeds AND NOT result EQUAL 0) OR
        (NOT succeeds AND result EQUAL 0))
        message(FATAL_ERROR "postmortem fixture ${name}: ${output}${error}")
    endif()
endfunction()

check(complete true)
file(APPEND "${root}/${report}" "unreviewed change\n")
check(changed false)
file(WRITE "${root}/${report}" "${contents}")
file(WRITE "${root}/docs/postmortem/omitted.md" "unlisted evidence\n")
check(unlisted false)
file(REMOVE "${root}/docs/postmortem/omitted.md")
file(APPEND "${manifest}" "${row}")
check(duplicate false)
file(WRITE "${manifest}" "${row}${hash}\tdocs/postmortem/missing.md\tfixture\n")
check(missing false)
file(WRITE "${manifest}" "${hash}\tdocs/postmortem/../escape.md\tfixture\n")
check(escape false)
file(WRITE "${manifest}" "# removed from both inventory and tree\n")
check(required_report false)
file(WRITE "${manifest}" "${row}")
check(restored true)
message(STATUS "postmortem omission and corruption fixtures passed")
