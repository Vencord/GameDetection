{
  "targets": [
    {
      "target_name": "rpc_native",
      "sources": ["src/addon.cc", "src/matcher.cc"],
      "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")"],
      "defines": ["NODE_ADDON_API_CPP_EXCEPTIONS", "NODE_API_SWALLOW_UNTHROWABLE_EXCEPTIONS"],
      "cflags_cc!": ["-fno-exceptions", "-std=gnu++17"],
      "cflags_cc": ["-std=c++20", "-O3"],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LANGUAGE_STANDARD": "c++20"
      },
      "conditions": [
        ["OS=='linux'", { "sources": ["src/process_linux.cc"] }],
        ["OS=='mac'", {
          "sources": ["src/process_mac.cc"],
          "libraries": ["-lproc"]
        }],
        ["OS=='win'", {
          "sources": ["src/process_win.cc"],
          "defines": ["NOMINMAX", "WIN32_LEAN_AND_MEAN"],
          "libraries": ["ntdll.lib", "user32.lib"],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1,
              "AdditionalOptions": ["/std:c++20", "/utf-8"]
            }
          }
        }]
      ]
    }
  ]
}
