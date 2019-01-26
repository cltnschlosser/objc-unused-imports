add_clang_tool(objc-unused-imports
  UnusedImports.cpp
  )

target_link_libraries(objc-unused-imports
  clangTooling
  )