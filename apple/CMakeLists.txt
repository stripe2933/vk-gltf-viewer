enable_language(OBJCXX)

add_library(apple src/filesystem.cpp)
target_include_directories(apple PUBLIC include)
target_link_libraries(apple PRIVATE
    "-framework Cocoa"
    "-framework Foundation"
)
set_source_files_properties(src/filesystem.cpp PROPERTIES LANGUAGE OBJCXX)
