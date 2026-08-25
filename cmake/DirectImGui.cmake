set(UVSR_IMGUI_UPSTREAM_URL "https://github.com/ocornut/imgui.git")
set(UVSR_IMGUI_UPSTREAM_COMMIT
    "45acd5e0e82f4c954432533ae9985ff0e1aad6d5")
set(UVSR_IMGUI_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(UVSR_IMGUI_SOURCE_TREE_DIGEST
    "dbce2679c4571dea928ed4dd3e48f9dbc088e6d3bfa08081ae87153dc2945fb8")

# This manifest is the complete direct vendor boundary. Files are byte-exact
# copies from the pinned upstream commit; UVSR customizations stay outside it.
set(UVSR_IMGUI_FILE_MANIFEST
    "LICENSE.txt|C80C5789748D955C4A650562BAA0D750494E2C7128C2CA66AAEBE2E3C4E198BF"
    "backends/imgui_impl_dx12.cpp|AC3AD20744FEF194975EB7D13D905108F33A3111496C136854840FBCFD5ECC59"
    "backends/imgui_impl_dx12.h|133CBFCB18916CEA24397F171BEA0C5D59A2EB8EF050C56BC6B4ED87B1B2E0EF"
    "backends/imgui_impl_win32.cpp|288E93C2736BD1AE63903A7EA2F9D95B199AF165B61F36680E5F57950B7698F3"
    "backends/imgui_impl_win32.h|7E720E7FC54E348152CF8B29145F3D291DA5E9026176A36D08F5B46563F7F672"
    "imconfig.h|A92534D918030048328FFFCF0A50236B8C05F79558355B37E34B664E13C71091"
    "imgui.cpp|ED3957012D62A4E6444CCB0B02C163DA8938734B2F735832EC4124FB41A8CC89"
    "imgui.h|3A9CC7043CE915A47735002F38388C364A697DA80773426B3CDEDED9DCADD9B6"
    "imgui_draw.cpp|66C8CBF7BEC88CD1EE52ABB14B47A8356484AF1CF4B9A7A4DEF40DF3EBDF7CDE"
    "imgui_internal.h|4753615AB93B8DC70B5732589B96FA9F477827C1841AD530063FAA9556BD0498"
    "imgui_tables.cpp|1352614888CBD8287DF2F4FD65BEDB92191CDB72D580D1B9FCF77187B42ED0D0"
    "imgui_widgets.cpp|6AB702D37E257206A84DB2FBE15D3BC07C924E64005B5FFCEE45343E1A488668"
    "imstb_rectpack.h|2EFA3D5F7D003C19743B15155DAD9F46D9F9FC783A18893D703B431D3C990972"
    "imstb_textedit.h|10777411BF758908AADAF7D73D4710D6BA0464F332F0015DB9D1444C19456E6A"
    "imstb_truetype.h|88F0A25E27F5EEFD6EC50C42FC5FE3026AA8170603EFA08AA18D622DB8660923")

set(UVSR_IMGUI_EXPECTED_FILES)
foreach(manifest_entry IN LISTS UVSR_IMGUI_FILE_MANIFEST)
    string(REPLACE "|" ";" manifest_fields "${manifest_entry}")
    list(GET manifest_fields 0 relative_path)
    list(GET manifest_fields 1 expected_hash)
    set(source_path "${UVSR_IMGUI_SOURCE_DIR}/${relative_path}")
    if (NOT EXISTS "${source_path}")
        message(FATAL_ERROR
            "Pinned Dear ImGui file is missing: ${relative_path}")
    endif()
    file(SHA256 "${source_path}" actual_hash)
    string(TOUPPER "${actual_hash}" actual_hash)
    if (NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "Pinned Dear ImGui file changed: ${relative_path}; expected "
            "${expected_hash}, found ${actual_hash}")
    endif()
    list(APPEND UVSR_IMGUI_EXPECTED_FILES "${relative_path}")
endforeach()

file(GLOB_RECURSE UVSR_IMGUI_ACTUAL_FILES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    RELATIVE "${UVSR_IMGUI_SOURCE_DIR}"
    "${UVSR_IMGUI_SOURCE_DIR}/*")
list(SORT UVSR_IMGUI_EXPECTED_FILES)
list(SORT UVSR_IMGUI_ACTUAL_FILES)
if (NOT "${UVSR_IMGUI_ACTUAL_FILES}" STREQUAL
        "${UVSR_IMGUI_EXPECTED_FILES}")
    message(FATAL_ERROR
        "Direct Dear ImGui inventory differs from the pinned 15-file manifest")
endif()
